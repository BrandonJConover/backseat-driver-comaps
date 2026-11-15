#pragma once

// Internal to libs/backseat: ratios, odds and map scales read "to", not as clock times (Ratios), used by
// ForSpeech() in pronunciation.cpp. Included only by pronunciation.cpp (internal linkage).
// Not public API; hosts include pronunciation.hpp.
//
// Without it "a 1:50 scale map" was read as a time ("one fifty"), "odds 3:1" as "three:one" and
// "1:25,000" as "one twenty five,zero". The same rule as BookLyre (text/spoken_ratios.py, CROSS X-063),
// golden cases shared in backseat_tests/spoken_ratios.json. A colon between two numbers becomes " to "
// when:
//   - a ratio word is one of the three words before it in the same clause ("the scale was 1:50",
//     "odds of 3:1", "outnumbered them 10:1"), or
//   - a ratio noun follows it ("a 1:50 scale map", "a 3:2 majority", "1:25,000 map"), or
//   - the right side is 1 ("they won 2:1"), which is never a clock time.
// Left as written: a capitalised word right before it (verses "John 3:16", "Heb. 11:1"; "A 1:50 scale"
// still counts), am/pm after it ("1:50 p.m."), a time word right before it ("at 1:50"), hh:mm:ss and
// 1:2:3 chains, decimals, a left side over three digits. The output has no colon, so it is idempotent.

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace backseat::pronunciation
{
namespace
{
namespace ratios
{
constexpr std::string_view kCuesBefore[] = {
    "ratio",  "ratios", "odds",        "scale",      "scales",      "margin",  "against",
    "outnumbered", "proportion", "proportions", "mix", "mixed", "mixture", "dilute",
    "diluted", "aspect", "bet",         "betting",    "majority",
};
constexpr std::string_view kCuesAfter[] = {"scale", "ratio", "odds", "margin", "majority",
                                           "split", "mix",   "mixture", "aspect", "map"};
constexpr std::string_view kTimeWords[] = {
    "at",     "by",     "until", "till",          "til",     "from", "since", "before", "after",
    "around", "about",  "past",  "approximately", "roughly", "o",    "noon",  "midnight",
};
// Sentence-initial capitals that are not a book name ("A 1:50 scale model.").
constexpr std::string_view kDeterminers[] = {"a",     "an",  "the",   "this", "that", "these", "those",
                                             "his",   "her", "its",   "their", "our", "my",    "your",
                                             "each",  "every", "one", "some", "any"};

template <size_t N>
bool In(std::string_view const (&set)[N], std::string const & w)
{
  for (auto const & s : set)
    if (s == w)
      return true;
  return false;
}

bool Dig(char c) { return c >= '0' && c <= '9'; }

// Bytes of general punctuation (U+2000..U+206F: dashes, curly quotes) and Latin-1 marks (U+0080..U+00BF,
// NBSP) are not letters; any other non-ASCII byte is taken as part of a letter.
size_t PunctLen(std::string const & s, size_t i)
{
  auto const b = static_cast<unsigned char>(s[i]);
  if (b == 0xE2 && i + 2 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xFE) == 0x80)
    return 3;
  if (b == 0xC2 && i + 1 < s.size())
    return 2;
  return 0;
}

bool LetterAt(std::string const & s, size_t i)
{
  auto const b = static_cast<unsigned char>(s[i]);
  if (b < 0x80)
    return std::isalpha(b) != 0;
  // A continuation byte belongs to the character it continues; check that character's lead byte.
  size_t lead = i;
  while (lead > 0 && (static_cast<unsigned char>(s[lead]) & 0xC0) == 0x80)
    --lead;
  return PunctLen(s, lead) == 0;
}

bool WordAt(std::string const & s, size_t i)
{
  return Dig(s[i]) || s[i] == '_' || LetterAt(s, i);
}

std::string LowerAscii(std::string_view w)
{
  std::string out(w);
  for (auto & c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool ClauseMark(std::string const & s, size_t i)
{
  static constexpr std::string_view kMarks = ".,;!?()[]\"";
  if (kMarks.find(s[i]) != std::string_view::npos)
    return true;
  // Curly double quotes U+201C/U+201D: E2 80 9C / E2 80 9D (i is any byte of them).
  for (size_t k = (i >= 2 ? i - 2 : 0); k <= i; ++k)
    if (k + 2 < s.size() && static_cast<unsigned char>(s[k]) == 0xE2 &&
        static_cast<unsigned char>(s[k + 1]) == 0x80 &&
        (static_cast<unsigned char>(s[k + 2]) == 0x9C || static_cast<unsigned char>(s[k + 2]) == 0x9D))
      return true;
  return false;
}

// "1:50 p.m.", "5:30 pm", "1:50PM" (and not "1:50 pmx").
bool MeridiemAfter(std::string const & s, size_t end)
{
  size_t i = end;
  if (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
    ++i;
  if (i >= s.size() || (std::tolower(static_cast<unsigned char>(s[i])) != 'a' &&
                        std::tolower(static_cast<unsigned char>(s[i])) != 'p'))
    return false;
  ++i;
  if (i < s.size() && s[i] == '.')
    ++i;
  if (i >= s.size() || std::tolower(static_cast<unsigned char>(s[i])) != 'm')
    return false;
  ++i;
  return i >= s.size() || !LetterAt(s, i);
}

// "Heb. 11:1": a capitalised word and a period right before the number.
bool AbbreviationBefore(std::string const & s, size_t start)
{
  size_t const floor = start >= 24 ? start - 24 : 0;
  size_t i = start;
  while (i > floor && std::isspace(static_cast<unsigned char>(s[i - 1])))
    --i;
  if (i == start || i == floor || s[i - 1] != '.')
    return false;
  size_t const wordEnd = --i;
  while (i > floor && LetterAt(s, i - 1))
    --i;
  return i < wordEnd && std::isupper(static_cast<unsigned char>(s[i]));
}

// The word after "1:50" past one to three spaces or dashes ("-scale", " map"), lowercased.
std::string NextWord(std::string const & s, size_t end)
{
  size_t i = end;
  int seps = 0;
  while (i < s.size() && seps < 3)
  {
    if (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == '-')
      i += 1;
    else if (s.compare(i, 3, "\xE2\x80\x90") == 0 || s.compare(i, 3, "\xE2\x80\x93") == 0)
      i += 3;  // hyphen U+2010, en dash U+2013
    else
      break;
    ++seps;
  }
  if (seps == 0)
    return {};
  size_t const b = i;
  while (i < s.size() && LetterAt(s, i))
    ++i;
  return LowerAscii(std::string_view(s).substr(b, i - b));
}

struct Word
{
  size_t b, e;
};

bool IsRatio(std::string const & s, size_t start, size_t end, std::string_view right)
{
  if (MeridiemAfter(s, end))
    return false;
  if (AbbreviationBefore(s, start))
    return false;
  constexpr size_t kWindow = 80;
  size_t ws = start > kWindow ? start - kWindow : 0;
  bool cut = ws > 0;
  for (size_t i = start; i > ws; --i)
    if (ClauseMark(s, i - 1))
    {
      ws = i;
      cut = false;
      break;
    }
  // The last four words of the clause before the number (the first may be cut by the window).
  std::vector<Word> words;
  for (size_t i = ws; i < start;)
  {
    if (!LetterAt(s, i))
    {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < start && LetterAt(s, j))
      ++j;
    words.push_back({i, j});
    i = j;
  }
  if (cut && !words.empty())
    words.erase(words.begin());
  bool prevTouches = false;
  if (!words.empty() && words.back().e < start)
  {
    prevTouches = true;
    for (size_t i = words.back().e; i < start; ++i)
      if (!std::isspace(static_cast<unsigned char>(s[i])))
        prevTouches = false;
  }
  std::string const prev =
      prevTouches ? LowerAscii(std::string_view(s).substr(words.back().b, words.back().e - words.back().b))
                  : std::string();
  bool const cueAfter = In(kCuesAfter, NextWord(s, end));
  if (prevTouches && std::isupper(static_cast<unsigned char>(s[words.back().b])) && !In(kCuesBefore, prev) &&
      !In(kDeterminers, prev))
    return false;
  if (In(kTimeWords, prev) && !cueAfter)
    return false;
  if (cueAfter || right == "1")
    return true;
  for (size_t k = words.size() > 3 ? words.size() - 3 : 0; k < words.size(); ++k)
    if (In(kCuesBefore, LowerAscii(std::string_view(s).substr(words[k].b, words[k].e - words[k].b))))
      return true;
  return false;
}

// The right side of "N:" starting at i: "50", "25,000" (1-3 digits then ,ddd groups) or up to six digits,
// followed by no word character, colon, or [.,-] and a digit. Returns its end, or npos.
size_t RightEnd(std::string const & s, size_t i)
{
  size_t j = i;
  while (j < s.size() && Dig(s[j]))
    ++j;
  size_t const run = j - i;
  if (run == 0)
    return std::string::npos;
  if (run <= 3)
    while (j + 3 < s.size() && s[j] == ',' && Dig(s[j + 1]) && Dig(s[j + 2]) && Dig(s[j + 3]) &&
           (j + 4 >= s.size() || !Dig(s[j + 4])))
      j += 4;
  if (j == i + run && run > 6)
    return std::string::npos;
  if (j < s.size())
  {
    if (WordAt(s, j) || s[j] == ':')
      return std::string::npos;
    if ((s[j] == '.' || s[j] == ',' || s[j] == '-') && j + 1 < s.size() && Dig(s[j + 1]))
      return std::string::npos;
  }
  return j;
}
}  // namespace ratios

// "odds were 3:1" -> "odds were 3 to 1", "a 1:50 scale map" -> "a 1 to 50 scale map".
std::string Ratios(std::string const & s)
{
  using namespace ratios;
  if (s.find(':') == std::string::npos)
    return s;
  std::string out;
  size_t copied = 0;
  for (size_t c = s.find(':'); c != std::string::npos; c = s.find(':', c + 1))
  {
    // Left side: 1-3 digits right before the colon, with no word character, ':', '.', ',' or '/' before.
    size_t b = c;
    while (b > 0 && Dig(s[b - 1]))
      --b;
    if (b == c || c - b > 3 || b < copied)
      continue;
    if (b > 0 && (WordAt(s, b - 1) || s[b - 1] == ':' || s[b - 1] == '.' || s[b - 1] == ',' || s[b - 1] == '/'))
      continue;
    size_t const e = RightEnd(s, c + 1);
    if (e == std::string::npos)
      continue;
    if (!IsRatio(s, b, e, std::string_view(s).substr(c + 1, e - c - 1)))
      continue;
    out.append(s, copied, b - copied);
    out.append(s, b, c - b);
    out += " to ";
    out.append(s, c + 1, e - c - 1);
    copied = e;
    c = e - 1;
  }
  if (copied == 0)
    return s;
  out.append(s, copied, std::string::npos);
  return out;
}
}  // namespace
}  // namespace backseat::pronunciation
