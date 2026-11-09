#pragma once

// Internal to libs/backseat: the street passes (street types, compass points, Saint/Doctor/Fort titles,
// address tails, the rarer USPS suffixes) and their helpers, used by ForSpeech() and the route passes in
// pronunciation.cpp. Included only by pronunciation.cpp (internal linkage); moved out of it unchanged
// (loop B104). Not public API; hosts include pronunciation.hpp.

#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>

namespace backseat::pronunciation
{
namespace
{
// Text helpers these passes share with the rest of pronunciation.cpp, defined there.
bool IsAsciiLetter(char ch);
std::string Lower(std::string_view w);
std::string Replace(std::string const & s, std::regex const & re,
                    std::function<std::optional<std::string>(std::smatch const &, size_t)> const & fn);

std::string const kStreetWords[][2] = {
    {"St", "Street"},     {"Ave", "Avenue"},  {"Blvd", "Boulevard"}, {"Rd", "Road"},        {"Dr", "Drive"},
    {"Ln", "Lane"},       {"Ct", "Court"},    {"Pkwy", "Parkway"},   {"Hwy", "Highway"},    {"Pl", "Place"},
    {"Cir", "Circle"},    {"Ter", "Terrace"}, {"Trl", "Trail"},      {"Expy", "Expressway"}, {"Fwy", "Freeway"},
    {"Sq", "Square"},     {"Cv", "Cove"},     {"Xing", "Crossing"},  {"Ext", "Extension"},
};

std::string Street(std::string const & abbreviation)
{
  for (auto const & w : kStreetWords)
    if (w[0] == abbreviation)
      return w[1];
  return abbreviation;
}

std::string Compass(std::string const & c)
{
  static std::map<std::string, std::string> const words = {
      {"N", "North"},     {"S", "South"},     {"E", "East"},      {"W", "West"},
      {"NE", "Northeast"}, {"NW", "Northwest"}, {"SE", "Southeast"}, {"SW", "Southwest"}};
  auto const it = words.find(c);
  return it == words.end() ? c : it->second;
}

bool Space(char ch)
{
  return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

// MARK: - Titles in names

// Capitalised words that can sit right before "St"/"Dr"/"Ft" without being the street's own name
// ("Take St. Johns Rd", "Call Dr Smith"). Lowercase.
bool LeadInWord(std::string const & w)
{
  static std::set<std::string> const words = {
      "take", "turn", "continue", "merge", "exit", "keep", "stay", "head", "follow", "use", "visit",
      "call", "ask", "see", "meet", "find", "blame", "thank", "onto", "on", "to", "at", "via", "toward",
      "towards", "from", "into", "past", "near", "then", "and", "or", "by", "for", "in", "of", "the", "a", "off"};
  return words.count(w) > 0;
}

/// True when the word just before `pos` names a street, so "St"/"Dr" at `pos` is its type ("Main St.",
/// "5th St"): an ordinal, or a capitalised word that isn't a lead-in. Start of text, punctuation or a
/// lowercase word before it mean a title ("onto St. Johns Rd", "Go to St Louis").
bool StreetNameBefore(std::string const & s, size_t pos)
{
  if (pos < 2 || s[pos - 1] != ' ')
    return false;
  size_t const end = pos - 1;
  size_t start = end;
  while (start > 0 && (std::isalnum(static_cast<unsigned char>(s[start - 1])) || s[start - 1] == '\''))
    --start;
  if (start == end)
    return false;
  auto const first = static_cast<unsigned char>(s[start]);
  // "5th St" is a street; a bare house number before it is not ("3502 St. Johns Rd").
  if (std::isdigit(first))
    return !std::all_of(s.begin() + static_cast<std::ptrdiff_t>(start), s.begin() + static_cast<std::ptrdiff_t>(end),
                        [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; });
  return std::isupper(first) && !LeadInWord(Lower(std::string_view(s).substr(start, end - start)));
}

bool StreetWord(std::string const & w)
{
  for (auto const & sw : kStreetWords)
    if (w == sw[0] || w == sw[1])
      return true;
  return false;
}

bool StreetTypeWord(std::string const & w);

/// True when 1-3 capitalised name words and then a street type follow `pos`, so "St." there is a
/// saint naming that street even after a capitalised word: "Down St. James Street", "Exit 12B St.
/// Johns Bluff Rd". "Main St. Mary was there" and "Baker St. Then left" don't end in a type.
bool SaintStreetAfter(std::string const & s, size_t pos)
{
  for (int names = 0; names <= 3; ++names)
  {
    while (pos < s.size() && s[pos] == ' ')
      ++pos;
    size_t end = pos;
    while (end < s.size() && (IsAsciiLetter(s[end]) || s[end] == '\''))
      ++end;
    if (end == pos || !std::isupper(static_cast<unsigned char>(s[pos])))
      return false;
    auto const word = s.substr(pos, end - pos);
    if (names > 0 && StreetTypeWord(word))
      return true;
    if (names == 3 || LeadInWord(Lower(word)) || (end < s.size() && s[end] != ' '))
      return false;
    pos = end;
  }
  return false;
}

/// "St. Johns Rd" is "Saint Johns Rd", "Dr Martin Luther King Jr Blvd" is "Doctor Martin Luther
/// King Junior Blvd", "Ft Worth" is "Fort Worth", "Hwy 290" is "Highway 290", "101 Fwy" is "101
/// Freeway". "Main St. Then left" and "Elm Dr." stay street types for the street pass.
/// A "St" after a capitalised word is still Saint when a name and a street type follow ("Down St.
/// James Street").
std::string Titles(std::string s)
{
  static std::regex const title("\\b(St|Ste|Dr|Ft)\\.? (?=[A-Z][a-z])");
  s = Replace(s, title, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (StreetNameBefore(s, pos) &&
        !(m.str(1) == "St" && SaintStreetAfter(s, pos + static_cast<size_t>(m.length(0)))))
      return std::nullopt;
    static std::map<std::string, std::string> const words = {
        {"St", "Saint"}, {"Ste", "Saint"}, {"Dr", "Doctor"}, {"Ft", "Fort"}};
    return words.at(m.str(1)) + " ";
  });
  // Jr and Sr after a name. The period goes when the sentence carries on ("King Jr. Blvd",
  // "Jr. was"), and stays when it may end the sentence ("Sammy Davis Jr.").
  static std::regex const suffix("(,? )(Jr|Sr)(\\.?)(?=$|[\\s,;:!?)])");
  s = Replace(s, suffix, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (pos == 0 || !IsAsciiLetter(s[pos - 1]))
      return std::nullopt;
    size_t start = pos;
    while (start > 0 && IsAsciiLetter(s[start - 1]))
      --start;
    if (!std::isupper(static_cast<unsigned char>(s[start])))
      return std::nullopt;
    std::string out = m.str(1) + (m.str(2) == "Jr" ? "Junior" : "Senior");
    if (m[3].length() > 0)
    {
      size_t next = pos + static_cast<size_t>(m.length(0));
      while (next < s.size() && Space(s[next]))
        ++next;
      size_t wordEnd = next;
      while (wordEnd < s.size() && IsAsciiLetter(s[wordEnd]))
        ++wordEnd;
      auto const word = s.substr(next, wordEnd - next);
      bool const carriesOn = !word.empty() && (std::islower(static_cast<unsigned char>(word[0])) || StreetWord(word));
      if (!carriesOn)
        out += ".";
    }
    return out;
  });
  // Road types next to a route number.
  // "Ranch Rd 2222", "County Rd 12": a capitalised word, then Rd and a number.
  static std::regex const roadBefore("\\b(Hwy|Fwy|Expy|Pkwy|Rte|Rd)\\.? (?=\\d)");
  s = Replace(s, roadBefore, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (m.str(1) == "Rd" && (pos < 2 || s[pos - 1] != ' ' || !std::isupper(static_cast<unsigned char>(
                                                                    s[s.find_last_of(' ', pos - 2) + 1]))))
      return std::nullopt;
    return (m.str(1) == "Rte" ? std::string("Route") : Street(m.str(1))) + " ";
  });
  static std::regex const roadAfter("\\b(\\d+[A-Z]?) (Hwy|Fwy|Expy|Pkwy)\\b");
  s = Replace(s, roadAfter, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    return m.str(1) + " " + Street(m.str(2));
  });
  return s;
}

/// A street type spelled out ("Street", "Boulevard", "Loop", "Heights"), as the street pass leaves it.
bool FullStreetTypeWord(std::string const & w)
{
  if (w.size() < 3)
    return false;
  for (auto const & sw : kStreetWords)
    if (w == sw[1])
      return true;
  return StreetTypeWord(w) && w != "Pass";
}

/// " / ", "/" or " - " between two road names is a pause: "Main Street / Elm Street" is "Main Street, Elm
/// Street", "Grand Avenue / CA-1" "Grand Avenue, CA-1", "US-290 / Ben White Boulevard" too. A road name is a
/// spelled-out street type on the left, or 1-3 capitalised words and a street type on the right. "24/7",
/// "and/or", "Dr / Smith" and "1/2 mi" are left. Runs after the street pass.
std::string RoadSeparators(std::string const & s)
{
  if (s.find('/') == std::string::npos && s.find(" - ") == std::string::npos)
    return s;
  static std::regex const separator("( ?/ ?| - )(?=[A-Z0-9])");
  return Replace(s, separator, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    size_t start = pos;
    while (start > 0 && IsAsciiLetter(s[start - 1]))
      --start;
    // "Elm Street / ...": a name, then a spelled-out type, right before the separator.
    bool const left = start < pos && start > 1 && Space(s[start - 1]) &&
                      std::isalnum(static_cast<unsigned char>(s[start - 2])) &&
                      FullStreetTypeWord(s.substr(start, pos - start));
    // "... / Elm Street": a capitalised word or a route or exit number before it ("US-290", "Exit 12B"),
    // so "w/ Main St views" is left.
    size_t token = pos;
    while (token > 0 && (std::isalnum(static_cast<unsigned char>(s[token - 1])) || s[token - 1] == '-'))
      --token;
    bool const right = token < pos && (std::isupper(static_cast<unsigned char>(s[token])) ||
                                       std::isdigit(static_cast<unsigned char>(s[token]))) &&
                       SaintStreetAfter(s, pos + static_cast<size_t>(m.length(0)));
    if (!left && !right)
      return std::nullopt;
    return std::string(", ");
  });
}

// MARK: - Address tails

/// The word that ends right before `end` (spaces, one comma and a period skipped: "Oak St, Ste 4",
/// "Elm Dr. Ste 4"), or empty; `start` gets where it starts.
std::string StreetPartBefore(std::string const & s, size_t end, size_t & start)
{
  while (end > 0 && Space(s[end - 1]))
    --end;
  if (end > 0 && (s[end - 1] == ',' || s[end - 1] == '.'))
    --end;
  while (end > 0 && Space(s[end - 1]))
    --end;
  start = end;
  while (start > 0 && std::isalnum(static_cast<unsigned char>(s[start - 1])))
    --start;
  return s.substr(start, end - start);
}

std::map<std::string, std::string> const & TailWords()
{
  static std::map<std::string, std::string> const words = {
      {"Ste", "Suite"},   {"Apt", "Apartment"}, {"Bldg", "Building"},     {"Fl", "Floor"},
      {"Flr", "Floor"},   {"Rm", "Room"},       {"Spc", "Space"},         {"Dept", "Department"},
      {"Ofc", "Office"},  {"Trlr", "Trailer"},  {"Hngr", "Hangar"},       {"Unit", "Unit"}};
  return words;
}

bool TailWord(std::string const & w)
{
  for (auto const & [abbreviation, word] : TailWords())
    if (w == abbreviation || w == word)
      return true;
  return false;
}

bool CompassWord(std::string const & w)
{
  static std::set<std::string> const words = {"N", "S", "E", "W", "NE", "NW", "SE", "SW", "North", "South",
                                              "East", "West", "Northeast", "Northwest", "Southeast", "Southwest"};
  return words.count(w) > 0;
}

/// USPS street suffixes past the common ones ("Shady Crk", "Forest Hts Dr", "Mesa Ctr Ste 100"). Said only
/// after a street's own name (StreetNameBefore), so "Sta 4", "the Lk" and "NB: Hl" are left alone.
std::string const kMoreStreetWords[][2] = {
    {"Hts", "Heights"},   {"Plz", "Plaza"},     {"Ctr", "Center"},    {"Tpke", "Turnpike"}, {"Hl", "Hill"},
    {"Vw", "View"},       {"Bnd", "Bend"},      {"Crk", "Creek"},     {"Cyn", "Canyon"},    {"Gln", "Glen"},
    {"Grv", "Grove"},     {"Hbr", "Harbor"},    {"Holw", "Hollow"},   {"Jct", "Junction"},  {"Lk", "Lake"},
    {"Mdw", "Meadow"},    {"Mnr", "Manor"},     {"Rdg", "Ridge"},     {"Shr", "Shore"},     {"Spg", "Spring"},
    {"Sta", "Station"},   {"Smt", "Summit"},    {"Trce", "Trace"},    {"Vly", "Valley"},    {"Vis", "Vista"},
    {"Vlg", "Village"},   {"Bch", "Beach"},     {"Brg", "Bridge"},    {"Cswy", "Causeway"}, {"Frk", "Fork"},
    {"Gdns", "Gardens"},  {"Ldg", "Lodge"},     {"Mtn", "Mountain"},  {"Plns", "Plains"},   {"Rnch", "Ranch"},
    {"Tunl", "Tunnel"},   {"Ests", "Estates"},  {"Pt", "Point"},
};

std::string MoreStreet(std::string const & abbreviation)
{
  for (auto const & w : kMoreStreetWords)
    if (w[0] == abbreviation)
      return w[1];
  return {};
}

bool StreetTypeWord(std::string const & w)
{
  static std::set<std::string> const more = {"Way", "Plaza", "Loop", "Row", "Walk", "Path", "Pike", "Run", "Alley", "Pass"};
  return StreetWord(w) || more.count(w) > 0 || !MoreStreet(w).empty();
}

/// True when what comes right before `pos` ends a street address, so a "Ste"/"Apt"/"Fl" there is a
/// suite or floor: a street type ("Lamar Blvd Ste 200"), a compass point after one ("Main St NW Ste
/// 2"), a route number ("Hwy 290 Ste 5") or another tail ("Bldg 4 Ste 200"). "Miami, Fl 33101" and
/// "Lake Rm 5" are left.
bool AfterStreetPart(std::string const & s, size_t pos)
{
  if (pos < 2 || !Space(s[pos - 1]))
    return false;
  size_t start = 0;
  auto const word = StreetPartBefore(s, pos, start);
  if (word.empty())
    return false;
  if (StreetTypeWord(word))
    return true;
  size_t before = 0;
  auto const previous = StreetPartBefore(s, start, before);
  if (CompassWord(word))
    return StreetTypeWord(previous);
  bool const value = std::isdigit(static_cast<unsigned char>(word[0])) ||
                     (word.size() == 1 && std::isupper(static_cast<unsigned char>(word[0])));
  return value && (TailWord(previous) || StreetTypeWord(previous));
}

/// "Ste 200", "Apt B", "Bldg 4", "Fl 3", "Hngr 7" after a street are Suite, Apartment, Building,
/// Floor, Hangar; a "#" between goes ("Ste #300" is "Suite 300"; Symbols has already made it "number
/// "). Runs after Titles, so "Ste.
/// Genevieve" is already Saint.
std::string AddressTails(std::string const & s)
{
  static std::regex const tail(
      "\\b(Ste|Apt|Bldg|Flr|Fl|Rm|Spc|Dept|Ofc|Trlr|Hngr|Unit)\\.? (?:#|number )?(?=(?:\\d{1,5}[A-Z]?|[A-Z])(?:$|[\\s,;:!?.)]))");
  return Replace(s, tail, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (!AfterStreetPart(s, pos))
      return std::nullopt;
    return TailWords().at(m.str(1)) + " ";
  });
}

/// The USPS suffixes in kMoreStreetWords after a street's name: at the end, before punctuation, a
/// lowercase word, a compass point, a suite or "number 5", or another street type ("Forest Hts Drive",
/// "Lake Vw Ln"). A period after one stays when it may end the sentence ("Continue on Oak Hl.") and goes
/// when the sentence plainly carries on ("Elm Crk., then left"). Runs after the street pass.
std::string MoreStreetTypes(std::string const & s)
{
  static std::regex const word(
      "\\b(Hts|Plz|Ctr|Tpke|Hl|Vw|Bnd|Crk|Cyn|Gln|Grv|Hbr|Holw|Jct|Lk|Mdw|Mnr|Rdg|Shr|Spg|Sta|Smt|Trce|Vly|Vis|"
      "Vlg|Bch|Brg|Cswy|Frk|Gdns|Ldg|Mtn|Plns|Rnch|Tunl|Ests|Pt)(\\.?)(?=$|[\\s,;:!?.)])");
  return Replace(s, word, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (!StreetNameBefore(s, pos))
      return std::nullopt;
    size_t const after = pos + static_cast<size_t>(m.length(0));
    auto const out = MoreStreet(m.str(1));
    if (after >= s.size())
      return out + m.str(2);
    char const next = s[after];
    if (next == ',' || next == ';' || next == ':')
      return out;
    if (!Space(next))
      return out + m.str(2);
    size_t start = after;
    while (start < s.size() && Space(s[start]))
      ++start;
    size_t end = start;
    while (end < s.size() && std::isalnum(static_cast<unsigned char>(s[end])))
      ++end;
    auto const following = s.substr(start, end - start);
    bool const carriesOn = !following.empty() &&
                           (std::islower(static_cast<unsigned char>(following[0])) || CompassWord(following) ||
                            StreetTypeWord(following) || TailWord(following) ||
                            (following == "number" && end < s.size() && s[end] == ' '));
    if (carriesOn)
      return out;
    // "Oak Hl. Then right" ends a sentence; "Pine Crk Mall" isn't a street type.
    return m[2].length() > 0 ? std::optional<std::string>(out + ".") : std::nullopt;
  });
}
}  // namespace
}  // namespace backseat::pronunciation
