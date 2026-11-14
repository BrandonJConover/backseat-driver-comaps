// A new Navigator starts the line pickers from its own defaults (loop B47, BI-043). The travel mode,
// housing and drive side LinesFor() filters on are process-wide; before this a walk, a "streets"
// answer or a UK trip in one Navigator leaked into the next, so a drive could say "On foot" lines
// and the suite's result depended on test order.
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/navigator.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
using Pools = std::map<std::string, std::vector<std::string>>;

/// Every key's every tier as LinesFor() answers right now.
Pools Snapshot()
{
  Pools pools;
  for (auto const & [key, tiers] : LineData())
    for (int tier = 0; tier < static_cast<int>(tiers.size()); ++tier)
      pools[key + "#" + std::to_string(tier)] = LinesFor(key, tier);
  return pools;
}

/// How many pools differ; `report` names the first few (only wanted where none should differ).
int Differences(Pools const & a, Pools const & b, bool report = false)
{
  int diffs = 0;
  for (auto const & [key, lines] : a)
  {
    auto const it = b.find(key);
    if (it == b.end() || it->second != lines)
    {
      if (report && diffs < 5)
        std::fprintf(stderr, "pool differs: %s\n", key.c_str());
      ++diffs;
    }
  }
  return diffs;
}

Navigator Fresh(unsigned seed)
{
  EngineConfig c;
  c.rng = SeededRng(seed);
  return Navigator(c);
}

struct FiltersReset
{
  ~FiltersReset()
  {
    SetLineTravelMode(TravelMode::Drive);
    SetLineHousing(Housing::Unknown);
    SetLineDriveSide(false);
    SetLineMetric(false);
  }
};
}  // namespace

TEST(FreshNavigatorStartsTheLineFiltersFromItsDefaults)
{
  FiltersReset reset;
  // The defaults, set by hand: driving, housing unknown, right-hand traffic, imperial.
  SetLineTravelMode(TravelMode::Drive);
  SetLineHousing(Housing::Unknown);
  SetLineDriveSide(false);
  SetLineMetric(false);
  Pools const defaults = Snapshot();

  {
    Navigator walker = Fresh(11);
    walker.SetTravelMode(TravelMode::Walk);
    walker.SetHousing(Housing::Streets);
    walker.SetCountry("UK");
    // The leftover filters really do change what gets said, so the check below can fail.
    CHECK(Differences(defaults, Snapshot()) > 0);
  }

  Navigator const driver = Fresh(12);
  CHECK(driver.Mode() == TravelMode::Drive);
  CHECK_EQUAL(Differences(defaults, Snapshot(), true), 0);
}

TEST(FreshNavigatorDropsEachLeftoverFilterOnItsOwn)
{
  FiltersReset reset;
  SetLineTravelMode(TravelMode::Drive);
  SetLineHousing(Housing::Unknown);
  SetLineDriveSide(false);
  Pools const defaults = Snapshot();

  // Walk alone: arrival lines on foot exist, and a new navigator puts the car back.
  SetLineTravelMode(TravelMode::Walk);
  Pools const walking = Snapshot();
  CHECK(Differences(defaults, walking) > 0);
  {
    [[maybe_unused]] Navigator const n = Fresh(21);
    CHECK_EQUAL(Differences(defaults, Snapshot(), true), 0);
  }

  // Streets alone.
  SetLineHousing(Housing::Streets);
  CHECK(Differences(defaults, Snapshot()) > 0);
  {
    [[maybe_unused]] Navigator const n = Fresh(22);
    CHECK_EQUAL(Differences(defaults, Snapshot(), true), 0);
  }

  // Left-hand traffic alone: the passing-lane joke comes back the US way.
  SetLineDriveSide(true);
  {
    [[maybe_unused]] Navigator const n = Fresh(23);
    bool usWay = false;
    for (auto const & line : LinesFor("road.highway", 0))
      usWay = usWay || line.find("The left lane is for passing.") != std::string::npos;
    CHECK(usWay);
    CHECK_EQUAL(Differences(defaults, Snapshot(), true), 0);
  }
}

TEST(SettingsAfterConstructionStillApply)
{
  FiltersReset reset;
  Navigator nav = Fresh(31);
  nav.SetCountry("UK");
  bool ukWay = false;
  for (auto const & line : LinesFor("road.highway", 0))
    ukWay = ukWay || line.find("The right lane is for passing.") != std::string::npos;
  CHECK(ukWay);
  // A second navigator built later (a preview, a restarted trip) starts clean, and the first one's
  // next SetCountry puts its own country back.
  {
    [[maybe_unused]] Navigator const other = Fresh(32);
  }
  nav.SetCountry("UK");
  ukWay = false;
  for (auto const & line : LinesFor("road.highway", 0))
    ukWay = ukWay || line.find("The right lane is for passing.") != std::string::npos;
  CHECK(ukWay);
}

TEST(LineFiltersSaveAndRestoreAsOne)
{
  // The test runner resets these before every test and fails a test that leaves them changed
  // (loop B83); this pins the save / restore it relies on.
  CHECK(CurrentLineFilters() == LineFilters{});
  CHECK_EQUAL(DescribeLineFilters(LineFilters{}), std::string("drive/unknown/imperial/right"));
  LineFilters changed;
  changed.mode = TravelMode::Walk;
  changed.housing = Housing::Streets;
  changed.metric = true;
  changed.leftHand = true;
  SetLineFilters(changed);
  CHECK(CurrentLineFilters() == changed);
  CHECK_EQUAL(DescribeLineFilters(CurrentLineFilters()), std::string("walk/streets/metric/left"));
  // Each Set* call moves only its own filter.
  SetLineHousing(Housing::House);
  CHECK(CurrentLineFilters().mode == TravelMode::Walk);
  CHECK(CurrentLineFilters().housing == Housing::House);
  // A walk filter left behind is what dropped "Staying home? The other drivers..." from the next
  // test's ask.stayPut pool; restoring the defaults brings it back.
  SetLineFilters({});
  CHECK(CurrentLineFilters() == LineFilters{});
  auto const & pool = LinesFor("ask.stayPut", 1);
  CHECK(std::any_of(pool.begin(), pool.end(), [](auto const & l) { return l.find("Staying home?") != std::string::npos; }));
}
