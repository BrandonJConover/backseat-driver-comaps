#pragma once

// Internal to libs/backseat: web addresses and e-mail read aloud (WebAddresses and its host/label/URL
// helpers), used by ForSpeech() in pronunciation.cpp. Included only by pronunciation.cpp (internal
// linkage); moved out of it unchanged (loop B135). Not public API; hosts include pronunciation.hpp.

#include "backseat/pronunciation.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace backseat::pronunciation
{
namespace
{
// Text helpers these passes share with the rest of pronunciation.cpp, defined there and in
// pronunciation_streets.hpp.
bool Digit(char ch);
bool Space(char ch);
}  // namespace

namespace
{
// Web addresses and e-mail, the same way BookLyre reads them (text/spoken_text.web_addresses, golden
// cases shared in backseat_tests/spoken_web.json). Hand-scanned rather than std::regex: libstdc++'s
// regex recurses per character, and a 20 KB run of dots and letters must not blow the stack.
bool WebWordByte(char c)
{
  auto const u = static_cast<unsigned char>(c);
  return std::isalnum(u) != 0 || c == '_' || u >= 0x80;
}

bool LabelByte(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-'; }

bool IsWebTld(std::string_view t)
{
  static char const * const kTlds[] = {"com", "org", "net", "edu", "gov", "mil", "int", "info", "biz",
                                       "io",  "uk",  "ca",  "au",  "nz",  "ie",  "de",  "fr",   "es",
                                       "it",  "nl",  "eu",  "in",  "jp",  "app", "dev"};
  bool const lower = std::all_of(t.begin(), t.end(), [](char c) { return c >= 'a' && c <= 'z'; });
  bool const upper = std::all_of(t.begin(), t.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
  if (t.empty() || !(lower || upper))
    return false;
  for (auto const * tld : kTlds)
  {
    std::string_view const want(tld);
    if (want.size() == t.size() &&
        std::equal(want.begin(), want.end(), t.begin(), [](char a, char b) { return a == (b | 0x20); }))
      return true;
  }
  return false;
}

// Dot-separated host labels from `at`: each starts and ends with a letter or digit, at most 63
// bytes. Stops before a dot not followed by one. `end` is just past the last label taken.
std::vector<std::string_view> HostLabels(std::string_view s, size_t at, size_t & end)
{
  std::vector<std::string_view> labels;
  end = at;
  size_t i = at;
  while (i < s.size() && std::isalnum(static_cast<unsigned char>(s[i])))
  {
    size_t j = i;
    while (j < s.size() && LabelByte(s[j]))
      ++j;
    size_t k = j;
    while (k > i && s[k - 1] == '-')
      --k;
    if (k - i > 63)
      break;
    labels.push_back(s.substr(i, k - i));
    end = k;
    if (k != j || k + 1 >= s.size() || s[k] != '.' || !std::isalnum(static_cast<unsigned char>(s[k + 1])))
      break;
    i = k + 1;
  }
  return labels;
}

// A host the voice can say without a scheme in front: two labels or more, the last one a common
// top-level domain all in one case, and no label byte or "_" right after it.
bool HostWithTld(std::string_view s, std::vector<std::string_view> const & labels, size_t end)
{
  if (labels.size() < 2 || !IsWebTld(labels.back()))
    return false;
  if (end < s.size() && (LabelByte(s[end]) || s[end] == '_'))
    return false;
  // "a-b.com-": the last label lost a trailing hyphen, so it isn't the whole word.
  return labels.back().data() + labels.back().size() == s.data() + end;
}

std::string SpokenHost(std::vector<std::string_view> const & labels)
{
  std::string out;
  for (auto const & label : labels)
  {
    if (!out.empty())
      out += " dot ";
    for (char c : label)
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool StartsWithNoCase(std::string_view s, size_t at, std::string_view word)
{
  if (at + word.size() > s.size())
    return false;
  for (size_t i = 0; i < word.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(s[at + i])) != word[i])
      return false;
  return true;
}

// "www." or "www2." at `at`: returns the length, or 0.
size_t Www(std::string_view s, size_t at)
{
  if (!StartsWithNoCase(s, at, "www"))
    return 0;
  size_t i = at + 3;
  if (i < s.size() && Digit(s[i]))
    ++i;
  return i < s.size() && s[i] == '.' ? i + 1 - at : 0;
}

bool TrailingPunct(std::string_view s, size_t end, size_t & width)
{
  static std::string_view const kAscii = ".,;:!?)]}'\"";
  if (end >= 1 && kAscii.find(s[end - 1]) != std::string_view::npos)
  {
    width = 1;
    return true;
  }
  // ’ and ” in UTF-8.
  if (end >= 3 && s[end - 3] == '\xE2' && s[end - 2] == '\x80' && (s[end - 1] == '\x99' || s[end - 1] == '\x9D'))
  {
    width = 3;
    return true;
  }
  return false;
}

// An e-mail address starting at `at`, or nullopt. `end` is just past it.
std::optional<std::string> Email(std::string_view s, size_t at, size_t & end)
{
  size_t j = at;
  while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == '_' ||
                          s[j] == '+' || s[j] == '-'))
    ++j;
  if (j == at || j - at > 64 || j >= s.size() || s[j] != '@' || !std::isalnum(static_cast<unsigned char>(s[at])) ||
      !std::isalnum(static_cast<unsigned char>(s[j - 1])))
    return std::nullopt;
  size_t hostEnd = 0;
  auto const labels = HostLabels(s, j + 1, hostEnd);
  if (!HostWithTld(s, labels, hostEnd))
    return std::nullopt;
  std::string local;
  for (size_t i = at; i < j; ++i)
  {
    char const c = s[i];
    if (c == '.')
      local += " dot ";
    else if (c == '_')
      local += " underscore ";
    else if (c == '+')
      local += " plus ";
    else
      local += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  end = hostEnd;
  return local + " at " + SpokenHost(labels);
}

// A web address starting at `at`, or nullopt: scheme or "www." or a host ending in a known
// top-level domain, then an optional port and path. The path is dropped but sentence punctuation
// it swallowed comes back. `end` is just past what was read.
std::optional<std::string> Url(std::string_view s, size_t at, size_t & end)
{
  size_t i = at;
  bool lead = false;
  for (std::string_view scheme : {"https://", "http://", "ftp://"})
  {
    if (StartsWithNoCase(s, i, scheme))
    {
      i += scheme.size();
      i += Www(s, i);
      lead = true;
      break;
    }
  }
  bool www = false;
  if (!lead)
  {
    if (size_t const w = Www(s, i))
    {
      i += w;
      www = true;
    }
  }
  size_t hostEnd = 0;
  auto labels = HostLabels(s, i, hostEnd);
  if (www && labels.size() < 2)
  {
    // "www.com" isn't www plus a host; it may still be a host itself.
    www = false;
    i = at;
    labels = HostLabels(s, i, hostEnd);
  }
  if (labels.empty())
    return std::nullopt;
  if (!lead && !www)
  {
    if (!HostWithTld(s, labels, hostEnd))
      return std::nullopt;
  }
  size_t k = hostEnd;
  if ((lead || www) && k + 1 < s.size() && s[k] == ':' && Digit(s[k + 1]))
  {
    size_t p = k + 1;
    while (p < s.size() && p - k <= 5 && Digit(s[p]))
      ++p;
    k = p;
  }
  std::string trailing;
  if (k < s.size() && (s[k] == '/' || s[k] == '?' || s[k] == '#'))
  {
    size_t p = k;
    while (p < s.size() && !Space(s[p]) && s[p] != '<' && s[p] != '>' && s[p] != '"')
      ++p;
    size_t t = p, width = 0;
    while (t > k && TrailingPunct(s, t, width))
      t -= width;
    trailing = std::string(s.substr(t, p - t));
    // "(see en.wikipedia.org/wiki/Foo_(bar))": the first ")" closes the path's own "(".
    if (!trailing.empty() && trailing[0] == ')' && s.substr(at, p - at).find('(') != std::string_view::npos)
      trailing.erase(0, 1);
    k = p;
  }
  end = k;
  return SpokenHost(labels) + trailing;
}
}  // namespace

std::string WebAddresses(std::string const & text)
{
  // Fast path: an address needs "://", or a dot between two letters or digits.
  bool maybe = text.find("://") != std::string::npos;
  for (size_t i = 1; !maybe && i + 1 < text.size(); ++i)
    maybe = text[i] == '.' && std::isalnum(static_cast<unsigned char>(text[i - 1])) &&
            std::isalnum(static_cast<unsigned char>(text[i + 1]));
  if (!maybe)
    return text;
  std::string_view const s(text);
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < s.size())
  {
    char const before = i == 0 ? ' ' : s[i - 1];
    bool const wordBefore = i > 0 && WebWordByte(before);
    size_t end = 0;
    if (std::isalnum(static_cast<unsigned char>(s[i])) && !wordBefore)
    {
      std::optional<std::string> said;
      if (before != '@' && before != '.' && before != '+' && before != '-')
        said = Email(s, i, end);
      if (!said && before != '@' && before != '.' && before != '/' && before != '-')
        said = Url(s, i, end);
      if (said)
      {
        out += *said;
        i = end;
        continue;
      }
    }
    out += s[i];
    ++i;
  }
  return out;
}
}  // namespace backseat::pronunciation
