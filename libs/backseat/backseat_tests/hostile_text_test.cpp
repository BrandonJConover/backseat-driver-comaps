// Hostile-text soak: every entry point that reads text a person (or a dictation engine, a map
// street name, an imported file) hands us gets garbage, truncated UTF-8, huge numbers and very
// long input. Nothing may throw, hang, or blow the output up. Loop fallback:security.
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"
#include "backseat/onboarding.hpp"
#include "backseat/plate_game.hpp"
#include "backseat/pop_quiz.hpp"
#include "backseat/pronunciation.hpp"
#include "backseat/pronunciation_backup.hpp"
#include "backseat/twenty_questions.hpp"
#include "backseat/voice_command.hpp"

#include <chrono>
#include <climits>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
using namespace backseat;
using namespace backseat::pronunciation;

struct FuzzRng
{
  uint64_t s;
  uint32_t Next()
  {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return static_cast<uint32_t>(s >> 11);
  }
  size_t Below(size_t n) { return n == 0 ? 0 : Next() % n; }
};

// Pieces that poke the rules: number/route/clock/year cues, street titles, curly quotes,
// Cyrillic, kana, stray UTF-8 lead and continuation bytes, braces, tabs, slashes.
std::vector<std::string> const kPieces = {
  " ", "  ", "-", "–", "—", ".", ",", "'", "’", "‘", "ʼ", "\"", "{", "}", "{speed}", "\t", "\n", "/",
  "I-", "I-405", "US-", "FM ", "Exit ", "238B", "St.", "St ", "Dr ", "Jr", "Hwy ", "Route ", "TX-",
  "1600", "99999999999999999999999", "0", "00", "12", "5pm", "5:30", ":", " PM", "a.m.", "in ", "1868",
  "the 1990s", "3-5 miles", "between 3 and ", "°F", "°", "~", "±", "%", "$", "1,234", ",,,",
  "King ", "Henry ", "VIII", "II", "XXXIX", "Pope ", "Calle ", "Rue ", "Straße", "Hauptstraße",
  "улица ", "Ленина", "ё", "ъ", "ь", "ありがとう", "ッ", "ー", "ゃ", "ャ", "ん", "中文", "한국어",
  "bow", "lead", "minute", "live", "read", "don't ", "do not ", "go to ", "take me ", "home",
  "shut up", "mute", "cancel", "what's the next turn", "a", "b", "option ", "yes", "no", "Texas",
  "\xC3", "\xE2\x80", "\xF0\x9F", "\x80", "\xBF", "\xFF", "\xFE", "\xED\xA0\x80", "\xF4\x90\x80\x80",
  "😀", "\\u", "\\uD83D", "\\", "[", "]", ":", "{\"", "\"say\"", "\t/ə/", "CAPS",
  // Dates, days, hours, fractions, rates, decades (passes added since the first soak).
  "1/2/2026", "31/12/1999", "2026-11-06", "11/6/26", "2/30", "Nov. ", "Sept 11, ", "Oct 2026", "Mon-Fri",
  "Tues-Thurs", "Mon/Wed/Fri", "Sat & Sun", "Sun ", "Sat, ", "Fri.", "9-5", "Open ", "daily", "½", "¼",
  "1½", "⅔", "1/3", "3/4 mi", "1 1/2 ", "#5", "#", "$3.49/gal", "mi/hr", "km/h", "hrs", "mins", "24/7",
  "the 90s", "'80s", "mid-70s", "10s",
};

std::string RandomText(FuzzRng & r, size_t pieces)
{
  std::string s;
  for (size_t i = 0; i < pieces; ++i)
  {
    if (r.Below(8) == 0)
      s.push_back(static_cast<char>(r.Below(256)));
    else
      s += kPieces[r.Below(kPieces.size())];
  }
  return s;
}

std::string RandomJson(FuzzRng & r)
{
  static std::vector<std::string> const kJson = {
    "{", "}", "[", "]", ",", ":", "\"word\"", "\"pronunciation\"", "\"isPattern\"", "true", "false",
    "null", "\"entries\"", "\"x\"", "\"\\u00e9\"", "\"\\uD83D\\uDE00\"", "\"\\uD83D\"", "\"\\uDE00x\"",
    "\"\\\"", "1e999", "-", "0.", " ", "\"\\q\"", "\"unterminated", "\xFF",
  };
  std::string s;
  size_t n = 1 + r.Below(40);
  for (size_t i = 0; i < n; ++i)
    s += kJson[r.Below(kJson.size())];
  return s;
}

// Run every text entry point on one input; returns the longest output seen.
size_t Feed(std::string const & text)
{
  size_t longest = 0;
  auto keep = [&](std::string const & out) { longest = std::max(longest, out.size()); };
  keep(ForSpeech(text));
  keep(WebAddresses(text));
  keep(Symbols(text));
  keep(Decades(text));
  keep(FractionsWithUnits(text));
  keep(Dates(text));
  keep(Weekdays(text));
  keep(LocalPlaces(text));
  keep(PickSenses(text));
  keep(Ranges(text));
  keep(Foreign(text));
  keep(ForeignStreets(text));
  keep(Regnal(text));
  keep(Cyrillic(text));
  keep(Kana(text));
  keep(Roads(text));
  keep(HouseNumbers(text));
  keep(Clock(text));
  keep(Years(text));
  keep(StraightApostrophes(text));
  keep(SpeakableText(text));
  keep(MetricWording(text));
  keep(Fill(text, {{"speed", text}, {"limit", "35"}}));
  (void)RespellingPhonemes(text);
  (void)ParseAppCommands(text);
  (void)ParseDestinationCommand(text);
  (void)SaidPolitely(text);
  (void)SaidYes(text);
  (void)SaidNo(text);
  (void)IsFiller(text);
  (void)ParseNameAnswer(text);
  (void)ParseFavorite(text);
  (void)ParseRating(text);
  (void)ParsePronounAnswer(text);
  (void)PronounCorrection(text);
  (void)ParsePassengerAnswer(text);
  (void)ParseBusinessOwner(text);
  (void)IsEchoOf(text, "Turn left onto Main Street.");
  (void)MatchPlateRegion(text);
  (void)ParseYesNo(text);
  (void)MatchTwentyThing(text);
  (void)ParseTwentyHeard(text);
  (void)AsksForTwentyQuestions(text);
  (void)TypedPhraseMatches(text, "I agree");
  (void)DriveComesFirst(text);
  (void)DriveComesFirst(text, true);
  (void)AsksForPopQuiz(text);
  for (auto const & q : QuizQuestions())
  {
    (void)ParseQuizAnswer(q, text);
    (void)QuizAnswerTakes(q, text);
  }
  return longest;
}
}  // namespace

using namespace backseat;
using namespace backseat::pronunciation;

TEST(HostileTextNeverThrowsOrBlowsUp)
{
  FuzzRng r{0x9E3779B97F4A7C15ULL};
  auto const start = std::chrono::steady_clock::now();
  for (int i = 0; i < 1500; ++i)
  {
    std::string const text = RandomText(r, 1 + r.Below(24));
    size_t longest = 0;
    try
    {
      longest = Feed(text);
    }
    catch (...)
    {
      CHECK(!"a text entry point threw on hostile input");
    }
    // Spelling numbers and respelling words grows text, but never by more than a small factor.
    CHECK(longest <= 40 * text.size() + 400);
  }
  auto const secs =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  CHECK(secs < 60.0);
}

TEST(HostileTextVeryLongInputFinishes)
{
  // A street name or dictation result the size of a page: must finish quickly (no quadratic blowup
  // on the voice thread) and not throw.
  FuzzRng r{42};
  std::string text;
  while (text.size() < 20000)
    text += RandomText(r, 8) + " ";
  auto const start = std::chrono::steady_clock::now();
  (void)Feed(text);
  auto const secs =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  CHECK(secs < 20.0);
}

TEST(HostileNumbersSpellWithoutOverflow)
{
  for (long long n : {LLONG_MIN, LLONG_MIN + 1, -1LL, 0LL, LLONG_MAX, 1000000000000000000LL})
    CHECK(!SpellNumber(n).empty());
  for (std::string d : {"", "0", "000", "99999999999999999999999", "18446744073709551616", "-1"})
    (void)RouteNumber(d);
  CHECK(!ForSpeech("Exit 99999999999999999999999999 in 99999999999999999999 miles").empty());
}

TEST(HostileDigitRunsDoNotOverflow)
{
  // ToNumber used to overflow (undefined behaviour, found by UBSan) on a digit run past 19 digits;
  // it now stops at LLONG_MAX, so these are never read as opening hours, dates or years.
  for (char const * text : {"Open 99999999999999999999-5", "Mon-Fri 9-99999999999999999999",
                            "on 99999999999999999999/12/2026", "the 99999999999999999999s",
                            "at 99999999999999999999:30", "$99999999999999999999.99"})
  {
    std::string const said = ForSpeech(text);
    CHECK(said.find("nine to five") == std::string::npos);
    CHECK(said.find(" to five") == std::string::npos);
    CHECK(said.size() < 2000);
  }
}

TEST(HostilePronunciationFilesAreRefused)
{
  FuzzRng r{7};
  for (int i = 0; i < 1500; ++i)
  {
    std::string const json = RandomJson(r);
    std::string lines;
    try
    {
      lines = FromBackupJson(json);
      (void)ToBackupJson(lines, json, 0);
    }
    catch (...)
    {
      CHECK(!"FromBackupJson/ToBackupJson threw on hostile input");
    }
  }
  // Deep nesting stays within the depth cap instead of overflowing the stack.
  CHECK_EQUAL(FromBackupJson(std::string(100000, '[')), std::string());
  CHECK_EQUAL(FromBackupJson(std::string(100000, '{')), std::string());
}

TEST(HostileUserWordsAreSafe)
{
  FuzzRng r{11};
  for (int i = 0; i < 300; ++i)
  {
    std::string lines;
    for (int j = 0; j < 5; ++j)
      lines += RandomText(r, 3) + "\t" + RandomText(r, 3) + "\n";
    SetUserWords(lines);
    (void)ForSpeech(RandomText(r, 12));
  }
  SetUserWords("");
  CHECK_EQUAL(ForSpeech("Turn left onto Main St."), ForSpeech("Turn left onto Main St."));
}
