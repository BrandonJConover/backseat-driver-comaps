#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/pronunciation.hpp"

#include <cctype>
#include <cstdio>

using namespace backseat;
using namespace backseat::pronunciation;

TEST(ShoutedLinesAreSpokenInNormalCase)
{
  CHECK_EQUAL(SpeakableText("WHO TAUGHT YOU THE GAS PEDAL HAS ONLY TWO SETTINGS?"),
              std::string("Who taught you the gas pedal has only two settings?"));
  CHECK_EQUAL(SpeakableText("YOU DID IT! YOU MADE THE TURN! I'M NOT CRYING, I'M A PHONE!"),
              std::string("You did it! You made the turn! I'm not crying, I'm a phone!"));
  CHECK_EQUAL(SpeakableText("57. IN A 50. I CAN SEE THE NUMBERS."), std::string("57. In a 50. I can see the numbers."));
  // Abbreviations and ordinary text are left alone.
  CHECK_EQUAL(SpeakableText("The GPS says NASCAR. OK?"), std::string("The GPS says NASCAR. OK?"));
  CHECK_EQUAL(SpeakableText("Turn left onto Elm Street."), std::string("Turn left onto Elm Street."));
}

TEST(MetricWordingSaysMetersAndLeavesFeetAlone)
{
  CHECK_EQUAL(MetricWording("Ten feet of glory, and we're done again. Savor it."),
              std::string("Three meters of glory, and we're done again. Savor it."));
  CHECK_EQUAL(MetricWording("You saw that coming a quarter mile ago. I know, because I pointed at it."),
              std::string("You saw that coming a few hundred meters ago. I know, because I pointed at it."));
  CHECK_EQUAL(MetricWording("the last one for a hundred miles."), std::string("the last one for a hundred and fifty kilometers."));
  // Body parts and facts stay.
  CHECK_EQUAL(MetricWording("One foot in front of the other."), std::string("One foot in front of the other."));
  CHECK_EQUAL(MetricWording("arrested for speeding at twelve miles an hour"),
              std::string("arrested for speeding at twelve miles an hour"));

  // Every written-in distance in the table has a metric version, so a metric driver never hears one.
  // Facts and the mileage log (which is in miles on purpose) are the exceptions.
  SetLineMetric(true);
  for (auto const & [key, tiers] : LineData())
  {
    if (key == "fact" || key.rfind("mileage.", 0) == 0)
      continue;
    for (int tier = 0; tier < 3; ++tier)
      for (auto const & line : LinesFor(key, tier))
        for (char const * word : {" miles", " mile ", " mile.", " feet of", "moved four feet", "moved three feet"})
          if (line.find(word) != std::string::npos)
          {
            std::fprintf(stderr, "imperial in %s: %s\n", key.c_str(), line.c_str());
            CHECK(false);
          }
  }
  SetLineMetric(false);
}


namespace
{
std::string Lower(std::string s)
{
  for (char & c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool StartsWith(std::string const & s, std::string const & prefix) { return s.rfind(prefix, 0) == 0; }
}  // namespace

TEST(ForeignPhraseLinesAreIdleBanterNeverAtATurn)
{
  // Brandon's template, "X means 'turn left' in Y.", lives in the idle banter: idle is never
  // said within 600 m of a maneuver, so a "means turn left" line can't land near a real right turn.
  size_t foreignLines = 0;
  for (auto const & tier : LineData().at("idle"))
    for (auto const & line : tier)
    {
      auto const at = line.find(" means '");
      if (at == std::string::npos)
        continue;
      ++foreignLines;
      std::string const phrase = Lower(line.substr(0, at));
      bool known = false;
      for (auto const & entry : ForeignPhrases())
        known = known || Lower(entry.first) == phrase;
      CHECK(known);
      // The voice says the phrase in its own sounds, never the raw spelling.
      std::string const said = ForSpeech(SpeakableText(line));
      CHECK(StartsWith(said, "fx"));
      // A translated direction is followed at once by a word that it is not an instruction.
      std::string const lower = Lower(line);
      if (lower.find("'turn left'") != std::string::npos || lower.find("'turn right'") != std::string::npos)
        CHECK(lower.find("not now") != std::string::npos || lower.find("not yet") != std::string::npos ||
              lower.find("don't") != std::string::npos);
    }
  CHECK(foreignLines >= 12);

  // No line said at a turn, lane, exit, U-turn or reroute, or as part of an instruction, has a
  // foreign phrase in it.
  char const * const maneuverKeys[] = {"countdown", "lane.", "instruction.", "uturn", "straight", "rerouted",
                                       "curve.", "sketch.", "cmd.", "search.", "fear.curve", "road.highway"};
  size_t checked = 0;
  for (auto const & [key, tiers] : LineData())
  {
    bool maneuver = false;
    for (char const * k : maneuverKeys)
      maneuver = maneuver || StartsWith(key, k);
    if (!maneuver)
      continue;
    for (auto const & tier : tiers)
      for (auto const & line : tier)
      {
        ++checked;
        CHECK_EQUAL(Foreign(line), line);
        CHECK(line.find(" means '") == std::string::npos);
      }
  }
  CHECK(checked > 200);
}
