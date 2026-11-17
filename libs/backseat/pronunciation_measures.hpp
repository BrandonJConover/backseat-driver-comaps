#pragma once

// Internal to libs/backseat: feet and inches read aloud (FeetAndInches), used by ForSpeech() in
// pronunciation.cpp (loop B149, BI-125). Included only by pronunciation.cpp. Not public API beyond what
// pronunciation.hpp declares; hosts include pronunciation.hpp.

#include "backseat/pronunciation.hpp"

#include <cctype>
#include <functional>
#include <optional>
#include <regex>
#include <string>
#include <string_view>

namespace backseat::pronunciation
{
namespace
{
// Shared helpers defined in pronunciation.cpp.
bool IsAsciiLetter(char ch);
long long ToNumber(std::string const & digits);
std::string Replace(std::string const & s, std::regex const & re,
                    std::function<std::optional<std::string>(std::smatch const &, size_t)> const & fn);

// Feet and inch marks: straight ' and ", curly ’ and ”, and the primes ′ and ″.
#define BACKSEAT_FOOT_MARK "(?:'|\xE2\x80\x99|\xE2\x80\xB2)"
#define BACKSEAT_INCH_MARK "(?:\"|\xE2\x80\x9D|\xE2\x80\xB3)"

// "13 feet 6 inches"; before a noun after "a"/"an" it's an adjective: "a 12 foot boat", "a 6 foot 2 inch man".
std::string FeetInchesWords(long long feet, long long inches, bool adjective = false)
{
  std::string out = std::to_string(feet) + (feet == 1 || adjective ? " foot" : " feet");
  if (inches > 0)
    out += " " + std::to_string(inches) + (inches == 1 || adjective ? " inch" : " inches");
  return out;
}

// "a 12' boat", "An 8′ ladder": an article right before the number and a lowercase word right after.
bool Adjective(std::string const & s, size_t pos, size_t after)
{
  static std::regex const article("(?:^|\\s)(?:a|an|A|An)\\s$");
  static std::regex const noun("^\\s[a-z]");
  return std::regex_search(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(pos), article) &&
         std::regex_search(s.begin() + static_cast<std::ptrdiff_t>(after), s.end(), noun);
}

// Nothing glued in front: "13'6\"" yes; "$13'", "A13'", "1.3'" and "4:13'" are something else.
bool FreeBefore(std::string const & s, size_t pos)
{
  if (pos == 0)
    return true;
  unsigned char const prev = static_cast<unsigned char>(s[pos - 1]);
  return !(std::isalnum(prev) || prev == '_' || std::string_view("$.,:/#-'").find(static_cast<char>(prev)) !=
                                                    std::string_view::npos);
}

// Whether the text before pos has an open single quote: "Sign says 'Clearance 12'" ends a quote, not a
// height. An opening quote is ‘ or a ' at the start of a word ('Turn, not '90s); a closing one is ’ or '
// after a letter or full stop that isn't followed by a letter (so "don't" and "Joe's" are apostrophes).
bool InsideSingleQuote(std::string const & s, size_t pos)
{
  int open = 0;
  for (size_t i = 0; i < pos; ++i)
  {
    bool const curlyOpen = s.compare(i, 3, "\xE2\x80\x98") == 0;
    bool const curlyClose = s.compare(i, 3, "\xE2\x80\x99") == 0;
    if (s[i] != '\'' && !curlyOpen && !curlyClose)
      continue;
    size_t const len = s[i] == '\'' ? 1 : 3;
    unsigned char const prev = i > 0 ? static_cast<unsigned char>(s[i - 1]) : ' ';
    unsigned char const next = i + len < s.size() ? static_cast<unsigned char>(s[i + len]) : ' ';
    bool const prevSpace = std::isspace(prev) || prev == '(' || prev == '"';
    if (curlyOpen || (s[i] == '\'' && prevSpace && IsAsciiLetter(static_cast<char>(next))))
      ++open;
    else if (!prevSpace && !IsAsciiLetter(static_cast<char>(next)) && !std::isdigit(prev) && open > 0)
      --open;
    i += len - 1;
  }
  return open > 0;
}
}  // namespace

std::string FeetAndInches(std::string const & text)
{
  if (text.find_first_of("'\"f\xE2") == std::string::npos)
    return text;
  std::string s = text;
  // 13'6" / 13' 6" / 6′2″ / 13'6 (no inch mark): inches 0-11 only, nothing glued after.
  static std::regex const marks("(\\d{1,3})\\s?" BACKSEAT_FOOT_MARK "\\s?(\\d{1,2})(?:\\s?(" BACKSEAT_INCH_MARK "))?"
                                "(?![A-Za-z0-9'])");
  s = Replace(s, marks, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (!FreeBefore(s, pos))
      return std::nullopt;
    auto const inches = ToNumber(m.str(2));
    if (inches > 11)
      return std::nullopt;
    // "13' 6 cars" without an inch mark isn't a height; glued "13'6" is.
    if (!m[3].matched && m.str(0).find(' ') != std::string::npos)
      return std::nullopt;
    return FeetInchesWords(ToNumber(m.str(1)), inches,
                           Adjective(s, pos, pos + static_cast<size_t>(m.length(0))));
  });
  // A lone 12' or 12′: not "the 90's", not a closing quote ("says 'Clearance 12'").
  static std::regex const lone("(\\d{1,3})" BACKSEAT_FOOT_MARK "(?![A-Za-z0-9'\"])");
  s = Replace(s, lone, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (!FreeBefore(s, pos) || InsideSingleQuote(s, pos))
      return std::nullopt;
    return FeetInchesWords(ToNumber(m.str(1)), 0, Adjective(s, pos, pos + static_cast<size_t>(m.length(0))));
  });
  // "14 ft 2 in" / "14ft 2in." / "6 ft. 1 in": the 'in' must follow a number after ft, and not start a
  // phrase ("go 500 ft 2 in a row" is left).
  static std::regex const words("(\\d{1,3})\\s?(?:ft|FT|Ft)\\.?\\s(\\d{1,2})\\s?(?:in|IN|In)\\b(\\.)?");
  static std::regex const phrase("^\\s+(?:a|an|the|my|your|front|of)\\b", std::regex::icase);
  s = Replace(s, words, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (!FreeBefore(s, pos))
      return std::nullopt;
    auto const inches = ToNumber(m.str(2));
    if (inches > 11)
      return std::nullopt;
    auto const after = pos + static_cast<size_t>(m.length(0));
    if (std::regex_search(s.begin() + static_cast<std::ptrdiff_t>(after), s.end(), phrase))
      return std::nullopt;
    std::string out = FeetInchesWords(ToNumber(m.str(1)), inches);
    // "in." ends the sentence only before the end or a capital: "2 in. Turn" keeps it, "2 in. clearance" not.
    if (m[3].matched)
    {
      size_t next = after;
      while (next < s.size() && s[next] == ' ')
        ++next;
      if (next >= s.size() || std::isupper(static_cast<unsigned char>(s[next])))
        out += '.';
    }
    return out;
  });
  return s;
}
#undef BACKSEAT_FOOT_MARK
#undef BACKSEAT_INCH_MARK
}  // namespace backseat::pronunciation
