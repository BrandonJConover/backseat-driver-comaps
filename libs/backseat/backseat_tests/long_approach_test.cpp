// A turn announced far out keeps its countdown (loop B146, BI-130): the engine used to forget a
// pending turn three minutes after it was announced, so a left 3 km out at 8 m/s, or an exit 15 km
// out on the highway, got the "In 1 mile" reminder and then nothing: no far call, no "Now, ...".
// Now a turn is only stale after minutes with no progress toward it, and stale only mutes the
// heading checks; getting closer again revives it.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/units.hpp"

#include <string>
#include <vector>

using namespace backseat;

namespace
{
CritiqueEngine Engine()
{
  EngineConfig c;
  c.rng = [] { return 0.0; };
  c.units = Units::Mph;
  return CritiqueEngine(c, 0);
}

void Drive(CritiqueEngine & e, double t, double mps, double headingDeg, std::vector<std::string> & heard)
{
  TelemetrySample s;
  s.t = t;
  s.speedMps = mps;
  s.headingDeg = headingDeg;
  for (auto const & c : e.OnTelemetry(s))
    heard.push_back(c.text);
}

void Nav(CritiqueEngine & e, NavEvent const & ev, std::vector<std::string> & heard)
{
  for (auto const & c : e.OnNav(ev))
    heard.push_back(c.text);
}

int Count(std::vector<std::string> const & heard, std::string const & prefix)
{
  int n = 0;
  for (auto const & s : heard)
    n += s.rfind(prefix, 0) == 0 ? 1 : 0;
  return n;
}

bool Has(std::vector<std::string> const & heard, std::string const & part)
{
  for (auto const & s : heard)
    if (s.find(part) != std::string::npos)
      return true;
  return false;
}

/// 1 Hz fixes with a steady heading from `fromM` to the maneuver; everything said.
std::vector<std::string> Approach(char const * text, double fromM, double mps)
{
  auto e = Engine();
  std::vector<std::string> heard;
  Drive(e, 0, mps, 90, heard);
  Nav(e, NavEvent::Instruction(0, text, fromM), heard);
  double t = 1000;
  for (double d = fromM - mps; d > 0; d -= mps, t += 1000)
  {
    Drive(e, t, mps, 90, heard);
    Nav(e, NavEvent::Progress(t, d), heard);
  }
  return heard;
}
}  // namespace

TEST(ALongCityApproachKeepsItsCalls)
{
  for (double const mps : {8.0, 13.0, 18.0})
  {
    auto const heard = Approach("Turn left onto Elm Street", 3000, mps);
    CHECK_EQUAL(Count(heard, "Now, turn left onto Elm Street."), 1);
    CHECK(Has(heard, "In 1 mile, turn left onto Elm Street."));
    // The far call, in its own words, between the reminder and the final call.
    CHECK(heard.size() >= 4);
  }
}

TEST(AHighwayExitFarOutKeepsItsCalls)
{
  for (double const fromM : {15000.0, 40000.0})
  {
    auto const heard = Approach("Take the exit on the right", fromM, 30);
    CHECK_EQUAL(Count(heard, "Now, take the exit on the right."), 1);
    CHECK(Has(heard, "In 1 mile, take the exit on the right."));
  }
}

TEST(AParkedApproachStillGetsItsCallsAfter)
{
  auto e = Engine();
  std::vector<std::string> heard;
  Drive(e, 0, 13, 90, heard);
  Nav(e, NavEvent::Instruction(0, "Turn left onto Elm Street", 3000), heard);
  double t = 1000, d = 3000;
  for (; d > 2000; d -= 13, t += 1000)
  {
    Drive(e, t, 13, 90, heard);
    Nav(e, NavEvent::Progress(t, d), heard);
  }
  // Ten minutes at a coffee shop, then on to the turn.
  for (double end = t + 600000; t < end; t += 1000)
    Drive(e, t, 0, 90, heard);
  heard.clear();
  for (; d > 0; d -= 13, t += 1000)
  {
    Drive(e, t, 13, 90, heard);
    Nav(e, NavEvent::Progress(t, d), heard);
  }
  CHECK_EQUAL(Count(heard, "Now, turn left onto Elm Street."), 1);
}

TEST(AStaleTurnGetsNoHeadingVerdict)
{
  // Parked 10 minutes 100 m from the left, then pulling out of the lot swings right before any
  // progress toward the turn: that swing is about the parking lot, not "your other left".
  auto e = Engine();
  std::vector<std::string> heard;
  Drive(e, 0, 13, 90, heard);
  Nav(e, NavEvent::Instruction(0, "Turn left onto Elm Street", 400), heard);
  double t = 1000, d = 400;
  for (; d > 100; d -= 13, t += 1000)
  {
    Drive(e, t, 13, 90, heard);
    Nav(e, NavEvent::Progress(t, d), heard);
  }
  for (double end = t + 600000; t < end; t += 1000)
    Drive(e, t, 0, 90, heard);
  int const wrongBefore = e.Stats().wrongTurns;
  // Rolling out of the lot: heading 90 -> 180 (a right swing), distance not shrinking.
  for (double h = 90; h <= 180; h += 10, t += 1000)
  {
    Drive(e, t, 4, h, heard);
    Nav(e, NavEvent::Progress(t, d + 40), heard);
  }
  for (int i = 0; i < 8; ++i, t += 1000)
    Drive(e, t, 4, 180, heard);
  CHECK_EQUAL(e.Stats().wrongTurns, wrongBefore);
}

TEST(AFreshTurnStillGetsItsHeadingVerdict)
{
  // The live case is unchanged: a right swing at a left turn is "your other left".
  auto e = Engine();
  std::vector<std::string> heard;
  Drive(e, 0, 10, 90, heard);
  Nav(e, NavEvent::Instruction(0, "Turn left onto Elm Street", 300), heard);
  double t = 1000, d = 300;
  for (; d > 50; d -= 10, t += 1000)
  {
    Drive(e, t, 10, 90, heard);
    Nav(e, NavEvent::Progress(t, d), heard);
  }
  for (double h = 90; h <= 180; h += 15, t += 1000)
    Drive(e, t, 8, h, heard);
  for (int i = 0; i < 8; ++i, t += 1000)
    Drive(e, t, 8, 180, heard);
  CHECK_EQUAL(e.Stats().wrongTurns, 1);
}
