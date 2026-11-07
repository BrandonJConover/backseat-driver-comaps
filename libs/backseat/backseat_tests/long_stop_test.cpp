// Loop B50: parked mid-route, the longStop remark backs off (45 s, 90, 180) and then goes quiet,
// stays out of the way of an unplannedStop remark, and a traffic jam still nags every 45 s.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/lines.hpp"

#include <vector>

using namespace backseat;

namespace
{
CritiqueEngine StopEngine(uint32_t seed)
{
  EngineConfig c;
  c.rng = SeededRng(seed);
  return CritiqueEngine(c, 0);
}

TelemetrySample Parked(double t)
{
  TelemetrySample s;
  s.t = t;
  s.speedMps = 0;
  return s;
}

/// Seconds (from t = 0) at which `category` spoke, feeding one parked fix a second until `untilS`.
std::vector<double> ParkedFor(CritiqueEngine & e, Category category, int fromS, int untilS)
{
  std::vector<double> at;
  for (int s = fromS; s < untilS; ++s)
    for (auto const & c : e.OnTelemetry(Parked(s * 1000.0)))
      if (c.category == category)
        at.push_back(s);
  return at;
}
}  // namespace

TEST(LongStopBacksOffThenGoesQuiet)
{
  for (uint32_t seed = 1; seed <= 10; ++seed)
  {
    auto e = StopEngine(seed);
    // Parked from t = 0 for 20 minutes: remarks at 45 s, +90 s, +180 s, then nothing.
    auto const at = ParkedFor(e, Category::LongStop, 0, 20 * 60);
    CHECK_EQUAL(at.size(), size_t(3));
    CHECK_EQUAL(at[0], 45.0);
    CHECK_EQUAL(at[1] - at[0], 90.0);
    CHECK_EQUAL(at[2] - at[1], 180.0);
  }
}

TEST(LongStopStartsOverAfterDriving)
{
  auto e = StopEngine(3);
  CHECK_EQUAL(ParkedFor(e, Category::LongStop, 0, 600).size(), size_t(3));
  // Drive off for a minute, then park again: the next stop gets its own 45 s remark.
  for (int s = 600; s < 660; ++s)
  {
    TelemetrySample m = Parked(s * 1000.0);
    m.speedMps = 10;
    e.OnTelemetry(m);
  }
  auto const again = ParkedFor(e, Category::LongStop, 660, 800);
  CHECK(!again.empty());
  CHECK_EQUAL(again[0], 660.0 + 45);
}

TEST(LongStopStaysOutOfTheUnplannedStopsWay)
{
  auto e = StopEngine(5);
  ParkedFor(e, Category::LongStop, 0, 40);
  NavEvent stop;
  stop.type = NavEvent::Type::UnplannedStop;
  stop.t = 40000;
  bool named = false;
  for (auto const & c : e.OnNav(stop))
    named |= c.category == Category::UnplannedStop;
  CHECK(named);
  // Without the unplannedStop, longStop would speak at 45 s; now it waits a full 45 s after it.
  auto const at = ParkedFor(e, Category::LongStop, 40, 200);
  CHECK(!at.empty());
  CHECK(at[0] >= 85.0);
}

TEST(TrafficJamStillNagsEvery45Seconds)
{
  auto e = StopEngine(7);
  NavEvent jam;
  jam.type = NavEvent::Type::Traffic;
  jam.t = 0;
  jam.heavy = true;
  jam.alternate = "Elm Street";
  e.OnNav(jam);
  // A jam is the route's fault, not a parked car: it keeps blaming the route, no back-off.
  auto const at = ParkedFor(e, Category::Traffic, 1, 15 * 60);
  CHECK(at.size() >= 15);
  CHECK(ParkedFor(e, Category::LongStop, 15 * 60, 16 * 60).empty());
}
