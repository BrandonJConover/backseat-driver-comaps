#include "backseat/history.hpp"

#include "backseat/lines.hpp"
#include "backseat/units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace backseat
{
namespace
{
std::vector<std::string> const kLeadFoot = {
    "This route should take {expected} minutes, but knowing you it'll probably take {predicted} with your lead foot. "
    "I'm just glad I don't have eyes.",
    "Estimated time: {expected} minutes. Your estimated time: {predicted}. The speed limit's estimated time: we'll "
    "never know.",
    "{expected} minutes for normal people. {predicted} for you. Please remember the brake is the other pedal.",
};
std::vector<std::string> const kSlowpoke = {
    "This should take {expected} minutes. With you driving, call it {predicted}. Maybe pack a snack.",
    "{expected} minutes, says the map. The map has never met you. I'm saying {predicted}.",
};
std::vector<std::string> const kOnTime = {
    "{expected} minutes. You usually manage that, somehow. Don't get cocky.",
};
std::vector<std::string> const kUnknown = {
    "New route. I have no data on you here. Yet.",
    "Never driven this one before. Neither of us knows how bad this will be. That's exciting. For me.",
};
std::vector<std::string> const kRepeat = {
    " This is your {n} trip to {destination}. You'd think you'd know the way by now.",
    " {destination} again, for the {n} time. I could drive this myself, and I don't have hands.",
};
std::vector<std::string> const kMissedBefore = {
    " Last time you missed {missed} turn{s} on this exact route. Let's aim for fewer. It's a low bar. Step over it.",
};
std::vector<std::string> const kBrakesBefore = {
    " Last time you brake checked {brakes} times here. The guy behind you still talks about it.",
};

double Median(std::vector<double> xs)
{
  std::sort(xs.begin(), xs.end());
  size_t const mid = xs.size() / 2;
  return xs.size() % 2 ? xs[mid] : (xs[mid - 1] + xs[mid]) / 2;
}

std::string OrdinalWord(int n)
{
  static char const * const kWords[] = {"zeroth", "first",   "second", "third", "fourth", "fifth",
                                        "sixth",  "seventh", "eighth", "ninth", "tenth"};
  if (n < 11)
    return kWords[n];
  int const m = n % 100;
  std::string suffix = "th";
  if (!(m >= 11 && m <= 13))
  {
    static char const * const kSuffixes[] = {"th", "st", "nd", "rd"};
    if (n % 10 < 4)
      suffix = kSuffixes[n % 10];
  }
  return std::to_string(n) + suffix;
}
}  // namespace

namespace
{
int UsualMinutes(std::vector<TripRecord const *> const & trips)
{
  double expected = 0, actual = 0;
  int withExpected = 0, withActual = 0;
  for (auto const * t : trips)
  {
    if (t->expectedMinutes > 0)
      expected += t->expectedMinutes, ++withExpected;
    if (t->actualMinutes > 0)
      actual += t->actualMinutes, ++withActual;
  }
  if (withExpected)
    return static_cast<int>(std::lround(expected / withExpected));
  return withActual ? static_cast<int>(std::lround(actual / withActual)) : 0;
}
}  // namespace

std::vector<PastPlace> PastPlaces(std::vector<TripRecord> const & history, size_t max)
{
  struct Group
  {
    std::vector<TripRecord const *> trips;
    TripRecord const * last = nullptr;
  };
  std::map<std::string, Group> byPlace;
  for (auto const & trip : history)
  {
    auto const arrow = trip.routeKey.find('>');
    if (arrow == std::string::npos || trip.endedAt <= 0)
      continue;
    auto & g = byPlace[trip.routeKey.substr(arrow + 1)];
    g.trips.push_back(&trip);
    if (!g.last || trip.endedAt >= g.last->endedAt)
      g.last = &trip;
  }
  std::vector<PastPlace> out;
  for (auto const & [cell, g] : byPlace)
  {
    PastPlace p;
    p.destination = g.last->destination;
    if (g.last->to)
      p.to = *g.last->to;
    else if (std::sscanf(cell.c_str(), "%lf,%lf", &p.to.lat, &p.to.lon) != 2)
      continue;
    p.trips = static_cast<int>(g.trips.size());
    p.lastEndedAt = g.last->endedAt;
    p.usualMinutes = UsualMinutes(g.trips);
    out.push_back(std::move(p));
  }
  std::sort(out.begin(), out.end(), [](PastPlace const & a, PastPlace const & b)
  { return a.trips != b.trips ? a.trips > b.trips : a.lastEndedAt > b.lastEndedAt; });
  if (out.size() > max)
    out.resize(max);
  return out;
}

std::string RouteKey(LatLon origin, LatLon destination)
{
  auto const snap = [](LatLon p)
  { return ToFixed(JsRound(p.lat * 400) / 400, 4) + "," + ToFixed(JsRound(p.lon * 400) / 400, 4); };
  return snap(origin) + ">" + snap(destination);
}

TripRecord RecordTrip(CritiqueEngine const & engine, TripPlan const & plan)
{
  auto const & st = engine.Stats();
  double const endedAt = st.endedAt.value_or(st.startedAt);
  double const minutes = std::max(0.0, (endedAt - st.startedAt) / 60000);
  TripRecord r;
  r.routeKey = RouteKey(plan.origin, plan.destination);
  r.destination = plan.destinationName;
  r.endedAt = endedAt;
  r.expectedMinutes = plan.expectedMinutes;
  r.actualMinutes = RoundTo(minutes, 1);
  r.speedingShare = minutes > 0 ? RoundTo(st.speedingSeconds / (minutes * 60), 3) : 0;
  r.missedTurns = st.missedTurns;
  r.hardBrakes = st.hardBrakes;
  r.grade = engine.Report().overall.letter;
  if (plan.destination.lat || plan.destination.lon)
    r.to = plan.destination;
  return r;
}

TripPrediction PredictTrip(std::vector<TripRecord> const & history, TripPlan const & plan, Rng const & rng)
{
  std::string const key = RouteKey(plan.origin, plan.destination);
  std::vector<TripRecord const *> sameRoute;
  std::vector<double> ratios;
  for (auto const & r : history)
  {
    if (r.routeKey == key)
      sameRoute.push_back(&r);
    if (r.expectedMinutes > 0 && r.actualMinutes > 0)
    {
      // Same-route trips count triple; the median keeps one road-trip disaster from skewing it.
      double const ratio = r.actualMinutes / r.expectedMinutes;
      ratios.insert(ratios.end(), r.routeKey == key ? 3 : 1, ratio);
    }
  }
  auto const pick = [&](std::vector<std::string> const & lines)
  { return lines[static_cast<size_t>(std::floor(rng() * lines.size()))]; };

  TripPrediction p;
  if (ratios.empty())
  {
    p.text = pick(kUnknown);
    p.predictedMinutes = plan.expectedMinutes;
    return p;
  }

  double const ratio = Median(ratios);
  double const predicted = std::max(1.0, JsRound(plan.expectedMinutes * ratio));
  p.habit = ratio <= 0.92 ? Habit::LeadFoot : ratio >= 1.1 ? Habit::Slowpoke : Habit::OnTime;
  Vars const vars{{"expected", JsNumber(JsRound(plan.expectedMinutes))},
                  {"predicted", JsNumber(predicted)},
                  {"destination", plan.destinationName},
                  {"n", OrdinalWord(static_cast<int>(sameRoute.size()) + 1)}};
  p.text = Fill(pick(p.habit == Habit::LeadFoot ? kLeadFoot : p.habit == Habit::Slowpoke ? kSlowpoke : kOnTime), vars);

  TripRecord const * last = sameRoute.empty() ? nullptr : sameRoute.back();
  if (last && last->missedTurns)
    p.text += Fill(pick(kMissedBefore),
                   {{"missed", std::to_string(last->missedTurns)}, {"s", last->missedTurns == 1 ? "" : "s"}});
  else if (last && last->hardBrakes >= 2)
    p.text += Fill(pick(kBrakesBefore), {{"brakes", std::to_string(last->hardBrakes)}});
  else if (sameRoute.size() >= 2)
    p.text += Fill(pick(kRepeat), vars);
  p.predictedMinutes = predicted;
  p.timesOnRoute = static_cast<int>(sameRoute.size());
  return p;
}

std::vector<Routine> LearnRoutines(std::vector<TripRecord> const & history,
                                   std::function<std::tm(double)> const & localTime)
{
  struct Start
  {
    int minute;
    int day;
    TripRecord const * trip;
  };
  std::map<std::string, std::vector<Start>> byRoute;
  for (auto const & trip : history)
  {
    if (trip.routeKey.empty() || trip.endedAt <= 0)
      continue;
    std::tm const tm = localTime(trip.endedAt - trip.actualMinutes * 60000);
    byRoute[trip.routeKey].push_back({tm.tm_hour * 60 + tm.tm_min, tm.tm_wday, &trip});
  }
  int constexpr kSameTimeMinutes = 75;
  int constexpr kMinTrips = 4;
  std::vector<Routine> out;
  for (auto & [key, starts] : byRoute)
  {
    std::stable_sort(starts.begin(), starts.end(), [](Start const & a, Start const & b) { return a.minute < b.minute; });
    // Runs of starts close together in the day: 7:50, 8:05, 8:20 is one routine, 17:30 another.
    for (size_t i = 0; i < starts.size();)
    {
      size_t j = i + 1;
      while (j < starts.size() && starts[j].minute - starts[j - 1].minute <= kSameTimeMinutes / 2)
        ++j;
      if (static_cast<int>(j - i) >= kMinTrips)
      {
        Routine r;
        r.routeKey = key;
        int sum = 0;
        double lastEnded = -1;
        for (size_t k = i; k < j; ++k)
        {
          sum += starts[k].minute;
          r.tripsByDay[starts[k].day]++;
          if (starts[k].trip->endedAt > lastEnded)
          {
            lastEnded = starts[k].trip->endedAt;
            r.destination = starts[k].trip->destination;
            if (starts[k].trip->to)
              r.to = starts[k].trip->to;
          }
        }
        r.trips = static_cast<int>(j - i);
        r.startMinute = sum / r.trips;
        std::vector<TripRecord const *> trips;
        for (size_t k = i; k < j; ++k)
          trips.push_back(starts[k].trip);
        r.usualMinutes = UsualMinutes(trips);
        bool any = false;
        for (int d = 0; d < 7; ++d)
          any |= r.usualOn[d] = r.tripsByDay[d] >= 2;
        if (any)
          out.push_back(std::move(r));
      }
      i = j;
    }
  }
  return out;
}

std::optional<Routine> UsualTrip(std::vector<Routine> const & routines, LatLon here, std::tm const & now)
{
  std::string const key = RouteKey(here, here);
  std::string const from = key.substr(0, key.find('>'));
  int const minute = now.tm_hour * 60 + now.tm_min;
  std::optional<Routine> best;
  for (auto const & r : routines)
    if (r.routeKey.compare(0, from.size() + 1, from + ">") == 0 && r.usualOn[now.tm_wday] &&
        std::abs(minute - r.startMinute) <= 45 && (!best || r.trips > best->trips))
      best = r;
  return best;
}

std::optional<RoutineBreak> CheckRoutine(std::vector<Routine> const & routines, std::string const & routeKey,
                                         std::tm const & now)
{
  int const minute = now.tm_hour * 60 + now.tm_min;
  int const day = now.tm_wday;
  auto const from = [](std::string const & key) { return key.substr(0, key.find('>')); };
  auto const usualDays = [](Routine const & r) { return std::count(r.usualOn.begin(), r.usualOn.end(), true); };
  // This exact trip is usual right now: nothing to say.
  for (auto const & r : routines)
    if (r.routeKey == routeKey && r.usualOn[day] && std::abs(minute - r.startMinute) <= 45)
      return {};
  // Same place, same time, usual day, different destination: "normally we'd be going to work".
  for (auto const & r : routines)
    if (r.routeKey != routeKey && from(r.routeKey) == from(routeKey) && r.usualOn[day] &&
        std::abs(minute - r.startMinute) <= 30)
      return RoutineBreak{RoutineBreak::Kind::Elsewhere, r, 0};
  for (auto const & r : routines)
  {
    if (r.routeKey != routeKey)
      continue;
    // The commute on a day off. Only for routines with a few usual days, so weekend brunch on a
    // Sunday doesn't count as a break when it's on Saturday.
    if (!r.usualOn[day] && usualDays(r) >= 3 && std::abs(minute - r.startMinute) <= 120)
      return RoutineBreak{RoutineBreak::Kind::OffDay, r, 0};
    if (r.usualOn[day] && minute - r.startMinute >= 45 && minute - r.startMinute <= 240)
      return RoutineBreak{RoutineBreak::Kind::Late, r, minute - r.startMinute};
  }
  return {};
}
namespace
{
/// One memory per line, tab-separated, so tabs and line breaks in a fact become spaces.
std::string OneLine(std::string s)
{
  std::replace_if(s.begin(), s.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
  auto const first = s.find_first_not_of(' ');
  auto const last = s.find_last_not_of(' ');
  return first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
}
}  // namespace

bool DriverMemory::Remember(std::string const & topicIn, std::string const & factIn, double nowMs)
{
  std::string const topic = OneLine(topicIn);
  std::string const fact = OneLine(factIn);
  if (topic.empty() || fact.empty())
    return false;
  if (topic != "note")
  {
    for (auto & m : m_memories)
    {
      if (m.topic != topic)
        continue;
      if (m.fact == fact)
        return false;
      m.fact = fact;
      m.learnedAt = nowMs;
      return true;
    }
  }
  else
  {
    for (auto const & m : m_memories)
      if (m.topic == topic && m.fact == fact)
        return false;
  }
  m_memories.push_back({topic, fact, nowMs});
  return true;
}

bool DriverMemory::Forget(std::string const & topic)
{
  auto const before = m_memories.size();
  m_memories.erase(std::remove_if(m_memories.begin(), m_memories.end(), [&](Memory const & m) { return m.topic == topic; }),
                   m_memories.end());
  return m_memories.size() != before;
}

std::optional<std::string> DriverMemory::Recall(std::string const & topic) const
{
  for (auto const & m : m_memories)
    if (m.topic == topic)
      return m.fact;
  return {};
}

std::string DriverMemory::ForModel(size_t maxChars) const
{
  // Topics first (they're the facts that matter), then notes newest first, until it's full.
  std::vector<std::string> lines;
  for (auto const & m : m_memories)
    if (m.topic != "note")
      lines.push_back(m.topic + ": " + m.fact);
  for (auto it = m_memories.rbegin(); it != m_memories.rend(); ++it)
    if (it->topic == "note")
      lines.push_back("note: " + it->fact);
  std::string out;
  for (auto const & line : lines)
  {
    if (out.size() + line.size() + 1 > maxChars)
      break;
    out += line + "\n";
  }
  return out;
}

std::string DriverMemory::Serialize() const
{
  std::string out;
  for (auto const & m : m_memories)
    out += ToFixed(m.learnedAt, 0) + "\t" + m.topic + "\t" + m.fact + "\n";
  return out;
}

DriverMemory DriverMemory::Parse(std::string const & text)
{
  DriverMemory memory;
  size_t start = 0;
  while (start < text.size())
  {
    size_t end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    std::string const line = text.substr(start, end - start);
    start = end + 1;
    auto const a = line.find('\t');
    auto const b = a == std::string::npos ? a : line.find('\t', a + 1);
    if (b == std::string::npos)
      continue;
    char * stop = nullptr;
    double const at = std::strtod(line.c_str(), &stop);
    memory.m_memories.push_back({line.substr(a + 1, b - a - 1), line.substr(b + 1), stop == line.c_str() ? 0 : at});
  }
  return memory;
}
}  // namespace backseat
