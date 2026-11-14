#pragma once

// Internal to libs/backseat: the street passes (street types, compass points, Saint/Doctor/Fort titles,
// address tails, the rarer USPS suffixes) and their helpers, used by ForSpeech() and the route passes in
// pronunciation.cpp. Included only by pronunciation.cpp (internal linkage); moved out of it unchanged
// (loop B104). Not public API; hosts include pronunciation.hpp.

#include <cctype>
#include <functional>
#include <initializer_list>
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
bool HasLetterWord(std::string_view s, std::initializer_list<std::string_view> words);
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

// MARK: - County and township roads

/// "County Road K" and "County Highway N" name the road by a letter, not a compass point. True when the word
/// "Road"/"Highway" that starts at `start` follows "County " or "Township " (loop B106).
bool CountyRoadBefore(std::string const & s, size_t start)
{
  for (std::string_view const prefix : {"County ", "Township "})
    if (start >= prefix.size() && s.compare(start - prefix.size(), prefix.size(), prefix) == 0)
      return true;
  return false;
}

/// A route number right before position `pos` (which starts a street type after one space): "FM 1960 Rd",
/// "Old Rte 9 Rd", "Hwy 6 Rd", "SR 7 Ave", "County Rd 12 St", "Exit 12 St", "I-35 Dr" (loop B141). A bare
/// house number ("3502 St. Johns Rd", "Arrive at 1960 Rd W") has no route word before it, so it doesn't count.
bool RouteNumberBefore(std::string const & s, size_t pos)
{
  if (pos < 2 || s[pos - 1] != ' ')
    return false;
  size_t end = pos - 1;  // one past the number token
  size_t start = end;
  while (start > 0 && !Space(s[start - 1]))
    --start;
  std::string const token = s.substr(start, end - start);
  // "I-35", "SR-7", "CA-1": the route word glued on with a hyphen.
  static std::regex const glued("[A-Z]{1,3}-\\d{1,4}[A-Z]?");
  if (std::regex_match(token, glued))
    return true;
  static std::regex const number("\\d{1,4}[A-Z]?");
  if (!std::regex_match(token, number) || start < 2 || s[start - 1] != ' ')
    return false;
  size_t wordStart = start - 1;
  while (wordStart > 0 && !Space(s[wordStart - 1]))
    --wordStart;
  std::string word = s.substr(wordStart, start - 1 - wordStart);
  if (!word.empty() && word.back() == '.')
    word.pop_back();
  static std::set<std::string> const routeWords = {
      "Hwy", "Highway", "Rte", "Route", "Rd", "Road", "FM", "RM", "RR", "SR", "SH", "US", "CR", "Exit",
      "Interstate", "I", "Fwy", "Freeway", "Loop", "Spur", "Pkwy", "Parkway", "Expy", "Expressway"};
  return routeWords.count(word) > 0;
}

/// Lettered county roads and the Co/Twp prefixes the Midwest signs use (loop B106): "County Rd K" is
/// "County Road K", "Co Rd K" and "CR-K" too, "Hwy KK" "Highway K K", "Twp Rd 12" "Township Road 12",
/// "Co. Rd. 45" "County Road 45". "Smith & Sons Co.", "CR-V", "Hwy US 10" and "Hwy N" are left.
std::string CountyRoads(std::string s)
{
  if (s.find("Co") == std::string::npos && s.find("Twp") == std::string::npos &&
      s.find("CR") == std::string::npos && s.find("Hwy") == std::string::npos &&
      s.find("Highway") == std::string::npos)
    return s;
  // A road letter (or two) as a whole word: not a route ("US 10", "I-35"), not the start of a word.
#define BACKSEAT_ROAD_LETTERS "([A-Z]{1,2})(?![A-Za-z0-9'-]| ?-? ?\\d)"
  // "Co Rd", "Co. Rd.", "Twp Hwy": the prefix only counts right before a road type.
  static std::regex const prefix("\\b(Co|Twp)\\.? (?=(?:Rd|Road|Hwy|Highway)\\b)");
  s = Replace(s, prefix, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    return std::string(m.str(1) == "Co" ? "County " : "Township ");
  });
  // "County Rd 45", "Township Hwy 7": the road type in full once the prefix says what it is.
  static std::regex const type("\\b(County|Township) (Rd|Hwy)\\.?(?= )");
  s = Replace(s, type, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    return m.str(1) + (m.str(2) == "Rd" ? " Road" : " Highway");
  });
  // Spell a road's letters apart: "KK" is "K K".
  auto const letters = [](std::string const & l) {
    return l.size() == 2 ? std::string(1, l[0]) + " " + l[1] : l;
  };
  // "CR-K", "CR K": County Road by letter (by number is the route pass's). Not the Honda "CR-V"/"CR-Z".
  static std::regex const cr("\\bCR(?:-| )" BACKSEAT_ROAD_LETTERS);
  s = Replace(s, cr, [&](std::smatch const & m, size_t) -> std::optional<std::string> {
    if (m.str(1) == "V" || m.str(1) == "Z")
      return std::nullopt;
    return "County Road " + letters(m.str(1));
  });
  // "County Road KK", "County Highway PD", "Hwy KK": the letters apart. A bare highway with a compass
  // point after it ("Hwy N", "Highway SW") may be a direction, so it is left.
  static std::regex const lettered("\\b((?:County|Township) (?:Road|Highway)|Hwy|Highway) " BACKSEAT_ROAD_LETTERS);
#undef BACKSEAT_ROAD_LETTERS
  s = Replace(s, lettered, [&](std::smatch const & m, size_t) -> std::optional<std::string> {
    auto const road = m.str(1), l = m.str(2);
    bool const bare = road == "Hwy" || road == "Highway";
    static std::set<std::string> const compass = {"N", "S", "E", "W", "NE", "NW", "SE", "SW"};
    if (bare && compass.count(l) > 0)
      return std::nullopt;
    return (road == "Hwy" ? std::string("Highway") : road) + " " + letters(l);
  });
  return s;
}

/// Rank and office titles with their period, before a capitalised name (loop B140, CROSS X-052): "Capt. D's"
/// is "Captain D's", "Col. Glenn Hwy" "Colonel Glenn Highway", "Lt. Col. Smith" "Lieutenant Colonel Smith".
/// Only with the period and a capitalised word after it: "col." lowercase, "Gen. 3" and "Capt. I said" stay,
/// and so does a title before a word that likely starts a new sentence ("He was Capt. Then he left").
std::string DottedTitles(std::string s)
{
  if (s.find(". ") == std::string::npos ||
      !HasLetterWord(s, {"Capt", "Lt", "Col", "Sgt", "Gen", "Maj", "Cpl", "Adm", "Cmdr", "Prof", "Rev", "Insp",
                         "Supt", "Gov", "Sen", "Pres", "Msgr", "Fr"}))
    return s;
  static std::map<std::string, std::string> const words = {
      {"Capt", "Captain"},   {"Lt", "Lieutenant"}, {"Col", "Colonel"},    {"Sgt", "Sergeant"},
      {"Gen", "General"},    {"Maj", "Major"},     {"Cpl", "Corporal"},   {"Adm", "Admiral"},
      {"Cmdr", "Commander"}, {"Prof", "Professor"}, {"Rev", "Reverend"},  {"Insp", "Inspector"},
      {"Supt", "Superintendent"}, {"Gov", "Governor"}, {"Sen", "Senator"}, {"Pres", "President"},
      {"Msgr", "Monsignor"}, {"Fr", "Father"}};
  // Words that start a sentence far more often than they follow a title.
  static std::set<std::string> const starters = {
      "I", "The", "Then", "He", "She", "It", "We", "They", "You", "This", "That", "There", "These", "Those",
      "His", "Her", "Its", "Our", "Their", "My", "Your", "An", "And", "But", "So", "Now", "When", "If"};
  static std::regex const title(
      "\\b(Capt|Lt|Col|Sgt|Gen|Maj|Cpl|Adm|Cmdr|Prof|Rev|Insp|Supt|Gov|Sen|Pres|Msgr|Fr)\\. (?=[A-Z](?:[a-z]|'[A-Za-z]))");
  return Replace(s, title, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    size_t const next = pos + static_cast<size_t>(m.length(0));
    size_t end = next;
    while (end < s.size() && IsAsciiLetter(s[end]))
      ++end;
    if (starters.count(s.substr(next, end - next)) > 0)
      return std::nullopt;
    return words.at(m.str(1)) + " ";
  });
}

/// "St. Johns Rd" is "Saint Johns Rd", "Dr Martin Luther King Jr Blvd" is "Doctor Martin Luther
/// King Junior Blvd", "Ft Worth" is "Fort Worth", "Hwy 290" is "Highway 290", "101 Fwy" is "101
/// Freeway". "Main St. Then left" and "Elm Dr." stay street types for the street pass.
/// A "St" after a capitalised word is still Saint when a name and a street type follow ("Down St.
/// James Street").
std::string Titles(std::string s)
{
  s = DottedTitles(std::move(s));
  static std::regex const title("\\b(St|Ste|Dr|Ft)\\.? (?=[A-Z][a-z])");
  if (HasLetterWord(s, {"St", "Ste", "Dr", "Ft"}))
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
  if (HasLetterWord(s, {"Jr", "Sr"}))
    s = Replace(s, suffix, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (pos == 0 || !IsAsciiLetter(s[pos - 1]))
      return std::nullopt;
    size_t start = pos;
    while (start > 0 && IsAsciiLetter(s[start - 1]))
      --start;
    if (!std::isupper(static_cast<unsigned char>(s[start])))
      return std::nullopt;
    std::string out = m.str(1) + (m.str(2) == "Jr" ? "Junior" : "Senior");
    size_t next = pos + static_cast<size_t>(m.length(0));
    // "Ken Griffey, Sr., played": the comma already pauses, so the period goes (loop B140).
    if (m[3].length() > 0 && next < s.size() && (s[next] == ',' || s[next] == ';' || s[next] == ':'))
      return out;
    if (m[3].length() > 0)
    {
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
  if (!HasLetterWord(s, {"Hwy", "Fwy", "Expy", "Pkwy", "Rte", "Rd"}))
    return s;
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
  if (!HasLetterWord(s, {"Ste", "Apt", "Bldg", "Flr", "Fl", "Rm", "Spc", "Dept", "Ofc", "Trlr", "Hngr", "Unit"}))
    return s;
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
  if (!HasLetterWord(s, {"Hts", "Plz", "Ctr", "Tpke", "Hl", "Vw", "Bnd", "Crk", "Cyn", "Gln", "Grv", "Hbr", "Holw",
                          "Jct", "Lk", "Mdw", "Mnr", "Rdg", "Shr", "Spg", "Sta", "Smt", "Trce", "Vly", "Vis", "Vlg",
                          "Bch", "Brg", "Cswy", "Frk", "Gdns", "Ldg", "Mtn", "Plns", "Rnch", "Tunl", "Ests", "Pt"}))
    return s;
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

// MARK: - Place-name abbreviations (loop B108)

/// Abbreviations inside place names: "Austin-Bergstrom Intl Airport", "St. Luke's Med. Ctr.", "Univ. of
/// Texas", "Acme Inc.". Lead words sit inside a name and drop their period before the next name word;
/// tail words end one and keep a period that may end the sentence ("Arriving at Acme Inc.").
struct PlaceWord
{
  char const * abbreviation;
  char const * word;
  bool tail;
};

PlaceWord const kPlaceWords[] = {
    {"Intl", "International", false}, {"Natl", "National", false},  {"Univ", "University", false},
    {"Med", "Medical", false},         {"Mem", "Memorial", false},   {"Elem", "Elementary", false},
    {"Comm", "Community", false},      {"Dept", "Department", false}, {"Hosp", "Hospital", true},
    {"Sch", "School", true},           {"Arpt", "Airport", true},     {"Inc", "Incorporated", true},
    {"Corp", "Corporation", true},     {"Co", "Company", true},
};

/// The word right before `pos` (skipping spaces and one comma, "Acme, Inc."), or empty.
std::string PlaceWordBefore(std::string const & s, size_t pos)
{
  while (pos > 0 && Space(s[pos - 1]))
    --pos;
  if (pos > 0 && s[pos - 1] == ',')
    --pos;
  while (pos > 0 && Space(s[pos - 1]))
    --pos;
  size_t start = pos;
  while (start > 0 && (std::isalnum(static_cast<unsigned char>(s[start - 1])) || s[start - 1] == '\''))
    --start;
  return s.substr(start, pos - start);
}

/// The word starting at `pos` after spaces, and where it ends.
std::string PlaceWordAfter(std::string const & s, size_t pos, size_t & end)
{
  while (pos < s.size() && Space(s[pos]))
    ++pos;
  end = pos;
  while (end < s.size() && (std::isalnum(static_cast<unsigned char>(s[end])) || s[end] == '\''))
    ++end;
  return s.substr(pos, end - pos);
}

bool UpperStart(std::string const & w)
{
  return !w.empty() && std::isupper(static_cast<unsigned char>(w[0]));
}

/// A capitalised name word, not a lead-in like "Head" or "Onto".
bool Capitalised(std::string const & w)
{
  return UpperStart(w) && !LeadInWord(Lower(w));
}

/// Expands kPlaceWords inside a name: a lead word needs a capitalised word on either side ("Veterans Mem
/// Park", "Univ. of Texas", "Intl Blvd"); a tail word needs a name before it ("Methodist Hosp", "Acme,
/// Inc.", "Smith and Sons Co.") and, for "Co", nothing named after it ("Weld Co Sheriff" may be a county;
/// "Co Rd" is CountyRoads'). Exact case only, so "med school" and "inc" stay. Runs after Titles and
/// AddressTails, before the street passes, so "Med. Ctr." is "Medical Center".
std::string PlaceNameWords(std::string const & s)
{
  static std::regex const word(
      "\\b(Intl|Natl|Univ|Med|Mem|Elem|Comm|Dept|Hosp|Sch|Arpt|Inc|Corp|Co)(\\.?)(?=$|[\\s,;:!?)])");
  if (!HasLetterWord(s, {"Intl", "Natl", "Univ", "Med", "Mem", "Elem", "Comm", "Dept", "Hosp", "Sch", "Arpt", "Inc",
                          "Corp", "Co"}))
    return s;
  return Replace(s, word, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (pos > 0 && (s[pos - 1] == '-' || s[pos - 1] == '.' || s[pos - 1] == '/'))
      return std::nullopt;
    PlaceWord const * place = nullptr;
    for (auto const & w : kPlaceWords)
      if (m.str(1) == w.abbreviation)
        place = &w;
    size_t const after = pos + static_cast<size_t>(m.length(0));
    size_t end = after;
    auto next = PlaceWordAfter(s, after, end);
    bool const spaceNext = after < s.size() && Space(s[after]);
    if (next == "of" && spaceNext)
    {
      size_t ofEnd = end;
      auto const named = PlaceWordAfter(s, end, ofEnd);
      if (Capitalised(named))
        next = named;
    }
    auto const before = PlaceWordBefore(s, pos);
    // "5th St Mem Hosp": an ordinal names a place too.
    bool const nameBefore = Capitalised(before) || (!before.empty() && std::isdigit(static_cast<unsigned char>(before[0])) &&
                                                    std::isalpha(static_cast<unsigned char>(before.back())));
    bool const nameAfter = spaceNext && Capitalised(next);
    if (place->tail)
    {
      bool const andBefore = before == "and" || before == "&";
      if (!nameBefore && !andBefore)
        return std::nullopt;
      if (m.str(1) == "Co" && nameAfter && m[2].length() == 0)
        return std::nullopt;
      // "Acme Inc. Then left" ends the sentence; "Acme Inc., then left" and "Acme Inc. and" carry on.
      bool const keepPeriod = m[2].length() > 0 && (after >= s.size() || (spaceNext && UpperStart(next)));
      return std::string(place->word) + (keepPeriod ? "." : "");
    }
    if (!nameBefore && !nameAfter)
      return std::nullopt;
    // "Med. Ctr." and "Univ. of Texas" carry on; "Lincoln Elem." may end the sentence.
    bool const keepPeriod = m[2].length() > 0 && !nameAfter && (after >= s.size() || !spaceNext || UpperStart(next));
    return std::string(place->word) + (keepPeriod ? "." : "");
  });
}
}  // namespace
}  // namespace backseat::pronunciation
