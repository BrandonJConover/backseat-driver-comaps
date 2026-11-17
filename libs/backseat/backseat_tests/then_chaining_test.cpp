// "Then" chaining scales with speed (loop B86, BI-077). The maneuver after next used to be chained onto
// the call only within a fixed 150 m: at 30 m/s that is 5 s, so an exit 350 m after a merge came before
// its own call could, and the first call never warned of it. Exits and roundabouts were never chained.
// Now: within 10 s of driving (150 m floor, 500 m cap); straight still never chains.
#include "check.hpp"

#include "backseat/navigator.hpp"

#include <string>

using namespace backseat;

namespace
{
RouteSnapshot Pair(TurnKind turn, std::string street, TurnKind then, double gapM, std::string thenStreet = "")
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = turn;
  r.distToTurnM = 600;
  r.nextStreet = std::move(street);
  r.thenTurn = then;
  r.thenStreet = std::move(thenStreet);
  r.thenGapM = gapM;
  r.destination = "Grandma's";
  return r;
}

Fix At(double tMs, double mps)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.7;
  f.lon = -105;
  f.speedMps = mps;
  return f;
}
}  // namespace

TEST(ChainingWindowGrowsWithSpeed)
{
  CHECK_EQUAL(ChainWithinM(0), 150.0);
  CHECK_EQUAL(ChainWithinM(10), 150.0);
  CHECK_EQUAL(ChainWithinM(30), 300.0);
  CHECK_EQUAL(ChainWithinM(45), 450.0);
  CHECK_EQUAL(ChainWithinM(80), 500.0);  // capped
  CHECK_EQUAL(ChainWithinM(-1), 150.0);  // unknown
  CHECK_EQUAL(ChainWithinM(1.0 / 0.0), 150.0);
}

TEST(AFarSecondManeuverChainsAtHighwaySpeedOnly)
{
  auto const r = Pair(TurnKind::SlightRight, "I-25 North", TurnKind::ExitRight, 280);
  CHECK_EQUAL(InstructionText(r, 30), std::string("Bear right onto I-25 North, then take the exit on the right"));
  CHECK_EQUAL(InstructionText(r, 10), std::string("Bear right onto I-25 North"));
  CHECK_EQUAL(InstructionText(r), std::string("Bear right onto I-25 North"));  // default keeps 150 m
  auto far = r;
  far.thenGapM = 350;
  CHECK_EQUAL(InstructionText(far, 30), std::string("Bear right onto I-25 North"));  // 10 s at 30 m/s is 300 m
  CHECK_EQUAL(InstructionText(far, 40), std::string("Bear right onto I-25 North, then take the exit on the right"));
  far.thenGapM = 520;
  CHECK_EQUAL(InstructionText(far, 80), std::string("Bear right onto I-25 North"));  // past the cap
}

TEST(ExitsAndRoundaboutsChainToo)
{
  CHECK_EQUAL(InstructionText(Pair(TurnKind::Right, "Elm Street", TurnKind::ExitLeft, 120, "Main Street")),
              std::string("Turn right onto Elm Street, then take the exit on the left onto Main Street"));
  CHECK_EQUAL(InstructionText(Pair(TurnKind::Right, "Elm Street", TurnKind::ExitRight, 120)),
              std::string("Turn right onto Elm Street, then take the exit on the right"));
  CHECK_EQUAL(InstructionText(Pair(TurnKind::Left, "Elm Street", TurnKind::Roundabout, 120)),
              std::string("Turn left onto Elm Street, then take the roundabout"));
  CHECK_EQUAL(InstructionText(Pair(TurnKind::Roundabout, "", TurnKind::Right, 120, "Oak Lane")),
              std::string("Take the roundabout, then right onto Oak Lane"));
}

TEST(GoingStraightNeverChains)
{
  CHECK_EQUAL(InstructionText(Pair(TurnKind::Left, "Elm Street", TurnKind::Straight, 60), 30),
              std::string("Turn left onto Elm Street"));
  CHECK_EQUAL(InstructionText(Pair(TurnKind::Left, "Elm Street", TurnKind::None, 60), 30),
              std::string("Turn left onto Elm Street"));
  CHECK_EQUAL(InstructionText(Pair(TurnKind::Straight, "", TurnKind::Right, 60), 30), std::string("Continue straight"));
}

TEST(TheNavigatorChainsAtItsOwnSpeed)
{
  for (double const mps : {30.0, 10.0})
  {
    Navigator nav(EngineConfig{});
    std::string said;
    double t = 0;
    for (double d = 900; d > 0 && said.empty(); d -= mps, t += 1000)
    {
      nav.OnFix(At(t, mps), Pair(TurnKind::SlightRight, "I-25 North", TurnKind::ExitRight, 280));
      nav.TakeLines();
      if (!nav.LastInstructionText().empty())
        said = nav.LastInstructionText();
    }
    CHECK(!said.empty());
    bool const chained = said.find("then take the exit on the right") != std::string::npos;
    CHECK_EQUAL(chained, mps > 20);
  }
}
