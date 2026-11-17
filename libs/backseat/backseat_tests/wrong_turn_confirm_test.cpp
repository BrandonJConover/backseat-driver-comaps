// False "That was a left. I said right." (core GPS round 2, 2026-11-15): the heading check called any
// 60-degree swing within 120 m of a turn a wrong turn, so a road that bends left just before a right
// turn got the driver roasted, and it counted against the grade. In the Navigator a wrong-way swing now
// waits for the route to agree (the car leaves it, or the router calls the turn missed). Still on the
// route and closing in on the turn, it was a bend and nothing is said.
#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/lines.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
double constexpr kT0 = 1.7e12;

EngineConfig Seeded()
{
  EngineConfig c;
  c.rng = SeededRng(21);
  return c;
}

RouteSnapshot Leg(RouteState state, uint32_t turnId, double dist)
{
  RouteSnapshot r;
  r.state = state;
  r.turnId = turnId;
  r.turn = TurnKind::Right;
  r.distToTurnM = dist;
  r.nextStreet = "Cameo Lane";
  r.destination = "Grandma's";
  r.etaSeconds = 600;
  r.plannedTurns = {TurnKind::Right, TurnKind::Left, TurnKind::Destination};
  return r;
}

Fix FixAt(double tMs, double mps, double bearing)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 36.39;
  f.lon = -94.29;
  f.speedMps = mps;
  f.bearingDeg = bearing;
  f.accuracyM = 5;
  return f;
}

bool HasKey(std::vector<Commentary> const & lines, std::string const & prefix)
{
  return std::any_of(lines.begin(), lines.end(), [&](Commentary const & l) { return l.key.rfind(prefix, 0) == 0; });
}

struct Drive
{
  Navigator nav{Seeded()};
  double t = kT0;
  std::vector<Commentary> said;

  void Step(double mps, double bearing, RouteSnapshot const & r)
  {
    t += 1000;
    nav.OnFix(FixAt(t, mps, bearing), r);
    auto const lines = nav.TakeLines();
    said.insert(said.end(), lines.begin(), lines.end());
  }

  /// Straight north toward a right turn, ending `endM` short of it.
  double Approach(double fromM, double endM)
  {
    double dist = fromM;
    while (dist > endM)
    {
      Step(10, 0, Leg(RouteState::OnRoute, 4, dist));
      dist -= 10;
    }
    return dist;
  }
};
}  // namespace

TEST(ARoadBendingBeforeTheTurnIsntAWrongTurn)
{
  Drive d;
  double dist = d.Approach(400, 115);
  // The road bends 70 degrees left over the last 110 m; the car stays on the route the whole way.
  for (int i = 0; i < 8; ++i, dist -= 10)
    d.Step(10, 360 - 9 * (i + 1), Leg(RouteState::OnRoute, 4, dist));
  // Then the right turn is made and the router moves on.
  d.Step(8, 20, Leg(RouteState::OnRoute, 4, 15));
  d.Step(8, 60, Leg(RouteState::OnRoute, 9, 800));
  CHECK(!HasKey(d.said, "wrongTurn."));
  CHECK_EQUAL(d.nav.Engine()->Stats().wrongTurns, 0);
}

TEST(AWrongTurnTheRouteConfirmsIsStillCalled)
{
  Drive d;
  double dist = d.Approach(400, 45);
  // Left instead of right at the turn: the heading swings left and the car stops closing in.
  for (int i = 0; i < 7; ++i)
    d.Step(8, 360 - 13 * (i + 1), Leg(RouteState::OnRoute, 4, dist));
  CHECK(!HasKey(d.said, "wrongTurn."));
  // The router takes the car off the route: now the line comes, once.
  d.Step(10, 270, Leg(RouteState::OffRoute, 4, dist));
  d.Step(10, 270, Leg(RouteState::OffRoute, 4, dist));
  for (int i = 0; i < 5; ++i)
    d.Step(10, 270, Leg(RouteState::OffRoute, 4, dist));
  CHECK(HasKey(d.said, "wrongTurn.right"));
  CHECK_EQUAL(static_cast<int>(std::count_if(d.said.begin(), d.said.end(), [](Commentary const & l) {
                return l.key.rfind("wrongTurn.", 0) == 0 || l.key == "missedTurn";
              })),
              1);
  CHECK_EQUAL(d.nav.Engine()->Stats().wrongTurns, 1);
}

TEST(ASwingAfterLeavingTheRouteIsCalledRightAway)
{
  Drive d;
  double dist = d.Approach(400, 45);
  // The router notices first, then the heading finishes swinging the wrong way.
  d.Step(8, 350, Leg(RouteState::OffRoute, 4, dist));
  for (int i = 0; i < 3; ++i)
    d.Step(8, 350 - 25 * (i + 1), Leg(RouteState::OffRoute, 4, dist));
  CHECK(HasKey(d.said, "wrongTurn.right"));
}
