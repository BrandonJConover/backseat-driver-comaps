// Sparse fixes keep the final call (loop B148, BI-132): with one fix every 5 s at 30 m/s (battery
// saver, weak GPS) the 150 m steps jumped over the plain "Now, turn left ..." window, so a left 3 km
// out got "In 1 mile" and then only "You are turning left" at the turn. Now, when the next fix will
// land past the final mark, the final call goes out on this one.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/units.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
struct Heard
{
  std::string text;
  std::string key;
  double distM;  // distance to the maneuver on the update that said it
};

/// Fixes every `gapS` seconds at a steady `mps` from `fromM` to the maneuver, the last one at 0.
std::vector<Heard> Approach(char const * text, double fromM, double mps, double gapS)
{
  EngineConfig c;
  c.rng = [] { return 0.0; };
  c.units = Units::Mph;
  CritiqueEngine e(c, 0);
  std::vector<Heard> heard;
  auto feed = [&](std::vector<Commentary> const & out, double d) {
    for (auto const & x : out)
      heard.push_back({x.text, x.key, d});
  };
  TelemetrySample s;
  s.t = 0;
  s.speedMps = mps;
  s.headingDeg = 90;
  feed(e.OnTelemetry(s), fromM);
  feed(e.OnNav(NavEvent::Instruction(0, text, fromM)), fromM);
  double t = 0, d = fromM;
  while (d > 0)
  {
    t += gapS * 1000;
    d = std::max(0.0, d - mps * gapS);
    s.t = t;
    feed(e.OnTelemetry(s), d);
    feed(e.OnNav(NavEvent::Progress(t, d)), d);
  }
  return heard;
}

int FinalCalls(std::vector<Heard> const & heard, bool beforeTurnOnly)
{
  int n = 0;
  for (auto const & h : heard)
    if (h.text.rfind("Now, ", 0) == 0 && (!beforeTurnOnly || h.distM > 0))
      ++n;
  return n;
}
}  // namespace

TEST(AFiveSecondFixAtHighwaySpeedStillGetsTheFinalCall)
{
  auto const heard = Approach("Turn left onto Elm Street", 3000, 30, 5);
  CHECK_EQUAL(FinalCalls(heard, true), 1);
}

TEST(EveryManeuverGetsOneFinalCallBeforeTheTurn)
{
  int failures = 0;
  for (double const mps : {5.0, 10.0, 15.0, 20.0, 25.0, 30.0, 35.0})
    for (double const gapS : {1.0, 2.0, 3.0, 4.0, 5.0})
      for (double const legM : {300.0, 1000.0, 3000.0, 20000.0})
      {
        auto const heard = Approach("Turn left onto Elm Street", legM, mps, gapS);
        int const before = FinalCalls(heard, true);
        int const all = FinalCalls(heard, false);
        if (before != 1 || all != 1)
        {
          ++failures;
          std::printf("  final calls %d (%d before the turn) at %.0f m/s, %.0f s fixes, %.0f m leg\n", all, before,
                      mps, gapS, legM);
        }
      }
  CHECK_EQUAL(failures, 0);
}

TEST(AVerySparseFixGetsTheFarCallAFixEarly)
{
  // 10 s fixes at 30 m/s: 400 m (past the 360 m far mark), then 100 m (inside the final window).
  // The far call goes out at 400 m and says what's left, then the final call at 100 m.
  auto const heard = Approach("Turn left onto Elm Street", 3100, 30, 10);
  int far = 0;
  for (auto const & h : heard)
    if (h.key == "countdown.far")
    {
      ++far;
      CHECK_EQUAL(h.distM, 400.0);
    }
  CHECK_EQUAL(far, 1);
  CHECK_EQUAL(FinalCalls(heard, true), 1);
}

TEST(AStopAtALightIsNotAFixRate)
{
  // A minute stopped 600 m out, then 1 Hz again: the long gap must not make the final call early.
  EngineConfig c;
  c.rng = [] { return 0.0; };
  c.units = Units::Mph;
  CritiqueEngine e(c, 0);
  TelemetrySample s;
  s.speedMps = 15;
  s.headingDeg = 90;
  e.OnTelemetry(s);
  e.OnNav(NavEvent::Instruction(0, "Turn left onto Elm Street", 1000));
  double t = 0, d = 1000, finalAtM = -1;
  auto step = [&](double gapS) {
    t += gapS * 1000;
    d = std::max(0.0, d - 15 * gapS);
    s.t = t;
    e.OnTelemetry(s);
    for (auto const & x : e.OnNav(NavEvent::Progress(t, d)))
      if (x.text.rfind("Now, ", 0) == 0)
        finalAtM = d;
  };
  while (d > 600)
    step(1);
  t += 60000;  // parked at the light, no progress updates
  while (d > 0)
    step(1);
  CHECK(finalAtM > 0 && finalAtM <= 60);  // 4 s out at 15 m/s, as at 1 Hz
}
