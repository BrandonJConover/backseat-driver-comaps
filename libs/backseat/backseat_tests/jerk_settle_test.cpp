// B129: a hard-brake or hard-launch roast waits until the car has settled (EngineConfig::jerkSettleMs),
// so it never lands in the middle of an emergency stop. Stats and the incident log still count the
// jolt at its onset.
#include "check.hpp"

#include "backseat/engine.hpp"

using namespace backseat;

namespace
{
CritiqueEngine SettleEngine()
{
  EngineConfig c;
  c.rng = SeededRng(1);
  c.implausibleMps2 = 12;
  c.jerkSettleMs = 3000;
  return CritiqueEngine(c, 0);
}

TelemetrySample At(double t, double mps)
{
  TelemetrySample s;
  s.t = t;
  s.speedMps = mps;
  return s;
}

bool Has(std::vector<Commentary> const & out, Category c)
{
  for (auto const & l : out)
    if (l.category == c)
      return true;
  return false;
}

/// Feeds samples one per second from `t0` at `speeds`; returns when (ms) the first `c` line came, or -1.
double FirstAt(CritiqueEngine & e, double t0, std::vector<double> const & speeds, Category c, int * count = nullptr)
{
  double first = -1;
  for (size_t i = 0; i < speeds.size(); ++i)
  {
    double const t = t0 + 1000.0 * static_cast<double>(i);
    auto const out = e.OnTelemetry(At(t, speeds[i]));
    for (auto const & l : out)
      if (l.category == c)
      {
        if (first < 0)
          first = t;
        if (count)
          ++*count;
      }
  }
  return first;
}
}  // namespace

TEST(JerkSettleIsOffByDefault)
{
  EngineConfig c;
  CHECK_EQUAL(c.jerkSettleMs, 0.0);
  c.rng = SeededRng(1);
  CritiqueEngine e(c, 0);
  e.OnTelemetry(At(0, 25));
  auto const out = e.OnTelemetry(At(1000, 20));
  CHECK(Has(out, Category::HardBraking));
}

TEST(BrakeThenSteadyRoastsAfterThreeSteadySeconds)
{
  auto e = SettleEngine();
  // 25 -> 5 m/s over four seconds (5 m/s² each), then steady at 5.
  e.OnTelemetry(At(0, 25));
  auto const onBrake = e.OnTelemetry(At(1000, 20));
  CHECK(!Has(onBrake, Category::HardBraking));
  CHECK_EQUAL(e.Stats().hardBrakes, 1);
  CHECK_EQUAL(e.Incidents().size(), size_t{1});
  CHECK(e.JerkRoastWaiting());
  // Still braking at 2-4 s; steady from 4 s on; settled 3 s later at 7 s.
  double const at = FirstAt(e, 2000, {15, 10, 5, 5, 5, 5, 5, 5, 5}, Category::HardBraking);
  CHECK_EQUAL(at, 7000.0);
  CHECK(!e.JerkRoastWaiting());
  CHECK_EQUAL(e.Stats().hardBrakes, 1);
}

TEST(BrakeIntoAStopRoastsAfterThreeSecondsStopped)
{
  auto e = SettleEngine();
  e.OnTelemetry(At(0, 20));
  double const at = FirstAt(e, 1000, {14, 8, 2, 0, 0, 0, 0, 0, 0}, Category::HardBraking);
  // Last braking step lands at 4 s (2 -> 0 is 2 m/s², not hard but not steady); steady from 5 s.
  CHECK(at >= 7000.0);
  CHECK(at <= 8000.0);
  CHECK_EQUAL(e.Stats().hardBrakes, 1);
}

TEST(BrakeInABendWaitsForTheBendToEnd)
{
  auto e = SettleEngine();
  e.SetBending(true);
  e.OnTelemetry(At(0, 25));
  double const at = FirstAt(e, 1000, {20, 15, 15, 15, 15, 15, 15}, Category::HardBraking);
  CHECK_EQUAL(at, -1.0);
  CHECK(e.JerkRoastWaiting());
  e.SetBending(false);
  CHECK(Has(e.OnTelemetry(At(8000, 15)), Category::HardBraking));
}

TEST(BrakeThenTurnDropsTheRoast)
{
  auto e = SettleEngine();
  e.OnTelemetry(At(0, 25));
  e.OnTelemetry(At(1000, 18));
  CHECK(e.JerkRoastWaiting());
  e.OnNav(NavEvent::TurnCompleted(1500, true));
  double const at = FirstAt(e, 2000, {18, 18, 18, 18, 18, 18, 18}, Category::HardBraking);
  CHECK_EQUAL(at, -1.0);
  CHECK(!e.JerkRoastWaiting());
  CHECK_EQUAL(e.Stats().hardBrakes, 1);
}

TEST(NeverSettlingDropsTheRoastAfterTwentySeconds)
{
  auto e = SettleEngine();
  e.OnTelemetry(At(0, 25));
  e.OnTelemetry(At(1000, 20));
  // Stop-start for 25 s: alternating 2 m/s² changes, never steady, never another hard brake.
  std::vector<double> wobble;
  for (int i = 0; i < 25; ++i)
    wobble.push_back(i % 2 == 0 ? 18 : 20);
  double const at = FirstAt(e, 2000, wobble, Category::HardBraking);
  CHECK_EQUAL(at, -1.0);
  CHECK(!e.JerkRoastWaiting());
  // Settling later says nothing: the moment has passed.
  CHECK_EQUAL(FirstAt(e, 27000, {20, 20, 20, 20, 20, 20}, Category::HardBraking), -1.0);
}

TEST(RepeatedBrakesDuringTheHoldGetOneRoast)
{
  auto e = SettleEngine();
  int roasts = 0;
  // Brake, ease, brake again within the hold, then settle.
  FirstAt(e, 0, {25, 20, 20, 21, 16, 16, 16, 16, 16, 16, 16, 16}, Category::HardBraking, &roasts);
  CHECK_EQUAL(roasts, 1);
  CHECK_EQUAL(e.Stats().hardBrakes, 2);
}

TEST(HardLaunchWaitsToo)
{
  auto e = SettleEngine();
  e.OnTelemetry(At(0, 0));
  auto const onLaunch = e.OnTelemetry(At(1000, 4));
  CHECK(!Has(onLaunch, Category::HardAccel));
  CHECK_EQUAL(e.Stats().hardAccels, 1);
  double const at = FirstAt(e, 2000, {8, 12, 12, 12, 12, 12}, Category::HardAccel);
  CHECK(at >= 6000.0);
}
