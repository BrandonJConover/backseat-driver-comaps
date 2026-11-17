// A highway exit right after a merge (loop B81, BI-075). The next instruction used to wait for the fixed
// 250 m mark; at 33 m/s that is 7.5 s out, the "Now, ..." call then landed inside the instruction's own
// "just said" window and was dropped, so the driver got one call, late. Now the instruction comes about
// 15 s out at speed, and the final call still comes when it was the only other call.
#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/lines.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
RouteSnapshot Leg(uint32_t turnId, TurnKind turn, double dist, std::string street)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = turnId;
  r.turn = turn;
  r.distToTurnM = dist;
  r.nextStreet = std::move(street);
  r.destination = "Grandma's";
  r.etaSeconds = 1800;
  r.plannedTurns = {TurnKind::SlightRight, TurnKind::ExitRight, TurnKind::Right, TurnKind::Destination};
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

struct ExitCalls
{
  std::optional<double> instructionM;  // distance to the exit when the instruction went out
  std::optional<double> nowS;          // seconds before the exit the "Now, ..." call went out
  int calls = 0;                       // instruction-category lines about the exit
};

/// Merge onto the highway, then an exit `gapM` later, at a steady `mps` and 1 Hz fixes.
ExitCalls MergeThenExit(double mps, double gapM)
{
  EngineConfig c;
  c.rng = SeededRng(7);
  Navigator nav(c);
  double t = 0;
  // The on-ramp: a long leg so the opening lines are long over by the merge.
  for (double d = 3000; d > 0; d -= mps, t += 1000)
  {
    nav.OnFix(At(t, mps), Leg(1, TurnKind::SlightRight, d, "I-25 North"));
    nav.TakeLines();
  }
  ExitCalls out;
  for (double d = gapM; d > 0; d -= mps, t += 1000)
  {
    nav.OnFix(At(t, mps), Leg(2, TurnKind::ExitRight, d, "Main Street"));
    for (auto const & l : nav.TakeLines())
    {
      if (l.category != Category::Instruction)
        continue;
      ++out.calls;
      if (l.text.rfind("Now, ", 0) == 0)
        out.nowS = d / mps;
      else if (!out.instructionM && l.text.find("Main Street") != std::string::npos)
        out.instructionM = d;
    }
  }
  return out;
}
}  // namespace

TEST(AnExitRightAfterAMergeGetsTheInstructionAndTheFinalCall)
{
  for (double const mps : {25.0, 33.0})
  {
    for (double const gapM : {350.0, 400.0, 600.0})
    {
      auto const calls = MergeThenExit(mps, gapM);
      CHECK(calls.instructionM.has_value());
      CHECK(calls.nowS.has_value());
      if (calls.nowS)
        CHECK(*calls.nowS >= 3);
      // Early enough to move over: at once when the exit is under ~15 s away, else after the usual
      // breath following the merge.
      if (calls.instructionM)
        CHECK(*calls.instructionM >= gapM - (gapM < 15 * mps ? 1.5 : 4) * mps);
    }
  }
}

TEST(TownStreetsStillWaitForTheMarkAfterATurn)
{
  // 15 m/s (34 mph): 15 s is 225 m, under the 250 m mark, so a turn 300 m on still gets its breath
  // (3 s) after the last one before the instruction.
  auto const calls = MergeThenExit(15, 300);
  CHECK(calls.instructionM.has_value());
  if (calls.instructionM)
    CHECK(*calls.instructionM < 300 - 2 * 15 && *calls.instructionM > 230);
}
