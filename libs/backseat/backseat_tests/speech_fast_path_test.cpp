// ForSpeech skips its slow regex passes when a line has nothing they could read (no digit, no
// degree sign, no dollar sign...). These pin that every pass still fires on just its own trigger,
// and that a digit that only shows up mid-pipeline (from the driver's own words) is still read.
// The skip was checked against the old pipeline on all 14,585 line-bank lines (raw and filled):
// byte-identical output, about a third faster.
#include "check.hpp"

#include "backseat/pronunciation.hpp"

#include <string>

using namespace backseat::pronunciation;

namespace
{
std::string Say(char const * text) { return ForSpeech(text); }
}  // namespace

TEST(FastPathEachNumberPassStillFiresOnItsTrigger)
{
  CHECK_EQUAL(Say("It's -5°F out."), std::string("It's minus five degrees Fahrenheit out."));
  CHECK_EQUAL(Say("~5 minutes"), std::string("about five minutes"));
  CHECK_EQUAL(Say("±3 meters"), std::string("plus or minus three meters"));
  CHECK_EQUAL(Say("$3.50"), std::string("three dollars and fifty cents"));
  CHECK_EQUAL(Say("20%"), std::string("twenty percent"));
  CHECK_EQUAL(Say("at 2:30"), std::string("at two thirty"));
  CHECK_EQUAL(Say("8th Street"), std::string("eighth Street"));
  CHECK_EQUAL(Say("1,200 people"), std::string("one thousand two hundred people"));
  CHECK_EQUAL(Say("3–5 miles"), std::string("three to five miles"));
  CHECK_EQUAL(Say("in 1868"), std::string("in eighteen sixty eight"));
  CHECK_EQUAL(Say("I-35"), std::string("I thirty five"));
  CHECK_EQUAL(Say("1600 Pennsylvania Avenue"), std::string("sixteen hundred Pennsylvania Avenue"));
  CHECK_EQUAL(Say("3/4 full"), std::string("three quarters full"));
}

TEST(FastPathDashesAndWordsWithoutDigits)
{
  // No digit at all: the dash, street and compass passes still run.
  CHECK_EQUAL(Say("Go—now"), std::string("Go, now"));
  CHECK_EQUAL(Say("Turn left at the light."), std::string("Turn left at the light."));
  CHECK_EQUAL(Say("Wait… what?"), std::string("Wait, what?"));
  // A curly apostrophe shares a byte with the dashes; it must stay as it was.
  CHECK_EQUAL(Say("Don’t."), std::string("Don’t."));
  // A spelled clock hour has no digit and is still a clock time.
  CHECK_EQUAL(Say("Up at five a.m. again."), std::string("Up at five ayem again."));
}

TEST(FastPathReadsDigitsThatAppearMidPipeline)
{
  // The driver's own words run after the early passes; a digit they bring in is still read.
  SetUserWords("Gizmo\t5 mi\n");
  CHECK_EQUAL(Say("Gizmo ahead"), std::string("five miles ahead"));
  SetUserWords("");
  CHECK_EQUAL(Say("Gizmo ahead"), std::string("Gizmo ahead"));
}

TEST(FastPathNoTriggerLeavesTextAlone)
{
  // Characters the skipped passes look for, alone without a digit, stay as written.
  CHECK_EQUAL(Say("Costs $ and % and ~ and :"), std::string("Costs $ and % and ~ and :"));
  CHECK_EQUAL(Say("and/or"), std::string("and/or"));
  CHECK_EQUAL(Say(""), std::string(""));
}
