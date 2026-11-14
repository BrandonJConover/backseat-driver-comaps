// An off-route blip right at a turn (loop B85, BI-076). A one-second OffRoute 50 m before the turn used to
// say "And you missed it" at once, count a reroute and a miss, and leave the navigator rerouting, so the
// turn the driver then made correctly got no TurnCompleted. Now the miss waits a few seconds: back on the
// same maneuver before then is a GPS blip, and nothing is said or counted.
#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/lines.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
EngineConfig Config()
{
  EngineConfig c;
  c.rng = SeededRng(3);
  return c;
}

RouteSnapshot Leg(uint32_t turnId, double dist, RouteState state = RouteState::OnRoute)
{
  RouteSnapshot r;
  r.state = state;
  r.turnId = turnId;
  r.turn = turnId % 2 ? TurnKind::Left : TurnKind::Right;
  r.distToTurnM = dist;
  r.nextStreet = "Elm Street";
  r.destination = "Grandma's";
  r.etaSeconds = 900;
  r.plannedTurns = {TurnKind::Left, TurnKind::Right, TurnKind::Destination};
  return r;
}

Fix At(double tMs, double mps = 10)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.7;
  f.lon = -105;
  f.speedMps = mps;
  return f;
}

int Count(std::vector<Commentary> const & lines, Category c)
{
  int n = 0;
  for (auto const & l : lines)
    n += l.category == c;
  return n;
}

// Up to a turn 60 m out, lines taken.
Navigator & Approach(Navigator & nav)
{
  nav.OnFix(At(0), Leg(1, 900));
  nav.OnFix(At(60000), Leg(1, 140));
  nav.OnFix(At(70000), Leg(1, 60));
  nav.TakeLines();
  return nav;
}
}  // namespace

TEST(OffRouteBlipAtTheTurnIsNotAMiss)
{
  Navigator nav(Config());
  Approach(nav);
  nav.OnFix(At(71000), Leg(1, 50, RouteState::OffRoute));
  nav.OnFix(At(72000), Leg(1, 40));  // back, same maneuver
  nav.OnFix(At(76000), Leg(1, 5));
  nav.OnFix(At(80000), Leg(2, 400));  // made the turn
  auto const lines = nav.TakeLines();
  CHECK_EQUAL(Count(lines, Category::MissedTurn), 0);
  CHECK_EQUAL(Count(lines, Category::Rerouted), 0);
  auto const & stats = nav.Engine()->Stats();
  CHECK_EQUAL(stats.missedTurns, 0);
  CHECK_EQUAL(stats.reroutes, 0);
  CHECK_EQUAL(stats.turnsCompleted, 1);
}

TEST(OffRouteThatLastsIsStillAMissThenAReroute)
{
  Navigator nav(Config());
  Approach(nav);
  RouteSnapshot const off = Leg(1, 50, RouteState::OffRoute);
  nav.OnFix(At(71000), off);
  CHECK_EQUAL(Count(nav.TakeLines(), Category::MissedTurn), 0);
  nav.OnFix(At(73000), off);
  CHECK_EQUAL(Count(nav.TakeLines(), Category::MissedTurn), 0);
  nav.OnFix(At(74000), off);  // 3 s off: sure now
  CHECK_EQUAL(Count(nav.TakeLines(), Category::MissedTurn), 1);
  nav.OnFix(At(76100), off);
  CHECK_EQUAL(Count(nav.TakeLines(), Category::Rerouted), 1);
  nav.OnFix(At(80000), Leg(7, 300));  // the new route's first step is not a completed turn
  CHECK_EQUAL(Count(nav.TakeLines(), Category::TurnCompleted), 0);
  CHECK_EQUAL(nav.Engine()->Stats().missedTurns, 1);
  CHECK_EQUAL(nav.Engine()->Stats().reroutes, 1);
  CHECK_EQUAL(nav.Engine()->Stats().turnsCompleted, 0);
}

TEST(OffRouteBackOnANewRouteQuicklyIsAMiss)
{
  Navigator nav(Config());
  Approach(nav);
  nav.OnFix(At(71000), Leg(1, 50, RouteState::OffRoute));
  nav.OnFix(At(72500), Leg(7, 300));  // a new route, not the same turn: it was missed
  auto const lines = nav.TakeLines();
  CHECK_EQUAL(Count(lines, Category::MissedTurn), 1);
  CHECK_EQUAL(Count(lines, Category::TurnCompleted), 0);
  CHECK_EQUAL(nav.Engine()->Stats().missedTurns, 1);
  CHECK_EQUAL(nav.Engine()->Stats().reroutes, 1);  // counted quietly, as before
}

TEST(OffRouteAfterAVoiceCommandIsCalledAtOnce)
{
  // The driver asked for the change: unchanged, the miss is said straight away and the reroute counts quietly.
  Navigator nav(Config());
  Approach(nav);
  nav.NoteVoiceCommand();
  nav.OnFix(At(71000), Leg(1, 50, RouteState::OffRoute));
  auto const lines = nav.TakeLines();
  CHECK_EQUAL(Count(lines, Category::MissedTurn), 1);
  CHECK_EQUAL(Count(lines, Category::Rerouted), 0);
  CHECK_EQUAL(nav.Engine()->Stats().reroutes, 1);
}

TEST(TownDriveWithTenBlipsSaysNoMissOrReroute)
{
  // Five minutes of town streets at 12 m/s, a 360 m leg every 30 s; ten turns get a one-second blip 30-50 m out.
  Navigator nav(Config());
  std::vector<Commentary> said;
  int blips = 0;
  for (int s = 0; s <= 300; ++s)
  {
    uint32_t const leg = static_cast<uint32_t>(s / 30) + 1;
    double const dist = 360 - 12.0 * (s % 30);
    bool const blip = leg >= 1 && leg <= 10 && s % 30 == 27 - static_cast<int>(leg % 3);
    blips += blip;
    nav.OnFix(At(1.7e12 + s * 1000.0, 12), Leg(leg, dist, blip ? RouteState::OffRoute : RouteState::OnRoute));
    for (auto & l : nav.TakeLines())
      said.push_back(std::move(l));
  }
  CHECK_EQUAL(blips, 10);
  CHECK_EQUAL(Count(said, Category::MissedTurn), 0);
  CHECK_EQUAL(Count(said, Category::Rerouted), 0);
  CHECK_EQUAL(nav.Engine()->Stats().missedTurns, 0);
  CHECK_EQUAL(nav.Engine()->Stats().reroutes, 0);
  CHECK_EQUAL(nav.Engine()->Stats().turnsCompleted, 10);
}
