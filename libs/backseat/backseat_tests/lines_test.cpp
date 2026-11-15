#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"

#include <cstdio>

using namespace backseat;

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
