// A familiar commute thins the optional banter (loop B60, BI-055): past a learned routine's tenth run,
// idle remarks and quiet-stretch bits come at ~0.6x their usual rate; the turn calls don't change, a
// route that isn't a routine yet is untouched, and "talk more" (TalkLevel::Full said mid-trip) brings
// the full rate back for the rest of the trip.
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace backseat;
using namespace backseat_tests;

namespace
{
struct Talk
{
  int optional = 0;  // idle remarks and quiet-stretch bits (call, 20 Questions offer, I spy, fact, aside...)
  int nav = 0;       // turn calls and arrivals
  std::vector<std::string> lines;
};

bool Optional(Commentary const & l)
{
  return l.category == Category::Idle || l.category == Category::Quiet || l.key == "call.script" ||
         l.key == "twenty.offer" || l.key == "twenty.start";
}

/// Drives the commute `days` times (one a day, same start time), keeping history and SaidLog between
/// trips. On each trip at or past `fromDay`, `talkMore` says TalkLevel::Full on the first fix (the
/// driver asked for more talk); the talk on those trips is summed.
Talk DriveCommute(uint32_t routeSeed, TripShape shape, int days, int fromDay, bool talkMore, double stopFromS = -1)
{
  Talk out;
  std::string saved;
  std::vector<TripRecord> history;
  for (int day = 0; day < days; ++day)
  {
    EngineConfig c;
    c.rng = SeededRng(routeSeed * 100 + day);
    Navigator nav(c, history, [&history](TripRecord const & r, TripReport const &) { history.push_back(r); });
    nav.SetSaidLog(SaidLog::Parse(saved), [&saved](SaidLog const & log) { saved = log.Serialize(); });
    SimOptions o;
    o.shape = shape;
    o.seed = routeSeed;
    o.missTurns = false;
    if (day >= fromDay && stopFromS >= 0)
    {
      // An unplanned stop in a parking lot, early on: something new on the trip.
      o.stopFromS = stopFromS;
      o.stopS = 240;
    }
    bool asked = false;
    if (talkMore && day >= fromDay)
      o.afterFix = [&asked](Navigator & n, double, double) {
        if (!asked)
          n.SetTalkLevel(TalkLevel::Full);
        asked = true;
      };
    auto const r = SimulateTrip(nav, o, 1.7e12 + day * 86400000.0);
    if (day < fromDay)
      continue;
    for (auto const & s : r.spoken)
    {
      if (s.line.priority == Priority::Nav)
      {
        // The calls that carry the turn: the final instruction (no key) and the arrival. The far
        // countdown and the straight-on reminders are allowed to give way to whatever else is talking.
        if (s.line.key.empty() || s.line.key == "arrived")
          ++out.nav;
      }
      else if (Optional(s.line))
        ++out.optional;
      out.lines.push_back(s.line.key + " | " + s.line.text);
    }
  }
  return out;
}
}  // namespace

// Trips 11-16 of a daily commute: the optional bits drop by at least 30% against the same trips with
// "talk more" said, and every turn call and arrival still goes out.
TEST(FamiliarCommuteThinsOptionalBanter)
{
  int thinned = 0, full = 0, navThinned = 0, navFull = 0;
  for (uint32_t routeSeed : {3u, 17u, 29u, 41u})
    for (TripShape shape : {TripShape::Town, TripShape::RoadTrip})
    {
      auto const a = DriveCommute(routeSeed, shape, 16, 10, false);
      auto const b = DriveCommute(routeSeed, shape, 16, 10, true);
      CHECK_EQUAL(a.nav, b.nav);
      navThinned += a.nav;
      navFull += b.nav;
      thinned += a.optional;
      full += b.optional;
    }
  if (std::getenv("BACKSEAT_SOAK_VERBOSE"))
    std::printf("  familiar commute: %d optional bits thinned vs %d full; turn calls %d vs %d\n", thinned, full, navThinned, navFull);
  CHECK(full > 20);
  CHECK(thinned * 10 <= full * 7);
}

// Before the tenth run the route isn't familiar yet: saying TalkLevel::Full changes nothing, line for line.
TEST(NewRouteKeepsFullBanter)
{
  for (uint32_t routeSeed : {3u, 29u})
    for (TripShape shape : {TripShape::Town, TripShape::RoadTrip})
    {
      auto const a = DriveCommute(routeSeed, shape, 9, 4, false);
      auto const b = DriveCommute(routeSeed, shape, 9, 4, true);
      CHECK(a.lines == b.lines);
    }
}

// Something new on a familiar trip (here an unplanned stop before any banter was due) brings the full
// rate back: the trip talks line for line as it would after "talk more".
TEST(SomethingNewRestoresFullBanter)
{
  int optional = 0;
  for (uint32_t routeSeed : {3u, 17u})
    for (TripShape shape : {TripShape::Town, TripShape::RoadTrip})
    {
      auto const a = DriveCommute(routeSeed, shape, 13, 10, false, 60);
      auto const b = DriveCommute(routeSeed, shape, 13, 10, true, 60);
      CHECK(a.lines == b.lines);
      optional += a.optional;
    }
  CHECK(optional > 0);
}
