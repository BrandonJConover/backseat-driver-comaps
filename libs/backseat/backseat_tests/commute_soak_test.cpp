// The same commute, day after day (loop B59, BI-054): ten trips over one route, the SaidLog saved and
// read back between trips the way the apps do, and the trip history carried along. A driver who hears
// the same remark on two days running, or on most days of the fortnight, stops finding it funny.
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace backseat;
using namespace backseat_tests;

namespace
{
bool CommuteVerbose() { return std::getenv("BACKSEAT_SOAK_VERBOSE") != nullptr; }

constexpr int kTrips = 10;

/// The biggest pool `key` has in any tier.
size_t PoolSize(std::string const & key)
{
  size_t n = 0;
  for (int tier = 0; tier <= 3; ++tier)
    n = std::max(n, LinesFor(key, tier).size());
  return n;
}

struct Commute
{
  /// Per trip, the remark lines said (text -> key), directions left out.
  std::vector<std::map<std::string, std::string>> said;
  int arrived = 0;
  size_t historySize = 0;
};

/// `kTrips` drives of the route `routeSeed` builds, one a day, each with a fresh Navigator (the app
/// restarted) handed the saved SaidLog text and the history so far.
Commute DriveCommute(uint32_t routeSeed, TripShape shape)
{
  Commute out;
  std::string saved;
  std::vector<TripRecord> history;
  for (int day = 0; day < kTrips; ++day)
  {
    EngineConfig c;
    c.rng = SeededRng(routeSeed * 100 + day);
    Navigator nav(c, history, [&history](TripRecord const & r, TripReport const &) { history.push_back(r); });
    nav.SetSaidLog(SaidLog::Parse(saved), [&saved](SaidLog const & log) { saved = log.Serialize(); });
    SimOptions o;
    o.shape = shape;
    o.seed = routeSeed;  // same route every day
    o.missTurns = false;  // a commuter knows the way
    auto const r = SimulateTrip(nav, o, 1.7e12 + day * 86400000.0);
    out.arrived += r.arrived ? 1 : 0;
    std::map<std::string, std::string> lines;
    for (auto const & s : r.spoken)
      if (!s.line.key.empty() && s.line.priority != Priority::Nav)
        lines[s.line.text] = s.line.key;
    out.said.push_back(std::move(lines));
  }
  out.historySize = history.size();
  return out;
}

struct WearOut
{
  /// Lines said on two trips in a row, from keys with >= 4 lines that aren't used up in a day
  /// (a key said more than half its pool's size a trip has to come round again).
  std::vector<std::string> backToBack;
  /// Lines heard on 4+ of the days and on more days than an even rotation of their key needs
  /// (the key's sayings spread over its lines, plus one).
  std::vector<std::string> mostDays;
};

WearOut Check(Commute const & c, char const * label)
{
  WearOut w;
  std::map<std::string, int> days;
  std::map<std::string, std::string> keyOf;
  std::map<std::string, int> keySaid;
  std::map<std::string, std::set<std::string>> keyLines;
  for (auto const & trip : c.said)
    for (auto const & [text, key] : trip)
    {
      ++days[text];
      keyOf[text] = key;
      ++keySaid[key];
      keyLines[key].insert(text);
    }
  auto pool = [&](std::string const & key) { return std::max(PoolSize(key), keyLines[key].size()); };
  for (size_t i = 1; i < c.said.size(); ++i)
    for (auto const & [text, key] : c.said[i])
      if (c.said[i - 1].count(text) && pool(key) >= 4 && keySaid[key] * 2 <= static_cast<int>(pool(key)) * kTrips)
        w.backToBack.push_back(key + " | " + text);
  for (auto const & [text, n] : days)
  {
    auto const & key = keyOf[text];
    int const even = (keySaid[key] + static_cast<int>(pool(key)) - 1) / static_cast<int>(pool(key));
    if (n >= 4 && pool(key) >= 4 && n > even + 1)
      w.mostDays.push_back(key + " x" + std::to_string(n) + " (even " + std::to_string(even) + ") | " + text);
  }
  if (CommuteVerbose())
  {
    std::map<std::string, std::pair<int, int>> perKey;  // key -> (times said, distinct lines)
    for (auto const & [text, n] : days)
    {
      perKey[keyOf[text]].first += n;
      ++perKey[keyOf[text]].second;
    }
    std::printf("  [%s] %d/%d arrived, %zu trips in history\n", label, c.arrived, kTrips, c.historySize);
    for (auto const & [key, p] : perKey)
      std::printf("    %-34s said %2d, %2d distinct, pool %zu\n", key.c_str(), p.first, p.second, PoolSize(key));
    for (auto const & l : w.backToBack)
      std::printf("    back-to-back: %s\n", l.c_str());
    for (auto const & l : w.mostDays)
      std::printf("    most days: %s\n", l.c_str());
  }
  return w;
}
}  // namespace

// Ten trips of the same town commute and the same road trip: every trip arrives, the history keeps
// them all, no line from a pool of 4+ comes back the next day unless its key goes through the pool
// that fast, and no line is heard on more days than an even rotation of its key would need.
TEST(CommuteRemarksDoNotWearOut)
{
  int backToBack = 0, mostDays = 0;
  for (uint32_t routeSeed : {3u, 17u, 29u})
    for (TripShape shape : {TripShape::Town, TripShape::RoadTrip})
    {
      auto const c = DriveCommute(routeSeed, shape);
      CHECK_EQUAL(c.arrived, kTrips);
      CHECK_EQUAL(c.historySize, static_cast<size_t>(kTrips));
      auto const w = Check(c, shape == TripShape::Town ? "town" : "road trip");
      backToBack += static_cast<int>(w.backToBack.size());
      mostDays += static_cast<int>(w.mostDays.size());
    }
  if (CommuteVerbose())
    std::printf("  commute soak: %d back-to-back, %d most-days\n", backToBack, mostDays);
  CHECK_EQUAL(backToBack, 0);
  CHECK_EQUAL(mostDays, 0);
}

// A pick taken back restores the rotation as it was; a pick of another line, or a newer pick, can't
// be taken back by mistake.
TEST(SaidLogUnpickRestoresTheRotation)
{
  std::vector<std::string> const pool{"a", "b", "c", "d"};
  SaidLog log;
  log.Pick("k", pool, 0);  // first ever: "a"
  std::string const second = log.Pick("k", pool, 0.5);
  CHECK(second != "a");
  std::string const before = log.Serialize();
  std::string const third = log.Pick("k", pool, 0.9);
  log.Unpick("k", third);
  CHECK_EQUAL(log.Serialize(), before);
  log.Unpick("k", second);  // already taken back once: nothing
  CHECK_EQUAL(log.Serialize(), before);
  std::string const fourth = log.Pick("k", pool, 0);
  log.Unpick("k", fourth == "a" ? std::string("b") : std::string("a"));  // not the line it gave
  CHECK(log.Serialize() != before);
  SaidLog fresh;
  fresh.Pick("first", pool, 0.7);
  fresh.Unpick("first", "a");
  CHECK(!fresh.Said("first"));
  CHECK_EQUAL(fresh.Pick("first", pool, 0.7), std::string("a"));  // Brandon's line still goes first
}
