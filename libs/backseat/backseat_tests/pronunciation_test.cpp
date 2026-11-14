#include "check.hpp"

#include "backseat/pronunciation.hpp"

#include <algorithm>
#include <cctype>
#include <map>
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
    // The last case closes the array; objects after it ("places", "localPlaces") aren't cases.
    auto const next = json.find_first_not_of(" \t\r\n", close + 1);
    if (next == std::string::npos || json[next] != ',')
      break;
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
  CHECK_EQUAL(ForSpeech("3.5 miles"), std::string("three and a half miles"));
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

TEST(KanaIsSaidNotSkipped)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_KANA_PATH);
  CHECK(cases.size() >= 20);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  CHECK_EQUAL(Kana("きって"), std::string("kitte"));
  CHECK_EQUAL(Kana("マッチャ"), std::string("matcha"));
  CHECK_EQUAL(Kana("ティー"), std::string("ti"));
  CHECK_EQUAL(Kana("ヷ"), std::string("ヷ"));
  CHECK_EQUAL(Kana("no kana here"), std::string("no kana here"));
  // Phrases the lines quote keep their hand-made respellings: Foreign() runs first.
  CHECK(ForSpeech("左に曲がってください").find("magatte") == std::string::npos);
  CHECK_EQUAL(Kana(Foreign("左に曲がってください")), Foreign("左に曲がってください"));
}

TEST(CyrillicIsSaidNotSkipped)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_CYRILLIC_PATH);
  CHECK(cases.size() >= 20);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  CHECK_EQUAL(Cyrillic("СССР"), std::string("S-S-S-R"));
  CHECK_EQUAL(Cyrillic("моё"), std::string("moyo"));
  CHECK_EQUAL(Cyrillic("no Cyrillic here"), std::string("no Cyrillic here"));
  // Phrases the lines quote keep their hand-made respellings: Foreign() runs first.
  CHECK(ForSpeech("Поверните налево").find("Povernite") == std::string::npos);
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
  CHECK_EQUAL(ForSpeech("Delays of 10–15 min"), std::string("Delays of ten to fifteen minutes"));
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
  // Opening hours: a bare hyphen between two whole hours after a day or "open" is "to".
  CHECK_EQUAL(ForSpeech("Mon-Fri 9-5"), std::string("Monday to Friday nine to five"));
  CHECK_EQUAL(ForSpeech("Open 9-5, closed Sun."), std::string("Open nine to five, closed Sun."));
  CHECK_EQUAL(ForSpeech("Open 9-5pm"), std::string("Open nine to five peeem"));
  CHECK_EQUAL(ForSpeech("the 9-5 grind"), std::string("the nine-five grind"));
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
  // The rest of Brandon's tables, said where the car is stopped or the trip is over.
  CHECK_EQUAL(ForSpeech("Je te l'avais dit means 'I told you so' in French."),
              std::string("fxzhuh fxtuh fxlahveh fxdee means 'I told you so' in French."));
  CHECK_EQUAL(Foreign("HAB ICH DIR DOCH GESAGT!"), std::string("fxhahp fxish fxdeer fxdokh fxgehzahkt!"));
  CHECK_EQUAL(ForSpeech("Route wird neu berechnet."), std::string("fxrooteh fxveert fxnoy fxbehreshnet."));
  CHECK_EQUAL(ForSpeech("Leer autorijden."), std::string("fxlayr fxowtohrayden."));
  CHECK_EQUAL(ForSpeech("Научись водить."), std::string("fxnahoochees fxvahdeet."));
  CHECK_EQUAL(ForSpeech("Я отличный водитель."), std::string("fxyah fxahtleechnee fxvahdeetyel."));
  CHECK_EQUAL(ForSpeech("Sla linksaf. Not now."), std::string("fxslah fxlinksahf. Not now."));
  CHECK_EQUAL(ForSpeech("좌회전하세요."), std::string("fxjwahhwehjun fxhahsehyo."));
  CHECK_EQUAL(ForSpeech("向左转."), std::string("fxshyahng fxdzwoh fxjwahn."));
  // "Leer" alone is English; the Dutch phrase needs both words.
  CHECK_EQUAL(Foreign("Don't leer at the car next to us."), std::string("Don't leer at the car next to us."));
  CHECK_EQUAL(Foreign("The route will be recalculated"), std::string("The route will be recalculated"));
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

TEST(StreetTypesAbroadAreSaidInTheirOwnLanguage)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_STREET_TYPES_PATH);
  CHECK(cases.size() >= 25);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  // Every stand-in the pass can swap in is in the dictionary, or the voice would read "fxkahyeh".
  std::set<std::string> words;
  for (auto const & [w, ph] : Lexicon())
  {
    CHECK(!ph.empty());
    words.insert(w);
  }
  CHECK(ForeignStreetRespellings().size() >= 40);
  for (auto const & said : ForeignStreetRespellings())
  {
    std::string standIn = "fx";
    for (char ch : said)
      if (std::isalpha(static_cast<unsigned char>(ch)))
        standIn += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    CHECK(words.count(standIn) == 1);
  }
  // Names and English words are left; the pass only touches the type and its particles.
  CHECK_EQUAL(ForeignStreets("Turn left onto Main Street."), std::string("Turn left onto Main Street."));
  CHECK_EQUAL(ForeignStreets("de la Reforma"), std::string("de la Reforma"));
  CHECK_EQUAL(ForeignStreets("Calle"), std::string("Calle"));
  CHECK_EQUAL(ForeignStreets("Calle, Ocho"), std::string("Calle, Ocho"));
  // The driver's own pronunciation of a name still wins: user words run first.
  SetUserWords("Calle Ocho\tKAI-yeh OH-cho\n");
  CHECK(ForSpeech("Turn onto Calle Ocho.").find("fxkahyeh") == std::string::npos);
  SetUserWords("");
}

TEST(HeteronymSensesMatchTheGoldenCasesSharedWithBookLyre)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_HETERONYMS_PATH);
  CHECK(cases.size() >= 50);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  // "boughed" and "boughing" aren't words, so the voice needs them in the dictionary.
  std::set<std::string> words;
  for (auto const & [w, ph] : Lexicon())
    words.insert(w);
  for (auto const * standIn : {"bough", "boughs", "boughed", "boughing", "mynoot", "bayss"})
    CHECK(words.count(standIn) == 1);
}

TEST(BowLeadAndMinuteNeedTheirWholeContext)
{
  // Needs the third word after: "the bow of the ship", "minute and careful examination".
  CHECK_EQUAL(PickSenses("the bow of the ship"), std::string("the bough of the ship"));
  CHECK_EQUAL(PickSenses("the bow of the queen"), std::string("the bow of the queen"));
  CHECK_EQUAL(PickSenses("a minute and careful examination"), std::string("a mynoot and careful examination"));
  CHECK_EQUAL(PickSenses("a minute and a half"), std::string("a minute and a half"));
  CHECK_EQUAL(PickSenses("so minute and a half"), std::string("so minute and a half"));
  // A sentence break cuts the context: "...the boat. Bow ties..." isn't a ship's bow.
  CHECK_EQUAL(PickSenses("Sell the boat's. Bow ties too."), std::string("Sell the boat's. Bow ties too."));
  // "bowed" at the end of a clause only after someone did it.
  CHECK_EQUAL(PickSenses("He bowed"), std::string("He boughed"));
  CHECK_EQUAL(PickSenses("The shelf bowed."), std::string("The shelf bowed."));
  CHECK_EQUAL(PickSenses("They bowed \xE2\x80\x94 then left."), std::string("They boughed \xE2\x80\x94 then left."));
  // Lead after a material verb, unless it's the other lead.
  CHECK_EQUAL(PickSenses("heavy as lead singers go"), std::string("heavy as lead singers go"));
  CHECK_EQUAL(PickSenses("pumped him full of lead"), std::string("pumped him full of led"));
}

TEST(RegnalNumbersMatchTheGoldenCases)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_REGNAL_PATH);
  CHECK(cases.size() >= 25);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  // The pass alone: titles make any name regnal, a title alone doesn't.
  CHECK_EQUAL(Regnal("King Zog I"), std::string("King Zog the first"));
  CHECK_EQUAL(Regnal("King II"), std::string("King II"));
  CHECK_EQUAL(Regnal("Henry I of England"), std::string("Henry the first of England"));
  CHECK_EQUAL(Regnal("Charles XL"), std::string("Charles XL"));
  CHECK_EQUAL(Regnal("charles V"), std::string("charles V"));
  // Lines with no numeral after a name come back byte for byte.
  CHECK_EQUAL(Regnal("Turn left onto Main St, then I-35 N."), std::string("Turn left onto Main St, then I-35 N."));
}

TEST(PlaceNamesAreSaidTheLocalWay)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_PLACES_PATH);
  CHECK(cases.size() >= 15);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  std::map<std::string, std::string> lexicon;
  std::vector<std::string> order;
  for (auto const & [w, ph] : Lexicon())
  {
    CHECK(!ph.empty());
    order.push_back(w);
    lexicon.emplace(w, ph);
  }
  // The places Kokoro's dictionary lacks are in ours, each with phonemes.
  std::vector<std::string> const places = {
      "buda",     "manchaca",  "pflugerville", "gruene",    "boerne",     "mexia",      "refugio",
      "bexar",    "pedernales", "guadalupe",   "lamar",     "cibolo",     "bastrop",    "lampasas",
      "kerrville", "seguin",    "hutto",       "helotes",   "uvalde",     "waxahachie", "wimberley",
      "marfa",    "natchitoches", "puyallup",  "schuylkill", "willamette", "kosciusko", "yosemite",
      "sequim",   "tualatin",  "mukilteo"};
  CHECK(places.size() >= 25);
  for (auto const & p : places)
  {
    CHECK(lexicon.count(p) == 1);
    CHECK(!lexicon[p].empty());
  }
  CHECK_EQUAL(lexicon["gruene"], std::string("ɡɹˈin"));
  CHECK_EQUAL(lexicon["bexar"], std::string("bˈɛɹ"));
  // Kyle was already a name: the place doesn't add a second entry or replace it.
  CHECK(std::count(order.begin(), order.end(), std::string("kyle")) == 1);
  CHECK(std::count(order.begin(), order.end(), std::string("buda")) == 1);
  // Every stand-in LocalPlaces() can swap in is in the dictionary.
  CHECK(LocalPlaceRespellings().size() >= 4);
  for (auto const & said : LocalPlaceRespellings())
  {
    std::string standIn = "fx";
    for (char ch : said)
      if (std::isalpha(static_cast<unsigned char>(ch)))
        standIn += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    CHECK(lexicon.count(standIn) == 1);
  }
  CHECK_EQUAL(lexicon["fxburnit"], std::string("bˈɜɹnɪt"));
  // "Burnet" anywhere else keeps the dictionary's sound; lowercase is never the place.
  CHECK_EQUAL(LocalPlaces("Burnet"), std::string("Burnet"));
  CHECK_EQUAL(LocalPlaces("onto burnet rd"), std::string("onto burnet rd"));
  CHECK_EQUAL(LocalPlaces("Burnetts Rd"), std::string("Burnetts Rd"));
  CHECK_EQUAL(LocalPlaces("in Palestine"), std::string("in Palestine"));
  CHECK_EQUAL(LocalPlaces("Burnet, Rd"), std::string("Burnet, Rd"));
  // Reading is RED-ing only where it's the place; each place takes its own state after a comma.
  CHECK(!lexicon["fxreding"].empty());
  CHECK_EQUAL(LocalPlaces("Reading, PA"), std::string("fxreding, PA"));
  CHECK_EQUAL(LocalPlaces("Reading, TX"), std::string("Reading, TX"));
  CHECK_EQUAL(LocalPlaces("Humble, PA"), std::string("Humble, PA"));
  CHECK_EQUAL(LocalPlaces("Reading glasses, please."), std::string("Reading glasses, please."));
  CHECK_EQUAL(LocalPlaces("onto reading rd"), std::string("onto reading rd"));
  // The driver's own pronunciation still wins: user words run first.
  SetUserWords("Burnet\tbur-NET\n");
  CHECK(ForSpeech("Turn right onto Burnet Rd").find("fxburnit") == std::string::npos);
  SetUserWords("");
}

TEST(StoreNamesAreInTheDictionary)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_BRANDS_PATH);
  CHECK(cases.size() >= 12);
  for (auto const & c : cases)
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  std::map<std::string, std::string> lexicon;
  std::vector<std::string> order;
  for (auto const & [w, ph] : Lexicon())
  {
    order.push_back(w);
    lexicon.emplace(w, ph);
  }
  // Every store name resolves to phonemes (a respelling the reader can't parse would drop it).
  auto const brands = BrandWords();
  CHECK(brands.size() >= 28);
  for (auto const & b : brands)
  {
    CHECK(lexicon.count(b) == 1);
    CHECK(!lexicon[b].empty());
    CHECK(std::count(order.begin(), order.end(), b) == 1);
  }
  for (std::string const w : {"kroger", "meijer", "arby", "wawa", "sheetz", "buckees", "qdoba", "whataburger"})
    CHECK(lexicon.count(w) == 1);
  CHECK_EQUAL(lexicon["sheetz"], std::string("ʃˈits"));
  // The driver's own word for a store still wins.
  SetUserWords("HEB\theb\n");
  CHECK(ForSpeech("Arrive at HEB").find("H E B") == std::string::npos);
  SetUserWords("");
  CHECK_EQUAL(Brands("Arrive at Kroger"), std::string("Arrive at Kroger"));
  CHECK_EQUAL(Brands("hebrew HEBREW Heb"), std::string("hebrew HEBREW Heb"));
}

TEST(DatesMatchTheGoldenCases)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_DATES_PATH);
  CHECK(cases.size() >= 20);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  // The pass on its own: only dates change.
  CHECK_EQUAL(Dates("Turn left onto Main St, then I-35 N."), std::string("Turn left onto Main St, then I-35 N."));
  CHECK_EQUAL(Dates("1/2 mi"), std::string("1/2 mi"));
  CHECK_EQUAL(Dates("by 3/4 mile"), std::string("by 3/4 mile"));
  CHECK_EQUAL(Dates("on 1/2"), std::string("on 1/2"));
  CHECK_EQUAL(Dates("open 24/7"), std::string("open 24/7"));
  CHECK_EQUAL(Dates("12/25"), std::string("12/25"));  // no date word: could be anything
  CHECK_EQUAL(Dates("1/2/3/4"), std::string("1/2/3/4"));
  CHECK_EQUAL(Dates("A1/2/2026"), std::string("A1/2/2026"));
  CHECK_EQUAL(Dates("11/6/2026B"), std::string("11/6/2026B"));
  CHECK_EQUAL(Dates("2026-13-01"), std::string("2026-13-01"));
  CHECK_EQUAL(Dates("800-555-1234"), std::string("800-555-1234"));
  CHECK_EQUAL(Dates("Mayday 5"), std::string("Mayday 5"));
  CHECK_EQUAL(Dates("may 5"), std::string("may 5"));
  CHECK_EQUAL(Dates("Nov 31"), std::string("Nov 31"));
  CHECK_EQUAL(Dates("Feb 29"), std::string("February twenty ninth"));
  CHECK_EQUAL(Dates("Nov 6.5"), std::string("Nov 6.5"));
  CHECK_EQUAL(Dates("by 12/31/49"), std::string("by December thirty first, twenty forty nine"));
  CHECK_EQUAL(Dates("by 12/31/50"), std::string("by December thirty first, nineteen fifty"));
}

TEST(WeekdaysAreSaidAsDays)
{
  // The pass on its own: day abbreviations only, everything else byte for byte.
  CHECK_EQUAL(Weekdays("Turn left onto Main St, then I-35 N."), std::string("Turn left onto Main St, then I-35 N."));
  CHECK_EQUAL(Weekdays("See you Thu"), std::string("See you Thursday"));
  CHECK_EQUAL(Weekdays("Mon\xE2\x80\x93" "Fri"), std::string("Monday to Friday"));
  CHECK_EQUAL(Weekdays("Mon, Wed, Fri"), std::string("Monday, Wednesday and Friday"));
  CHECK_EQUAL(Weekdays("Fri or Sat"), std::string("Friday or Saturday"));
  CHECK_EQUAL(Weekdays("Fri. 11/7"), std::string("Friday 11/7"));
  // Longer words, other case and glued letters stay.
  CHECK_EQUAL(Weekdays("Monday Friar Wedge Sunday's Saturn TUE tue fri ASun Sun\xE2\x80\x99s"),
              std::string("Monday Friar Wedge Sunday's Saturn TUE tue fri ASun Sun\xE2\x80\x99s"));
  // A lone "Sat" or "Sun" is a word unless a date follows.
  CHECK_EQUAL(Weekdays("Sun and rain. Sat down. Sun. The glare. Sun, then"),
              std::string("Sun and rain. Sat down. Sun. The glare. Sun, then"));
  CHECK_EQUAL(Weekdays("Sun 9"), std::string("Sunday 9"));
  CHECK_EQUAL(Weekdays("Sun, November ninth"), std::string("Sunday, November ninth"));
  // Month and year, inside Dates.
  CHECK_EQUAL(Dates("Oct 2026"), std::string("October 2026"));
  CHECK_EQUAL(Dates("Oct 20261 Mar 2026:30 May 2026 oct 2026"), std::string("Oct 20261 Mar 2026:30 May 2026 oct 2026"));
}

TEST(GroupedNumbersAreQuantitiesWhateverTheirLength)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_NUMBERS_PATH);
  CHECK(cases.size() >= 10);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
}

TEST(DecimalMilesReadTheWayDriversSayThem)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_DISTANCES_PATH);
  CHECK(cases.size() >= 12);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  // The pass on its own: only decimal miles change.
  CHECK_EQUAL(DecimalMiles("Turn left onto Main St in 0.5 mi."), std::string("Turn left onto Main St in a half mile."));
  CHECK_EQUAL(DecimalMiles("In 0.3 km, 4.5 m, 110.5 mi"), std::string("In 0.3 km, 4.5 m, 110.5 mi"));
  CHECK_EQUAL(DecimalMiles("Version 2.5 minutes"), std::string("Version 2.5 minutes"));
  // After a determiner the half or quarter takes no article; only a whole word counts, and it stays put.
  CHECK_EQUAL(DecimalMiles("the 0.5 mi road"), std::string("the half mile road"));
  CHECK_EQUAL(DecimalMiles("A .25 mi gap"), std::string("A quarter mile gap"));
  CHECK_EQUAL(DecimalMiles("breathe 0.5 mi"), std::string("breathe a half mile"));
  CHECK_EQUAL(DecimalMiles("the  0.5 mi"), std::string("the  a half mile"));
  CHECK_EQUAL(DecimalMiles("0.5 mi"), std::string("a half mile"));
  for (char const * text : {"the 0.5 mi road", "every 0.75 mi", "Bathe 0.25 mi"})
    CHECK_EQUAL(DecimalMiles(DecimalMiles(text)), DecimalMiles(text));
}

TEST(SymbolsAndLeftoverAbbreviationsMatchTheGoldenCases)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_SYMBOLS_PATH);
  CHECK(cases.size() >= 20);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  // The pass on its own: only symbols change, everything else byte for byte.
  CHECK_EQUAL(Symbols("Turn left onto Main St, then I-35 N."), std::string("Turn left onto Main St, then I-35 N."));
  CHECK_EQUAL(Symbols("\xC2\xBD mile"), std::string("1/2 mile"));
  CHECK_EQUAL(Symbols("2\xC2\xBE"), std::string("2 and 3/4"));
  CHECK_EQUAL(Symbols("#blessed #5"), std::string("#blessed number 5"));
  CHECK_EQUAL(Symbols("$2.99/gal"), std::string("$2.99 per gallon"));
  CHECK_EQUAL(Symbols("24/7 and/or w/o"), std::string("24/7 and/or w/o"));
  CHECK_EQUAL(Symbols("hours/day"), std::string("hours/day"));
  // "Ct" or "Ln" alone, or inside a word, is not a street type.
  CHECK_EQUAL(ForSpeech("Ct"), std::string("Ct"));
  CHECK_EQUAL(ForSpeech("Kiln"), std::string("Kiln"));
  // "min" and "sec" stay words when no number comes first.
  CHECK_EQUAL(ForSpeech("Hold on a sec"), std::string("Hold on a sec"));
  // Decades and fractions with a unit, each pass on its own.
  CHECK_EQUAL(Decades("the 90s, the '80s, the 70's"), std::string("the nineties, the eighties, the seventies"));
  CHECK_EQUAL(Decades("$90s A90s 10s 90sec 1995s x'90s"), std::string("$90s A90s 10s 90sec 1995s x'90s"));
  CHECK_EQUAL(Decades("Exit 90 is 30 miles"), std::string("Exit 90 is 30 miles"));
  CHECK_EQUAL(FractionsWithUnits("1/2 mi"), std::string("a half mile"));
  CHECK_EQUAL(FractionsWithUnits("3/4 hr"), std::string("three quarters of an hour"));
  CHECK_EQUAL(FractionsWithUnits("1 1/2 mi"), std::string("1 and a half miles"));
  CHECK_EQUAL(FractionsWithUnits("1 and 1/2 miles"), std::string("1 and a half miles"));
  // Dates, other fractions, bare fractions and glued ones are left for later passes.
  CHECK_EQUAL(FractionsWithUnits("1/2/2026 5/8 mi 1/2 of it 1.1/2 mi -1/2 mi"),
              std::string("1/2/2026 5/8 mi 1/2 of it 1.1/2 mi -1/2 mi"));
}

TEST(WebAddressesMatchTheGoldenCasesSharedWithBookLyre)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_WEB_PATH);
  CHECK(cases.size() >= 40);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(WebAddresses(c.input), c.expected);
  }
  // Through the whole pipeline: the address comes out as words and nothing later takes it apart.
  CHECK_EQUAL(ForSpeech("Visit www.heb.com."), std::string("Visit heb dot com."));
  CHECK_EQUAL(ForSpeech("Mail jo@example.com today."), std::string("Mail jo at example dot com today."));
  CHECK_EQUAL(ForSpeech("See https://www.gutenberg.org/ebooks/1342 for details."),
              std::string("See gutenberg dot org for details."));
  // Lines with no address go through untouched by this pass, whatever else they hold.
  for (char const * line : {"Turn left onto Main St.", "Exit 238B toward U.S. 183.", "e.g. 3.14 and v1.2.3",
                            "Meet at 5 @ noon.", "St.Louis", "{street}. Then {instruction}.", ""})
    CHECK_EQUAL(WebAddresses(line), std::string(line));
  // Long runs of dots and letters are read in one linear scan (no regex recursion).
  std::string big;
  for (int i = 0; i < 4000; ++i)
    big += "ab.";
  big += "com";
  auto const said = WebAddresses(big);
  CHECK(said.size() > big.size());
  CHECK_EQUAL(said.substr(said.size() - 7), std::string("dot com"));
}

// BI-063: a street abbreviation's period mid-sentence isn't a full stop, or Kokoro stops dead
// halfway through "Oak Drive. and then left".
TEST(StreetPeriodMidSentenceIsNotAFullStop)
{
  CHECK_EQUAL(ForSpeech("Oak Dr. and then left"), std::string("Oak Drive and then left"));
  CHECK_EQUAL(ForSpeech("Main St., then right"), std::string("Main Street, then right"));
  CHECK_EQUAL(ForSpeech("Turn onto Pine Ln.; it's narrow"), std::string("Turn onto Pine Lane; it's narrow"));
  CHECK_EQUAL(ForSpeech("Take Elm Ave.: the slow one"), std::string("Take Elm Avenue: the slow one"));
  CHECK_EQUAL(ForSpeech("Turn on Ave. B now"), std::string("Turn on Avenue B now"));
  CHECK_EQUAL(ForSpeech("Stay on Oak Ln. 7 more miles"), std::string("Stay on Oak Lane seven more miles"));
  CHECK_EQUAL(ForSpeech("Main St. NW is closed"), std::string("Main Street Northwest is closed"));
  CHECK_EQUAL(ForSpeech("Take Burnet Rd. or don't"), ForSpeech("Take Burnet Rd or don't"));
  // The period stays where the sentence may end.
  CHECK_EQUAL(ForSpeech("Elm Ave. Then right."), std::string("Elm Avenue. Then right."));
  CHECK_EQUAL(ForSpeech("Turn onto Oak Rd."), std::string("Turn onto Oak Road."));
  CHECK_EQUAL(ForSpeech("Oak Rd. I hate this road."), std::string("Oak Road. I hate this road."));
  CHECK_EQUAL(ForSpeech("Oak Rd. A truck. Wonderful."), std::string("Oak Road. A truck. Wonderful."));
  CHECK_EQUAL(ForSpeech("Main St.) Fine."), std::string("Main Street.) Fine."));
  // Titles keep working: the period after Saint/Doctor is theirs.
  CHECK_EQUAL(ForSpeech("Take St. Johns Rd. and keep left"), std::string("Take Saint Johns Road and keep left"));
  CHECK_EQUAL(ForSpeech("Call Dr. Smith"), std::string("Call Doctor Smith"));
  CHECK_EQUAL(ForSpeech("Mt. Bonnell Rd., then left"), std::string("Mount Bonnell Road, then left"));
}

TEST(StreetTypesBeforeASeparatorAreSaid)
{
  // Loop B105 (BI-091): "Main St / Elm St" is "Main Street, Elm Street"; "24/7" and "and/or" are left.
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_STREET_SEPARATORS_PATH);
  CHECK(cases.size() >= 20);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
}

TEST(PlaceNameAbbreviationsAreSaidInFull)
{
  // Loop B108 (BI-094): "Intl", "Med. Ctr.", "Univ. of", "Inc." inside place names are said in full; lowercase
  // words, county roads and a "Co" with a name after it are left. A second pass changes nothing.
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_PLACE_NAMES_PATH);
  CHECK(cases.size() >= 20);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
    CHECK_EQUAL(ForSpeech(c.expected), c.expected);
  }
}

TEST(LetteredCountyRoadsAreSaidAsLetters)
{
  // Loop B106 (BI-092): "Co Rd K" is "County Road K", "Hwy KK" "Highway K K", grid house numbers keep
  // their letter. The road letter is never read as a compass point, and a second pass changes nothing.
  CHECK_EQUAL(ForSpeech("Turn onto County Rd N, then left"), std::string("Turn onto County Road N, then left"));
  CHECK_EQUAL(ForSpeech("Turn onto Co Rd K"), std::string("Turn onto County Road K"));
  CHECK_EQUAL(ForSpeech("Arrive at N1234 County Rd K"), std::string("Arrive at N twelve thirty four County Road K"));
  // Not a county road: the company, the Honda, a direction after a bare highway.
  // (Loop B108 says the company's "Co." in full.)
  CHECK_EQUAL(ForSpeech("Smith & Sons Co."), std::string("Smith and Sons Company."));
  CHECK_EQUAL(ForSpeech("Park the CR-V"), std::string("Park the CR-V"));
  CHECK_EQUAL(ForSpeech("Take Hwy N"), std::string("Take Highway North"));
  for (std::string const once : {"Take County Road K K west", "Turn onto Township Road twelve", "Take County Highway P D."})
    CHECK_EQUAL(ForSpeech(once), once);
}

TEST(CompactDurationsZeroDecimalsAndSplitExitsAreSaid)
{
  // Loop B109 (BI-095): "2h 15m" is hours and minutes, "+12 min" more minutes, "2.0 mi" two miles,
  // "Exit 12B-C" twelve B and C. A second pass changes nothing.
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_DURATIONS_PATH);
  CHECK(cases.size() >= 20);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
    CHECK_EQUAL(ForSpeech(c.expected), c.expected);
  }
  // The pass on its own: only compact units change, digits stay for the number pass.
  CHECK_EQUAL(CompactUnits("2h 15m, +3 mins, 2.0 mi"), std::string("2 hours 15 minutes, 3 more minutes, 2 mi"));
  CHECK_EQUAL(CompactUnits("45m, v2.0 mi, 1.2.0 km, a+12 min, version 2.0"),
              std::string("45m, v2.0 mi, 1.2.0 km, a+12 min, version 2.0"));
}

TEST(ForSpeechPassOrderSurvivesTheSplit)
{
  // Loop B111 split ForSpeech's inline passes into DegreesAndDashes, StreetsAndCompass and NumberWords.
  // One line crossing all three must read as before: degrees and the dash first, the street type and
  // compass after the name, then the numbers.
  CHECK_EQUAL(ForSpeech("It's -5°F — turn onto Main St NW in 0.5 mi, then $3.50 at 2:30"),
              std::string("It's minus five degrees Fahrenheit, turn onto Main Street Northwest in a half mile, then "
                          "three dollars and fifty cents at two thirty"));
  CHECK_EQUAL(ForSpeech("Ave B & Oak Dr. and then left"), std::string("Avenue B and Oak Drive and then left"));
}

TEST(CentsSignIsSaidAsCents)
{
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_CURRENCY_PATH);
  CHECK(cases.size() >= 8);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  // Glued to something, or no amount: the sign is left where it is.
  for (char const * s : {"x50¢", "50¢¢", "50¢s", "1,50¢", "¢"})
    CHECK(ForSpeech(s).find("cent") == std::string::npos);
  CHECK(ForSpeech("x50¢").find("¢") != std::string::npos);
  CHECK(ForSpeech("50¢¢").find("cents") == std::string::npos);
  // Only "a"/"an" right before makes it singular; "Cuban 5¢ cigar" is still plural.
  CHECK_EQUAL(ForSpeech("Cuban 5¢ cigar"), std::string("Cuban five cents cigar"));
  CHECK_EQUAL(ForSpeech("a 5¢."), std::string("a five cents."));
  // Hostile input stays quick and doesn't throw.
  std::string longText;
  for (int i = 0; i < 2000; ++i)
    longText += "1¢";
  CHECK(!ForSpeech(longText).empty());
}

TEST(MilitaryAndTwentyFourHourTimes)
{
  // Loop B139, CROSS X-051: BookLyre's military-time cases plus 24-hour colon times.
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_MILITARY_TIMES_PATH);
  CHECK(cases.size() >= 29);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  // A duration stays a count; "1430 h" with a space is not the glued "h".
  CHECK(ForSpeech("logged 1430 hours of driving").find("fourteen thirty") == std::string::npos);
  CHECK(ForSpeech("at 1430 h").find("hours") == std::string::npos);
  // Hostile input stays quick and doesn't throw.
  std::string longText;
  for (int i = 0; i < 2000; ++i)
    longText += "at 0600h 23:59 ";
  CHECK(!ForSpeech(longText).empty());
}

TEST(DottedTitlesBeforeNames)
{
  // Loop B140, CROSS X-052: BookLyre's dotted-title cases plus street names with a rank in them.
  auto const cases = GoldenCases(BACKSEAT_SPOKEN_DOTTED_TITLES_PATH);
  CHECK(cases.size() >= 45);
  for (auto const & c : cases)
  {
    CHECK(!c.input.empty());
    CHECK_EQUAL(ForSpeech(c.input), c.expected);
  }
  // Every title reads in full before a name, and stays as written before a lowercase word.
  for (char const * t : {"Capt", "Lt", "Col", "Sgt", "Gen", "Maj", "Cpl", "Adm", "Cmdr", "Prof", "Rev", "Insp",
                         "Supt", "Gov", "Sen", "Pres", "Msgr", "Fr"})
  {
    std::string const title(t);
    CHECK(ForSpeech("Meet " + title + ". Morgan Rd").find(title + ".") == std::string::npos);
    CHECK(ForSpeech("Meet " + title + ". morgan").find(title + ".") != std::string::npos);
  }
  // Hostile input stays quick and doesn't throw.
  std::string longText;
  for (int i = 0; i < 2000; ++i)
    longText += "Lt. Col. Capt. ";
  CHECK(!ForSpeech(longText).empty());
}
