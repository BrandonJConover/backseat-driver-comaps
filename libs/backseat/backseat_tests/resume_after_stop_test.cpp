// When the car moves again after a long stop mid-route, the remarks that were waiting don't all
// land at once. IDEAS BI-046: the 2026-11-07 probe heard straight.please + music +
// speeding.story.ticket inside ~17 s of pulling out (seeds 5 and 9), three remarks back to back.
// Loop B51. BACKSEAT_SOAK_VERBOSE=1 prints every remark in the window.
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/navigator.hpp"

#include <cstdio>
#include <cstdlib>

using namespace backseat;
using namespace backseat_tests;

namespace
{
constexpr double kStartMs = 1.7e12;
bool Verbose() { return std::getenv("BACKSEAT_SOAK_VERBOSE") != nullptr; }
}  // namespace

bool Chatter(Category c) { return c == Category::Idle || c == Category::Music; }
bool AboutTheStop(Category c)
{
  return c == Category::UnplannedStop || c == Category::LongStop || c == Category::PullOver;
}

// 3 to 15 minute stops, in a lot and stopped dead in a lane, 20 seeds each. In the minute after the
// car moves again: idle chatter and the music ask never land within a breath (8 s) of another
// remark, nothing about the stop ("Why are we stopping here?") is said once the car is moving,
// and at most two remarks in the first 20 s. Before B51: 'clean.1' + 'music' + a streak remark in
// the same second, an idle line in the same second as "You left me in the car!", and the 3-minute
// stop remark held for a turn and said as the car pulled away.
TEST(ResumeAfterStopDoesNotPileUp)
{
  int trips = 0, failed = 0;
  for (bool const parked : {true, false})
    for (double const len : {180.0, 600.0, 900.0})
      for (uint32_t seed = 1; seed <= 20; ++seed)
      {
        SimOptions o{TripShape::Town, seed};
        o.missTurns = false;
        o.stopFromS = 50 + seed * 5;
        o.stopS = len;
        o.stopParked = parked;
        EngineConfig c;
        c.rng = SeededRng(seed);
        Navigator nav(c);
        auto const r = SimulateTrip(nav, o);
        ++trips;
        double const back = o.stopFromS + o.stopS;
        int crowded = 0, stale = 0, early = 0;
        double lastS = -1e9;
        Category lastC = Category::Idle;
        for (auto const & s : r.spoken)
        {
          double const q = (s.queuedMs - kStartMs) / 1000;
          if (q < back - 10 || q > back + 60 || s.line.priority == Priority::Nav)
            continue;
          if (q > back && AboutTheStop(s.line.category))
            ++stale;
          if (q - lastS < 8 && (Chatter(s.line.category) || Chatter(lastC)))
            ++crowded;
          if (q >= back && q < back + 20)
            ++early;
          lastS = q;
          lastC = s.line.category;
          if (Verbose())
            std::fprintf(stderr, "  resume parked=%d len=%.0f seed=%u +%.0fs [%s] %s\n", parked, len, seed, q - back,
                         s.line.key.c_str(), s.line.text.c_str());
        }
        bool const bad = !r.arrived || crowded || stale || early > 2;
        failed += bad;
        if (bad || Verbose())
          std::fprintf(stderr, "  resume parked=%d len=%.0f seed=%u arrived=%d crowded=%d stale=%d early=%d\n", parked,
                       len, seed, r.arrived, crowded, stale, early);
      }
  CHECK(trips == 120);
  CHECK_EQUAL(failed, 0);
}
