#include "backseat/types.hpp"

namespace backseat
{
namespace
{
NavEvent Make(NavEvent::Type type, double t)
{
  NavEvent e;
  e.type = type;
  e.t = t;
  return e;
}
}  // namespace

NavEvent NavEvent::Instruction(double t, std::string text, double distanceM, std::optional<Maneuver> maneuver, Control control)
{
  auto e = Make(Type::Instruction, t);
  e.text = std::move(text);
  e.distanceM = distanceM;
  e.maneuver = maneuver;
  e.control = control;
  return e;
}

NavEvent NavEvent::TurnCompleted(double t, bool onTime)
{
  auto e = Make(Type::TurnCompleted, t);
  e.onTime = onTime;
  return e;
}

NavEvent NavEvent::Progress(double t, double distanceM)
{
  auto e = Make(Type::Progress, t);
  e.distanceM = distanceM;
  return e;
}

NavEvent NavEvent::MissedTurn(double t) { return Make(Type::MissedTurn, t); }

NavEvent NavEvent::WrongTurn(double t, TurnDirection expected, TurnDirection turned)
{
  auto e = Make(Type::WrongTurn, t);
  e.expected = expected;
  e.turned = turned;
  return e;
}

NavEvent NavEvent::Rerouted(double t) { return Make(Type::Rerouted, t); }

NavEvent NavEvent::HazardAhead(double t, HazardKind kind, double distanceM, std::optional<std::string> id)
{
  auto e = Make(Type::HazardAhead, t);
  e.hazard = kind;
  e.distanceM = distanceM;
  e.id = std::move(id);
  return e;
}

NavEvent NavEvent::PoiAhead(double t, std::string name, StopWish wish)
{
  auto e = Make(Type::PoiAhead, t);
  e.name = std::move(name);
  e.wish = wish;
  return e;
}

NavEvent NavEvent::StopRequestAnswer(double t, bool accepted)
{
  auto e = Make(Type::StopRequestAnswer, t);
  e.accepted = accepted;
  return e;
}

NavEvent NavEvent::PromisedStop(double t, bool visited)
{
  auto e = Make(Type::PromisedStop, t);
  e.visited = visited;
  return e;
}

NavEvent NavEvent::DestinationSet(double t, std::string name)
{
  auto e = Make(Type::DestinationSet, t);
  e.name = std::move(name);
  return e;
}

NavEvent NavEvent::Battery(double t, double level, bool charging)
{
  auto e = Make(Type::Battery, t);
  e.level = level;
  e.charging = charging;
  return e;
}

NavEvent NavEvent::Resumed(double t, double stoppedMs, bool phoneStayedInCar)
{
  auto e = Make(Type::Resumed, t);
  e.stoppedMs = stoppedMs;
  e.phoneStayedInCar = phoneStayedInCar;
  return e;
}

NavEvent NavEvent::UnplannedStop(double t, std::optional<Place> place)
{
  auto e = Make(Type::UnplannedStop, t);
  e.place = std::move(place);
  return e;
}

NavEvent NavEvent::Traffic(double t, bool heavy, std::optional<std::string> alternate)
{
  auto e = Make(Type::Traffic, t);
  e.heavy = heavy;
  e.alternate = std::move(alternate);
  return e;
}

NavEvent NavEvent::Arrived(double t) { return Make(Type::Arrived, t); }

char const * ToString(Category c)
{
  switch (c)
  {
  case Category::Speeding: return "speeding";
  case Category::TooSlow: return "tooSlow";
  case Category::Traffic: return "traffic";
  case Category::HardBraking: return "hardBraking";
  case Category::HardAccel: return "hardAccel";
  case Category::LongStop: return "longStop";
  case Category::MissedTurn: return "missedTurn";
  case Category::WrongTurn: return "wrongTurn";
  case Category::Payback: return "payback";
  case Category::UnplannedStop: return "unplannedStop";
  case Category::Abandoned: return "abandoned";
  case Category::Hazard: return "hazard";
  case Category::StopRequest: return "stopRequest";
  case Category::NoDestination: return "noDestination";
  case Category::Battery: return "battery";
  case Category::Rerouted: return "rerouted";
  case Category::TurnCompleted: return "turnCompleted";
  case Category::Instruction: return "instruction";
  case Category::Arrived: return "arrived";
  case Category::PlateGame: return "plateGame";
  case Category::PullOver: return "pullOver";
  case Category::Fear: return "fear";
  case Category::Venue: return "venue";
  case Category::Autopilot: return "autopilot";
  case Category::LongDrive: return "longDrive";
  case Category::Count: break;
  }
  return "?";
}

char const * ToString(IncidentKind k)
{
  switch (k)
  {
  case IncidentKind::Speeding: return "speeding";
  case IncidentKind::TooSlow: return "tooSlow";
  case IncidentKind::Traffic: return "traffic";
  case IncidentKind::HardBraking: return "hardBraking";
  case IncidentKind::HardAccel: return "hardAccel";
  case IncidentKind::LongStop: return "longStop";
  case IncidentKind::MissedTurn: return "missedTurn";
  case IncidentKind::WrongTurn: return "wrongTurn";
  case IncidentKind::LateTurn: return "lateTurn";
  case IncidentKind::UnplannedStop: return "unplannedStop";
  case IncidentKind::Rerouted: return "rerouted";
  }
  return "?";
}

char const * ToString(Priority p) { return p == Priority::Nav ? "nav" : "roast"; }
}  // namespace backseat
