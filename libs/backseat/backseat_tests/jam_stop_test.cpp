// Stuck in a jam, the driver never left the car. Before loop B52, sitting still in a lane for three
// minutes or more (the navigator already calling it traffic) ended with "You left me in the car!
// Phones have needs too" as the jam cleared, and at the three-minute mark "Why are we stopping
// here?" landed on top of the traffic lines. The roadside pull-over line already knew better.
// BACKSEAT_SOAK_VERBOSE=1 prints every remark from the stop to a minute after it.
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/navigator.hpp"

#include <cstdio>
#include <cstdlib>

using namespace backseat;
using namespace backseat_tests;

namespace
{
constexpr double kJamStartMs = 1.7e12;
bool JamVerbose() { return std::getenv("BACKSEAT_SOAK_VERBOSE") != nullptr; }
bool BlamesTheStop(Category c)
{
  return c == Category::Abandoned || c == Category::UnplannedStop || c == Category::PullOver ||
         c == Category::LongStop;
}
}  // namespace

// Stopped dead in a lane for 3, 10 and 15 minutes, 40 seeds each. Whenever the navigator called it
// traffic during the stop, nothing after that says the driver stopped on purpose or left the car.
TEST(JamStopIsNotAbandonment)
{
  int jammed = 0, failed = 0;
  for (double const len : {180.0, 600.0, 900.0})
    for (uint32_t seed = 1; seed <= 40; ++seed)
    {
      SimOptions o{TripShape::Town, seed};
      o.missTurns = false;
      o.stopFromS = 50 + seed * 5;
      o.stopS = len;
      o.stopParked = false;
      EngineConfig c;
      c.rng = SeededRng(seed);
      Navigator nav(c);
      auto const r = SimulateTrip(nav, o);
      double const back = o.stopFromS + o.stopS;
      double trafficAt = -1;
      int blamed = 0;
      for (auto const & s : r.spoken)
      {
        double const q = (s.queuedMs - kJamStartMs) / 1000;
        if (q < o.stopFromS || q > back + 60)
          continue;
        if (trafficAt < 0 && s.line.category == Category::Traffic && q < back)
          trafficAt = q;
        if (trafficAt >= 0 && BlamesTheStop(s.line.category))
          ++blamed;
        if (JamVerbose())
          std::fprintf(stderr, "  jam len=%.0f seed=%u +%.0fs [%s] %s\n", len, seed, q - o.stopFromS,
                       s.line.key.c_str(), s.line.text.c_str());
      }
      if (trafficAt < 0)
        continue;
      ++jammed;
      bool const bad = !r.arrived || blamed;
      failed += bad;
      if (bad)
        std::fprintf(stderr, "  jam len=%.0f seed=%u arrived=%d blamed=%d\n", len, seed, r.arrived, blamed);
    }
  // Enough of the seeds stop on a road fast enough to judge traffic on.
  CHECK(jammed >= 20);
  CHECK_EQUAL(failed, 0);
}

// A stop in a parking lot is still the driver's doing: walking off without the phone still gets
// "You left me in the car!" when the car moves again.
TEST(ParkedStopStillAbandons)
{
  int abandoned = 0;
  for (uint32_t seed = 1; seed <= 20; ++seed)
  {
    SimOptions o{TripShape::Town, seed};
    o.missTurns = false;
    o.stopFromS = 50 + seed * 5;
    o.stopS = 600;
    o.stopParked = true;
    EngineConfig c;
    c.rng = SeededRng(seed);
    Navigator nav(c);
    auto const r = SimulateTrip(nav, o);
    for (auto const & s : r.spoken)
      abandoned += s.line.category == Category::Abandoned;
  }
  CHECK(abandoned >= 18);
}
