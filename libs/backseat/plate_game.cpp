#include "backseat/plate_game.hpp"

#include "backseat/lines.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace backseat
{
namespace
{
struct PlateRegion
{
  char const * code;
  char const * name;
  /// Other ways people (or speech recognition) say it.
  std::vector<char const *> aliases;
};

std::vector<PlateRegion> const kRegions = {
    {"AL", "Alabama", {}},
    {"AK", "Alaska", {}},
    {"AZ", "Arizona", {}},
    {"AR", "Arkansas", {}},
    {"CA", "California", {"cali"}},
    {"CO", "Colorado", {}},
    {"CT", "Connecticut", {}},
    {"DE", "Delaware", {}},
    {"DC", "Washington D.C.", {"washington dc", "dc", "d c", "district of columbia"}},
    {"FL", "Florida", {}},
    {"GA", "Georgia", {}},
    {"HI", "Hawaii", {}},
    {"ID", "Idaho", {}},
    {"IL", "Illinois", {}},
    {"IN", "Indiana", {}},
    {"IA", "Iowa", {}},
    {"KS", "Kansas", {}},
    {"KY", "Kentucky", {}},
    {"LA", "Louisiana", {}},
    {"ME", "Maine", {}},
    {"MD", "Maryland", {}},
    {"MA", "Massachusetts", {}},
    {"MI", "Michigan", {}},
    {"MN", "Minnesota", {}},
    {"MS", "Mississippi", {}},
    {"MO", "Missouri", {}},
    {"MT", "Montana", {}},
    {"NE", "Nebraska", {}},
    {"NV", "Nevada", {}},
    {"NH", "New Hampshire", {}},
    {"NJ", "New Jersey", {"jersey"}},
    {"NM", "New Mexico", {}},
    {"NY", "New York", {}},
    {"NC", "North Carolina", {}},
    {"ND", "North Dakota", {}},
    {"OH", "Ohio", {}},
    {"OK", "Oklahoma", {}},
    {"OR", "Oregon", {}},
    {"PA", "Pennsylvania", {}},
    {"RI", "Rhode Island", {}},
    {"SC", "South Carolina", {}},
    {"SD", "South Dakota", {}},
    {"TN", "Tennessee", {}},
    {"TX", "Texas", {}},
    {"UT", "Utah", {}},
    {"VT", "Vermont", {}},
    {"VA", "Virginia", {}},
    {"WA", "Washington", {"washington state"}},
    {"WV", "West Virginia", {}},
    {"WI", "Wisconsin", {}},
    {"WY", "Wyoming", {}},
    {"PR", "Puerto Rico", {}},
    {"AB", "Alberta", {}},
    {"BC", "British Columbia", {}},
    {"MB", "Manitoba", {}},
    {"NB", "New Brunswick", {}},
    {"NL", "Newfoundland", {"newfoundland and labrador", "labrador"}},
    {"NS", "Nova Scotia", {}},
    {"NT", "the Northwest Territories", {"northwest territories"}},
    {"NU", "Nunavut", {}},
    {"ON", "Ontario", {}},
    {"PE", "Prince Edward Island", {"pei", "p e i"}},
    {"QC", "Quebec", {"quebek"}},
    {"SK", "Saskatchewan", {}},
    {"YT", "Yukon", {"the yukon"}},
    {"MX", "Mexico", {"mexican"}},
};

/// Land borders, plus Alaska's with Canada. Listed once; looked up both ways.
char const * const kBorders[][2] = {
    {"AL", "FL"}, {"AL", "GA"}, {"AL", "MS"}, {"AL", "TN"}, {"AZ", "CA"}, {"AZ", "CO"}, {"AZ", "NM"}, {"AZ", "NV"},
    {"AZ", "UT"}, {"AZ", "MX"}, {"AR", "LA"}, {"AR", "MO"}, {"AR", "MS"}, {"AR", "OK"}, {"AR", "TN"}, {"AR", "TX"},
    {"CA", "NV"}, {"CA", "OR"}, {"CA", "MX"}, {"CO", "KS"}, {"CO", "NE"}, {"CO", "NM"}, {"CO", "OK"}, {"CO", "UT"},
    {"CO", "WY"}, {"CT", "MA"}, {"CT", "NY"}, {"CT", "RI"}, {"DE", "MD"}, {"DE", "NJ"}, {"DE", "PA"}, {"DC", "MD"},
    {"DC", "VA"}, {"FL", "GA"}, {"GA", "NC"}, {"GA", "SC"}, {"GA", "TN"}, {"ID", "MT"}, {"ID", "NV"}, {"ID", "OR"},
    {"ID", "UT"}, {"ID", "WA"}, {"ID", "WY"}, {"ID", "BC"}, {"IL", "IN"}, {"IL", "IA"}, {"IL", "KY"}, {"IL", "MO"},
    {"IL", "WI"}, {"IN", "KY"}, {"IN", "MI"}, {"IN", "OH"}, {"IA", "MN"}, {"IA", "MO"}, {"IA", "NE"}, {"IA", "SD"},
    {"IA", "WI"}, {"KS", "MO"}, {"KS", "NE"}, {"KS", "OK"}, {"KY", "MO"}, {"KY", "OH"}, {"KY", "TN"}, {"KY", "VA"},
    {"KY", "WV"}, {"LA", "MS"}, {"LA", "TX"}, {"ME", "NH"}, {"ME", "NB"}, {"ME", "QC"}, {"MD", "PA"}, {"MD", "VA"},
    {"MD", "WV"}, {"MA", "NH"}, {"MA", "NY"}, {"MA", "RI"}, {"MA", "VT"}, {"MI", "OH"}, {"MI", "WI"}, {"MI", "ON"},
    {"MN", "ND"}, {"MN", "SD"}, {"MN", "WI"}, {"MN", "MB"}, {"MN", "ON"}, {"MS", "TN"}, {"MO", "NE"}, {"MO", "OK"},
    {"MO", "TN"}, {"MT", "ND"}, {"MT", "SD"}, {"MT", "WY"}, {"MT", "AB"}, {"MT", "BC"}, {"MT", "SK"}, {"NE", "SD"},
    {"NE", "WY"}, {"NV", "OR"}, {"NV", "UT"}, {"NH", "VT"}, {"NH", "QC"}, {"NJ", "NY"}, {"NJ", "PA"}, {"NM", "OK"},
    {"NM", "TX"}, {"NM", "UT"}, {"NM", "MX"}, {"NY", "PA"}, {"NY", "VT"}, {"NY", "ON"}, {"NY", "QC"}, {"NC", "SC"},
    {"NC", "TN"}, {"NC", "VA"}, {"ND", "SD"}, {"ND", "MB"}, {"ND", "SK"}, {"OH", "PA"}, {"OH", "WV"}, {"OK", "TX"},
    {"OR", "WA"}, {"PA", "WV"}, {"SD", "WY"}, {"TN", "VA"}, {"TX", "MX"}, {"UT", "WY"}, {"VT", "QC"}, {"VA", "WV"},
    {"WA", "BC"}, {"AK", "BC"}, {"AK", "YT"}, {"AB", "BC"}, {"AB", "SK"}, {"AB", "NT"}, {"BC", "YT"}, {"BC", "NT"},
    {"MB", "SK"}, {"MB", "ON"}, {"MB", "NU"}, {"NB", "QC"}, {"NB", "NS"}, {"NB", "PE"}, {"NL", "QC"}, {"NS", "PE"},
    {"NT", "YT"}, {"NT", "SK"}, {"NT", "NU"}, {"NU", "SK"}, {"ON", "QC"},
};

/// Lowercase words separated by single spaces, padded with a space on each side so
/// " kansas " doesn't match inside " arkansas ".
std::string Words(std::string const & s)
{
  std::string out = " ";
  for (unsigned char const c : s)
  {
    if (std::isalnum(c))
      out += static_cast<char>(std::tolower(c));
    else if (out.back() != ' ' && c != '\'')
      out += ' ';
  }
  if (out.back() != ' ')
    out += ' ';
  return out;
}

bool HasWord(std::string const & words, char const * w) { return words.find(Words(w)) != std::string::npos; }

PlateRegion const * FindCode(std::string const & code)
{
  for (auto const & r : kRegions)
    if (code == r.code)
      return &r;
  return nullptr;
}

/// 2 for a rare plate, 1 for any other new one, 0 for a neighbor: how exciting a find is.
int Excitement(std::string const & code, std::optional<std::string> const & here)
{
  if (RarePlate(code))
    return 2;
  return here && PlateNeighbors(code, *here) ? 0 : 1;
}
}  // namespace

std::optional<std::string> MatchPlateRegion(std::string const & spoken)
{
  std::string const words = Words(spoken);
  std::string best;
  size_t bestLen = 0;
  // Longest name wins: "west virginia" over "virginia", "new mexico" over "mexico".
  for (auto const & r : kRegions)
  {
    std::vector<std::string> names = {r.name};
    for (auto const * a : r.aliases)
      names.push_back(a);
    for (auto const & n : names)
    {
      std::string const w = Words(n.rfind("the ", 0) == 0 ? n.substr(4) : n);
      if (w.size() > bestLen && words.find(w) != std::string::npos)
      {
        best = r.code;
        bestLen = w.size();
      }
    }
  }
  if (best.empty())
    return {};
  return best;
}

std::string PlateRegionName(std::string const & code)
{
  auto const * r = FindCode(code);
  return r ? r->name : "";
}

std::optional<std::string> PlateRegionCode(std::string const & name)
{
  std::string const words = Words(name);
  for (auto const & r : kRegions)
    if (Words(r.name) == words)
      return std::string(r.code);
  return {};
}

bool PlateNeighbors(std::string const & a, std::string const & b)
{
  for (auto const & pair : kBorders)
    if ((a == pair[0] && b == pair[1]) || (a == pair[1] && b == pair[0]))
      return true;
  return false;
}

bool RarePlate(std::string const & code)
{
  return code == "HI" || code == "AK" || code == "PR" || code == "YT" || code == "NT" || code == "NU";
}

YesNo ParseYesNo(std::string const & spoken)
{
  std::string const w = Words(spoken);
  for (auto const * no : {"no", "nope", "nah", "not now", "pass", "no thanks", "not really", "don't", "dont", "stop"})
    if (HasWord(w, no))
      return YesNo::No;
  for (auto const * yes : {"yes", "yeah", "yep", "yup", "sure", "ok", "okay", "fine", "why not", "let's", "lets",
                           "sounds good", "absolutely", "deal", "i'm in", "im in", "go"})
    if (HasWord(w, yes))
      return YesNo::Yes;
  return YesNo::Unclear;
}

std::string PlateLog::Serialize() const
{
  auto const join = [](std::set<std::string> const & codes)
  {
    std::string s;
    for (auto const & c : codes)
      s += " " + c;
    return s;
  };
  std::string out = "grudges " + std::to_string(grudges) + "\n";
  out += "lifetime" + join(lifetime) + "\n";
  for (auto const & [key, codes] : byRoute)
    out += "route " + key + join(codes) + "\n";
  return out;
}

PlateLog PlateLog::Parse(std::string const & text)
{
  PlateLog log;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line))
  {
    std::istringstream in(line);
    std::string kind;
    in >> kind;
    std::set<std::string> * target = nullptr;
    if (kind == "grudges")
      in >> log.grudges;
    else if (kind == "lifetime")
      target = &log.lifetime;
    else if (kind == "route")
    {
      std::string key;
      if (in >> key)
        target = &log.byRoute[key];
    }
    std::string code;
    while (target && in >> code)
      if (FindCode(code))
        target->insert(code);
  }
  return log;
}

PlateGame::PlateGame(Rng rng) : m_rng(std::move(rng)) {}

void PlateGame::NewTrip(std::string routeKey)
{
  m_routeKey = std::move(routeKey);
  m_state = State::Idle;
  m_trip.clear();
}

bool PlateGame::ShouldOffer(int tripsOnRoute, double distToManeuverM, int frustration)
{
  return tripsOnRoute >= kCommuteTrips && distToManeuverM >= kQuietStretchM && frustration <= kMaxFrustration;
}

PlateGame::Reply PlateGame::Offer(int tier)
{
  m_state = State::Offered;
  return Make("plateGame.offer", tier);
}

std::optional<PlateGame::Reply> PlateGame::OnDriverSaid(std::string const & spoken, int tier)
{
  if (m_state == State::Idle || m_state == State::Declined)
    return {};
  auto const code = MatchPlateRegion(spoken);
  if (m_state == State::Offered)
  {
    // Naming a plate straight away is a yes.
    if (code)
    {
      m_state = State::Playing;
      return Plate(*code, tier);
    }
    switch (ParseYesNo(spoken))
    {
    case YesNo::Yes: m_state = State::Playing; return Make("plateGame.accepted", tier);
    case YesNo::No:
    {
      m_state = State::Declined;
      m_log.grudges++;
      auto r = Make("plateGame.declined", tier);
      r.grudge = true;
      return r;
    }
    case YesNo::Unclear: return {};
    }
  }
  if (code)
    return Plate(*code, tier);
  // "I saw a Narnia plate": a plate, but no such place. Anything else we just didn't catch.
  std::string const w = Words(spoken);
  if (HasWord(w, "plate") || HasWord(w, "plates") || HasWord(w, "license"))
  {
    m_log.grudges++;
    auto r = Make("plateGame.madeUp", tier);
    r.grudge = true;
    return r;
  }
  return Make("plateGame.unheard", tier);
}

PlateGame::Reply PlateGame::Plate(std::string const & code, int tier)
{
  Vars const vars = {{"plate", PlateRegionName(code)}};
  if (m_here && code == *m_here)
    return Make("plateGame.sameState", tier, vars);
  auto & commute = m_log.byRoute[m_routeKey];
  if (commute.count(code))
    return Make("plateGame.repeat", tier, vars);
  commute.insert(code);
  m_log.lifetime.insert(code);
  m_trip.push_back(code);
  Vars withCount = vars;
  withCount["count"] = std::to_string(m_trip.size());
  if (RarePlate(code))
    return Make("plateGame.rare", tier, withCount);
  if (m_here && PlateNeighbors(code, *m_here))
    return Make("plateGame.neighbor", tier, withCount);
  return Make("plateGame.newFar", tier, withCount);
}

std::optional<PlateGame::Reply> PlateGame::WrapUp(int tier)
{
  if (m_state != State::Playing)
    return {};
  if (m_trip.empty())
    return Make("plateGame.wrapUpNone", tier);
  std::string best = m_trip.front();
  for (auto const & code : m_trip)
    if (Excitement(code, m_here) > Excitement(best, m_here))
      best = code;
  return Make("plateGame.wrapUp", tier,
              {{"count", std::to_string(m_trip.size())},
               {"total", std::to_string(m_log.lifetime.size())},
               {"best", PlateRegionName(best)}});
}

PlateGame::Reply PlateGame::Make(std::string const & key, int tier, Vars vars)
{
  vars["here"] = m_here ? PlateRegionName(*m_here) : "here";
  Reply r;
  r.key = key;
  r.text = Fill(Pick(key, tier), vars);
  return r;
}

/// Random line from the tier, never the same one twice in a row.
std::string PlateGame::Pick(std::string const & key, int tier)
{
  auto const & options = LinesFor(key, std::clamp(tier, 0, 2));
  std::vector<std::string> fresh;
  for (auto const & o : options)
    if (o != m_lastPicked[key])
      fresh.push_back(o);
  if (fresh.empty())
    fresh = options;
  double const r = m_rng ? m_rng() : 0;
  std::string const & line = fresh[std::min(fresh.size() - 1, static_cast<size_t>(r * fresh.size()))];
  m_lastPicked[key] = line;
  return line;
}
}  // namespace backseat
