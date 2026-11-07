// Whole simulated trips (sim_drive.hpp) with the gaps real phones have: the GPS going quiet in a
// tunnel or a parking garage, the app suspended by the OS, a long stop mid-route, and the units
// the driver picked. Locks in what the 2026-11-07 probe found working (loop B48).
// BACKSEAT_SOAK_VERBOSE=1 prints every trip.
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/navigator.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace backseat;
using namespace backseat_tests;

namespace
{
bool Verbose() { return std::getenv("BACKSEAT_SOAK_VERBOSE") != nullptr; }

SimResult DriveWith(SimOptions o, Units units = Units::Mph)
{
  EngineConfig c;
  c.rng = SeededRng(o.seed);
  Navigator nav(c);
  nav.SetUnits(units);
  return SimulateTrip(nav, o);
}

constexpr double kStartMs = 1.7e12;

/// Seconds into the trip a maneuver was reached.
double ReachedS(backseat_tests::Maneuver const & m) { return (m.reachedMs - kStartMs) / 1000; }

/// Every turn the navigator could see coming got a direction: turns reached while it was blind
/// (and the first few seconds after, before a fresh fix lands) are left out.
int UndirectedOutside(SimResult const & r, double fromS, double lenS)
{
  int undirected = 0;
  for (auto const & m : r.maneuvers)
  {
    if (m.reachedMs < 0 || m.missed || m.turn == TurnKind::Destination)
      continue;
    double const at = ReachedS(m);
    if (at >= fromS && at < fromS + lenS + 15)
      continue;
    if (!m.directed)
      ++undirected;
  }
  return undirected;
}
}  // namespace

// The GPS goes quiet for 20 s to 15 minutes mid-route while the app keeps polling a snapshot
// that's stuck where the router last saw the car. No "Now, ..." for a turn the car already made,
// every turn it could see gets a direction, and the trip still arrives.
TEST(SoakGpsOutageMidRoute)
{
  int trips = 0, failed = 0;
  for (TripShape const shape : {TripShape::Town, TripShape::RoadTrip})
    for (double const len : {20.0, 120.0, 900.0})
      for (uint32_t seed = 1; seed <= (shape == TripShape::Town ? 20u : 4u); ++seed)
      {
        SimOptions o{shape, seed};
        o.missTurns = false;
        o.outageFromS = 45 + seed * 7;
        o.outageS = len;
        auto const r = DriveWith(o);
        ++trips;
        int const undirected = UndirectedOutside(r, o.outageFromS, o.outageS);
        bool const bad = !r.arrived || r.staleNowCalls != 0 || undirected != 0;
        failed += bad;
        if (bad || Verbose())
          std::fprintf(stderr, "  outage shape=%d len=%.0f seed=%u arrived=%d stale=%d undirected=%d\n",
                       static_cast<int>(shape), len, seed, r.arrived, r.staleNowCalls, undirected);
      }
  CHECK(trips == 72);
  CHECK_EQUAL(failed, 0);
}

// The OS suspends the app (nothing runs, nothing is said) and wakes it a turn or ten later.
TEST(SoakAppSuspendedMidRoute)
{
  int failed = 0;
  for (TripShape const shape : {TripShape::Town, TripShape::RoadTrip})
    for (double const len : {20.0, 120.0, 900.0})
      for (uint32_t seed = 1; seed <= (shape == TripShape::Town ? 20u : 4u); ++seed)
      {
        SimOptions o{shape, seed};
        o.missTurns = false;
        o.suspendFromS = 45 + seed * 7;
        o.suspendS = len;
        auto const r = DriveWith(o);
        int const undirected = UndirectedOutside(r, o.suspendFromS, o.suspendS);
        // Nothing is handed over while the app is asleep.
        int asleep = 0;
        for (auto const & s : r.spoken)
        {
          double const q = (s.queuedMs - kStartMs) / 1000;
          asleep += q >= o.suspendFromS && q < o.suspendFromS + o.suspendS;
        }
        bool const bad = !r.arrived || r.staleNowCalls != 0 || undirected != 0 || asleep != 0;
        failed += bad;
        if (bad || Verbose())
          std::fprintf(stderr, "  suspend shape=%d len=%.0f seed=%u arrived=%d stale=%d undirected=%d asleep=%d\n",
                       static_cast<int>(shape), len, seed, r.arrived, r.staleNowCalls, undirected, asleep);
      }
  CHECK_EQUAL(failed, 0);
}

// The driver pulls into a lot mid-route for 3 to 15 minutes (engine running, app open). The
// remarks about it stay at least 40 s apart, and once the car moves again the trip still arrives
// with every turn directed. Known and ratcheted here (IDEAS BI-047): the longStop remark repeats
// every 45 s for as long as the car sits there (20 of them in 15 minutes parked), and at the
// 3-minute mark the unplanned-stop remark lands in the same second as one of them, and an idle
// bit (music) can land 10 s after another (worst trip today: 3 bunched). Lower the bar to 0
// when BI-047 is built.
TEST(SoakLongStopMidRouteDoesNotBunchUp)
{
  int stops = 0, failed = 0;
  for (double const len : {180.0, 600.0, 900.0})
    for (uint32_t seed = 1; seed <= 20; ++seed)
    {
      SimOptions o{TripShape::Town, seed};
      o.missTurns = false;
      o.stopFromS = 50 + seed * 5;
      o.stopS = len;
      auto const r = DriveWith(o);
      ++stops;
      // Parked: from ~15 s after the stop starts (the car has come to rest) to when it's over.
      int said = 0, bunched = 0;
      double lastS = -1e9;
      for (auto const & s : r.spoken)
      {
        double const q = (s.queuedMs - kStartMs) / 1000;
        if (q < o.stopFromS + 15 || q >= o.stopFromS + o.stopS || s.line.priority == Priority::Nav)
          continue;
        ++said;
        if (q - lastS < 40)
          ++bunched;
        lastS = q;
        if (Verbose())
          std::fprintf(stderr, "  stop seed=%u len=%.0f +%.0fs [%s] %s\n", seed, len, q - o.stopFromS,
                       s.line.key.c_str(), s.line.text.c_str());
      }
      int const undirected = UndirectedOutside(r, -1, 0);
      // One remark per 40 s at most, whatever the backoff ends up being.
      bool const bad = !r.arrived || bunched > 3 || said > len / 40 + 1 || undirected != 0;
      failed += bad;
      if (bad || Verbose())
        std::fprintf(stderr, "  stop seed=%u len=%.0f arrived=%d said=%d bunched=%d undirected=%d\n", seed, len,
                     r.arrived, said, bunched, undirected);
    }
  CHECK(stops == 60);
  CHECK_EQUAL(failed, 0);
}

namespace
{
std::string Lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

/// Whole-word (or word-prefix, for plurals) match.
bool HasWord(std::string const & text, std::string const & word)
{
  for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1))
  {
    bool const startOk = at == 0 || !std::isalpha(static_cast<unsigned char>(text[at - 1]));
    size_t const end = at + word.size();
    bool const endOk = end >= text.size() || !std::isalpha(static_cast<unsigned char>(text[end])) ||
                       text.compare(end, 1, "s") == 0;
    if (startOk && endOk)
      return true;
  }
  return false;
}
}  // namespace

// A driver who picked kilometers never hears miles, feet or mph, and one on miles never hears
// kilometers or meters, over whole town and highway trips.
TEST(SoakSpokenUnitsFollowTheSetting)
{
  char const * const imperial[] = {"mile", "miles", "feet", "foot", "mph", "yard", "yards"};
  char const * const metric[] = {"kilometer", "kilometre", "kilometers", "meter", "meters", "metre", "km", "kph"};
  int lines = 0, failed = 0;
  for (Units const units : {Units::Kmh, Units::Mph})
    for (TripShape const shape : {TripShape::Town, TripShape::RoadTrip})
      for (uint32_t seed = 1; seed <= (shape == TripShape::Town ? 20u : 4u); ++seed)
      {
        auto const r = DriveWith(SimOptions{shape, seed}, units);
        for (auto const & s : r.spoken)
        {
          ++lines;
          std::string text = Lower(s.line.text);
          // "on foot" is about walking, not a distance.
          for (size_t at; (at = text.find("on foot")) != std::string::npos;)
            text.erase(at, 7);
          bool bad = false;
          if (units == Units::Kmh)
            for (char const * w : imperial)
              bad = bad || HasWord(text, w);
          else
            for (char const * w : metric)
              bad = bad || HasWord(text, w);
          failed += bad;
          if (bad)
            std::fprintf(stderr, "  units=%s seed=%u [%s] %s\n", units == Units::Kmh ? "kmh" : "mph", seed,
                         s.line.key.c_str(), s.line.text.c_str());
        }
      }
  CHECK(lines > 1000);
  CHECK_EQUAL(failed, 0);
}
