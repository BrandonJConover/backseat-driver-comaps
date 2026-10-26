#include "backseat/voice_command.hpp"

#include <cctype>
#include <cstdlib>
#include <map>

namespace backseat
{
namespace
{
/// Lowercase words separated by single spaces, padded with a space at each end.
std::string CommandWords(std::string const & text)
{
  std::string out = " ";
  for (unsigned char const ch : text)
  {
    char const c = std::isalnum(ch) || ch == '\'' ? static_cast<char>(std::tolower(ch)) : ' ';
    if (c != ' ' || out.back() != ' ')
      out += c;
  }
  if (out.back() != ' ')
    out += ' ';
  return out;
}

std::optional<LatLon> RouteKeyLatLon(std::string const & s)
{
  auto const comma = s.find(',');
  if (comma == std::string::npos)
    return {};
  char * end = nullptr;
  double const lat = std::strtod(s.c_str(), &end);
  if (end != s.c_str() + comma)
    return {};
  double const lon = std::strtod(s.c_str() + comma + 1, &end);
  if (end != s.c_str() + s.size())
    return {};
  return LatLon{lat, lon};
}
/// What comes after "take me to", ending in a space: "the starbucks please " -> "starbucks".
std::string ParseTarget(std::string const & rest)
{
  std::string target = " " + rest;
  for (bool trimmed = true; trimmed;)
  {
    trimmed = false;
    for (std::string const tail : {" please ", " thanks ", " thank you ", " now ", " asap "})
      if (target.size() >= tail.size() && target.compare(target.size() - tail.size(), tail.size(), tail) == 0)
      {
        target.erase(target.size() - tail.size() + 1);
        trimmed = true;
      }
  }
  target = target.size() < 2 ? "" : target.substr(1, target.size() - 2);
  for (std::string const lead : {"the ", "my "})
    if (target.rfind(lead, 0) == 0)
      target.erase(0, lead.size());
  return target;
}
}  // namespace

std::optional<DestinationCommand> ParseDestinationCommand(std::string const & spoken)
{
  std::string const words = CommandWords(spoken);
  std::string target;
  for (char const * cue : {" take me to ", " take us to ", " navigate to ", " drive me to ", " drive us to ",
                           " directions to ", " get me to ", " get us to ", " route to ", " route me to ",
                           " let's go to ", " lets go to ", " head to ", " go to ", " bring me to "})
  {
    auto const at = words.find(cue);
    if (at != std::string::npos)
    {
      target = ParseTarget(words.substr(at + std::string(cue).size()));
      break;
    }
  }
  // "Take me home" needs no "to".
  for (char const * cue : {" take me home ", " take us home ", " go home ", " drive me home ", " drive us home ",
                           " navigate home ", " head home ", " bring me home ", " get me home ", " get us home "})
    if (target.empty() && words.find(cue) != std::string::npos)
      target = "home";
  if (target.empty())
    return {};

  bool polite = false;
  for (char const * nice : {" please ", " pls ", " thanks ", " thank you ", " hi ", " hello ", " hey ", " good morning ",
                            " good afternoon ", " good evening ", " morning ", " could you ", " would you ",
                            " can you ", " kindly ", " if you don't mind ", " when you get a chance "})
    if (words.find(nice) != std::string::npos)
      polite = true;
  return DestinationCommand{target, polite};
}

std::optional<KnownPlace> KnownPlaceOf(std::string const & target)
{
  for (char const * home : {"home", "my house", "house", "my place", "my home"})
    if (target == home)
      return KnownPlace::Home;
  for (char const * work : {"work", "my work", "the office", "office", "my office", "my job", "job"})
    if (target == work)
      return KnownPlace::Work;
  return {};
}

std::optional<LatLon> GuessPlace(std::vector<TripRecord> const & history, KnownPlace place)
{
  // Places by the route key's grid cell (~250 m): how often trips start or end there, and the
  // exact destination points seen for it.
  struct Seen
  {
    int visits = 0;
    int arrivals = 0;
    LatLon sum{0, 0};
    int exact = 0;
    LatLon cell{0, 0};
  };
  std::map<std::string, Seen> seen;
  for (auto const & trip : history)
  {
    auto const arrow = trip.routeKey.find('>');
    if (arrow == std::string::npos)
      continue;
    std::string const from = trip.routeKey.substr(0, arrow);
    std::string const to = trip.routeKey.substr(arrow + 1);
    if (auto const p = RouteKeyLatLon(from))
    {
      seen[from].visits++;
      seen[from].cell = *p;
    }
    if (auto const p = RouteKeyLatLon(to))
    {
      auto & s = seen[to];
      s.visits++;
      s.arrivals++;
      s.cell = *p;
      if (trip.to)
      {
        s.sum.lat += trip.to->lat;
        s.sum.lon += trip.to->lon;
        s.exact++;
      }
    }
  }
  auto const best = [&](std::string const & skip, bool byArrivals) -> std::map<std::string, Seen>::const_iterator
  {
    auto top = seen.cend();
    for (auto it = seen.cbegin(); it != seen.cend(); ++it)
    {
      int const n = byArrivals ? it->second.arrivals : it->second.visits;
      if (it->first != skip && n >= 2 && (top == seen.cend() || n > (byArrivals ? top->second.arrivals : top->second.visits)))
        top = it;
    }
    return top;
  };
  auto const home = best("", false);
  auto const it = place == KnownPlace::Home ? home : best(home == seen.cend() ? "" : home->first, true);
  if (it == seen.cend())
    return {};
  auto const & s = it->second;
  if (s.exact > 0)
    return LatLon{s.sum.lat / s.exact, s.sum.lon / s.exact};
  return s.cell;
}
}  // namespace backseat
