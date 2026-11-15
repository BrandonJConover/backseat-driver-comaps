// Rerouting, fast and smooth. A rebuilt route numbers its maneuvers afresh, so the new route's first turn
// could carry the same id as the turn just missed: the navigator took it for the same maneuver and said
// nothing until the "Now" call. The direction also has to come first when the route lands, ahead of the
// "you missed it", and the host can hand the new route over between GPS fixes (Poll) to have it said at once.
#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/lines.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
EngineConfig Config()
{
  EngineConfig c;
  c.rng = SeededRng(5);
  return c;
}

RouteSnapshot Leg(uint64_t routeId, uint32_t turnId, double dist, TurnKind turn = TurnKind::Right,
                  RouteState state = RouteState::OnRoute)
{
  RouteSnapshot r;
  r.state = state;
  r.routeId = routeId;
  if (state == RouteState::OnRoute)
  {
    r.turnId = turnId;
    r.turn = turn;
    r.distToTurnM = dist;
    r.nextStreet = turn == TurnKind::Right ? "Elm Street" : "Oak Avenue";
  }
  r.destination = "Grandma's";
  r.etaSeconds = 900;
  r.plannedTurns = {TurnKind::Right, TurnKind::Left, TurnKind::Destination};
  return r;
}

RouteSnapshot Off(uint64_t routeId) { return Leg(routeId, 0, 0, TurnKind::None, RouteState::OffRoute); }
RouteSnapshot Unmatched(uint64_t routeId) { return Leg(routeId, 0, 0, TurnKind::None, RouteState::NotStarted); }

Fix At(double tMs, double mps = 12)
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

int IndexOf(std::vector<Commentary> const & lines, Category c)
{
  for (size_t i = 0; i < lines.size(); ++i)
    if (lines[i].category == c)
      return static_cast<int>(i);
  return -1;
}

bool Says(std::vector<Commentary> const & lines, std::string const & text)
{
  for (auto const & l : lines)
    if (l.category == Category::Instruction && l.text.find(text) != std::string::npos)
      return true;
  return false;
}

// Route 1, turn 3 (right onto Elm) 60 m out, lines taken.
Navigator & Approach(Navigator & nav)
{
  nav.OnFix(At(0), Leg(1, 3, 900));
  nav.OnFix(At(60000), Leg(1, 3, 140));
  nav.OnFix(At(70000), Leg(1, 3, 60));
  nav.TakeLines();
  return nav;
}
}  // namespace

TEST(RebuiltRouteWithTheSameTurnIdStillGetsItsDirection)
{
  // Missed the right onto Elm; the new route's first maneuver (a left onto Oak) happens to be index 3 too.
  Navigator nav(Config());
  Approach(nav);
  nav.OnFix(At(71000), Off(1));
  nav.OnFix(At(72000), Off(1));
  nav.TakeLines();
  nav.OnFix(At(73000), Leg(2, 3, 400, TurnKind::Left));
  auto const lines = nav.TakeLines();
  CHECK(Says(lines, "Oak Avenue"));
  CHECK_EQUAL(Count(lines, Category::MissedTurn), 1);
  CHECK_EQUAL(Count(lines, Category::TurnCompleted), 0);
  CHECK_EQUAL(nav.Engine()->Stats().missedTurns, 1);
  CHECK_EQUAL(nav.Engine()->Stats().reroutes, 1);
}

TEST(RebuiltRouteSaysTheWayBackBeforeTheJoke)
{
  // Back on a new route before the miss was sure: the direction goes out first, the "you missed it" after.
  Navigator nav(Config());
  Approach(nav);
  nav.OnFix(At(71000), Off(1));
  nav.TakeLines();
  nav.OnFix(At(72500), Leg(2, 5, 400, TurnKind::Left));
  auto const lines = nav.TakeLines();
  int const direction = IndexOf(lines, Category::Instruction);
  int const missed = IndexOf(lines, Category::MissedTurn);
  CHECK(direction >= 0);
  CHECK(missed >= 0);
  CHECK(direction < missed);
}

TEST(MissWaitsForTheRebuiltRouteToFindTheCar)
{
  // The new route has arrived but hasn't matched the car yet (no turn): the miss holds for the direction.
  Navigator nav(Config());
  Approach(nav);
  nav.OnFix(At(71000), Off(1));
  nav.TakeLines();
  nav.OnFix(At(72000), Unmatched(2));
  CHECK_EQUAL(Count(nav.TakeLines(), Category::MissedTurn), 0);
  nav.OnFix(At(72600), Leg(2, 5, 400, TurnKind::Left));
  auto const lines = nav.TakeLines();
  CHECK(IndexOf(lines, Category::Instruction) >= 0);
  CHECK(IndexOf(lines, Category::Instruction) < IndexOf(lines, Category::MissedTurn));
  CHECK_EQUAL(nav.Engine()->Stats().missedTurns, 1);
}

TEST(MissIsSaidOnTimeEvenIfTheRebuiltRouteNeverFindsTheCar)
{
  Navigator nav(Config());
  Approach(nav);
  nav.OnFix(At(71000), Off(1));
  nav.TakeLines();
  nav.OnFix(At(72000), Unmatched(2));
  nav.OnFix(At(74500), Unmatched(2));  // past the 3 s confirm
  CHECK_EQUAL(Count(nav.TakeLines(), Category::MissedTurn), 1);
}

TEST(RebuiltRouteIsSaidBetweenFixes)
{
  // The host hands the matched new route over as soon as it lands (Poll), not at the next GPS fix.
  Navigator nav(Config());
  Approach(nav);
  nav.OnFix(At(71000), Off(1));
  nav.OnFix(At(72000), Off(1));
  nav.TakeLines();
  nav.Poll(Leg(2, 2, 350, TurnKind::Left));
  auto const lines = nav.TakeLines();
  CHECK(Says(lines, "Oak Avenue"));
  // The next fix on the same route doesn't say it again.
  nav.OnFix(At(73000), Leg(2, 2, 340, TurnKind::Left));
  CHECK(!Says(nav.TakeLines(), "Oak Avenue"));
}

TEST(NewRouteWithTheSameNextTurnIsQuiet)
{
  // A rebuild while on the route (traffic, a stop added) with the same right turn next: it says exactly
  // what it would have said on the old route, and it's no completed turn.
  auto const drive = [](bool rebuilt)
  {
    Navigator nav(Config());
    nav.OnFix(At(0), Leg(1, 3, 900));
    nav.OnFix(At(30000), Leg(1, 3, 600));
    nav.TakeLines();
    std::vector<Commentary> said;
    for (int s = 1; s <= 50; ++s)
    {
      nav.OnFix(At(30000 + s * 1000.0), rebuilt ? Leg(2, 8, 600 - 12.0 * s) : Leg(1, 3, 600 - 12.0 * s));
      for (auto & l : nav.TakeLines())
        said.push_back(std::move(l));
    }
    CHECK_EQUAL(nav.Engine()->Stats().turnsCompleted, 0);
    return said;
  };
  auto const kept = drive(false);
  auto const rebuilt = drive(true);
  CHECK_EQUAL(Count(rebuilt, Category::TurnCompleted), 0);
  CHECK_EQUAL(Count(rebuilt, Category::Instruction), Count(kept, Category::Instruction));
  CHECK(Says(rebuilt, "Elm Street"));
}

TEST(NewRouteWithADifferentNextTurnSaysItButIsNoCompletedTurn)
{
  Navigator nav(Config());
  nav.OnFix(At(0), Leg(1, 3, 900));
  nav.OnFix(At(30000), Leg(1, 3, 600));
  nav.TakeLines();
  nav.OnFix(At(31000), Leg(2, 3, 250, TurnKind::Left));
  auto const lines = nav.TakeLines();
  CHECK(Says(lines, "Oak Avenue"));
  CHECK_EQUAL(Count(lines, Category::TurnCompleted), 0);
  CHECK_EQUAL(nav.Engine()->Stats().turnsCompleted, 0);
}

TEST(TurnCompletedStillCountsOnTheSameRoute)
{
  // Unchanged: moving on to the next maneuver of the same route is a completed turn.
  Navigator nav(Config());
  Approach(nav);
  nav.OnFix(At(76000), Leg(1, 3, 5));
  nav.OnFix(At(80000), Leg(1, 9, 500, TurnKind::Left));
  nav.TakeLines();
  CHECK_EQUAL(nav.Engine()->Stats().turnsCompleted, 1);
}
