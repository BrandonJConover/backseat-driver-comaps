// Lines that take right-hand traffic for granted are mirrored where traffic keeps left (loop B42,
// BI-040): in the UK the driver sits on the right and the right lane is for passing, so "The left
// lane is for passing" and "Left is the side with the driver" are simply false there.
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/navigator.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
bool Contains(std::vector<std::string> const & lines, std::string const & text)
{
  return std::any_of(lines.begin(), lines.end(),
                     [&](std::string const & l) { return l.find(text) != std::string::npos; });
}

struct DriveSideReset
{
  ~DriveSideReset()
  {
    SetLineDriveSide(false);
    SetLineMetric(false);
  }
};
}  // namespace

TEST(DrivesOnLeftKnowsTheCountries)
{
  for (char const * c : {"UK", "uk", "UK_England", "United Kingdom", "GB", "Ireland", "Australia",
                         "Australia_Victoria", "New Zealand", "New Zealand North_Auckland", "Japan", "JP",
                         "India", "South Africa", "Thailand", "Singapore", "Hong Kong", "Jamaica"})
  {
    if (!DrivesOnLeft(c))
      std::fprintf(stderr, "should keep left: %s\n", c);
    CHECK(DrivesOnLeft(c));
  }
  for (char const * c : {"", "US", "US_Texas", "Canada", "France", "Germany", "Mexico", "United States",
                         "Brazil", "China", "South Korea", "Indiana", "Iceland", "Ukraine", " "})
  {
    if (DrivesOnLeft(c))
      std::fprintf(stderr, "should keep right: %s\n", c);
    CHECK(!DrivesOnLeft(c));
  }
}

TEST(DriveSideWordingMirrorsOnlyTheSeatAndPassingJokes)
{
  CHECK_EQUAL(DriveSideWording("The left lane is for passing. You aren't passing. You're visiting."),
              std::string("The right lane is for passing. You aren't passing. You're visiting."));
  CHECK_EQUAL(DriveSideWording("Left is the side with the driver. You're sitting on it right now."),
              std::string("Left is the side with the passenger seat. Usually. You turned toward yourself."));
  CHECK_EQUAL(DriveSideWording("Right is the side with the passenger seat. Usually. You turned toward yourself."),
              std::string("Right is the side with the driver. You're sitting on it right now."));
  CHECK_EQUAL(DriveSideWording("Yes. Keep going. Slowly. Like you drive in the left lane."),
              std::string("Yes. Keep going. Slowly. Like you drive in the right lane."));
  CHECK_EQUAL(DriveSideWording("Highways on. Please don't drive in the left lane at forty."),
              std::string("Highways on. Please don't drive in the right lane at forty."));
  // Real lane calls and turns are instructions: never touched.
  for (char const * same : {"Left lane, please. That's the one on the left. I know. Hard to remember.",
                            "Right lane. Blinker. Now. Not after you've already moved.",
                            "Your other left.", "Turn left onto {street}.", ""})
    CHECK_EQUAL(DriveSideWording(same), std::string(same));
}

TEST(LeftHandTrafficHearsTheMirroredLines)
{
  DriveSideReset reset;
  for (bool const metric : {false, true})
  {
    SetLineMetric(metric);
    SetLineDriveSide(false);
    CHECK(Contains(LinesFor("road.highway", 0), "The left lane is for passing."));
    CHECK(Contains(LinesFor("wrongTurn.left", 0), "Left is the side with the driver."));
    CHECK(Contains(LinesFor("wrongTurn.right", 0), "Right is the side with the passenger seat."));
    SetLineDriveSide(true);
    for (int tier = 0; tier < 3; ++tier)
    {
      CHECK(!Contains(LinesFor("road.highway", tier), "left lane is for passing"));
      CHECK(Contains(LinesFor("road.highway", tier), "The right lane is for passing."));
    }
    CHECK(Contains(LinesFor("wrongTurn.left", 0), "Left is the side with the passenger seat."));
    CHECK(Contains(LinesFor("wrongTurn.right", 0), "Right is the side with the driver."));
    CHECK(Contains(LinesFor("twenty.yes", 0), "Like you drive in the right lane."));
    CHECK(Contains(LinesFor("cmd.allowHighways", 1), "right lane at forty"));
    // The lane calls themselves stay as they are.
    CHECK(LinesFor("lane.left", 0) == LineData().at("lane.left")[0]);
  }
  // Off the cached path too (walking, apartment): the mirrored table is what gets filtered.
  SetLineTravelMode(TravelMode::Walk);
  SetLineHousing(Housing::Apartment);
  SetLineDriveSide(true);
  for (int tier = 0; tier < 3; ++tier)
    CHECK(!Contains(LinesFor("road.highway", tier), "left lane is for passing"));
  SetLineTravelMode(TravelMode::Drive);
  SetLineHousing(Housing::Unknown);
}

TEST(NoLeftHandTrafficLineSurvivesAndEveryKeyKeepsItsLines)
{
  DriveSideReset reset;
  SetLineDriveSide(true);
  for (bool const metric : {false, true})
  {
    SetLineMetric(metric);
    for (auto const & [key, tiers] : LineData())
      for (int tier = 0; tier < static_cast<int>(tiers.size()); ++tier)
      {
        auto const & lines = LinesFor(key, tier);
        CHECK_EQUAL(lines.size(), LinesFor(key, tier).size());
        if (!tiers[static_cast<size_t>(tier)].empty())
          CHECK(!lines.empty());
        for (auto const & line : lines)
          for (char const * bad : {"left lane is for passing", "side with the driver. You're sitting on it",
                                   "drive in the left lane"})
            if (line.find(bad) != std::string::npos && key.rfind("wrongTurn.right", 0) != 0)
            {
              std::fprintf(stderr, "right-hand traffic in %s: %s\n", key.c_str(), line.c_str());
              CHECK(false);
            }
      }
  }
}

TEST(NavigatorSetCountrySetsTheDriveSide)
{
  DriveSideReset reset;
  EngineConfig c;
  c.rng = SeededRng(3);
  Navigator nav(c);
  nav.SetCountry("UK");
  CHECK(Contains(LinesFor("road.highway", 0), "The right lane is for passing."));
  nav.SetCountry("US");
  CHECK(Contains(LinesFor("road.highway", 0), "The left lane is for passing."));
  nav.SetCountry("Australia_Queensland");
  CHECK(Contains(LinesFor("wrongTurn.right", 0), "Right is the side with the driver."));
  nav.SetCountry("");
  CHECK(Contains(LinesFor("wrongTurn.right", 0), "Right is the side with the passenger seat."));
}
