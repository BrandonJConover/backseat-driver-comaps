#include "check.hpp"

#include "backseat/pronunciation.hpp"

#include <fstream>
#include <set>
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
  CHECK_EQUAL(ForSpeech("Call Dr Smith"), std::string("Call Doctor Smith"));
  CHECK_EQUAL(ForSpeech("Go to St Louis"), std::string("Go to Saint Louis"));
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

TEST(NamesTheVoiceWouldGuessAreInTheDictionary)
{
  // Misaki phonemes from BookLyre's names lexicon (CROSS X-003): line-bank names and drivers' names.
  CHECK_EQUAL(Lex("neil"), std::string("nˈil"));
  CHECK_EQUAL(Lex("armstrong"), std::string("ˈɑɹmstɹˌɑŋ"));
  CHECK_EQUAL(Lex("gertrude"), std::string("ɡˈɜɹtɹud"));
  CHECK_EQUAL(Lex("nigel"), std::string("nˈIʤᵊl"));
  CHECK_EQUAL(Lex("floyd"), std::string("flˈYd"));
  CHECK_EQUAL(Lex("maxine"), std::string("mæksˈin"));
  CHECK_EQUAL(Lex("doug"), std::string("dˈʌɡ"));
  CHECK_EQUAL(Lex("linda"), std::string("lˈɪndə"));
  CHECK_EQUAL(Lex("larry"), std::string("lˈɛɹi"));
  CHECK_EQUAL(Lex("emily"), std::string("ˈɛməli"));
  CHECK_EQUAL(Lex("paul"), std::string("pˈɔl"));
  CHECK_EQUAL(Lex("kevin"), std::string("kˈɛvən"));
  CHECK_EQUAL(Lex("yuri"), std::string("jˈʊɹi"));
  // Words the dictionary had before keep their sounds.
  CHECK_EQUAL(Lex("liv"), std::string("lˈɪv"));
  CHECK_EQUAL(Lex("bayss"), std::string("bˈAs"));
  // One entry per word, or a host's dictionary would keep whichever came last.
  std::set<std::string> words;
  for (auto const & [w, ph] : Lexicon())
    CHECK(words.insert(w).second);
  // Names only reach the voice through the dictionary: ForSpeech leaves them as written.
  CHECK_EQUAL(ForSpeech("Nice one, Gertrude."), std::string("Nice one, Gertrude."));
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

TEST(RoadsMatchTheGoldenCases)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_ROADS_PATH);
  CHECK(cases.size() >= 20);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
}

TEST(HouseNumbersAreSaidInPairs)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_ADDRESSES_PATH);
  CHECK(cases.size() >= 20);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  CHECK_EQUAL(HouseNumbers("1600 Pennsylvania Avenue"), std::string("sixteen hundred Pennsylvania Avenue"));
  CHECK_EQUAL(HouseNumbers("1,600 Main Street"), std::string("1,600 Main Street"));
  CHECK_EQUAL(HouseNumbers("$1600 Main Street"), std::string("$1600 Main Street"));
  CHECK_EQUAL(HouseNumbers("1600 feet"), std::string("1600 feet"));
}

TEST(ClockTimesAreSaidWithAmAndPm)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_TIMES_PATH);
  CHECK(cases.size() >= 20);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  CHECK_EQUAL(Clock("by 5pm"), std::string("by five peeem"));
  CHECK_EQUAL(Clock("12 PM"), std::string("noon"));
  CHECK_EQUAL(Clock("12:00 a.m. Then"), std::string("midnight. Then"));
  CHECK_EQUAL(Clock("five am"), std::string("five am"));
  CHECK_EQUAL(Clock("13 pm"), std::string("13 pm"));
  CHECK_EQUAL(Clock("$5 pm"), std::string("$5 pm"));
  // The stand-ins are in the dictionary, so the voice says the letters.
  bool ayem = false, peeem = false;
  for (auto const & [w, ph] : Lexicon())
  {
    ayem = ayem || w == "ayem";
    peeem = peeem || w == "peeem";
  }
  CHECK(ayem);
  CHECK(peeem);
}

TEST(YearsAreSaidAsYears)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_YEARS_PATH);
  CHECK(cases.size() >= 20);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  CHECK_EQUAL(Years("in 1868"), std::string("in eighteen sixty eight"));
  CHECK_EQUAL(Years("since 1000"), std::string("since one thousand"));
  CHECK_EQUAL(Years("in 1066"), std::string("in ten sixty six"));
  CHECK_EQUAL(Years("the 1900s"), std::string("the nineteen hundreds"));
  CHECK_EQUAL(Years("the 1870s"), std::string("the eighteen seventies"));
  CHECK_EQUAL(Years("by 2100"), std::string("by 2100"));
  // Navigation distances are never years, whatever the word before them.
  for (auto const * unit : {"feet", "ft", "m", "meters", "miles", "yards", "km"})
  {
    auto const said = Years(std::string("In 1500 ") + unit + ", turn left.");
    CHECK_EQUAL(said, std::string("In 1500 ") + unit + ", turn left.");
  }
}

TEST(RouteNumbersAreSaidTheWayDriversSayThem)
{
  CHECK_EQUAL(RouteNumber("5"), std::string("five"));
  CHECK_EQUAL(RouteNumber("66"), std::string("sixty six"));
  CHECK_EQUAL(RouteNumber("405"), std::string("four oh five"));
  CHECK_EQUAL(RouteNumber("183"), std::string("one eighty three"));
  CHECK_EQUAL(RouteNumber("200"), std::string("two hundred"));
  CHECK_EQUAL(RouteNumber("1431"), std::string("fourteen thirty one"));
  CHECK_EQUAL(RouteNumber("3502"), std::string("thirty five oh two"));
  CHECK_EQUAL(RouteNumber("1400"), std::string("fourteen hundred"));
  CHECK_EQUAL(RouteNumber("3000"), std::string("three thousand"));
  CHECK_EQUAL(RouteNumber("12345"), std::string("12345"));
  CHECK_EQUAL(RouteNumber("07"), std::string("zero seven"));
  CHECK_EQUAL(Roads("Take I-405"), std::string("Take I four oh five"));
  CHECK_EQUAL(Roads("Drive 405 miles"), std::string("Drive 405 miles"));
  CHECK_EQUAL(Roads("Take OR 3 now"), std::string("Take OR 3 now"));
  CHECK_EQUAL(Roads("Take OR-99"), std::string("Take Oregon ninety nine"));
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

TEST(ForeignPhrasesAreSaidInTheirOwnSounds)
{
  // Brandon's template: "X means 'turn left' in Y." Spanish, French, German, Japanese.
  CHECK_EQUAL(ForSpeech("Gire a la izquierda means 'turn left' in Spanish."),
              std::string("fxheereh fxah fxlah fxeeskeeairdah means 'turn left' in Spanish."));
  CHECK_EQUAL(ForSpeech("Tournez à gauche means 'turn left' in French."),
              std::string("fxtoornay fxah fxgohsh means 'turn left' in French."));
  CHECK_EQUAL(ForSpeech("Biegen Sie links ab means 'turn left' in German."),
              std::string("fxbeegen fxzee fxlinks fxahp means 'turn left' in German."));
  CHECK_EQUAL(ForSpeech("左に曲がってください means 'please turn left' in Japanese."),
              std::string("fxheedahree fxnee fxmahgahtteh fxkoodahsigh means 'please turn left' in Japanese."));
  CHECK_EQUAL(ForSpeech("Aprende a conducir means 'learn to drive' in Spanish."),
              std::string("fxahprendeh fxah fxkohndooseer means 'learn to drive' in Spanish."));
  CHECK_EQUAL(ForSpeech("Apprends à conduire."), std::string("fxahprahn fxah fxkohndweer."));
  CHECK_EQUAL(ForSpeech("Lern Auto fahren."), std::string("fxlairn fxowtoh fxfahren."));
  CHECK_EQUAL(ForSpeech("運転を習ってください."), std::string("fxoontehn fxoh fxnahrahtteh fxkoodahsigh."));
  CHECK_EQUAL(ForSpeech("\"Soy un conductor excelente.\""),
              std::string("\"fxsoy fxoon fxkohndooktor fxehksehlenteh.\""));
  CHECK_EQUAL(ForSpeech("Je suis un excellent conducteur. Trust me."),
              std::string("fxzhuh fxswee fxuhn fxehksehlahn fxkohndooktur. Trust me."));
  CHECK_EQUAL(ForSpeech("Ich bin ein ausgezeichneter Fahrer!"),
              std::string("fxish fxbin fxeyen fxowsgehtsighshnehter fxfahrer!"));
  CHECK_EQUAL(ForSpeech("私は運転がとても上手です"),
              std::string("fxwahtahshee fxwah fxoontehn fxgah fxtohtehmoh fxjohzoo fxdess"));
  CHECK_EQUAL(ForSpeech("Te lo dije. Recalculando."), std::string("fxteh fxloh fxdeeheh. fxrehkahlkoolahndoh."));
  // Case doesn't matter; word boundaries do; English look-alikes are left alone.
  CHECK_EQUAL(Foreign("GIRE A LA IZQUIERDA!"), std::string("fxheereh fxah fxlah fxeeskeeairdah!"));
  CHECK_EQUAL(Foreign("Click the links above"), std::string("Click the links above"));
  CHECK_EQUAL(Foreign("Recalculandos"), std::string("Recalculandos"));
  CHECK_EQUAL(Foreign("Tournez a gauche"), std::string("Tournez a gauche"));
  CHECK_EQUAL(Foreign("Turn left in 300 ft"), std::string("Turn left in 300 ft"));
  // Each stand-in is in the dictionary, as the respelling says it.
  for (auto const & [phrase, standIns] : ForeignPhrases())
  {
    CHECK_EQUAL(Foreign(phrase), standIns);
    std::istringstream words(standIns);
    for (std::string w; words >> w;)
      CHECK(!Lex(w).empty());
  }
  CHECK_EQUAL(Lex("fxgohsh"), std::string("ɡˈOʃ"));
  CHECK_EQUAL(Lex("fxah"), std::string("ɑ"));
  CHECK_EQUAL(Lex("fxheereh"), std::string("hˈiɹɛ"));
  CHECK_EQUAL(Lex("fxkoodahsigh"), std::string("kudɑsˈI"));
  CHECK_EQUAL(Lex("fxeeskeeairdah"), std::string("iskiˈɛɹdɑ"));
  CHECK_EQUAL(Lex("liv"), std::string("lˈɪv"));
}

TEST(SaintsDoctorsAndJuniorsInStreetNames)
{
  // Titles before a name.
  CHECK_EQUAL(ForSpeech("Turn left onto St. Johns Rd"), std::string("Turn left onto Saint Johns Road"));
  CHECK_EQUAL(ForSpeech("Continue on St Charles Ave"), std::string("Continue on Saint Charles Avenue"));
  CHECK_EQUAL(ForSpeech("Take St. Johns Rd"), std::string("Take Saint Johns Road"));
  CHECK_EQUAL(ForSpeech("St Louis is ahead."), std::string("Saint Louis is ahead."));
  CHECK_EQUAL(ForSpeech("Visit St. John's Church"), std::string("Visit Saint John's Church"));
  CHECK_EQUAL(ForSpeech("Head to Ste. Genevieve"), std::string("Head to Saint Genevieve"));
  CHECK_EQUAL(ForSpeech("Exit toward Ft Worth"), std::string("Exit toward Fort Worth"));
  CHECK_EQUAL(ForSpeech("Turn right onto Dr Martin Luther King Jr Blvd"),
              std::string("Turn right onto Doctor Martin Luther King Junior Boulevard"));
  CHECK_EQUAL(ForSpeech("Turn right onto Dr. Martin Luther King Jr. Blvd."),
              std::string("Turn right onto Doctor Martin Luther King Junior Boulevard."));
  CHECK_EQUAL(ForSpeech("Martin Luther King, Jr. was right."), std::string("Martin Luther King, Junior was right."));
  CHECK_EQUAL(ForSpeech("Hank Williams Sr. Then left."), std::string("Hank Williams Senior. Then left."));
  CHECK_EQUAL(ForSpeech("Like Sammy Davis Jr."), std::string("Like Sammy Davis Junior."));
  // Street types stay street types.
  CHECK_EQUAL(ForSpeech("Main St. Then left"), std::string("Main Street. Then left"));
  CHECK_EQUAL(ForSpeech("Turn onto Elm Dr."), std::string("Turn onto Elm Drive."));
  CHECK_EQUAL(ForSpeech("Main St NW"), std::string("Main Street Northwest"));
  CHECK_EQUAL(ForSpeech("Turn onto 5th St. Johnson is waiting."), std::string("Turn onto fifth Street. Johnson is waiting."));
  CHECK_EQUAL(ForSpeech("Elm Dr. Keep going."), std::string("Elm Drive. Keep going."));
  // Road types next to a route number.
  CHECK_EQUAL(ForSpeech("Take Hwy 290"), std::string("Take Highway two ninety"));
  CHECK_EQUAL(ForSpeech("Merge onto the 101 Fwy."), std::string("Merge onto the one oh one Freeway."));
  CHECK_EQUAL(ForSpeech("Stay on Rte. 66"), std::string("Stay on Route sixty six"));
  // Not titles: all caps, lowercase, no name after.
  CHECK_EQUAL(ForSpeech("Take SR 71"), std::string("Take State Route seventy one"));
  CHECK_EQUAL(ForSpeech("the dr is in"), std::string("the dr is in"));
  CHECK_EQUAL(ForSpeech("Jr high"), std::string("Jr high"));
}
