#pragma once

// Internal to libs/backseat: British road numbers (UkRoads), used by ForSpeech() in pronunciation.cpp
// next to the US route pass Roads(). Included only by pronunciation.cpp (internal linkage).
// Not public API; hosts include pronunciation.hpp.
//
// Without it "Take the M25" and "Stay on the A303" reached the voice as letter-digit codes, and "Leave
// the M25 at J10" kept "J10" (loop B170, BI-151). Golden cases in backseat_tests/spoken_uk_roads.json.
//   - M, A and B roads glued to their number: one or two digits are a number ("M twenty five",
//     "A forty"), three digits route style ("A three oh three", "A one twenty seven"), four digits
//     digit by digit with 0 as "oh" ("B four oh oh nine").
//   - "A1(M)" is "A one M"; "M6 Toll" keeps Toll.
//   - Only in road context: after the/onto/on/via/join/take/along/leave/exit/off, or before (M), Toll,
//     a junction, "motorway", a -bound direction or (anti)clockwise. So "A1 sauce", "Seat A12",
//     "vitamin B12", "B2B" and "M&M" stay, and "the A4 paper" stays because of the word after it.
//   - "J10" / "J10a" is "junction ten" / "junction ten A" when the text names a motorway (an M ref or
//     the word motorway).
// The output has no digits, so the later number passes leave it alone (idempotent).

#include <cctype>
#include <optional>
#include <regex>
#include <set>
#include <string>

namespace backseat::pronunciation
{
std::string RouteNumber(std::string const & digits);

namespace
{
namespace uk_roads
{
std::string Lowered(std::string w)
{
  for (auto & ch : w)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return w;
}

std::string Number(std::string const & digits)
{
  if (digits.size() <= 3)
    return RouteNumber(digits);
  static char const * const kWords[] = {"oh", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
  std::string out;
  for (char ch : digits)
  {
    if (!out.empty())
      out += ' ';
    out += kWords[ch - '0'];
  }
  return out;
}

// The word right before `pos` (letters only), lowercased; empty when none.
std::string UkWordBefore(std::string const & s, size_t pos)
{
  size_t end = pos;
  while (end > 0 && s[end - 1] == ' ')
    --end;
  size_t start = end;
  while (start > 0 && std::isalpha(static_cast<unsigned char>(s[start - 1])))
    --start;
  return Lowered(s.substr(start, end - start));
}

// The word right after `pos` (letters only), lowercased; empty when none.
std::string UkWordAfter(std::string const & s, size_t pos)
{
  size_t start = pos;
  while (start < s.size() && s[start] == ' ')
    ++start;
  size_t end = start;
  while (end < s.size() && std::isalpha(static_cast<unsigned char>(s[end])))
    ++end;
  return Lowered(s.substr(start, end - start));
}
}  // namespace uk_roads

std::string UkRoads(std::string const & text)
{
  using namespace uk_roads;
  static std::set<std::string> const before = {"the",  "onto",    "on",    "via",     "join",   "joins",
                                               "joining", "rejoin", "take", "taking",  "along",  "leave",
                                               "leaving", "exit",   "exiting", "off",  "follow", "following"};
  // Words that make the code something other than a road even after "the": paper sizes, steak sauce,
  // vitamins, rifles, chips.
  static std::set<std::string> const notRoad = {
      "paper",   "papers",   "sheet",   "sheets",  "size",   "sized",     "sauce",    "steak",   "battery",
      "batteries", "envelope", "envelopes", "vitamin", "vitamins", "supplement", "supplements", "tablet",
      "tablets", "shot",     "shots",   "rifle",   "rifles", "carbine",   "grade",    "rating",  "format",
      "printer", "chip",     "chips",   "processor", "bomber", "bombers",  "form",     "seat",    "seats",
      "gate",    "gates",    "bingo"};
  static std::regex const ref(
      "(^|[^A-Za-z0-9&'_-])([MAB])([1-9]\\d{0,3})(\\(M\\))?(?![A-Za-z0-9&_]|[.,]\\d|-[A-Za-z0-9])");
  static std::regex const after(
      "^(?: Toll\\b| [Jj]\\d| (?:at )?[Jj]unction\\b| [Mm]otorway\\b| (?:[Nn]orth|[Ss]outh|[Ee]ast|[Ww]est)bound\\b|"
      " (?:[Aa]nti)?[Cc]lockwise\\b)");
  // Where the last road ref said here ended, so the second of "the M4/M5" or "the A1 and A2" is a road too.
  size_t lastRoadEnd = std::string::npos;
  auto s = Replace(text, ref, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    size_t const at = pos + static_cast<size_t>(m.length(1));
    size_t const end = pos + static_cast<size_t>(m.length(0));
    bool const motorwaySign = m[4].matched;
    bool pairedWithRoad = false;
    if (lastRoadEnd != std::string::npos && lastRoadEnd <= at)
    {
      auto const gap = text.substr(lastRoadEnd, at - lastRoadEnd);
      pairedWithRoad = gap == "/" || gap == " / " || gap == " and " || gap == " or " || gap == " & " || gap == ", ";
    }
    bool const roadWordBefore = pairedWithRoad || before.count(UkWordBefore(text, at)) > 0;
    bool const roadWordAfter = std::regex_search(text.begin() + static_cast<std::ptrdiff_t>(end), text.end(), after);
    if (!motorwaySign && !roadWordBefore && !roadWordAfter)
      return std::nullopt;
    if (!motorwaySign && notRoad.count(UkWordAfter(text, end)) > 0)
      return std::nullopt;
    lastRoadEnd = end;
    return m.str(1) + m.str(2) + " " + Number(m.str(3)) + (motorwaySign ? " M" : "");
  });
  // Junction numbers only where a motorway is named, so "J10" alone (a model, a seat) stays.
  static std::regex const motorway("(?:^|[^A-Za-z0-9])M[1-9]\\d{0,2}(?:\\(M\\))?(?![A-Za-z0-9])|\\b[Mm]otorways?\\b");
  if (!std::regex_search(text, motorway))
    return s;
  static std::regex const junction("(^|[^A-Za-z0-9])J([1-9]\\d?)([a-z]?)(?![A-Za-z0-9])");
  return Replace(s, junction, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    std::string out = m.str(1) + "junction " + RouteNumber(m.str(2));
    if (m[3].length() > 0)
      out += " " + std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(m.str(3)[0]))));
    return out;
  });
}
}  // namespace
}  // namespace backseat::pronunciation
