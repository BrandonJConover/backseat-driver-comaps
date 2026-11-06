// A turn call never says the other direction (loop B43, BI-038): "Turn left. Right here. Right now."
// tells a driver who hears only the last word to go right. Countdown and lane lines keep the
// direction words to the one in the {dir} slot (or the lane's own side).
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"

#include <cctype>
#include <cstdio>
#include <string>

using namespace backseat;

namespace
{
bool HasWord(std::string text, std::string const & word)
{
  for (auto & c : text)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (size_t p = text.find(word); p != std::string::npos; p = text.find(word, p + word.size()))
  {
    bool const before = p == 0 || !std::isalpha(static_cast<unsigned char>(text[p - 1]));
    size_t const end = p + word.size();
    bool const after = end >= text.size() || !std::isalpha(static_cast<unsigned char>(text[end]));
    if (before && after)
      return true;
  }
  return false;
}

/// The template with its {dir} slots taken out: what's left is what the line itself says.
std::string WithoutDirSlot(std::string text)
{
  for (char const * slot : {"{dir}", "{DIR}"})
    for (size_t p = text.find(slot); p != std::string::npos; p = text.find(slot))
      text.erase(p, 5);
  return text;
}

/// Whether `key` gives a direction the driver still has to act on. Done lines come after the turn,
/// nascar only plays for lefts, and U-turns and roundabouts don't speak the {dir} slot.
bool GivesDirection(std::string const & key)
{
  auto const starts = [&](char const * p) { return key.rfind(p, 0) == 0; };
  if (starts("lane."))
    return true;
  if (!starts("countdown.") || starts("countdown.uturn.") || starts("countdown.roundabout.") ||
      starts("countdown.nascar"))
    return false;
  return key.find(".done") == std::string::npos;
}

/// "Turn left. Right here." for `dir`: the other word anywhere outside the slot.
bool SaysTheOtherWay(std::string const & key, std::string const & line, std::string const & dir)
{
  std::string const other = dir == "left" ? "right" : "left";
  return HasWord(WithoutDirSlot(line), other);
}
}  // namespace

TEST(TurnWordsDetectorCatchesTheOtherDirection)
{
  CHECK(SaysTheOtherWay("countdown.now", "Turn {dir}. Right here. Right now.", "left"));
  CHECK(SaysTheOtherWay("countdown.far", "Left hand, right hand, whichever. {dir}.", "right"));
  CHECK(SaysTheOtherWay("countdown.exit.near", "Be in the right lane for it.", "left"));
  CHECK(!SaysTheOtherWay("countdown.now", "Turn {dir}. Here. Now.", "left"));
  CHECK(!SaysTheOtherWay("countdown.now", "Turn {dir}. Brighten my day.", "left"));
  CHECK(!SaysTheOtherWay("countdown.now", "{DIR}! NOW!", "right"));
  CHECK(GivesDirection("lane.left"));
  CHECK(GivesDirection("countdown.exit.mention"));
  CHECK(GivesDirection("countdown.far.more"));
  CHECK(!GivesDirection("countdown.done"));
  CHECK(!GivesDirection("countdown.exit.done"));
  CHECK(!GivesDirection("countdown.nascar"));
  CHECK(!GivesDirection("countdown.uturn.near"));
  CHECK(!GivesDirection("wrongTurn.left"));
}

TEST(TurnCallsNeverSayTheOtherDirection)
{
  size_t checked = 0;
  for (TravelMode mode : {TravelMode::Drive, TravelMode::Walk, TravelMode::Bike, TravelMode::Hike, TravelMode::OffRoad})
  {
    SetLineTravelMode(mode);
    for (auto const & entry : LineData())
    {
      std::string const & key = entry.first;
      if (!GivesDirection(key))
        continue;
      for (int tier = 0; tier < 3; ++tier)
        for (auto const & line : LinesFor(key, tier))
          for (char const * dir : {"left", "right"})
          {
            // A lane line names its own side; only the other side's word is a problem.
            if ((key == "lane.left" && std::string(dir) == "right") ||
                (key == "lane.right" && std::string(dir) == "left"))
              continue;
            ++checked;
            bool const bad = SaysTheOtherWay(key, line, dir);
            if (bad)
              std::printf("    other direction in a %s call: %s [%d]: %s\n", dir, key.c_str(), tier, line.c_str());
            CHECK(!bad);
          }
    }
  }
  SetLineTravelMode(TravelMode::Drive);
  CHECK(checked > 500);
}

TEST(RewrittenTurnLinesKeepTheirPlace)
{
  // The fixed lines are still in their pools (rewritten, not dropped), so pool sizes hold.
  auto const has = [](std::string const & key, int tier, std::string const & line) {
    for (auto const & l : LinesFor(key, tier))
      if (l == line)
        return true;
    return false;
  };
  SetLineTravelMode(TravelMode::Drive);
  CHECK(has("countdown.now", 0, "Turn {dir}. Here. Now."));
  CHECK(has("countdown.exit.near", 0, "Exit on the {dir}, {distance}. Now would be a great time to be in the lane for it."));
  CHECK(has("lane.left", 2, "LEFT LANE! THE BLINKER! USE IT! IT'S BY THE WHEEL!"));
  CHECK_EQUAL(Fill("Turn {dir}. Here. Now.", {{"dir", "left"}}), std::string("Turn left. Here. Now."));
}
