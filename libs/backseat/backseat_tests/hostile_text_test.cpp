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

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
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
  // Address tails, more street types, travel directions, decimal miles, grouped numbers,
  // Reading and saints' streets (passes added since the games soak, loop B72-B84).
  "Ste ", "Ste 200", "Apt ", "Apt. 4B", "Bldg ", "Fl ", "Rm ", "Spc ", "#200", "Unit ", "Blvd ",
  "Hts", "Plz ", "Pt. ", "Pt ", "Tpke", "Crk ", "Sta", "Vly", "Jct ", "NB", "SB", "EB", "WB",
  " NB / ", "US-290 W", " / TX-71 W", "IH 35 SB", "0.3 mi", "0.25 mi", ".5 mi", "10.0 mi", "0. mi",
  "1.", "1,000,000", "999,999,999,999,999,999", "1,2,3", ",000", "Reading ", "Reading Rd", ", PA",
  "St. Louis", "St. Paul St", "Elm St. ", "Main St.,", "Oak Dr. and",
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

// Which quiz questions one Feed call checks the answer parsers against. Running all ~100 for
// every fuzz input made this the slowest test in the suite (5 s), so the fuzz loop rotates two
// per input and counts that every question still gets plenty of inputs. Null means all of them.
struct QuizPick
{
  size_t a;
  size_t b;
  std::vector<int> * hits;
};

// Run every text entry point on one input; returns the longest output seen.
size_t Feed(std::string const & text, QuizPick const * pick = nullptr)
{
  size_t longest = 0;
  auto keep = [&](std::string const & out) { longest = std::max(longest, out.size()); };
  keep(ForSpeech(text));
  keep(WebAddresses(text));
  keep(Symbols(text));
  keep(Decades(text));
  keep(DecimalMiles(text));
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
  auto const & questions = QuizQuestions();
  auto quiz = [&](size_t i)
  {
    (void)ParseQuizAnswer(questions[i], text);
    (void)QuizAnswerTakes(questions[i], text);
    if (pick)
      ++(*pick->hits)[i];
  };
  if (pick)
  {
    quiz(pick->a);
    quiz(pick->b);
  }
  else
  {
    for (size_t i = 0; i < questions.size(); ++i)
      quiz(i);
  }
  return longest;
}
}  // namespace

using namespace backseat;
using namespace backseat::pronunciation;

TEST(HostileTextNeverThrowsOrBlowsUp)
{
  FuzzRng r{0x9E3779B97F4A7C15ULL};
  size_t const n = QuizQuestions().size();
  std::vector<int> hits(n, 0);
  auto const start = std::chrono::steady_clock::now();
  for (int i = 0; i < 1500; ++i)
  {
    std::string const text = RandomText(r, 1 + r.Below(24));
    size_t longest = 0;
    size_t const u = static_cast<size_t>(i);
    QuizPick const pick{u % n, (u * 7 + 3) % n, &hits};
    try
    {
      longest = Feed(text, &pick);
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
  // Every quiz question's answer parsers still saw a fair share of the garbage.
  int fewest = hits.empty() ? 0 : hits[0];
  for (int h : hits)
    fewest = std::min(fewest, h);
  if (fewest < 30)
    std::printf("  quiz question with fewest fuzz inputs got %d (of %zu questions)\n", fewest, n);
  CHECK(fewest >= 30);
}

namespace
{
// True when the bytes are well-formed UTF-8 (no stray continuation, no truncated sequence,
// no overlong form, no surrogate). Kokoro's phonemizer chokes on broken bytes mid-line.
bool ValidUtf8(std::string const & s)
{
  size_t i = 0;
  while (i < s.size())
  {
    auto const c = static_cast<unsigned char>(s[i]);
    size_t n = 0;
    uint32_t cp = 0;
    if (c < 0x80)
    {
      ++i;
      continue;
    }
    if (c >= 0xC2 && c <= 0xDF)
    {
      n = 1;
      cp = c & 0x1F;
    }
    else if (c >= 0xE0 && c <= 0xEF)
    {
      n = 2;
      cp = c & 0x0F;
    }
    else if (c >= 0xF0 && c <= 0xF4)
    {
      n = 3;
      cp = c & 0x07;
    }
    else
      return false;
    if (i + n >= s.size())
      return false;
    for (size_t k = 1; k <= n; ++k)
    {
      auto const cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80)
        return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if ((n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (n == 3 && (cp < 0x10000 || cp > 0x10FFFF)))
      return false;
    i += n + 1;
  }
  return true;
}

// Random text from the pieces that are themselves valid UTF-8 (no raw bytes).
std::string RandomValidText(FuzzRng & r, size_t pieces)
{
  std::string s;
  while (pieces > 0)
  {
    std::string const & piece = kPieces[r.Below(kPieces.size())];
    if (!ValidUtf8(piece))
      continue;
    s += piece;
    --pieces;
  }
  return s;
}
}  // namespace

// A pass that cuts a multi-byte letter in half (byte regexes, substr at a byte offset) hands the
// voice broken text. Clean input must come out clean from ForSpeech and from every pass.
TEST(HostileValidUtf8StaysValid)
{
  CHECK(ValidUtf8("Straße ありがとう 😀"));
  CHECK(!ValidUtf8("\xC3"));
  CHECK(!ValidUtf8(std::string("a\xC3")));
  CHECK(!ValidUtf8(std::string("\xED\xA0\x80")));
  FuzzRng r{0xD1B54A32D192ED03ULL};
  int broken = 0;
  for (int i = 0; i < 1500; ++i)
  {
    std::string const text = RandomValidText(r, 1 + r.Below(16));
    for (std::string const & out :
         {ForSpeech(text), WebAddresses(text), Symbols(text), Decades(text), DecimalMiles(text),
          FractionsWithUnits(text), Dates(text), Weekdays(text), LocalPlaces(text), PickSenses(text),
          Ranges(text), Foreign(text), ForeignStreets(text), Regnal(text), Cyrillic(text), Kana(text),
          Roads(text), HouseNumbers(text), Clock(text), Years(text), StraightApostrophes(text),
          SpeakableText(text), MetricWording(text)})
    {
      if (!ValidUtf8(out) && ++broken <= 5)
        std::printf("  broken UTF-8 from: %s\n", text.c_str());
    }
  }
  CHECK_EQUAL(broken, 0);
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
