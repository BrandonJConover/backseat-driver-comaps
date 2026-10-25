#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/units.hpp"

using namespace backseat;

namespace
{
EngineConfig Config()
{
  EngineConfig c;
  c.rng = SeededRng(3);
  return c;
}

RouteSnapshot OnRoute(uint32_t turnId, TurnKind turn, double dist, std::string street = "Elm Street")
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = turnId;
  r.turn = turn;
  r.distToTurnM = dist;
  r.nextStreet = std::move(street);
  r.destination = "Grandma's";
  r.etaSeconds = 600;
  r.plannedTurns = {TurnKind::Left, TurnKind::Right, TurnKind::Destination};
  return r;
}

Fix At(double tMs, double mps, std::optional<double> limit = std::nullopt)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.7;
  f.lon = -105;
  f.speedMps = mps;
  f.speedLimitMps = limit;
  return f;
}

bool Has(std::vector<Commentary> const & lines, Category c)
{
  for (auto const & l : lines)
    if (l.category == c)
      return true;
  return false;
}
}  // namespace

TEST(InstructionTextReadsLikeAGps)
{
  CHECK_EQUAL(InstructionText(TurnKind::Left, "Elm Street", ""), std::string("Turn left onto Elm Street"));
  CHECK_EQUAL(InstructionText(TurnKind::SlightRight, "", ""), std::string("Bear right"));
  CHECK_EQUAL(InstructionText(TurnKind::Destination, "Main St", "Grandma's"), std::string("Arrive at Grandma's"));
}

TEST(NavigatorStaysQuietWithoutARoute)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 20), RouteSnapshot{});
  CHECK(!nav.InTrip());
  CHECK(nav.TakeLines().empty());
}

TEST(NavigatorOpensWithGreetingRundownAndInstruction)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Left, 400));
  auto const lines = nav.TakeLines();
  // Greeting, rundown, time prediction, then the first instruction.
  CHECK_EQUAL(lines.size(), size_t{4});
  CHECK(lines[0].text.find("didn't ask to be in this car") != std::string::npos);
  CHECK(lines[1].text.find("turn left") != std::string::npos);
  CHECK(lines[3].text.find("turn left onto Elm Street") != std::string::npos);
}

TEST(NavigatorTurnsStepChangesIntoCompletedTurns)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Left, 400));
  nav.TakeLines();
  nav.OnFix(At(1000, 10), OnRoute(1, TurnKind::Left, 140));
  CHECK(Has(nav.TakeLines(), Category::Instruction));  // the 500 ft nag
  nav.OnFix(At(30000, 10), OnRoute(2, TurnKind::Right, 300, "Oak Avenue"));
  auto const lines = nav.TakeLines();
  CHECK(Has(lines, Category::TurnCompleted));
  CHECK_EQUAL(nav.Engine()->Stats().turnsCompleted, 1);
}

TEST(NavigatorCallsOffRouteNearTheTurnAMiss)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Left, 400));
  nav.OnFix(At(1000, 10), OnRoute(1, TurnKind::Left, 20));
  nav.TakeLines();
  RouteSnapshot off = OnRoute(1, TurnKind::Left, 20);
  off.state = RouteState::OffRoute;
  nav.OnFix(At(2000, 10), off);
  auto const lines = nav.TakeLines();
  CHECK(Has(lines, Category::MissedTurn));
  CHECK(Has(lines, Category::Rerouted));
  // The new route's first step isn't a completed turn.
  nav.OnFix(At(9000, 10), OnRoute(7, TurnKind::Right, 300));
  CHECK(!Has(nav.TakeLines(), Category::TurnCompleted));
  CHECK_EQUAL(nav.Engine()->Stats().missedTurns, 1);
}

TEST(NavigatorRoastsSpeedingFromTheFix)
{
  Navigator nav(Config());
  nav.OnFix(At(0, FromDisplay(50, Units::Mph), FromDisplay(50, Units::Mph)), OnRoute(1, TurnKind::Left, 900));
  nav.TakeLines();
  nav.OnFix(At(1000, FromDisplay(51, Units::Mph), FromDisplay(50, Units::Mph)), OnRoute(1, TurnKind::Left, 880));
  CHECK(Has(nav.TakeLines(), Category::Speeding));
}

TEST(NavigatorArrivesOnceAndRecordsTheTrip)
{
  int recorded = 0;
  Navigator nav(Config(), {}, [&](TripRecord const & r, TripReport const &)
  {
    ++recorded;
    CHECK_EQUAL(r.destination, std::string("Grandma's"));
  });
  nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Destination, 300));
  RouteSnapshot done = OnRoute(1, TurnKind::Destination, 0);
  done.state = RouteState::Finished;
  nav.OnFix(At(60000, 0), done);
  nav.Poll(done);
  auto const lines = nav.TakeLines();
  CHECK(Has(lines, Category::Arrived));
  CHECK_EQUAL(recorded, 1);
  nav.Poll(RouteSnapshot{});
  CHECK(!nav.InTrip());
}
