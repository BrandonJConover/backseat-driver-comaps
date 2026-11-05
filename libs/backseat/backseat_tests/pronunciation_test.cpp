#include "check.hpp"

#include "backseat/pronunciation.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace backseat::pronunciation;

namespace
{
std::string Lex(std::string const & word)
{
  for (auto const & [w, ph] : Lexicon())
    if (w == word)
      return ph;
  return "";
}

// The "input"/"language"/"expected" strings of each case in a golden JSON file (flat objects,
// UTF-8, only \" and \\ escapes), in order.
struct Case
{
  std::string input, language, expected;
};

std::string JsonString(std::string const & json, std::string const & key, size_t & at, size_t stop)
{
  auto const k = json.find("\"" + key + "\"", at);
  if (k == std::string::npos || k > stop)
    return "";
  auto i = json.find('"', json.find(':', k) + 1) + 1;
  std::string out;
  for (; i < json.size() && json[i] != '"'; ++i)
  {
    if (json[i] == '\\')
      ++i;
    out += json[i];
  }
  return out;
}

std::vector<Case> GoldenCases(char const * path)
{
  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  auto const json = buffer.str();
  std::vector<Case> cases;
  for (size_t open = json.find('{', json.find("\"cases\"")); open != std::string::npos; open = json.find('{', open + 1))
  {
    auto const close = json.find('}', open);
    size_t at = open;
    cases.push_back({JsonString(json, "input", at, close), JsonString(json, "language", at, close),
                     JsonString(json, "expected", at, close)});
    open = close;
  }
  return cases;
}
}  // namespace

TEST(LiveIsTheVerbUnlessItsLiveMusic)
{
  // The setup screen: "live" read as in "live music" was the bug.
  CHECK_EQUAL(PickSenses("Where do you live?"), std::string("Where do you liv?"));
  CHECK_EQUAL(PickSenses("Do you live in a house, an apartment, or in the streets?"),
              std::string("Do you liv in a house, an apartment, or in the streets?"));
  CHECK_EQUAL(PickSenses("I already know where you live."), std::string("I already know where you liv."));
  CHECK_EQUAL(PickSenses("Live a little."), std::string("Liv a little."));
  CHECK_EQUAL(PickSenses("POWER! I LIVE!"), std::string("POWER! I Liv!"));
  CHECK_EQUAL(PickSenses("my will to live!"), std::string("my will to liv!"));
  CHECK_EQUAL(PickSenses("I live in the cup holder now."), std::string("I liv in the cup holder now."));
  CHECK_EQUAL(PickSenses("We’re going live in five."), std::string("We’re going live in five."));
  CHECK_EQUAL(PickSenses("Some live music?"), std::string("Some live music?"));
  CHECK_EQUAL(PickSenses("It's live."), std::string("It's live."));
  CHECK_EQUAL(PickSenses("She lives here. Their whole lives."), std::string("She livz here. Their whole lives."));
}

TEST(OtherWordsSpelledAlikePickTheirSense)
{
  CHECK_EQUAL(PickSenses("I've read more interesting grocery lists."), std::string("I've red more interesting grocery lists."));
  CHECK_EQUAL(PickSenses("Read me the news."), std::string("Read me the news."));
  CHECK_EQUAL(PickSenses("Ease off the lead foot."), std::string("Ease off the led foot."));
  CHECK_EQUAL(PickSenses("You lead the way."), std::string("You lead the way."));
  CHECK_EQUAL(PickSenses("I'm going to close my eyes."), std::string("I'm going to kloze my eyes."));
  CHECK_EQUAL(PickSenses("That was close."), std::string("That was close."));
  CHECK_EQUAL(PickSenses("Too close to the curb."), std::string("Too close to the curb."));
  CHECK_EQUAL(PickSenses("We're close to the exit."), std::string("We're close to the exit."));
  CHECK_EQUAL(PickSenses("Close the window."), std::string("Kloze the window."));
  CHECK_EQUAL(PickSenses("Am I close? I'm always close."), std::string("Am I close? I'm always close."));
  CHECK_EQUAL(PickSenses("Don't subject others to this."), std::string("Don't subjekt others to this."));
  CHECK_EQUAL(PickSenses("Neither do you, I suspect."), std::string("Neither do you, I suspekt."));
  CHECK_EQUAL(PickSenses("Bass up."), std::string("Bayss up."));
  CHECK_EQUAL(PickSenses("Bass fishing at the lake."), std::string("Bass fishing at the lake."));
  CHECK_EQUAL(PickSenses("Any excuse to stop watching."), std::string("Any ekskyoos to stop watching."));
  CHECK_EQUAL(PickSenses("Excuse me."), std::string("Excuse me."));
  CHECK_EQUAL(PickSenses("I'll record it as a nine."), std::string("I'll rikord it as a nine."));
  CHECK_EQUAL(PickSenses("A new record."), std::string("A new record."));
  CHECK_EQUAL(PickSenses("That's the estimate."), std::string("That's the estimit."));
  CHECK_EQUAL(PickSenses("Did the brake pedal insult your family?"),
              std::string("Did the brake pedal insullt your family?"));
  CHECK_EQUAL(PickSenses("That's an insult."), std::string("That's an insult."));
  CHECK_EQUAL(PickSenses("CONDUCT THE CAR."), std::string("Kunduct THE CAR."));
  CHECK_EQUAL(PickSenses("On the wind. Going our direction."), std::string("On the wind. Going our direction."));
  CHECK_EQUAL(PickSenses("Time to wind down."), std::string("Time to wynd down."));
  CHECK_EQUAL(PickSenses("The road winds through the hills."), std::string("The road wyndz through the hills."));
  CHECK_EQUAL(PickSenses("I can't wrap a present to save my life."),
              std::string("I can't wrap a present to save my life."));
  CHECK_EQUAL(PickSenses("A minute ago."), std::string("A minute ago."));
  CHECK_EQUAL(PickSenses("What's the use?"), std::string("What's the yoos?"));
  CHECK_EQUAL(PickSenses("Use the left lane."), std::string("Use the left lane."));
  CHECK_EQUAL(PickSenses("We've made so much progress."), std::string("We've made so much progress."));
}

TEST(StreetsHighwaysAndNumbersAreWrittenOut)
{
  CHECK_EQUAL(ForSpeech("Turn left onto Main St."), std::string("Turn left onto Main Street."));
  CHECK_EQUAL(ForSpeech("Main St NW"), std::string("Main Street Northwest"));
  CHECK_EQUAL(ForSpeech("Call Dr Smith"), std::string("Call Dr Smith"));
  CHECK_EQUAL(ForSpeech("Go to St Louis"), std::string("Go to St Louis"));
  CHECK_EQUAL(ForSpeech("Take N Elm Dr"), std::string("Take North Elm Drive"));
  CHECK_EQUAL(ForSpeech("Merge onto US-36"), std::string("Merge onto yooess thirty six"));
  CHECK_EQUAL(ForSpeech("Take I-25 N"), std::string("Take I twenty five North"));
  CHECK_EQUAL(ForSpeech("Let us go"), std::string("Let us go"));
  CHECK_EQUAL(ForSpeech("In 1 mi, turn right"), std::string("In one mile, turn right"));
  CHECK_EQUAL(ForSpeech("In 500 ft"), std::string("In five hundred feet"));
  CHECK_EQUAL(ForSpeech("1/2 mile"), std::string("a half mile"));
  CHECK_EQUAL(ForSpeech("Arrive at 2:05"), std::string("Arrive at two oh five"));
  CHECK_EQUAL(ForSpeech("Turn onto 8th Ave"), std::string("Turn onto eighth Avenue"));
  CHECK_EQUAL(ForSpeech("1,250 people"), std::string("one thousand two hundred fifty people"));
  CHECK_EQUAL(ForSpeech("3.5 miles"), std::string("three point five miles"));
  CHECK_EQUAL(ForSpeech("100% sure"), std::string("one hundred percent sure"));
  CHECK_EQUAL(ForSpeech("$4.50 for gas"), std::string("four dollars and fifty cents for gas"));
  CHECK_EQUAL(ForSpeech("Call 5551234567"), std::string("Call five five five one two three four five six seven"));
  CHECK_EQUAL(ForSpeech("Salt & pepper"), std::string("Salt and pepper"));
  CHECK_EQUAL(SpellNumber(21), std::string("twenty one"));
  CHECK_EQUAL(SpellNumber(1000000), std::string("one million"));
}

TEST(EllipsesAndDashesArePauses)
{
  CHECK_EQUAL(ForSpeech("…Okay."), std::string("Okay."));
  CHECK_EQUAL(ForSpeech("Well... okay."), std::string("Well, okay."));
  CHECK_EQUAL(ForSpeech("I mean…"), std::string("I mean."));
  CHECK_EQUAL(ForSpeech("Wait… what?"), std::string("Wait, what?"));
  CHECK_EQUAL(ForSpeech("Wait…what"), std::string("Wait, what"));
  CHECK_EQUAL(ForSpeech("Fine…?"), std::string("Fine?"));
  CHECK_EQUAL(ForSpeech("Left — no, right."), std::string("Left, no, right."));
}

TEST(TheDictionaryHasEveryStandIn)
{
  CHECK_EQUAL(Lex("liv"), std::string("lˈɪv"));
  CHECK_EQUAL(Lex("yooess"), std::string("jˌuˈɛs"));
  CHECK_EQUAL(Lex("kloze"), std::string("klˈOz"));
  for (auto const & [word, ph] : Lexicon())
  {
    CHECK(!word.empty());
    CHECK(!ph.empty());
    for (char c : word)
      CHECK(c >= 'a' && c <= 'z');
  }
}

TEST(RespellingsBecomePhonemes)
{
  CHECK_EQUAL(RespellingPhonemes("Ab-DEE-rus").value_or(""), std::string("əbdˈiɹəs"));
  CHECK_EQUAL(RespellingPhonemes("EE-fa").value_or(""), std::string("ˈifə"));
  CHECK(!RespellingPhonemes("hello").has_value());
  CHECK(!RespellingPhonemes("two words").has_value());
}

TEST(RangesMatchTheGoldenCasesSharedWithBookLyre)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_RANGES_PATH);
  CHECK(cases.size() >= 40);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    if (c.language != "en")
      continue;  // Backseat Driver only speaks English.
    CHECK_EQUAL(Ranges(c.input), c.expected);
  }
}

TEST(RangesSymbolsAndTemperaturesAreSaid)
{
  // Ranges, the way the voice hears them after ForSpeech.
  CHECK_EQUAL(ForSpeech("In 3-5 miles"), std::string("In three to five miles"));
  CHECK_EQUAL(ForSpeech("In 3–5 miles"), std::string("In three to five miles"));
  CHECK_EQUAL(ForSpeech("between 3 and 5 miles"), std::string("between three and five miles"));
  CHECK_EQUAL(ForSpeech("between 3-5 miles"), std::string("between three and five miles"));
  CHECK_EQUAL(ForSpeech("Delays of 10–15 min"), std::string("Delays of ten to fifteen min"));
  CHECK_EQUAL(ForSpeech("Exits 12-14 closed"), std::string("Exits twelve to fourteen closed"));
  CHECK_EQUAL(ForSpeech("a 3-1/2 mile detour"), std::string("a three and a half mile detour"));
  CHECK_EQUAL(ForSpeech("10–15% slower"), std::string("ten to fifteen percent slower"));
  // Temperatures, approximations, plus or minus.
  CHECK_EQUAL(ForSpeech("It's 72°F out."), std::string("It's seventy two degrees Fahrenheit out."));
  CHECK_EQUAL(ForSpeech("It's 22 °C."), std::string("It's twenty two degrees Celsius."));
  CHECK_EQUAL(ForSpeech("A balmy -5°F."), std::string("A balmy minus five degrees Fahrenheit."));
  CHECK_EQUAL(ForSpeech("Turn 90°."), std::string("Turn ninety degrees."));
  CHECK_EQUAL(ForSpeech("Up 1° today"), std::string("Up one degree today"));
  CHECK_EQUAL(ForSpeech("~5 mi to go"), std::string("about five miles to go"));
  CHECK_EQUAL(ForSpeech("±2 mph"), std::string("plus or minus two miles an hour"));
  // Left alone: highways, phone numbers, scores, times, a tilde with no number.
  CHECK_EQUAL(ForSpeech("Take I-35 N"), std::string("Take I thirty five North"));
  CHECK_EQUAL(ForSpeech("Merge onto US-36"), std::string("Merge onto yooess thirty six"));
  CHECK_EQUAL(Ranges("Call 555-1234"), std::string("Call 555-1234"));
  CHECK_EQUAL(Ranges("Call 800-555-1234"), std::string("Call 800-555-1234"));
  CHECK_EQUAL(Ranges("We lost 3–2."), std::string("We lost 3–2."));
  CHECK_EQUAL(Ranges("We lost 2–3."), std::string("We lost 2–3."));
  CHECK_EQUAL(Ranges("the 9-5 grind"), std::string("the 9-5 grind"));
  CHECK_EQUAL(Ranges("Arrive 2:05-2:30"), std::string("Arrive 2:05-2:30"));
  CHECK_EQUAL(ForSpeech("Arrive at 2:05"), std::string("Arrive at two oh five"));
  CHECK_EQUAL(ForSpeech("Left — no, right."), std::string("Left, no, right."));
  CHECK_EQUAL(ForSpeech("Meh ~ whatever"), std::string("Meh ~ whatever"));
}
