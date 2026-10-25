#include "backseat/navigator.hpp"

#include "backseat/rundown.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace backseat
{
namespace
{
/// Off the route this close to the maneuver means the driver missed it, not wandered off.
double constexpr kMissedTurnWithinM = 80;
/// Slower than this counts as stopped.
double constexpr kStoppedMps = 0.5;
/// Faster than this means driving again, not walking around with the phone.
double constexpr kResumedMps = 4;
/// Stopped this long away from the destination is a stop the driver chose (gas, food...).
double constexpr kUnplannedStopAfterMs = 180000;
/// Stops this close to the destination are just arriving.
double constexpr kNearDestinationM = 250;
/// If the GPS position never strays this far from where the car stopped, the phone stayed
/// in the car. Carried around a gas station, a phone's fix wanders well past it.
double constexpr kPhoneWanderedM = 30;
/// Traffic: judged over this window, a crawl is under kCrawlShare of the limit on average,
/// and it's over once the average is back above kClearShare.
double constexpr kTrafficWindowMs = 90000;
double constexpr kCrawlShare = 0.3;
double constexpr kClearShare = 0.6;
/// Only roads at least this fast count; crawling down a 25 mph street is just driving.
double constexpr kTrafficMinLimitMps = 15;  // ~34 mph

double DistanceM(LatLon const & a, LatLon const & b)
{
  double constexpr kEarthRadiusM = 6371000;
  double const rad = M_PI / 180;
  double const dLat = (b.lat - a.lat) * rad;
  double const dLon = (b.lon - a.lon) * rad;
  double const h = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(a.lat * rad) * std::cos(b.lat * rad) * std::sin(dLon / 2) * std::sin(dLon / 2);
  return 2 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(h)));
}

Maneuver ManeuverOf(TurnKind k)
{
  switch (k)
  {
  case TurnKind::Left:
  case TurnKind::SharpLeft:
  case TurnKind::SlightLeft:
  case TurnKind::ExitLeft: return Maneuver::Left;
  case TurnKind::Right:
  case TurnKind::SharpRight:
  case TurnKind::SlightRight:
  case TurnKind::ExitRight: return Maneuver::Right;
  default: return Maneuver::Other;
  }
}

std::optional<RouteStep::Kind> StepKindOf(TurnKind k)
{
  if (k == TurnKind::UTurn)
    return RouteStep::Kind::UTurn;
  switch (ManeuverOf(k))
  {
  case Maneuver::Left: return RouteStep::Kind::Left;
  case Maneuver::Right: return RouteStep::Kind::Right;
  default: return RouteStep::Kind::Other;
  }
}
}  // namespace

std::string InstructionText(TurnKind turn, std::string const & street, std::string const & destination)
{
  std::string text;
  switch (turn)
  {
  case TurnKind::None:
  case TurnKind::Straight: text = "Continue straight"; break;
  case TurnKind::Left: text = "Turn left"; break;
  case TurnKind::SharpLeft: text = "Make a sharp left"; break;
  case TurnKind::SlightLeft: text = "Bear left"; break;
  case TurnKind::Right: text = "Turn right"; break;
  case TurnKind::SharpRight: text = "Make a sharp right"; break;
  case TurnKind::SlightRight: text = "Bear right"; break;
  case TurnKind::UTurn: text = "Make a U-turn"; break;
  case TurnKind::Roundabout: text = "Take the roundabout"; break;
  case TurnKind::ExitLeft: text = "Take the exit on the left"; break;
  case TurnKind::ExitRight: text = "Take the exit on the right"; break;
  case TurnKind::Destination:
    return destination.empty() ? "Arrive at your destination" : "Arrive at " + destination;
  }
  if (!street.empty())
    text += " onto " + street;
  return text;
}

Navigator::Navigator(EngineConfig config, std::vector<TripRecord> history, TripRecorded onTripRecorded)
  : m_config(std::move(config))
  , m_history(std::move(history))
  , m_onTripRecorded(std::move(onTripRecorded))
{
  if (!m_config.rng)
  {
    std::random_device rd;
    m_config.rng = SeededRng(rd());
  }
}

void Navigator::OnFix(Fix const & fix, RouteSnapshot const & route)
{
  // Clocks can step backwards (GPS vs. system time); the engine ignores those samples.
  m_lastT = std::max(m_lastT, fix.tMs);
  Update(m_lastT, route);
  if (!m_engine || m_arrived)
    return;
  TelemetrySample s;
  s.t = fix.tMs;
  s.speedMps = fix.speedMps < 0 ? 0 : fix.speedMps;
  s.speedLimitMps = fix.speedLimitMps;
  s.lat = fix.lat;
  s.lon = fix.lon;
  if (fix.bearingDeg >= 0)
    s.headingDeg = fix.bearingDeg;
  Say(m_engine->OnTelemetry(s));
  if (!m_plan.origin.lat && !m_plan.origin.lon)
    m_plan.origin = {fix.lat, fix.lon};
  TrackStop(fix, route);
  TrackTraffic(fix, route);
}

void Navigator::Poll(RouteSnapshot const & route) { Update(m_lastT, route); }

void Navigator::OnBattery(double tMs, double level, bool charging)
{
  int const percent = static_cast<int>(std::lround(std::clamp(level, 0.0, 1.0) * 100));
  if (m_batteryPercent == percent && m_charging == charging)
    return;
  m_batteryPercent = percent;
  m_charging = charging;
  if (m_engine && !m_arrived)
    Say(m_engine->OnNav(NavEvent::Battery(std::max(m_lastT, tMs), percent / 100.0, charging)));
}

void Navigator::TrackStop(Fix const & fix, RouteSnapshot const & route)
{
  if (fix.speedMps < 0)
    return;
  LatLon const here{fix.lat, fix.lon};
  if (!m_stopSince)
  {
    if (fix.speedMps < kStoppedMps)
    {
      m_stopSince = fix.tMs;
      m_stopAt = here;
      m_stopWanderM = 0;
      m_stopAnnounced = false;
    }
    return;
  }
  // A stop lasts until the car is moving again; walking pace with the phone is still the stop.
  if (fix.speedMps >= kResumedMps)
  {
    double const stoppedMs = fix.tMs - *m_stopSince;
    bool const stayedInCar = m_stopWanderM < kPhoneWanderedM;
    m_stopSince.reset();
    Say(m_engine->OnNav(NavEvent::Resumed(fix.tMs, stoppedMs, stayedInCar)));
    return;
  }
  m_stopWanderM = std::max(m_stopWanderM, DistanceM(m_stopAt, here));
  bool const hasDestination = route.destinationPoint.lat || route.destinationPoint.lon;
  bool const nearDestination = hasDestination && DistanceM(here, route.destinationPoint) < kNearDestinationM;
  if (!m_stopAnnounced && !nearDestination && fix.tMs - *m_stopSince >= kUnplannedStopAfterMs)
  {
    m_stopAnnounced = true;
    std::optional<Place> place;
    if (m_placeLookup)
      place = m_placeLookup(m_stopAt);
    Say(m_engine->OnNav(NavEvent::UnplannedStop(fix.tMs, place)));
  }
}

void Navigator::TrackTraffic(Fix const & fix, RouteSnapshot const & route)
{
  if (route.state != RouteState::OnRoute || !fix.speedLimitMps || *fix.speedLimitMps < kTrafficMinLimitMps)
  {
    m_speeds.clear();
    return;
  }
  m_speeds.push_back({fix.tMs, std::max(0.0, fix.speedMps), *fix.speedLimitMps});
  while (!m_speeds.empty() && fix.tMs - m_speeds.front().t > kTrafficWindowMs)
    m_speeds.erase(m_speeds.begin());
  // Need most of a window before judging.
  if (fix.tMs - m_speeds.front().t < kTrafficWindowMs * 0.8)
    return;
  double speed = 0;
  double limit = 0;
  for (auto const & s : m_speeds)
  {
    speed += s.mps;
    limit += s.limitMps;
  }
  double const share = speed / limit;
  if (!m_inTraffic && share < kCrawlShare)
  {
    m_inTraffic = true;
    Say(m_engine->OnNav(NavEvent::Traffic(fix.tMs, true)));
  }
  else if (m_inTraffic && share > kClearShare)
  {
    m_inTraffic = false;
    Say(m_engine->OnNav(NavEvent::Traffic(fix.tMs, false)));
  }
}

std::vector<Commentary> Navigator::TakeLines()
{
  std::vector<Commentary> out;
  out.swap(m_queue);
  return out;
}

void Navigator::Update(double t, RouteSnapshot const & route)
{
  if (route.state == RouteState::Inactive)
  {
    // Route cancelled or finished and closed: the trip is over either way.
    m_engine.reset();
    ResetTrip();
    return;
  }

  if (!m_engine)
    StartTrip(t, route);
  if (m_arrived)
    return;

  RouteState const prev = m_lastState;
  m_lastState = route.state;

  if (route.state == RouteState::Finished)
  {
    m_arrived = true;
    Say(m_engine->OnNav(NavEvent::Arrived(t)));
    TripRecord const record = RecordTrip(*m_engine, m_plan);
    m_history.push_back(record);
    if (m_onTripRecorded)
      m_onTripRecorded(record, m_engine->Report());
    return;
  }

  if (route.state == RouteState::OffRoute)
  {
    if (prev == RouteState::OnRoute)
    {
      if (m_turnId && m_lastDistToTurnM < kMissedTurnWithinM)
        Say(m_engine->OnNav(NavEvent::MissedTurn(t)));
      Say(m_engine->OnNav(NavEvent::Rerouted(t)));
      m_rerouting = true;
    }
    return;
  }

  if (!route.turnId)
    return;
  if (route.turnId != m_turnId)
  {
    // Moving on to a new maneuver means the last one was done, unless a reroute got us here.
    if (m_turnId && !m_rerouting)
      Say(m_engine->OnNav(NavEvent::TurnCompleted(t, true)));
    m_rerouting = false;
    m_turnId = route.turnId;
    std::string const text = InstructionText(route.turn, route.nextStreet, route.destination);
    Say(m_engine->OnNav(NavEvent::Instruction(t, text, route.distToTurnM, ManeuverOf(route.turn), route.control)));
  }
  else
  {
    Say(m_engine->OnNav(NavEvent::Progress(t, route.distToTurnM)));
  }
  m_lastDistToTurnM = route.distToTurnM;
}

void Navigator::StartTrip(double t, RouteSnapshot const & route)
{
  m_engine = std::make_unique<CritiqueEngine>(m_config, t);
  m_plan = {};
  m_plan.origin = route.origin;
  m_plan.destination = route.destinationPoint;
  m_plan.destinationName = route.destination;
  m_plan.expectedMinutes = route.etaSeconds / 60;
  ResetTrip();

  SayNav(t, TripGreeting(m_history, m_config.rng));
  // Already low when the trip starts? Say so after the greeting.
  if (m_batteryPercent && m_charging)
    Say(m_engine->OnNav(NavEvent::Battery(t, *m_batteryPercent / 100.0, *m_charging)));
  std::vector<RouteStep> steps;
  for (auto const k : route.plannedTurns)
    steps.push_back({InstructionText(k, "", ""), StepKindOf(k)});
  SayNav(t, RouteRundown(steps, m_history, m_config.rng));
  if (m_plan.expectedMinutes > 0)
    SayNav(t, PredictTrip(m_history, m_plan, m_config.rng).text);
  Say(m_engine->OnNav(NavEvent::DestinationSet(t, route.destination)));
}

void Navigator::ResetTrip()
{
  m_turnId.reset();
  m_rerouting = false;
  m_arrived = false;
  m_lastState = RouteState::Inactive;
  m_stopSince.reset();
  m_speeds.clear();
  m_inTraffic = false;
}

void Navigator::Say(std::vector<Commentary> const & lines) { m_queue.insert(m_queue.end(), lines.begin(), lines.end()); }

void Navigator::SayNav(double t, std::string const & text)
{
  Commentary c;
  c.t = t;
  c.category = Category::Instruction;
  c.text = text;
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Nav;
  m_queue.push_back(std::move(c));
}
}  // namespace backseat
