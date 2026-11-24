#include "backseat/road_match.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <set>
#include <vector>

namespace backseat
{
namespace
{
std::vector<std::string> Tokens(std::string const & text)
{
  std::vector<std::string> out;
  std::string word;
  for (char const ch : text + " ")
  {
    if (std::isalnum(static_cast<unsigned char>(ch)))
    {
      word += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      continue;
    }
    if (!word.empty())
      out.push_back(word);
    word.clear();
  }
  // "I49", "US71": the letters and the number apart.
  std::vector<std::string> split;
  for (auto const & w : out)
  {
    size_t const digit = w.find_first_of("0123456789");
    if (digit > 0 && digit != std::string::npos && w.find_first_not_of("0123456789", digit) == std::string::npos &&
        digit <= 3)
    {
      split.push_back(w.substr(0, digit));
      split.push_back(w.substr(digit));
    }
    else
      split.push_back(w);
  }
  return split;
}

std::string Expanded(std::string const & w)
{
  static std::pair<char const *, char const *> const kShort[] = {
      {"st", "street"},     {"rd", "road"},        {"ave", "avenue"},    {"av", "avenue"},     {"blvd", "boulevard"},
      {"dr", "drive"},      {"ln", "lane"},        {"hwy", "highway"},   {"pkwy", "parkway"},  {"fwy", "freeway"},
      {"expy", "expressway"}, {"ct", "court"},     {"pl", "place"},      {"cir", "circle"},    {"tpke", "turnpike"},
      {"trl", "trail"},     {"interstate", "i"},   {"n", "north"},       {"s", "south"},       {"e", "east"},
      {"w", "west"},        {"mt", "mount"},       {"ft", "fort"},       {"hy", "highway"}};
  for (auto const & [from, to] : kShort)
    if (w == from)
      return to;
  return w;
}

/// Words that say what kind of road it is, not which one.
bool KindWord(std::string const & w)
{
  static char const * const kRoadKindWords[] = {"street", "road", "avenue", "boulevard", "drive", "lane", "highway",
                                        "parkway", "freeway", "expressway", "court", "place", "circle", "turnpike",
                                        "pike", "trail", "way", "route", "bypass", "loop", "the", "state", "county",
                                        "north", "south", "east", "west", "jr"};
  return std::any_of(std::begin(kRoadKindWords), std::end(kRoadKindWords), [&w](char const * k) { return w == k; });
}

bool Number(std::string const & w) { return !w.empty() && std::all_of(w.begin(), w.end(), ::isdigit); }

std::set<std::string> KeyWords(std::string const & text)
{
  std::set<std::string> keys;
  for (auto const & t : Tokens(text))
    if (auto const w = Expanded(t); !KindWord(w))
      keys.insert(w);
  return keys;
}
}  // namespace

bool RoadNameMatches(std::string const & spoken, std::string const & name, std::string const & ref)
{
  auto const said = KeyWords(spoken);
  if (said.empty())
    return false;
  if (!name.empty() && said == KeyWords(name))
    return true;
  // "Highway 71" for "US 71", "I-49" for "I 49": the number has to be on the sign, and nothing said
  // that isn't. Refs can list several roads ("US 62;AR 45").
  bool const hasNumber = std::any_of(said.begin(), said.end(), Number);
  if (!hasNumber || ref.empty())
    return false;
  std::string one;
  for (char const ch : ref + ";")
  {
    if (ch != ';')
    {
      one += ch;
      continue;
    }
    auto const sign = KeyWords(one);
    one.clear();
    if (std::all_of(said.begin(), said.end(), [&sign](std::string const & w) { return sign.count(w) > 0; }))
      return true;
  }
  return false;
}

std::optional<std::pair<size_t, std::string>> AvoidRoadAsk(std::string const & words)
{
  for (char const * here : {" avoid this road ", " avoid this street ", " avoid this highway ", " get me off this road ",
                            " get us off this road ", " get off this road ", " stay off this road ",
                            " keep me off this road ", " get me off this street ", " get me off this highway ",
                            " i hate this road ", " take me off this road "})
    if (auto const at = words.find(here); at != std::string::npos)
      return std::make_pair(at, std::string{});

  static char const * const kCues[] = {" avoid ", " stay off ", " keep me off ", " keep us off ", " get me off ",
                                       " get us off ", " skip ", " stay away from "};
  static char const * const kNumbered[] = {"highway", "hwy", "route", "interstate", "i", "us", "state", "county", "sr",
                                           "ar", "ca", "tx"};
  static char const * const kEnds[] = {"street", "st", "road", "rd", "avenue", "ave", "boulevard", "blvd", "drive",
                                       "lane", "parkway", "pkwy", "turnpike", "pike", "trail", "bypass", "expressway",
                                       "freeway", "way", "circle", "court", "loop"};
  static char const * const kNotRoads[] = {"toll", "tolls", "highways", "freeways", "traffic", "left", "lefts", "turns",
                                           "exit", "exits", "stop", "stops", "this", "that", "it", "them", "construction"};
  auto const in = [](char const * const * list, size_t n, std::string const & w)
  { return std::any_of(list, list + n, [&w](char const * s) { return w == s; }); };

  for (char const * cue : kCues)
  {
    size_t at = words.find(cue);
    while (at != std::string::npos)
    {
      std::vector<std::string> after;
      {
        std::string rest = words.substr(at + std::strlen(cue));
        std::string word;
        for (char const ch : rest)
        {
          if (ch != ' ')
          {
            word += ch;
            continue;
          }
          if (!word.empty())
            after.push_back(word);
          word.clear();
          if (after.size() >= 6)
            break;
        }
      }
      // "I avoid Main Street on Fridays", "we usually stay off highway 71": a habit, not an order.
      {
        size_t const wordEnd = at;
        size_t const wordStart = words.rfind(' ', wordEnd > 0 ? wordEnd - 1 : 0);
        std::string const prev = wordStart == std::string::npos || wordEnd <= wordStart
                                     ? std::string{}
                                     : words.substr(wordStart + 1, wordEnd - wordStart - 1);
        static char const * const kHabit[] = {"i", "we", "they", "he", "she", "people", "usually", "always",
                                              "never", "normally", "often", "sometimes"};
        if (in(kHabit, std::size(kHabit), prev))
        {
          at = words.find(cue, at + 1);
          continue;
        }
      }
      size_t end = 0;
      for (size_t i = 0; i < after.size() && !end; ++i)
      {
        std::string const & w = after[i];
        if (w == "and" || w == "please" || w == "then" || w == "because" || w == "so" ||
            in(kNotRoads, std::size(kNotRoads), w))
          break;
        if (in(kNumbered, std::size(kNumbered), w) && i + 1 < after.size() && Number(after[i + 1]))
          end = i + 2;
        else if (Tokens(w).size() == 2 && Number(Tokens(w)[1]))  // "i49", "i-49"
          end = i + 1;
        else if (in(kEnds, std::size(kEnds), w))
          end = i + 1;
      }
      if (end)
      {
        std::string road;
        for (size_t i = 0; i < end; ++i)
          road += (road.empty() ? "" : " ") + after[i];
        if (!KeyWords(road).empty())
          return std::make_pair(at, road);
      }
      at = words.find(cue, at + 1);
    }
  }
  return std::nullopt;
}
}  // namespace backseat
