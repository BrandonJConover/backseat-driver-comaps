#include "backseat/history.hpp"

#include "backseat/lines.hpp"
#include "backseat/units.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
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

// More of each, for the app (PredictTrip with a SaidLog). The on-time driver heard the same single
// line on every commute.
std::vector<std::string> const kLeadFootMore = {
    "Map says {expected} minutes. You'll do it in {predicted}. Not because you're good. Because you're fast. Those "
    "are different things.",
    "{expected} minutes, officially. Unofficially, {predicted}, and I'll have aged a year. I'm a phone. That's a lot "
    "for a phone.",
    "The map gave this {expected} minutes. Your right foot read that as a dare. I'm guessing {predicted}.",
};
std::vector<std::string> const kSlowpokeMore = {
    "{expected} minutes for most people. {predicted} for you. I've cleared my schedule. I don't have a schedule.",
    "Estimated time, {expected} minutes. Adjusted for you, {predicted}. Adjusted for my patience, forever.",
    "The map says {expected} minutes. The map assumes you'll use the gas pedal. I'm saying {predicted}.",
};
std::vector<std::string> const kOnTimeMore = {
    "{expected} minutes. You're weirdly consistent about this. It's the only thing you're consistent about.",
    "Should take {expected} minutes. You usually hit that. I don't know how. I've seen you drive.",
    "{expected} minutes, and you'll probably make it. Then you'll do something in the last block to ruin it. I'll be "
    "ready.",
    "{expected} minutes. You always make it right on time. It's like watching a cat land on its feet. Messy, but it "
    "lands.",
    "This one takes {expected} minutes, and somehow you never miss that. Don't worry, I'll find something else.",
};
std::vector<std::string> const kUnknownMore = {
    "Somewhere new. I have no notes on you here. Give me ten minutes.",
    "First time on this route. Don't worry. I'll be learning all your bad habits as we go.",
    "Uncharted territory. For both of us. Mostly for the road.",
};
std::vector<std::string> const kRepeatMore = {
    " {destination}, for the {n} time. At this point we should just get a place there.",
};
std::vector<std::string> const kMissedBeforeMore = {
    " Last time here you missed {missed} turn{s}. I've been thinking about it. A lot.",
};

std::vector<std::string> Both(std::vector<std::string> a, std::vector<std::string> const & b)
{
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

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
    if (arrow == std::string::npos || !(trip.endedAt > 0))  // also skips a NaN end time
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

TripPrediction PredictTrip(std::vector<TripRecord> const & history, TripPlan const & plan, Rng const & rng,
                           SaidLog * said)
{
  std::string const key = RouteKey(plan.origin, plan.destination);
  std::vector<TripRecord const *> sameRoute;
  std::vector<double> ratios;
  for (auto const & r : history)
  {
    if (r.routeKey == key)
      sameRoute.push_back(&r);
    // Damaged records (NaN, infinite or negative minutes) say nothing about the driver.
    if (r.expectedMinutes > 0 && r.actualMinutes > 0 && std::isfinite(r.expectedMinutes) &&
        std::isfinite(r.actualMinutes))
    {
      // Same-route trips count triple; the median keeps one road-trip disaster from skewing it.
      double const ratio = r.actualMinutes / r.expectedMinutes;
      ratios.insert(ratios.end(), r.routeKey == key ? 3 : 1, ratio);
    }
  }
  static std::map<std::vector<std::string> const *, std::pair<char const *, std::vector<std::string>>> const kBigger = {
      {&kLeadFoot, {"_predict.leadFoot", Both(kLeadFoot, kLeadFootMore)}},
      {&kSlowpoke, {"_predict.slowpoke", Both(kSlowpoke, kSlowpokeMore)}},
      {&kOnTime, {"_predict.onTime", Both(kOnTime, kOnTimeMore)}},
      {&kUnknown, {"_predict.unknown", Both(kUnknown, kUnknownMore)}},
      {&kRepeat, {"_predict.repeat", Both(kRepeat, kRepeatMore)}},
      {&kMissedBefore, {"_predict.missed", Both(kMissedBefore, kMissedBeforeMore)}},
  };
  auto const pick = [&](std::vector<std::string> const & lines) -> std::string
  {
    auto const big = kBigger.find(&lines);
    if (said && big != kBigger.end())
      return said->Pick(big->second.first, big->second.second, rng());
    // Same clamp as SaidLog::Pick: NaN or negative -> first line, 1 or more -> last (was out of bounds).
    double const r = rng();
    size_t const i = r > 0 ? static_cast<size_t>(std::floor(std::min(r, 1.0) * lines.size())) : 0;
    return lines[std::min(i, lines.size() - 1)];
  };

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
    if (trip.routeKey.empty() || !(trip.endedAt > 0))
      continue;
    double const took = std::isfinite(trip.actualMinutes) && trip.actualMinutes > 0 ? trip.actualMinutes : 0;
    std::tm const tm = localTime(trip.endedAt - took * 60000);
    if (tm.tm_wday < 0 || tm.tm_wday > 6)
      continue;
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

std::string LogNumber(double x)
{
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.15g", x);
  return buf;
}

/// The whole field must be a finite number ("12abc", "nan" and "" are damage).
std::optional<double> LogDouble(std::string const & s)
{
  if (s.empty())
    return {};
  char * stop = nullptr;
  double const x = std::strtod(s.c_str(), &stop);
  if (stop != s.c_str() + s.size() || !std::isfinite(x))
    return {};
  return x;
}

std::optional<int> LogCount(std::string const & s)
{
  auto const x = LogDouble(s);
  if (!x || *x < 0 || *x > 1e6 || *x != std::floor(*x))
    return {};
  return static_cast<int>(*x);
}
}  // namespace

std::string TripToLine(TripRecord const & r)
{
  std::string line = OneLine(r.routeKey) + "\t" + OneLine(r.destination) + "\t" + LogNumber(r.endedAt) + "\t" +
                     LogNumber(r.expectedMinutes) + "\t" + LogNumber(r.actualMinutes) + "\t" +
                     LogNumber(r.speedingShare) + "\t" + std::to_string(r.missedTurns) + "\t" +
                     std::to_string(r.hardBrakes) + "\t" + OneLine(r.grade);
  if (r.to && std::isfinite(r.to->lat) && std::isfinite(r.to->lon))
    line += "\t" + LogNumber(r.to->lat) + "," + LogNumber(r.to->lon);
  return line;
}

std::optional<TripRecord> TripFromLine(std::string const & lineIn)
{
  std::string line = lineIn;
  if (!line.empty() && line.back() == '\r')  // saved on Windows or hand-edited
    line.pop_back();
  std::vector<std::string> f;
  size_t start = 0;
  while (true)
  {
    auto const tab = line.find('\t', start);
    f.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
    if (tab == std::string::npos)
      break;
    start = tab + 1;
  }
  if (f.size() < 9 || f[0].empty())
    return {};
  auto const ended = LogDouble(f[2]);
  auto const expected = LogDouble(f[3]);
  auto const actual = LogDouble(f[4]);
  auto const share = LogDouble(f[5]);
  auto const missed = LogCount(f[6]);
  auto const brakes = LogCount(f[7]);
  if (!ended || !expected || !actual || !share || !missed || !brakes)
    return {};
  TripRecord r;
  r.routeKey = f[0];
  r.destination = f[1];
  r.endedAt = *ended;
  r.expectedMinutes = *expected;
  r.actualMinutes = *actual;
  r.speedingShare = *share;
  r.missedTurns = *missed;
  r.hardBrakes = *brakes;
  r.grade = f[8];
  if (f.size() > 9)
  {
    // A damaged destination point loses only the point, not the trip.
    auto const comma = f[9].find(',');
    if (comma != std::string::npos)
    {
      auto const lat = LogDouble(f[9].substr(0, comma));
      auto const lon = LogDouble(f[9].substr(comma + 1));
      if (lat && lon && std::abs(*lat) <= 90 && std::abs(*lon) <= 180)
        r.to = LatLon{*lat, *lon};
    }
  }
  return r;
}

std::vector<TripRecord> ParseTrips(std::string const & text, size_t max, size_t * lines)
{
  std::vector<TripRecord> trips;
  size_t count = 0;
  size_t start = 0;
  while (start < text.size())
  {
    size_t end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    std::string const line = text.substr(start, end - start);
    start = end + 1;
    if (line.empty() || line == "\r")
      continue;
    ++count;
    if (auto r = TripFromLine(line))
      trips.push_back(std::move(*r));
  }
  if (trips.size() > max)
    trips.erase(trips.begin(), trips.end() - static_cast<std::ptrdiff_t>(max));
  if (lines)
    *lines = count;
  return trips;
}

std::string SerializeTrips(std::vector<TripRecord> const & trips)
{
  std::string out;
  for (auto const & r : trips)
    out += TripToLine(r) + "\n";
  return out;
}

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
    // Skip a line that doesn't fit rather than stopping, so one long fact can't crowd out the rest.
    if (out.size() + line.size() + 1 > maxChars)
      continue;
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
    std::string line = text.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r')  // saved on Windows or hand-edited
      line.pop_back();
    auto const a = line.find('\t');
    auto const b = a == std::string::npos ? a : line.find('\t', a + 1);
    if (b == std::string::npos || b == a + 1 || b + 1 == line.size())  // no topic or no fact
      continue;
    char * stop = nullptr;
    double const at = std::strtod(line.c_str(), &stop);
    // A hand-edited "nan"/"inf" time keeps the fact but not the time (0 = unknown).
    bool const timeOk = stop != line.c_str() && std::isfinite(at);
    memory.m_memories.push_back({line.substr(a + 1, b - a - 1), line.substr(b + 1), timeOk ? at : 0});
  }
  return memory;
}
}  // namespace backseat
