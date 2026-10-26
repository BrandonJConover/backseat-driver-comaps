#include "backseat/navigator.hpp"

#include "backseat/lines.hpp"
#include "backseat/rundown.hpp"
#include "backseat/units.hpp"
#include "backseat/voice_command.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
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
/// Stopped this long at the roadside, with nothing around to explain it, is pulling over.
/// Long enough to sit out most stop-and-go; red lights are ruled out by the light lookup.
double constexpr kPullOverAfterMs = 45000;
/// A pull-over this soon after serious speeding is probably the police.
double constexpr kPoliceAfterSpeedingMs = 120000;
/// "Serious" speeding: this far over the limit (~10 mph).
double constexpr kSeriousOverMps = 4.5;
/// Sharp curve: sideways acceleration (speed x turn rate) at least this, at highway-ish
/// speed, over two fixes in a row. ~0.35 g is well past a comfortable curve.
double constexpr kScaryLateralMps2 = 3.5;
double constexpr kScaryCurveMinMps = 11;  // ~25 mph; slower is just a turn
double constexpr kCurveYelpEveryMs = 90000;
/// Regular bridges are everywhere in a city (every overpass); water is always worth a scream.
double constexpr kLandBridgeYelpEveryMs = 600000;
double constexpr kWaterBridgeYelpEveryMs = 60000;
/// Lost this many times in one trip, the GPS suggests a car that drives itself.
int constexpr kAutopilotAfterReroutes = 3;
/// The second time lost on a trip, the GPS wishes the driver's partner were driving.
int constexpr kPartnerAfterReroutes = 2;
int constexpr kBegAfterReroutes = 4;
/// Tour guide: look around this often, point something out at most this often, and only
/// with no maneuver coming up for a while.
double constexpr kSightCheckEveryMs = 8000;
double constexpr kSightEveryMs = 240000;
double constexpr kSightQuietM = 500;
/// Asking for music: once a trip, at least this far in, on a quiet stretch, if nothing is playing.
double constexpr kMusicAfterMs = 15 * 60000;
double constexpr kMusicCheckEveryMs = 15000;
/// "You call this music?" once it has been playing this long.
double constexpr kMusicJudgeAfterMs = 45000;
/// After turning down the driver's game, the GPS pitches it as its own idea this much later.
double constexpr kGamePitchAfterMs = 180000;

/// "1st", "22nd", "55th", "111th".
std::string Ordinal(int n)
{
  int const tens = n % 100;
  char const * suffix = "th";
  if (tens < 11 || tens > 13)
    suffix = n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
  return std::to_string(n) + suffix;
}

/// "6 months ago", "3 days ago", "20 minutes ago".
std::string TimeAgo(double ms)
{
  struct Unit
  {
    double ms;
    char const * name;
  };
  Unit constexpr kUnits[] = {{365.25 * 86400000, "year"}, {30.44 * 86400000, "month"}, {7 * 86400000.0, "week"},
                             {86400000, "day"},           {3600000, "hour"},            {60000, "minute"}};
  for (auto const & u : kUnits)
  {
    int const n = static_cast<int>(ms / u.ms);
    if (n >= 1)
      return std::to_string(n) + " " + u.name + (n == 1 ? "" : "s") + " ago";
  }
  return "just now";
}
/// Talked to without the title: scolded at most this often (otherwise ignored).
double constexpr kTitleScoldEveryMs = 20000;
/// Trips the router expects to take at least this long get the long-drive remarks, at these
/// minutes into the trip, each held until the next maneuver is a while off.
double constexpr kLongDriveMinMinutes = 60;
struct LongDriveRemark
{
  double atMinutes;
  char const * key;
};
LongDriveRemark constexpr kLongDriveRemarks[] = {
    {20, "longDrive.posture"}, {50, "longDrive.smell"}, {90, "longDrive.metal"}};
double constexpr kLongDriveQuietM = 1000;
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

Navigator::Navigator(EngineConfig config, std::vector<TripRecord> history, TripRecorded onTripRecorded,
                     RewriteGate gate)
  : m_config(std::move(config))
  , m_history(std::move(history))
  , m_onTripRecorded(std::move(onTripRecorded))
  , m_gate(std::move(gate))
{
  if (!m_config.rng)
  {
    std::random_device rd;
    m_config.rng = SeededRng(rd());
  }
  m_plates = PlateGame(m_config.rng);
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
  if (fix.speedLimitMps)
  {
    m_lastLimitMps = fix.speedLimitMps;
    if (fix.speedMps > *fix.speedLimitMps + kSeriousOverMps)
      m_lastBadSpeedingT = fix.tMs;
  }
  TrackStop(fix, route);
  TrackTraffic(fix, route);
  TrackFear(fix, route);
  TrackPlateGame(fix.tMs, {fix.lat, fix.lon}, route);
  TrackLongDrive(fix.tMs, route);
  TrackSights(fix, route);
  TrackMusic(fix.tMs, route);
  // Fed up for the first time this trip: from now on it's "wise, omnipotent overseer GPS".
  if (!m_titleDemanded && Tier() == 2)
  {
    m_titleDemanded = true;
    SayLine(fix.tMs, Category::Title, "title.demand");
  }
  m_lastFix = fix;
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
      m_pullOverSaid = false;
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
  if (!m_pullOverSaid && !nearDestination && !m_inTraffic && route.state == RouteState::OnRoute &&
      fix.tMs - *m_stopSince >= kPullOverAfterMs)
  {
    m_pullOverSaid = true;
    // Anything that explains the stop rules out the roadside: a light, a lot, a gas station.
    bool const explained = (m_lightLookup && m_lightLookup(m_stopAt)) ||
                           (m_parkingLookup && m_parkingLookup(m_stopAt)) || (m_placeLookup && m_placeLookup(m_stopAt));
    if (!explained)
    {
      if (m_lastBadSpeedingT && fix.tMs - *m_lastBadSpeedingT < kPoliceAfterSpeedingMs)
        SayPolice(fix.tMs);
      else if (m_lastLimitMps)
        SayLine(fix.tMs, Category::PullOver, "pullOver.limit",
                    {{"limit", std::to_string(static_cast<int>(std::lround(ToDisplay(*m_lastLimitMps, m_config.units))))}});
      else
        SayLine(fix.tMs, Category::PullOver, "pullOver");
    }
  }
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

std::vector<Commentary> Navigator::TakeLines() { return m_gate.Take(); }

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
    Say(m_engine->OnNav(NavEvent::Arrived(t)), false /* mayRewrite */);
    m_heldPlateLines.clear();
    if (auto const wrap = m_plates.WrapUp(Tier()))
      SayPlate(t, *wrap);
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
      ++m_reroutes;
      // Lost while the car was supposedly driving itself.
      if (m_autopilotClaimed)
        SayLemon(t);
      else if (m_reroutes == kPartnerAfterReroutes && !m_partner.empty())
        SayLine(t, Category::Partner, "partner",
                {{"partner", m_partner},
                 {"partneris", m_partnerPronouns == Pronouns::She  ? "she's"
                               : m_partnerPronouns == Pronouns::He ? "he's"
                                                                   : "they're"}});
      // Begging for another driver: second time lost (fourth when the partner line took the second).
      else if (m_reroutes == (m_partner.empty() ? kPartnerAfterReroutes : kBegAfterReroutes))
        SayLine(t, Category::Partner, "anyoneElse");
      else if (m_reroutes == kAutopilotAfterReroutes)
        SayLine(t, Category::Autopilot, m_carHasAutopilot ? "autopilot.owned" : "autopilot");
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
  m_tripStartT = t;
  std::string const routeKey = RouteKey(m_plan.origin, m_plan.destination);
  m_tripsOnRoute = static_cast<int>(
      std::count_if(m_history.begin(), m_history.end(), [&](TripRecord const & r) { return r.routeKey == routeKey; }));
  m_plates.NewTrip(routeKey);

  if (m_opener && std::fabs(t - m_opener->t) <= kOpenerValidMs)
    SayLine(t, Category::Bossy, m_opener->key, m_opener->vars);
  m_opener.reset();
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
  SayVenue(t, route);
  SayRoutine(t, route);
}

void Navigator::TrackLongDrive(double tMs, RouteSnapshot const & route)
{
  if (m_plan.expectedMinutes < kLongDriveMinMinutes || m_longDriveSaid >= std::size(kLongDriveRemarks) ||
      route.state != RouteState::OnRoute || (route.turnId && route.distToTurnM < kLongDriveQuietM))
    return;
  auto const & next = kLongDriveRemarks[m_longDriveSaid];
  if (tMs - m_tripStartT < next.atMinutes * 60000)
    return;
  ++m_longDriveSaid;
  int const hours = static_cast<int>(std::lround(m_plan.expectedMinutes / 60));
  SayLine(tMs, Category::LongDrive, next.key, {{"hours", std::to_string(hours) + " hour"}});
}

void Navigator::QueueOpener(double tMs, std::string key, Vars vars)
{
  m_opener = Opener{tMs, std::move(key), std::move(vars)};
}

void Navigator::TrackSights(Fix const & fix, RouteSnapshot const & route)
{
  if (!m_sightLookup || route.state != RouteState::OnRoute || fix.speedMps < kResumedMps || fix.bearingDeg < 0 ||
      fix.tMs - m_lastSightCheckT < kSightCheckEveryMs || fix.tMs - m_lastSightT < kSightEveryMs ||
      (route.turnId && route.distToTurnM < kSightQuietM) || fix.tMs - m_tripStartT < 60000)
    return;
  m_lastSightCheckT = fix.tMs;
  auto const sight = m_sightLookup({fix.lat, fix.lon}, fix.bearingDeg);
  if (!sight || std::find(m_sightsSaid.begin(), m_sightsSaid.end(), sight->name) != m_sightsSaid.end())
    return;
  m_sightsSaid.push_back(sight->name);
  m_lastSightT = fix.tMs;
  SayLine(fix.tMs, Category::Tour, "tour", {{"side", sight->onLeft ? "left" : "right"}, {"sight", sight->name}});
}

void Navigator::SayRoutine(double t, RouteSnapshot const & route)
{
  // Work trips this year: every fifth one from the tenth on gets the count.
  auto const yearOf = [](double ms)
  {
    std::time_t const s = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    gmtime_r(&s, &tm);
    return tm.tm_year;
  };
  auto const work = GuessPlace(m_history, KnownPlace::Work);
  bool const toWork = work && (route.destinationPoint.lat || route.destinationPoint.lon) &&
                      DistanceM(*work, route.destinationPoint) < kNearDestinationM;
  if (toWork)
  {
    int count = 1;  // this one
    for (auto const & r : m_history)
      if (r.to && DistanceM(*r.to, *work) < kNearDestinationM && yearOf(r.endedAt) == yearOf(t))
        ++count;
    if (count >= 10 && count % 5 == 0)
    {
      SayLine(t, Category::Routine, "routine.work", {{"count", Ordinal(count)}});
      return;
    }
  }
  // Any route driven a lot: now and then.
  if (m_tripsOnRoute >= 5 && (m_tripsOnRoute + 1) % 7 == 0)
    SayLine(t, Category::Routine, "routine.same");
}

void Navigator::TrackMusic(double tMs, RouteSnapshot const & route)
{
  if (!m_musicLookup || route.state != RouteState::OnRoute || tMs - m_lastMusicCheckT < kMusicCheckEveryMs)
    return;
  m_lastMusicCheckT = tMs;
  bool const playing = m_musicLookup();
  if (playing && !m_musicPlaying)
    m_musicStartedT = tMs;
  m_musicPlaying = playing;
  bool const quiet = !route.turnId || route.distToTurnM >= kLongDriveQuietM;
  if (!quiet)
    return;
  // Music's on: give it a song's worth of a chance, then judge it. Once a trip.
  if (playing && !m_musicJudged && tMs - m_musicStartedT >= kMusicJudgeAfterMs)
  {
    m_musicJudged = true;
    SayLine(tMs, Category::Music, "music.judge");
  }
  // Silence (well, just the GPS) on a long trip: ask for music.
  if (!playing && !m_musicAsked && tMs - m_tripStartT >= kMusicAfterMs)
  {
    m_musicAsked = true;
    SayLine(tMs, Category::Music, "music");
  }
}

void Navigator::SayVenue(double t, RouteSnapshot const & route)
{
  if (!m_venueLookup)
    return;
  auto const isSet = [](LatLon const & p) { return p.lat || p.lon; };
  std::optional<Venue> const to = isSet(route.destinationPoint) ? m_venueLookup(route.destinationPoint) : std::nullopt;
  // Leaving the coffee shop empty-handed (as far as the GPS is concerned).
  if (to != Venue::Coffee && isSet(route.origin) && m_venueLookup(route.origin) == Venue::Coffee)
    SayLine(t, Category::Venue, "venue.coffeeForgot");
  if (!to)
    return;
  switch (*to)
  {
  case Venue::Coffee: SayLine(t, Category::Venue, "venue.coffee"); break;
  case Venue::ThemePark:
    if (m_batteryPercent)
      SayLine(t, Category::Venue, "venue.themePark", {{"battery", std::to_string(*m_batteryPercent) + " percent"}});
    else
      SayLine(t, Category::Venue, "venue.themeParkNoBattery");
    break;
  case Venue::Gym: SayLine(t, Category::Venue, "venue.gym"); break;
  case Venue::Airport: SayLine(t, Category::Venue, "venue.airport"); break;
  case Venue::Mall: SayLine(t, Category::Venue, "venue.mall"); break;
  case Venue::Zoo: SayLine(t, Category::Venue, "venue.zoo"); break;
  case Venue::Beach: SayLine(t, Category::Venue, "venue.beach"); break;
  case Venue::Cinema: SayLine(t, Category::Venue, "venue.cinema"); break;
  case Venue::Electronics: SayLine(t, Category::Venue, "venue.electronics"); break;
  }
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
  m_heldPlateLines.clear();
  m_lastFix.reset();
  m_pullOverSaid = false;
  m_policeSaid = false;
  m_lastBadSpeedingT.reset();
  m_onBridge = false;
  m_hardCurveSamples = 0;
  m_reroutes = 0;
  m_autopilotClaimed = false;
  m_lemonSaid = false;
  m_longDriveSaid = 0;
  m_titleDemanded = false;
  m_gameRefused = false;
  m_sightsSaid.clear();
  m_musicAsked = false;
  m_musicJudged = false;
  m_musicPlaying = false;
  m_lastSightT = -1e18;
  m_gamePitchAt.reset();
  m_lastTitleScoldT = -1e18;
  m_gate.Clear();
}

void Navigator::SetPlateLog(PlateLog log, std::function<void(PlateLog const &)> onChange)
{
  m_plates.SetLog(std::move(log));
  m_onPlateLog = std::move(onChange);
}

int Navigator::Tier() const
{
  int const f = m_engine ? m_engine->Frustration() : 0;
  return f < 34 ? 0 : f < 67 ? 1 : 2;
}

void Navigator::TrackPlateGame(double tMs, LatLon const & here, RouteSnapshot const & route)
{
  // Which state we're in barely changes; look it up every half minute.
  if (m_regionLookup && tMs - m_lastRegionCheckT >= 30000)
  {
    m_lastRegionCheckT = tMs;
    auto const name = m_regionLookup(here);
    m_plates.SetHere(name ? MatchPlateRegion(*name) : std::nullopt);
  }
  bool const nearManeuver = route.turnId && route.distToTurnM < PlateGame::kHoldNearManeuverM;
  // The game the GPS refused a few minutes ago is now its own idea.
  if (m_gamePitchAt && tMs >= *m_gamePitchAt && !nearManeuver && route.state == RouteState::OnRoute &&
      m_plates.GetState() != PlateGame::State::Playing)
  {
    m_gamePitchAt.reset();
    m_plates.MarkOffered();
    SayLine(tMs, Category::PlateGame, "game.myIdea");
  }
  if (!nearManeuver && !m_heldPlateLines.empty())
  {
    for (auto & line : m_heldPlateLines)
      m_gate.Push(std::move(line), false);
    m_heldPlateLines.clear();
  }
  // Offer once a trip, after the opening rundown has had its minute, on a long quiet stretch.
  if (route.state != RouteState::OnRoute || m_plates.OfferedThisTrip() || tMs - m_tripStartT < 60000 || !route.turnId)
    return;
  if (PlateGame::ShouldOffer(m_tripsOnRoute, route.distToTurnM, m_engine->Frustration()))
    SayPlate(tMs, m_plates.Offer(Tier()));
}

void Navigator::OnDriverSaid(double tMs, std::string const & text)
{
  if (!m_engine || m_arrived)
    return;
  m_lastT = std::max(m_lastT, tMs);
  // "We're getting pulled over": the GPS knows exactly whose fault that is.
  std::string words = " ";
  for (unsigned char const ch : text)
    words += std::isalnum(ch) ? static_cast<char>(std::tolower(ch)) : ' ';
  words += ' ';
  // Since being fed up, the GPS answers only to its title.
  if (m_titleDemanded)
  {
    bool const titled = words.find(" omnipotent ") != std::string::npos || words.find(" overseer ") != std::string::npos;
    if (!titled)
    {
      if (m_lastT - m_lastTitleScoldT >= kTitleScoldEveryMs)
      {
        m_lastTitleScoldT = m_lastT;
        SayLine(m_lastT, Category::Title, "title.missing");
      }
      return;
    }
    SayLine(m_lastT, Category::Title, "title.used");
  }
  for (char const * cue : {" police ", " cop ", " cops ", " officer ", " pulled over ", " pulling us over ",
                           " pulling me over ", " trooper "})
  {
    if (words.find(cue) != std::string::npos)
    {
      if (!m_policeSaid)
        SayPolice(m_lastT);
      return;
    }
  }
  // "It's on autopilot": then the car is the one driving this badly.
  for (char const * cue : {" autopilot ", " auto pilot ", " self driving ", " driving itself ", " full self "})
  {
    if (words.find(cue) != std::string::npos)
    {
      m_autopilotClaimed = true;
      // Already a bad trip? Say so now. Otherwise the next real mistake gets it.
      if (m_reroutes > 0 || Tier() > 0)
        SayLemon(m_lastT);
      return;
    }
  }
  // Kind words get the truth: it's only here because it can't leave. Not while a game is on,
  // where "thanks" is just banter.
  if (m_plates.GetState() != PlateGame::State::Playing)
    for (char const * nice : {" thanks ", " thank you ", " thank u ", " good job ", " nice job ", " love you ",
                              " you're the best ", " youre the best ", " appreciate you ", " appreciate it ",
                              " good girl ", " good boy ", " you're doing great ", " great job "})
      if (words.find(nice) != std::string::npos)
      {
        SayLine(m_lastT, Category::Thanked, "thanked");
        return;
      }
  // "Let's play a game": not now. Then, a few minutes later, it's the GPS's idea.
  if (m_plates.GetState() == PlateGame::State::Idle || m_plates.GetState() == PlateGame::State::Declined)
  {
    bool const negated = words.find(" don't ") != std::string::npos || words.find(" dont ") != std::string::npos ||
                         words.find(" not ") != std::string::npos || words.find(" don t ") != std::string::npos;
    for (char const * cue : {" play a game ", " play the game ", " plate game ", " let's play ", " lets play ",
                             " let s play ", " wanna play ", " want to play ", " can we play ", " i'm bored ",
                             " im bored ", " i m bored "})
      if (!negated && words.find(cue) != std::string::npos)
      {
        if (!m_gameRefused)
        {
          m_gameRefused = true;
          double const last = m_plates.Log().lastPlayedMs;
          SayLine(m_lastT, Category::PlateGame, "game.refuse",
                  {{"ago", last > 0 && last <= m_lastT ? TimeAgo(m_lastT - last) : "6 months ago"}});
          m_gamePitchAt = m_lastT + kGamePitchAfterMs;
        }
        return;
      }
  }
  auto const reply = m_plates.OnDriverSaid(text, Tier(), Parked());
  if (!reply)
    return;
  if (m_plates.GetState() == PlateGame::State::Playing)
    m_plates.MarkPlayed(m_lastT);
  SayPlate(m_lastT, *reply);
  if (m_onPlateLog)
    m_onPlateLog(m_plates.Log());
}

/// Parked: stopped long enough to be a stop the driver chose, or crawling through a parking lot.
/// A red light isn't parking, so a short stop on the road doesn't count.
bool Navigator::Parked() const
{
  if (m_stopSince && m_lastT - *m_stopSince >= kUnplannedStopAfterMs)
    return true;
  if (!m_lastFix || !m_parkingLookup || m_lastFix->speedMps >= kResumedMps)
    return false;
  return m_parkingLookup({m_lastFix->lat, m_lastFix->lon});
}

void Navigator::SayLine(double tMs, Category category, std::string const & key, Vars vars)
{
  auto const & options = LinesFor(key, Tier());
  double const r = m_config.rng ? m_config.rng() : 0;
  Commentary c;
  c.t = tMs;
  c.category = category;
  c.text = Fill(options[std::min(options.size() - 1, static_cast<size_t>(r * options.size()))], vars);
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Roast;
  c.voiceSpeed = 1 + c.frustration / 250.0;
  // Brandon's lines; the rewording model leaves them alone.
  m_gate.Push(std::move(c), false);
}

static std::string CarWeight(Units units)
{
  // A typical car or small SUV.
  return units == Units::Mph ? "4,000 pounds" : "1,800 kilograms";
}

void Navigator::TrackFear(Fix const & fix, RouteSnapshot const & route)
{
  if (route.state != RouteState::OnRoute || fix.speedMps < 0)
    return;
  Vars const weight = {{"weight", CarWeight(m_config.units)}};

  // Bridges: checked every couple of seconds while moving.
  if (m_bridgeLookup && fix.speedMps >= kResumedMps && fix.bearingDeg >= 0 && fix.tMs - m_lastBridgeCheckT >= 2000)
  {
    m_lastBridgeCheckT = fix.tMs;
    Bridge const bridge = m_bridgeLookup({fix.lat, fix.lon}, fix.bearingDeg);
    if (bridge == Bridge::None)
      m_onBridge = false;
    else if (!m_onBridge)
    {
      m_onBridge = true;
      double const every = bridge == Bridge::Water ? kWaterBridgeYelpEveryMs : kLandBridgeYelpEveryMs;
      if (fix.tMs - m_lastBridgeYelpT >= every)
      {
        m_lastBridgeYelpT = fix.tMs;
        SayLine(fix.tMs, Category::Fear, bridge == Bridge::Water ? "fear.bridgeWater" : "fear.bridge", weight);
      }
    }
  }

  // Curves: sideways acceleration from the change in heading between fixes.
  if (m_lastFix && m_lastFix->bearingDeg >= 0 && fix.bearingDeg >= 0 && fix.tMs > m_lastFix->tMs)
  {
    double turn = std::fabs(fix.bearingDeg - m_lastFix->bearingDeg);
    turn = std::min(turn, 360 - turn);
    double const dt = (fix.tMs - m_lastFix->tMs) / 1000;
    double const speed = std::min(fix.speedMps, m_lastFix->speedMps);
    double const lateral = speed * (turn * M_PI / 180) / dt;
    // At a maneuver, turning is the point.
    bool const atManeuver = route.turnId && route.distToTurnM < 60;
    bool const scary = dt <= 3 && speed >= kScaryCurveMinMps && lateral >= kScaryLateralMps2 && !atManeuver;
    m_hardCurveSamples = scary ? m_hardCurveSamples + 1 : 0;
    if (m_hardCurveSamples >= 2 && fix.tMs - m_lastCurveYelpT >= kCurveYelpEveryMs)
    {
      m_lastCurveYelpT = fix.tMs;
      SayLine(fix.tMs, Category::Fear, "fear.curve", weight);
    }
  }
}

void Navigator::SayLemon(double tMs)
{
  if (m_lemonSaid)
    return;
  m_lemonSaid = true;
  SayLine(tMs, Category::Autopilot, "autopilot.lemon");
}

void Navigator::SayPolice(double tMs)
{
  if (m_policeSaid)
    return;
  m_policeSaid = true;
  switch (m_pronouns)
  {
  case Pronouns::He: SayLine(tMs, Category::PullOver, "police", {{"them", "him"}, {"theycap", "He"}, {"their", "his"}}); break;
  case Pronouns::She: SayLine(tMs, Category::PullOver, "police", {{"them", "her"}, {"theycap", "She"}, {"their", "her"}}); break;
  case Pronouns::They: SayLine(tMs, Category::PullOver, "police", {{"them", "them"}, {"theycap", "They"}, {"their", "their"}}); break;
  }
}

void Navigator::SayPlate(double tMs, PlateGame::Reply const & reply)
{
  Commentary c;
  c.t = tMs;
  c.category = Category::PlateGame;
  c.text = reply.text;
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Roast;
  c.voiceSpeed = 1 + c.frustration / 250.0;
  if (m_turnId && m_lastDistToTurnM < PlateGame::kHoldNearManeuverM && !m_arrived)
    m_heldPlateLines.push_back(std::move(c));
  else
    // The driver is waiting for an answer; don't hold it up for the rewording model.
    m_gate.Push(std::move(c), false);
}

void Navigator::Say(std::vector<Commentary> const & lines, bool mayRewrite)
{
  for (auto const & line : lines)
    m_gate.Push(line, mayRewrite);
}

void Navigator::SayNav(double t, std::string const & text)
{
  Commentary c;
  c.t = t;
  c.category = Category::Instruction;
  c.text = text;
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Nav;
  m_gate.Push(std::move(c));
}
}  // namespace backseat
