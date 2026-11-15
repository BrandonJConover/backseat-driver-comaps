#include "backseat/place_search.hpp"

#include "backseat/voice_command.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <sstream>

namespace backseat
{
namespace
{
std::vector<std::string> Split(std::string const & text)
{
  std::istringstream in(text);
  std::vector<std::string> out;
  for (std::string w; in >> w;)
    out.push_back(w);
  return out;
}

std::string Join(std::vector<std::string> const & words, size_t from = 0, size_t to = std::string::npos)
{
  std::string out;
  for (size_t i = from; i < std::min(to, words.size()); ++i)
    out += (out.empty() ? "" : " ") + words[i];
  return out;
}

/// Lowercase words with the punctuation gone, apostrophes kept and straightened ("Joe’s" -> "joe's").
/// A "#" before a number is dropped, and so is a period inside letters ("c.v.s.").
std::string Words(std::string const & text)
{
  std::string out;
  for (unsigned char const ch : StraightApostrophes(text))
  {
    char const c = std::isalnum(ch) || ch == '\'' ? static_cast<char>(std::tolower(ch)) : ' ';
    if (c != ' ' || (!out.empty() && out.back() != ' '))
      out += c;
  }
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  // Quotes around a name ("'joe's'") aren't apostrophes.
  auto words = Split(out);
  for (auto & w : words)
  {
    while (!w.empty() && w.front() == '\'')
      w.erase(0, 1);
    while (!w.empty() && w.back() == '\'')
      w.pop_back();
  }
  words.erase(std::remove(words.begin(), words.end(), std::string{}), words.end());
  return Join(words);
}

bool StartsWith(std::string const & s, std::string const & lead) { return s.rfind(lead, 0) == 0; }
bool EndsWith(std::string const & s, std::string const & tail)
{
  return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

bool IsStreetType(std::string const & w)
{
  static char const * const kTypes[] = {"street", "st", "avenue", "ave", "road", "rd", "drive", "dr", "lane", "ln",
                                        "boulevard", "blvd", "way", "court", "ct", "place", "pl", "circle", "cir",
                                        "highway", "hwy", "parkway", "pkwy", "terrace", "trail", "loop", "pike",
                                        "freeway", "expressway", "square", "row", "crossing", "alley"};
  return std::find_if(std::begin(kTypes), std::end(kTypes), [&](char const * t) { return w == t; }) != std::end(kTypes);
}

int const kNoNumber = -1;

int UnitValue(std::string const & w)
{
  static std::map<std::string, int> const kUnits = {
      {"zero", 0},     {"oh", 0},        {"o", 0},        {"one", 1},       {"two", 2},        {"three", 3},
      {"four", 4},     {"five", 5},      {"six", 6},      {"seven", 7},     {"eight", 8},      {"nine", 9},
      {"ten", 10},     {"eleven", 11},   {"twelve", 12},  {"thirteen", 13}, {"fourteen", 14},  {"fifteen", 15},
      {"sixteen", 16}, {"seventeen", 17}, {"eighteen", 18}, {"nineteen", 19}};
  auto const it = kUnits.find(w);
  return it == kUnits.end() ? kNoNumber : it->second;
}

int TensValue(std::string const & w)
{
  static std::map<std::string, int> const kTens = {{"twenty", 20}, {"thirty", 30}, {"forty", 40}, {"fourty", 40},
                                                   {"fifty", 50},  {"sixty", 60},  {"seventy", 70}, {"eighty", 80},
                                                   {"ninety", 90}};
  auto const it = kTens.find(w);
  return it == kTens.end() ? kNoNumber : it->second;
}

bool IsNumberWord(std::string const & w)
{
  return UnitValue(w) != kNoNumber || TensValue(w) != kNoNumber || w == "hundred" || w == "thousand";
}

int OrdinalUnit(std::string const & w)
{
  static std::map<std::string, int> const kOrdinals = {
      {"first", 1},        {"second", 2},       {"third", 3},        {"fourth", 4},        {"fifth", 5},
      {"sixth", 6},        {"seventh", 7},      {"eighth", 8},       {"ninth", 9},         {"tenth", 10},
      {"eleventh", 11},    {"twelfth", 12},     {"thirteenth", 13},  {"fourteenth", 14},   {"fifteenth", 15},
      {"sixteenth", 16},   {"seventeenth", 17}, {"eighteenth", 18},  {"nineteenth", 19},   {"twentieth", 20},
      {"thirtieth", 30},   {"fortieth", 40},    {"fiftieth", 50},    {"sixtieth", 60},     {"seventieth", 70},
      {"eightieth", 80},   {"ninetieth", 90},   {"hundredth", 100}};
  auto const it = kOrdinals.find(w);
  return it == kOrdinals.end() ? kNoNumber : it->second;
}

std::string OrdinalDigits(int n)
{
  int const lastTwo = n % 100;
  char const * suffix = lastTwo >= 11 && lastTwo <= 13 ? "th"
                        : n % 10 == 1                  ? "st"
                        : n % 10 == 2                  ? "nd"
                        : n % 10 == 3                  ? "rd"
                                                       : "th";
  return std::to_string(n) + suffix;
}

/// The digits for a run of number words, as a house number is said: "one two three" and "one twenty
/// three" are 123, "fifteen oh five" is 1505, "twenty one hundred" is 2100, "four hundred twelve" 412.
std::string HouseNumber(std::vector<std::string> const & run)
{
  bool const counted = std::any_of(run.begin(), run.end(), [](auto const & w) { return w == "hundred" || w == "thousand"; });
  if (counted)
  {
    long total = 0, current = 0;
    for (auto const & w : run)
    {
      if (w == "hundred")
        current = (current == 0 ? 1 : current) * 100;
      else if (w == "thousand")
        total += (current == 0 ? 1 : current) * 1000, current = 0;
      else if (TensValue(w) != kNoNumber)
        current += TensValue(w);
      else
        current += UnitValue(w);
    }
    return std::to_string(total + current);
  }
  // Said in pieces: each tens word takes the unit after it ("twenty three"), everything else is its own digits.
  std::string digits;
  for (size_t i = 0; i < run.size(); ++i)
  {
    if (int const tens = TensValue(run[i]); tens != kNoNumber)
    {
      int unit = 0;
      if (i + 1 < run.size() && UnitValue(run[i + 1]) != kNoNumber && UnitValue(run[i + 1]) < 10 &&
          UnitValue(run[i + 1]) > 0)
        unit = UnitValue(run[++i]);
      digits += std::to_string(tens + unit);
    }
    else
    {
      digits += std::to_string(UnitValue(run[i]));
    }
  }
  return digits;
}

/// "o" is a number only next to other number words ("fifteen o five"), never alone.
bool NumberRunWord(std::vector<std::string> const & words, size_t i)
{
  if (words[i] != "o")
    return IsNumberWord(words[i]);
  return i > 0 && IsNumberWord(words[i - 1]) && i + 1 < words.size() && IsNumberWord(words[i + 1]);
}

/// Spoken numbers in an address in digits: the house number at the start, and a street ordinal before
/// its street type. Anything that isn't an address is left alone.
std::vector<std::string> AddressDigits(std::vector<std::string> words)
{
  bool const address = std::any_of(words.begin(), words.end(), IsStreetType);
  if (!address)
    return words;
  // "Forty second street", "fifth avenue": the ordinal goes to digits.
  for (size_t i = 0; i < words.size(); ++i)
  {
    int const ordinal = OrdinalUnit(words[i]);
    if (ordinal == kNoNumber || i + 1 >= words.size() || !IsStreetType(words[i + 1]))
      continue;
    int value = ordinal;
    size_t from = i;
    if (ordinal < 10 && i > 0 && TensValue(words[i - 1]) != kNoNumber)
      value += TensValue(words[i - 1]), from = i - 1;
    words.erase(words.begin() + from, words.begin() + i + 1);
    words.insert(words.begin() + from, OrdinalDigits(value));
    i = from;
  }
  // The house number: two or more number words at the start, before the street name. One alone is
  // usually part of the name ("Five Points Road", "Twelve Oaks Lane").
  size_t end = 0;
  while (end < words.size() && NumberRunWord(words, end))
    ++end;
  if (end >= 2 && end < words.size())
  {
    std::vector<std::string> const run(words.begin(), words.begin() + end);
    words.erase(words.begin(), words.begin() + end);
    words.insert(words.begin(), HouseNumber(run));
  }
  return words;
}

/// Letters said one at a time come back as single letters: "c v s" -> "cvs", "i hop" stays.
std::vector<std::string> JoinSpelledLetters(std::vector<std::string> const & words)
{
  std::vector<std::string> out;
  for (size_t i = 0; i < words.size();)
  {
    size_t j = i;
    while (j < words.size() && words[j].size() == 1 && std::isalpha(static_cast<unsigned char>(words[j][0])))
      ++j;
    // Two letters or more in a row; "a" alone is an article.
    if (j - i >= 2)
    {
      std::string joined;
      for (size_t k = i; k < j; ++k)
        joined += words[k];
      out.push_back(joined);
      i = j;
    }
    else
    {
      out.push_back(words[i++]);
    }
  }
  return out;
}

std::string Normalized(std::string w)
{
  w.erase(std::remove(w.begin(), w.end(), '\''), w.end());
  std::string out;
  for (size_t i = 0; i < w.size(); ++i)
  {
    // Sounds spelled two ways.
    if (w.compare(i, 2, "ph") == 0)
    {
      out += 'f';
      ++i;
      continue;
    }
    if (w.compare(i, 2, "ck") == 0)
    {
      out += 'k';
      ++i;
      continue;
    }
    // Doubled letters: "jimmy"/"jimy".
    if (!out.empty() && out.back() == w[i])
      continue;
    out += w[i];
  }
  return out;
}

size_t EditDistance(std::string const & a, std::string const & b)
{
  // Optimal string alignment: a swapped pair counts as one slip ("chiptole").
  std::vector<std::vector<size_t>> d(a.size() + 1, std::vector<size_t>(b.size() + 1));
  for (size_t i = 0; i <= a.size(); ++i)
    d[i][0] = i;
  for (size_t j = 0; j <= b.size(); ++j)
    d[0][j] = j;
  for (size_t i = 1; i <= a.size(); ++i)
    for (size_t j = 1; j <= b.size(); ++j)
    {
      size_t const cost = a[i - 1] == b[j - 1] ? 0 : 1;
      d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
      if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
        d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
    }
  return d[a.size()][b.size()];
}

/// How well `name` answers `query`: 2 when every meaningful word asked for is in it (give or take
/// the spelling), 1 when PlaceMatches says it's close enough, 0 when it isn't it.
int NameTier(std::string const & query, std::string const & name)
{
  if (!PlaceMatches(query, name))
    return 0;
  static char const * const kSmall[] = {"a", "an", "the", "to", "of", "in", "on", "at", "and", "or", "for", "by", "near"};
  auto const nameWords = Split(Words(name));
  std::string squashed;
  for (auto const & w : nameWords)
    squashed += w;
  for (auto const & word : Split(Words(query)))
  {
    if (std::find_if(std::begin(kSmall), std::end(kSmall), [&](char const * s) { return word == s; }) != std::end(kSmall))
      continue;
    if (squashed.find(word) == std::string::npos &&
        std::none_of(nameWords.begin(), nameWords.end(), [&](auto const & n) { return WordsClose(word, n); }))
      return 1;
  }
  return 2;
}

double MetersBetween(LatLon a, LatLon b)
{
  double constexpr kEarthRadiusM = 6371000;
  double constexpr kRad = 3.14159265358979323846 / 180;
  double const dLat = (b.lat - a.lat) * kRad;
  double const dLon = (b.lon - a.lon) * kRad;
  double const h = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(a.lat * kRad) * std::cos(b.lat * kRad) * std::sin(dLon / 2) * std::sin(dLon / 2);
  return 2 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(h)));
}
}  // namespace

std::string SearchableQuery(std::string const & spoken)
{
  std::string q = " " + Words(spoken) + " ";
  for (bool trimmed = true; trimmed;)
  {
    trimmed = false;
    for (std::string const lead : {" um ", " umm ", " uh ", " uhh ", " er ", " like ", " so ", " okay ", " ok ",
                                   " a place called ", " the place called ", " this place called ",
                                   " that place called ", " somewhere called ", " something called ", " a ", " an ",
                                   " the ", " some ", " back to ", " over to ", " to "})
      if (StartsWith(q, lead) && q.size() > lead.size())
      {
        q.erase(0, lead.size() - 1);
        trimmed = true;
      }
    for (std::string const tail : {" please ", " thanks ", " thank you ", " near me ", " nearby ", " around here ",
                                   " close by ", " near here ", " in town ", " real quick ", " right now ", " now ",
                                   " or something ", " i think ", " for me ", " um ", " uh "})
      if (EndsWith(q, tail) && q.size() > tail.size())
      {
        q.erase(q.size() - tail.size() + 1);
        trimmed = true;
      }
  }
  auto words = AddressDigits(JoinSpelledLetters(Split(q)));
  return Join(words);
}

std::vector<std::string> QueryVariants(std::string const & query)
{
  std::vector<std::string> out;
  auto const add = [&](std::string v)
  {
    if (!v.empty() && v != query && std::find(out.begin(), out.end(), v) == out.end())
      out.push_back(std::move(v));
  };
  auto const words = Split(query);
  // "Starbucks on Main": the search wants the name; the nearest one is usually it.
  for (size_t i = 1; i + 1 < words.size(); ++i)
    if (words[i] == "on" || words[i] == "by" || words[i] == "near" || words[i] == "off" || words[i] == "at")
    {
      add(Join(words, 0, i));
      break;
    }
  // "Star bucks", "wal mart": one word heard as two.
  for (size_t i = 0; i + 1 < words.size(); ++i)
  {
    auto const & a = words[i];
    auto const & b = words[i + 1];
    bool const letters = std::all_of(a.begin(), a.end(), ::isalpha) && std::all_of(b.begin(), b.end(), ::isalpha);
    if (letters && a.size() >= 2 && b.size() >= 2 && a.size() + b.size() <= 10 && a != "on" && b != "on" &&
        a != "at" && b != "at")
    {
      auto joined = words;
      joined[i] += joined[i + 1];
      joined.erase(joined.begin() + static_cast<long>(i) + 1);
      add(Join(joined));
      break;
    }
  }
  // "Mcdonalds" is "McDonald's", "joe's" may be "Joes".
  {
    auto changed = words;
    bool any = false;
    for (auto & w : changed)
    {
      if (w.find('\'') != std::string::npos)
      {
        w.erase(std::remove(w.begin(), w.end(), '\''), w.end());
        any = true;
      }
      else if (w.size() >= 5 && w.back() == 's' && w[w.size() - 2] != 's' &&
               std::all_of(w.begin(), w.end(), ::isalpha))
      {
        w.insert(w.size() - 1, "'");
        any = true;
        break;
      }
    }
    if (any)
      add(Join(changed));
  }
  // "Five Guys" is "5 Guys" on the map; "seven eleven" is "7-Eleven".
  for (size_t i = 0; i < words.size(); ++i)
    if (UnitValue(words[i]) > 0 || TensValue(words[i]) != kNoNumber)
    {
      auto digits = words;
      digits[i] = std::to_string(UnitValue(words[i]) > 0 ? UnitValue(words[i]) : TensValue(words[i]));
      add(Join(digits));
      break;
    }
  // "42nd street" may be "Forty-Second Street".
  for (size_t i = 0; i + 1 < words.size(); ++i)
  {
    auto const & w = words[i];
    if (w.size() < 3 || !IsStreetType(words[i + 1]) || !std::isdigit(static_cast<unsigned char>(w[0])))
      continue;
    std::string const suffix = w.substr(w.size() - 2);
    if (suffix != "st" && suffix != "nd" && suffix != "rd" && suffix != "th")
      continue;
    int const n = std::atoi(w.c_str());
    static char const * const kOrd[] = {"",        "first",   "second",  "third",   "fourth",
                                        "fifth",   "sixth",   "seventh", "eighth",  "ninth",
                                        "tenth",   "eleventh", "twelfth", "thirteenth", "fourteenth",
                                        "fifteenth", "sixteenth", "seventeenth", "eighteenth", "nineteenth"};
    if (n > 0 && n < 20)
    {
      auto spelled = words;
      spelled[i] = kOrd[n];
      add(Join(spelled));
    }
    break;
  }
  if (out.size() > 4)
    out.resize(4);
  return out;
}

bool WordsClose(std::string const & a, std::string const & b)
{
  std::string const x = Normalized(a);
  std::string const y = Normalized(b);
  if (x == y)
    return true;
  if (x.empty() || y.empty() || x[0] != y[0])
    return false;
  size_t const shorter = std::min(x.size(), y.size());
  // Short words have too many neighbors: "cafe" and "care", "shop" and "shoe".
  size_t const budget = shorter < 5 ? 0 : shorter < 8 ? 1 : 2;
  if (budget == 0)
    return false;
  size_t const longer = std::max(x.size(), y.size());
  if (longer - shorter > budget)
    return false;
  return EditDistance(x, y) <= budget;
}

std::vector<size_t> RankSearchResults(std::string const & query, std::vector<PlaceCandidate> const & candidates,
                                      std::vector<PastPlace> const & past)
{
  double constexpr kSamePlaceM = 250;
  struct Key
  {
    int named;
    bool familiar;
    double distanceM;
  };
  std::vector<Key> keys;
  for (auto const & c : candidates)
  {
    bool familiar = false;
    if (c.at)
      for (auto const & p : past)
        if (MetersBetween(*c.at, p.to) <= kSamePlaceM)
        {
          familiar = true;
          break;
        }
    double const d = std::isfinite(c.distanceM) && c.distanceM >= 0 ? c.distanceM : 1e12;
    keys.push_back({NameTier(query, c.name), familiar, d});
  }
  std::vector<size_t> order(candidates.size());
  for (size_t i = 0; i < order.size(); ++i)
    order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
  {
    auto const & x = keys[a];
    auto const & y = keys[b];
    if (x.named != y.named)
      return x.named > y.named;
    if (x.familiar != y.familiar)
      return x.familiar;
    return x.distanceM < y.distanceM;
  });
  return order;
}

std::optional<PastPlace> PastPlaceNamed(std::string const & query, std::vector<PastPlace> const & past)
{
  std::optional<PastPlace> best;
  // Nothing meaningful asked: everything would match.
  auto const words = Split(SearchableQuery(query));
  if (words.empty())
    return {};
  for (auto const & p : past)
  {
    if (p.destination.empty() || !PlaceMatches(query, p.destination))
      continue;
    if (!best || p.trips > best->trips ||
        (p.trips == best->trips && p.lastEndedAt > best->lastEndedAt))
      best = p;
  }
  return best;
}
}  // namespace backseat
