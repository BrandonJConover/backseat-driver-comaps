#pragma once

// Internal to libs/backseat: British and American spellings swapped toward the voice's own lexicon
// (SpellingVariants), used by ForSpeech() in pronunciation.cpp. Included only by pronunciation.cpp
// (internal linkage). Not public API; hosts include pronunciation.hpp.
//
// Kokoro's American misaki lexicon has no "grey", "theatre", "colour" or "fibre" (and the British one no
// "color" or "theater"), so map names like "Grey St" or "Theatre District" went to the guesser. Same rule
// as BookLyre (android SpellingVariants.kt, CROSS X-061), golden cases shared in
// backseat_tests/spoken_spelling_variants.json:
// - a word in the table is swapped for the other spelling of the same sound, case kept ("Grey" -> "Gray");
// - otherwise a possessive or plain suffix is respelled through its stem ("Colour's" -> "Color's",
//   "programmes" -> "programs", "realised" -> "realized"), suffixes tried longest first, stem >= 3 letters;
// - all-caps words (the voice spells them like an acronym either way), words glued to digits or other
//   letters ("Théâtre", "grey2") and words the table lacks are left.
// Direction: American unless SetBritishSpelling(true) (a British voice).

#include "backseat/pronunciation_spelling_data.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <string>
#include <string_view>

namespace backseat::pronunciation
{
namespace
{
std::atomic<bool> g_britishSpelling{false};

template <size_t N>
std::string_view SpellingLookup(SpellingPair const (&table)[N], std::string_view lower)
{
  auto const it = std::lower_bound(std::begin(table), std::end(table), lower,
                                   [](SpellingPair const & p, std::string_view w) { return p.spelled < w; });
  return (it != std::end(table) && it->spelled == lower) ? it->respelled : std::string_view{};
}

// The respelling of `lower` (ASCII lowercase, ' for apostrophes), or empty.
std::string RespellLower(std::string const & lower)
{
  bool const british = g_britishSpelling.load(std::memory_order_relaxed);
  auto const find = [british](std::string_view w) { return british ? SpellingLookup(kToGb, w) : SpellingLookup(kToUs, w); };
  if (auto const hit = find(lower); !hit.empty())
    return std::string(hit);
  for (std::string_view suffix : {"'s", "s'", "ing", "ed", "es", "s", "d"})
  {
    if (lower.size() <= suffix.size() + 2 || std::string_view(lower).substr(lower.size() - suffix.size()) != suffix)
      continue;
    if (auto const hit = find(std::string_view(lower).substr(0, lower.size() - suffix.size())); !hit.empty())
      return std::string(hit) + std::string(suffix);
  }
  return {};
}

// `respelled` (lowercase, ' apostrophes) written in `original`'s case and with its apostrophes.
std::string SpellingInCase(std::string const & original, std::string const & respelled)
{
  std::string out = respelled;
  if (std::isupper(static_cast<unsigned char>(original[0])))
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  // A curly apostrophe stays curly ("Colour’s" -> "Color’s").
  if (original.find("\xE2\x80\x99") != std::string::npos)
    for (size_t at = out.find('\''); at != std::string::npos; at = out.find('\'', at + 3))
      out.replace(at, 1, "\xE2\x80\x99");
  return out;
}

// Whether text[at] is part of a word: letters, digits, apostrophes (' and ’) and other non-ASCII letters
// ("Théâtre" stays one word). Dashes, curly double quotes and "…" (U+2000-U+203F) split words.
bool SpellingWordByte(std::string const & text, size_t at)
{
  auto const ch = static_cast<unsigned char>(text[at]);
  if (ch == 0xE2 && at + 2 < text.size() && static_cast<unsigned char>(text[at + 1]) == 0x80)
    return static_cast<unsigned char>(text[at + 2]) == 0x99;
  if ((ch & 0xC0) == 0x80 && at >= 1 && static_cast<unsigned char>(text[at - 1]) == 0x80 && at >= 2 &&
      static_cast<unsigned char>(text[at - 2]) == 0xE2)
    return static_cast<unsigned char>(text[at]) == 0x99;  // third byte of a U+20xx punctuation mark
  if (ch == 0x80 && at >= 1 && static_cast<unsigned char>(text[at - 1]) == 0xE2)
    return at + 1 < text.size() && static_cast<unsigned char>(text[at + 1]) == 0x99;
  return std::isalnum(ch) || ch == '\'' || (ch & 0x80);
}

std::string SpellingVariants(std::string const & text)
{
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();)
  {
    if (!SpellingWordByte(text, i))
    {
      out += text[i++];
      continue;
    }
    size_t j = i;
    while (j < text.size() && SpellingWordByte(text, j))
      ++j;
    std::string word = text.substr(i, j - i);
    i = j;
    // Quote marks around the word stay outside it ("'grey'" is a quoted word); a trailing one after
    // an s may be a plural possessive ("colours'").
    auto const quoteAt = [&word](size_t at) {
      return word.compare(at, 1, "'") == 0 || word.compare(at, 3, "\xE2\x80\x99") == 0;
    };
    while (!word.empty() && quoteAt(0))
    {
      size_t const n = word[0] == '\'' ? 1 : 3;
      out += word.substr(0, n);
      word.erase(0, n);
    }
    std::string tail;
    for (;;)
    {
      size_t const n = (!word.empty() && word.back() == '\'') ? 1
                       : (word.size() >= 3 && quoteAt(word.size() - 3)) ? 3 : 0;
      if (n == 0 || word.size() <= n || std::tolower(static_cast<unsigned char>(word[word.size() - n - 1])) == 's')
        break;
      tail.insert(0, word.substr(word.size() - n));
      word.erase(word.size() - n);
    }
    if (word.empty())
    {
      out += tail;
      continue;
    }
    // Letters with ' or ’ inside only.
    std::string lower;
    bool plain = true, anyLower = false;
    for (size_t k = 0; k < word.size() && plain; ++k)
    {
      char const ch = word[k];
      if (std::isalpha(static_cast<unsigned char>(ch)))
      {
        anyLower = anyLower || std::islower(static_cast<unsigned char>(ch));
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      }
      else if (ch == '\'')
        lower += '\'';
      else if (word.compare(k, 3, "\xE2\x80\x99") == 0)
      {
        lower += '\'';
        k += 2;
      }
      else
        plain = false;
    }
    std::string respelled;
    if (plain && anyLower && lower.size() >= 3)
      respelled = RespellLower(lower);
    out += respelled.empty() ? word : SpellingInCase(word, respelled);
    out += tail;
  }
  return out;
}
}  // namespace
}  // namespace backseat::pronunciation
