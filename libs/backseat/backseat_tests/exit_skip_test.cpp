// "Not this exit, the next one" (loop B100, BI-086). When the route's exit has another exit just
// before it, the driver hears "Skip this first exit. Yours is the next one." once, before the earlier
// exit and before the final call, and the instruction itself is unchanged.
#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/lines.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
RouteSnapshot Leg(uint32_t turnId, TurnKind turn, double dist, int exitsBefore, double gapM)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = turnId;
  r.turn = turn;
  r.distToTurnM = dist;
  r.nextStreet = "Main Street";
  r.destination = "Grandma's";
  r.etaSeconds = 1800;
  r.plannedTurns = {TurnKind::SlightRight, turn, TurnKind::Right, TurnKind::Destination};
  r.exitsBeforeTurn = exitsBefore;
  r.exitBeforeGapM = gapM;
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

bool IsSkip(std::string const & text)
{
  for (int tier = 0; tier < 3; ++tier)
    for (auto const & line : LinesFor("countdown.exit.skip", tier))
      if (text == line)
        return true;
  return false;
}

struct Heard
{
  int skips = 0;
  std::optional<double> skipAtM;  // distance to the route's exit when the skip line went out
  std::optional<double> nowAtM;   // ... and when the "Now, ..." call went out
  std::vector<std::string> instructions;
};

/// A long highway leg, then the exit leg from 2.5 km out at a steady `mps` and 1 Hz fixes.
Heard Drive(TurnKind turn, int exitsBefore, double gapM, double mps = 30)
{
  EngineConfig c;
  c.rng = SeededRng(11);
  Navigator nav(c);
  double t = 0;
  for (double d = 3000; d > 0; d -= mps, t += 1000)
  {
    nav.OnFix(At(t, mps), Leg(1, TurnKind::SlightRight, d, -1, -1));
    nav.TakeLines();
  }
  Heard out;
  for (double d = 2500; d > 0; d -= mps, t += 1000)
  {
    nav.OnFix(At(t, mps), Leg(2, turn, d, exitsBefore, gapM));
    for (auto const & l : nav.TakeLines())
    {
      if (IsSkip(l.text))
      {
        ++out.skips;
        out.skipAtM = d;
        CHECK(l.priority == Priority::Nav);
        continue;
      }
      if (l.category != Category::Instruction)
        continue;
      out.instructions.push_back(l.text);
      if (l.text.rfind("Now, ", 0) == 0 && !out.nowAtM)
        out.nowAtM = d;
    }
  }
  return out;
}
}  // namespace

TEST(AnExitRightAfterAnotherSaysSkipTheFirstOnce)
{
  for (TurnKind const turn : {TurnKind::ExitRight, TurnKind::ExitLeft})
  {
    auto const heard = Drive(turn, 1, 400);
    CHECK_EQUAL(heard.skips, 1);
    CHECK(heard.skipAtM.has_value());
    CHECK(heard.nowAtM.has_value());
    if (heard.skipAtM)
    {
      // Before the car reaches the earlier exit, and well before the final call.
      CHECK(*heard.skipAtM - 400 >= 60);
      if (heard.nowAtM)
        CHECK(*heard.skipAtM > *heard.nowAtM);
    }
  }
}

TEST(NoSkipLineWithoutAnExitInBetween)
{
  CHECK_EQUAL(Drive(TurnKind::ExitRight, 0, -1).skips, 0);
  CHECK_EQUAL(Drive(TurnKind::ExitRight, -1, -1).skips, 0);
  // Unknown gap, or two exits in between ("this first exit" could be the wrong one).
  CHECK_EQUAL(Drive(TurnKind::ExitRight, 1, -1).skips, 0);
  CHECK_EQUAL(Drive(TurnKind::ExitRight, 2, 400).skips, 0);
}

TEST(NoSkipLineWhenTheEarlierExitIsFarBack)
{
  CHECK_EQUAL(Drive(TurnKind::ExitRight, 1, 3000).skips, 0);
  // At town speed the window shrinks to its floor: 500 m is too far at 10 m/s.
  CHECK_EQUAL(Drive(TurnKind::ExitRight, 1, 500, 10).skips, 0);
}

TEST(OnlyExitsGetTheSkipLine)
{
  CHECK_EQUAL(Drive(TurnKind::Right, 1, 400).skips, 0);
}

TEST(TheSkipLineLeavesTheExitCallsAlone)
{
  auto const with = Drive(TurnKind::ExitRight, 1, 400);
  auto const without = Drive(TurnKind::ExitRight, 0, -1);
  CHECK(with.instructions == without.instructions);
  CHECK(!with.instructions.empty());
}

TEST(ExitSkipWindowScalesWithSpeed)
{
  CHECK_EQUAL(ExitSkipWithinM(0), 300.0);
  CHECK_EQUAL(ExitSkipWithinM(30), 810.0);
  CHECK_EQUAL(ExitSkipWithinM(60), 1000.0);
}

TEST(SkipLinesKeepEyesOnTheRoad)
{
  for (int tier = 0; tier < 3; ++tier)
  {
    CHECK(!LinesFor("countdown.exit.skip", tier).empty());
    for (auto const & line : LinesFor("countdown.exit.skip", tier))
    {
      for (char const * bad : {"look", "screen", "tap", "touch", "phone", "map"})
      {
        std::string lower = line;
        for (char & ch : lower)
          ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        CHECK(lower.find(bad) == std::string::npos);
      }
    }
  }
}
