#include "backseat/navigator.hpp"

#include "backseat/rundown.hpp"

#include <random>

namespace backseat
{
namespace
{
/// Off the route this close to the maneuver means the driver missed it, not wandered off.
double constexpr kMissedTurnWithinM = 80;

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
}

void Navigator::Poll(RouteSnapshot const & route) { Update(m_lastT, route); }

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
    m_arrived = false;
    m_turnId.reset();
    m_rerouting = false;
    m_lastState = RouteState::Inactive;
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
    Say(m_engine->OnNav(NavEvent::Instruction(t, text, route.distToTurnM, ManeuverOf(route.turn))));
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
  m_turnId.reset();
  m_rerouting = false;
  m_arrived = false;
  m_lastState = RouteState::Inactive;

  SayNav(t, TripGreeting(m_history, m_config.rng));
  std::vector<RouteStep> steps;
  for (auto const k : route.plannedTurns)
    steps.push_back({InstructionText(k, "", ""), StepKindOf(k)});
  SayNav(t, RouteRundown(steps, m_history, m_config.rng));
  if (m_plan.expectedMinutes > 0)
    SayNav(t, PredictTrip(m_history, m_plan, m_config.rng).text);
  Say(m_engine->OnNav(NavEvent::DestinationSet(t, route.destination)));
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
