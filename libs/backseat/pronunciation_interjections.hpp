#pragma once

// Internal to libs/backseat: interjections the voice can't say, swapped for words it can (Interjections),
// used by ForSpeech() in pronunciation.cpp. Included only by pronunciation.cpp (internal linkage).
// Not public API; hosts include pronunciation.hpp.
//
// Kokoro's misaki lexicon has no entry for vowelless or odd-spelled interjections, and with no espeak
// fallback the voice says nothing: "Shh. I'm quiet now." comes out as "I'm quiet now." The same rule as
// BookLyre (text/spoken_interjections.py, CROSS X-062), golden cases shared in
// backseat_tests/spoken_interjections.json:
//   shh/sh -> shush, tsk -> tut, argh/arrgh -> aargh, hmph/hmpf/mmph/hrmph/Mph -> humph, hrmm/hrm -> hmm,
//   brr -> burr, pff/pfft/pft/phht/psh/pssh -> pish, whoo -> woo, hee -> he, bleh -> bleah, erm -> um,
//   ahh/ohh/uhh/umm/aww/eww -> ah/oh/uh/um/aw/ew, heh -> ha, hehe -> haha, hehehe/hahaha/mwahaha/bwahaha
//   -> ha ha ha, woohoo -> whoopee, achoo -> ah chew, ack -> ach, wha -> wah, nuh-uh -> uh-uh,
//   mhm/mhmm/mm-hmm/mm-hm -> uh-huh, grr -> growl, zzz -> snore, arr -> are, yarr -> yah are.
// Whole words only ("Tutsk", "bashh", "Pshaw", "Ackerman" stay), case kept ("SHH" -> "SHUSH").
// Elongations fold first ("Shhhhh" is "Shh", "Zzzzz" "Zzz") and a doubled one splits ("Sh-sh" is
// "Shush, shush", "Heh-heh" "Ha, ha"). A bare "sh" changes only before punctuation or the end ("run sh
// script" stays); a capitalised one before a street type names the street ("Mph Rd", "Ack St"); "mph" is
// the speed unit unless it is "Mph" with no number before it; "arr" before a time ("arr. 10:15") is an
// arrival; all-caps ERM/MPH/ACK/ARR/ZZZ are acronyms or band names.

#include <cctype>
#include <string>
#include <string_view>

namespace backseat::pronunciation
{
namespace
{
// Defined in pronunciation_streets.hpp, which pronunciation.cpp includes first.
std::string Lower(std::string_view w);
bool StreetTypeWord(std::string const & w);

struct Interjection
{
  std::string_view spelled;
  std::string_view said;
};

constexpr Interjection kInterjections[] = {
    {"shh", "shush"},        {"sh", "shush"},       {"tsk", "tut"},          {"argh", "aargh"},
    {"arrgh", "aargh"},      {"hmph", "humph"},     {"hmpf", "humph"},       {"mmph", "humph"},
    {"mph", "humph"},        {"hrmph", "humph"},    {"hrmm", "hmm"},         {"brr", "burr"},
    {"pff", "pish"},         {"pfft", "pish"},      {"pft", "pish"},         {"phht", "pish"},
    {"psh", "pish"},         {"whoo", "woo"},       {"hee", "he"},           {"bleh", "bleah"},
    {"erm", "um"},           {"ahh", "ah"},         {"ohh", "oh"},           {"uhh", "uh"},
    {"umm", "um"},           {"aww", "aw"},         {"eww", "ew"},           {"heh", "ha"},
    {"hehe", "haha"},        {"hehehe", "ha ha ha"}, {"hahaha", "ha ha ha"}, {"mwahaha", "ha ha ha"},
    {"bwahaha", "ha ha ha"}, {"woohoo", "whoopee"}, {"achoo", "ah chew"},    {"ack", "ach"},
    {"wha", "wah"},          {"hrm", "hmm"},        {"pssh", "pish"},        {"nuh-uh", "uh-uh"},
    {"mhm", "uh-huh"},       {"mhmm", "uh-huh"},    {"mm-hmm", "uh-huh"},    {"mm-hm", "uh-huh"},
    {"grr", "growl"},        {"zzz", "snore"},      {"arr", "are"},          {"yarr", "yah are"},
};

std::string_view InterjectionSaid(std::string const & key)
{
  for (auto const & i : kInterjections)
    if (i.spelled == key)
      return i.said;
  return {};
}

bool AsciiAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// Runs of three or more of one letter folded to `keep` ("Shhhhh" -> "Shh" with keep 2).
std::string FoldRuns(std::string const & w, size_t keep)
{
  std::string out;
  for (size_t i = 0; i < w.size();)
  {
    size_t j = i;
    while (j < w.size() && std::tolower(static_cast<unsigned char>(w[j])) ==
                               std::tolower(static_cast<unsigned char>(w[i])))
      ++j;
    out.append(w, i, (j - i >= 3) ? keep : j - i);
    i = j;
  }
  return out;
}

bool AllUpper(std::string const & w)
{
  size_t letters = 0;
  for (char c : w)
  {
    if (std::islower(static_cast<unsigned char>(c)))
      return false;
    letters += AsciiAlpha(c) ? 1 : 0;
  }
  return letters > 1;
}

std::string Cased(std::string const & word, std::string_view said)
{
  std::string out(said);
  if (AllUpper(word))
  {
    for (auto & c : out)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  else if (std::isupper(static_cast<unsigned char>(word[0])))
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}

// The stand-in for one word (elongations folded), or empty when it isn't one we change.
std::string_view InterjectionFor(std::string const & word, std::string & key)
{
  key = Lower(word);
  if (auto const said = InterjectionSaid(key); !said.empty())
    return said;
  for (size_t keep : {size_t{2}, size_t{3}})
  {
    std::string const folded = Lower(FoldRuns(word, keep));
    if (folded == key)
      continue;
    if (auto const said = InterjectionSaid(folded); !said.empty())
    {
      key = folded;
      return said;
    }
  }
  return {};
}

// A right single quote (’) or a letter outside ASCII (any UTF-8 byte except the E2 80 xx punctuation
// block: “ ” … — –) glues a word to its neighbour.
bool GluedBefore(std::string const & s, size_t at)
{
  if (at == 0)
    return false;
  unsigned char const c = static_cast<unsigned char>(s[at - 1]);
  if (std::isalnum(c) || c == '\'' || c == '_' || c == '-')
    return true;
  if (c < 0x80)
    return false;
  if (at >= 3 && static_cast<unsigned char>(s[at - 3]) == 0xE2 && static_cast<unsigned char>(s[at - 2]) == 0x80)
    return c == 0x98 || c == 0x99;
  return true;
}

bool GluedAfter(std::string const & s, size_t at)
{
  if (at >= s.size())
    return false;
  unsigned char const c = static_cast<unsigned char>(s[at]);
  if (std::isalnum(c) || c == '\'' || c == '_')
    return true;
  if (c == '-')
    return at + 1 < s.size() && std::isalnum(static_cast<unsigned char>(s[at + 1]));
  if (c < 0x80)
    return false;
  if (c == 0xE2 && at + 2 < s.size() && static_cast<unsigned char>(s[at + 1]) == 0x80)
  {
    unsigned char const d = static_cast<unsigned char>(s[at + 2]);
    return d == 0x98 || d == 0x99;
  }
  return true;
}

bool PunctuationOrEndAfter(std::string const & s, size_t at)
{
  while (at < s.size() && s[at] == ' ')
    ++at;
  if (at >= s.size())
    return true;
  unsigned char const c = static_cast<unsigned char>(s[at]);
  return std::string_view(",.!?;:\")").find(static_cast<char>(c)) != std::string_view::npos || c == 0xE2;
}

bool NumberBefore(std::string const & s, size_t at)
{
  while (at > 0 && s[at - 1] == ' ')
    --at;
  return at > 0 && std::isdigit(static_cast<unsigned char>(s[at - 1]));
}

// A street type next ("Mph Rd", "Ack St."): a map name, not a grunt.
bool StreetTypeAfter(std::string const & s, size_t at)
{
  while (at < s.size() && s[at] == ' ')
    ++at;
  size_t end = at;
  while (end < s.size() && AsciiAlpha(s[end]))
    ++end;
  return end > at && StreetTypeWord(s.substr(at, end - at));
}

bool TimeAfter(std::string const & s, size_t at)
{
  if (at < s.size() && s[at] == '.')
    ++at;
  while (at < s.size() && s[at] == ' ')
    ++at;
  return at < s.size() && std::isdigit(static_cast<unsigned char>(s[at]));
}

// One part (no hyphen) or the whole hyphenated word, as said; empty when it stays.
std::string SayInterjection(std::string const & s, std::string const & word, size_t start, size_t end)
{
  std::string key;
  auto const said = InterjectionFor(word, key);
  if (said.empty())
    return {};
  bool const capsKept = key == "erm" || key == "mph" || key == "ack" || key == "arr" || key == "zzz";
  if (capsKept && AllUpper(word))
    return {};
  if (key == "mph" && (word != "Mph" || NumberBefore(s, start)))
    return {};
  if (key == "arr" && TimeAfter(s, end))
    return {};
  if (key == "sh" && !PunctuationOrEndAfter(s, end))
    return {};
  if (std::isupper(static_cast<unsigned char>(word[0])) && StreetTypeAfter(s, end))
    return {};
  return Cased(word, said);
}

std::string Interjections(std::string const & s)
{
  std::string out;
  out.reserve(s.size() + 16);
  size_t copied = 0;
  for (size_t i = 0; i < s.size();)
  {
    if (!AsciiAlpha(s[i]) || GluedBefore(s, i))
    {
      ++i;
      continue;
    }
    // A word of letters, maybe hyphenated ("Mm-hmm", "Sh-sh").
    size_t j = i;
    while (j < s.size() && (AsciiAlpha(s[j]) || (s[j] == '-' && j + 1 < s.size() && AsciiAlpha(s[j + 1]))))
      ++j;
    if (j - i > 40 || GluedAfter(s, j))
    {
      i = j;
      continue;
    }
    std::string const word = s.substr(i, j - i);
    std::string said = SayInterjection(s, word, i, j);
    if (said.empty() && word.find('-') != std::string::npos)
    {
      // "Sh-sh", "Tsk-tsk", "Heh-heh": the same interjection twice is said twice, with a pause.
      std::string first;
      bool same = true;
      std::string joined;
      for (size_t a = 0; a <= word.size() && same;)
      {
        size_t b = word.find('-', a);
        if (b == std::string::npos)
          b = word.size();
        std::string const part = word.substr(a, b - a);
        if (first.empty())
          first = Lower(part);
        same = Lower(part) == first;
        std::string key;
        auto const partSaid = InterjectionFor(part, key);
        same = same && !partSaid.empty() && !(AllUpper(part) && (key == "erm" || key == "mph" || key == "ack" ||
                                                                 key == "arr" || key == "zzz"));
        if (same)
          joined += (joined.empty() ? "" : ", ") + Cased(part, partSaid);
        a = b + 1;
      }
      if (same)
        said = joined;
    }
    if (!said.empty())
    {
      out.append(s, copied, i - copied);
      out += said;
      copied = j;
    }
    i = j;
  }
  if (copied == 0)
    return s;
  out.append(s, copied, std::string::npos);
  return out;
}
}  // namespace
}  // namespace backseat::pronunciation
