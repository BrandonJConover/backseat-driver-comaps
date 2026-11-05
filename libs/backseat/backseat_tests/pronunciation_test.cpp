#include "check.hpp"

#include "backseat/pronunciation.hpp"

#include <string>

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

TEST(TheDriversOwnWordsWin)
{
  SetUserWords("Quasi\tKWAH-zee\nmt hood\tmount hood\nlive\tlive\nGIF\tJIF\n");
  CHECK_EQUAL(ForSpeech("Quasi-legal. quasi"), std::string("Qzva-legal. qzva"));
  CHECK_EQUAL(ForSpeech("Off to Mt Hood!"), std::string("Off to mount hood!"));
  // The driver overrode the verb rule.
  CHECK_EQUAL(ForSpeech("Where do you live?"), std::string("Where do you live?"));
  CHECK_EQUAL(ForSpeech("A GIF of you."), std::string("A jif of you."));
  CHECK_EQUAL(ForSpeech("Gifted"), std::string("Gifted"));
  bool found = false;
  for (auto const & [word, ph] : Lexicon())
    if (word == "qzva")
      found = ph == "kwˈɑzi";
  CHECK(found);
  SetUserWords("");
  CHECK_EQUAL(ForSpeech("Where do you live?"), std::string("Where do you liv?"));
  CHECK_EQUAL(RespellingPhonemes("WINE-sap").value_or(""), std::string("wˈInsəp"));
}
