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

TEST(CarOnlyBitsDontFitWalksOrRides)
{
  CHECK(LinesFitTravelMode("gas.none", 0));
  SetLineTravelMode(TravelMode::Bike);
  CHECK(!LinesFitTravelMode("gas.none", 0));
  CHECK(!LinesFitTravelMode("autopilot", 0));
  // A reply to the driver still gets its "Noted.".
  CHECK(LinesFitTravelMode("cmd.avoidTolls", 0));
  CHECK(LinesFitTravelMode("idle", 0));
  SetLineTravelMode(TravelMode::Drive);
}

// A is a grade, not a shouted "a": "Grade: a." was said "Grade: uh."
TEST(GradeAIsSaidAsALetter)
{
  CHECK_EQUAL(SpeakableText("Trip grade: A."), std::string("Trip grade: A."));
  CHECK_EQUAL(SpeakableText("Grade: A minus. It would have been a A if it weren't for Elm Street."),
              std::string("Grade: A minus. It would have been an A if it weren't for Elm Street."));
  CHECK_EQUAL(SpeakableText("Without Elm Street, that's a A plus."), std::string("Without Elm Street, that's an A plus."));
  CHECK_EQUAL(SpeakableText("That's a F, honestly."), std::string("That's an F, honestly."));
  CHECK_EQUAL(SpeakableText("That's a B minus."), std::string("That's a B minus."));
  CHECK_EQUAL(SpeakableText("I'M NOT CRYING, I'M A PHONE!"), std::string("I'm not crying, I'm a phone!"));
}
