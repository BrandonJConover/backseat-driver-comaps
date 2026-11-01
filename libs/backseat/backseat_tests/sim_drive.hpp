// A whole drive, simulated: a random route (town streets or a long highway run), a driver who
// mostly follows it, and a phone that plays the GPS's lines one at a time like the app does
// (directions jump the queue, nothing is cut off). Used by soak_test.cpp to check things that
// only show up over a full trip: a turn called too late because a joke was still playing, two
// bits stepping on each other, a line said twice.
#pragma once

#include "backseat/navigator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace backseat_tests
{
enum class TripShape
{
  Town,
  RoadTrip
};

struct SimLeg
{
  double lengthM = 0;
  /// The maneuver at the end of the leg.
  backseat::TurnKind turn = backseat::TurnKind::Left;
  std::string street;
  double limitMps = 0;
  bool light = false;
  bool motorway = false;
};

/// One line as the phone played it.
struct Spoken
{
  backseat::Commentary line;
  /// When the navigator handed it over, and when the phone started and finished saying it.
  double queuedMs = 0;
  double startMs = 0;
  double endMs = 0;
  /// Which maneuver was next when it was handed over.
  size_t leg = 0;
};

/// Each maneuver's fate.
struct Maneuver
{
  backseat::TurnKind turn = backseat::TurnKind::None;
  std::string street;
  /// When the car got to it.
  double reachedMs = -1;
  double speedMps = 0;
  /// The plain "Now, ..." call, if one came (when the phone finished saying it).
  std::optional<double> finalEndMs;
  /// Any direction for it at all.
  bool directed = false;
  /// The driver blew it, so the router started over.
  bool missed = false;
};

struct SimResult
{
  std::vector<Spoken> spoken;
  std::vector<Maneuver> maneuvers;
  double endMs = 0;
  bool arrived = false;
};

/// Seconds the phone takes to say a line: Kokoro at the app's pace, plus the pause after it.
inline double SimSpeakMs(backseat::Commentary const & c)
{
  int const words = static_cast<int>(std::count(c.text.begin(), c.text.end(), ' ')) + 1;
  return words / 2.5 * 1000 / std::max(0.5, c.voiceSpeed) + 350;
}

inline std::vector<SimLeg> MakeRoute(TripShape shape, std::mt19937 & rng)
{
  using backseat::TurnKind;
  static char const * const kStreets[] = {"Walters Avenue", "Winesap Drive", "Elm Tree Road", "8th Street",
                                          "Cameo Lane",     "Melissa Drive", "Main Street",   "Oak Avenue",
                                          "Walton Boulevard", "Pleasant Grove Road", "Highway 412", "Interstate 49"};
  auto uni = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
  auto pick = [&](int n) { return static_cast<int>(rng() % n); };
  TurnKind const kTown[] = {TurnKind::Left, TurnKind::Right, TurnKind::Left, TurnKind::Right, TurnKind::SlightLeft,
                            TurnKind::SlightRight, TurnKind::SharpRight, TurnKind::Roundabout};
  std::vector<SimLeg> legs;
  auto town = [&](int n)
  {
    for (int i = 0; i < n; ++i)
    {
      SimLeg l;
      // Now and then two turns right after each other ("..., then right onto ...").
      l.lengthM = pick(6) == 0 ? uni(60, 140) : uni(180, 1400);
      l.turn = kTown[pick(8)];
      l.street = kStreets[pick(10)];
      l.limitMps = uni(11, 20);
      l.light = pick(3) == 0;
      legs.push_back(l);
    }
  };
  if (shape == TripShape::Town)
  {
    town(4 + pick(10));
  }
  else
  {
    town(2 + pick(3));
    SimLeg on{uni(300, 800), TurnKind::ExitRight, "Interstate 49", 20, false, false};
    legs.push_back(on);
    int const stretches = 1 + pick(3);
    for (int i = 0; i < stretches; ++i)
      legs.push_back({uni(20000, 70000), pick(2) ? TurnKind::SlightRight : TurnKind::ExitRight,
                      i + 1 == stretches ? "Highway 412" : "Interstate 49", uni(29, 33.5), false, true});
    town(2 + pick(4));
  }
  legs.push_back({uni(150, 600), TurnKind::Destination, "", uni(11, 15), false, false});
  return legs;
}

/// What the phone does with the lines: one at a time; a direction goes ahead of anything still
/// waiting, but never cuts off the line being said.
class SimPhone
{
public:
  void Queue(std::vector<backseat::Commentary> lines, double nowMs, size_t leg)
  {
    for (auto & l : lines)
    {
      Spoken s;
      s.line = std::move(l);
      s.queuedMs = nowMs;
      s.leg = leg;
      if (s.line.priority == backseat::Priority::Nav)
      {
        auto it = std::find_if(m_waiting.begin(), m_waiting.end(),
                               [](Spoken const & w) { return w.line.priority != backseat::Priority::Nav; });
        m_waiting.insert(it, std::move(s));
      }
      else
      {
        m_waiting.push_back(std::move(s));
      }
    }
  }
  /// Plays whatever can start by `nowMs`.
  void Advance(double nowMs, std::vector<Spoken> & done)
  {
    while (!m_waiting.empty() && m_freeAtMs <= nowMs)
    {
      Spoken s = std::move(m_waiting.front());
      m_waiting.pop_front();
      s.startMs = std::max(m_freeAtMs, s.queuedMs);
      s.endMs = s.startMs + SimSpeakMs(s.line);
      m_freeAtMs = s.endMs;
      done.push_back(std::move(s));
    }
  }

private:
  std::deque<Spoken> m_waiting;
  double m_freeAtMs = 0;
};

struct SimOptions
{
  TripShape shape = TripShape::Town;
  uint32_t seed = 1;
  /// Every lookup the app wires up, answering something now and then, so every bit can fire.
  bool lookups = true;
  /// The driver blows a turn now and then (a reroute follows).
  bool missTurns = true;
};

inline SimResult SimulateTrip(backseat::Navigator & nav, SimOptions const & o, double startMs = 1.7e12)
{
  using namespace backseat;
  std::mt19937 rng(o.seed);
  auto uni = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
  auto chance = [&](double p) { return uni(0, 1) < p; };

  std::vector<SimLeg> legs = MakeRoute(o.shape, rng);
  SimResult res;
  for (auto const & l : legs)
  {
    Maneuver m;
    m.turn = l.turn;
    m.street = l.street;
    res.maneuvers.push_back(m);
  }

  double lat = 36.3725, lon = -94.2088, bearing = 0;
  auto bearingAfter = [](TurnKind k, double b)
  {
    double d = 0;
    switch (k)
    {
    case TurnKind::Left: d = -90; break;
    case TurnKind::Right: d = 90; break;
    case TurnKind::SharpLeft: d = -135; break;
    case TurnKind::SharpRight: d = 135; break;
    case TurnKind::SlightLeft:
    case TurnKind::ExitLeft: d = -30; break;
    case TurnKind::SlightRight:
    case TurnKind::ExitRight: d = 30; break;
    case TurnKind::UTurn: d = 180; break;
    default: break;
    }
    return std::fmod(b + d + 720, 360);
  };
  auto move = [&](double m)
  {
    double const r = bearing * M_PI / 180;
    lat += m * std::cos(r) / 111320;
    lon += m * std::sin(r) / (111320 * std::cos(lat * M_PI / 180));
  };

  if (o.lookups)
  {
    // About one named place a kilometer, a little ahead of the car.
    nav.SetSightLookup([&rng](LatLon const & at, double) -> std::optional<Sight>
    {
      if (rng() % 60 != 0)
        return std::nullopt;
      static char const * const kinds[] = {"coffee", "food", "gas", "park", "store", "landmark", ""};
      return Sight{"a Walmart", false, LatLon{at.lat + 0.001, at.lon}, kinds[rng() % 7]};
    });
    nav.SetSurroundingsLookup([&legs, &res](LatLon const &, double)
    {
      Surroundings s;
      size_t const i = std::min(res.maneuvers.size() - 1, static_cast<size_t>(
          std::count_if(res.maneuvers.begin(), res.maneuvers.end(), [](Maneuver const & m) { return m.reachedMs >= 0; })));
      s.motorway = legs[i].motorway;
      s.farmRight = !s.motorway && i % 3 == 0;
      s.wild = s.motorway && i % 2 == 1;
      return s;
    });
    nav.SetRouteAheadLookup([&rng](double maxM)
    {
      RouteAhead a;
      a.searchedM = maxM;
      if (rng() % 4 == 0)
        a.stops.push_back({Stopover::RestArea, maxM * 0.6});
      return a;
    });
    nav.SetStopSpotLookup([](LatLon const &) { return StopSpot::None; });
    nav.SetNearbyThingsLookup([](LatLon const &) { return std::set<std::string>{"fuel", "water_tower", "farm"}; });
    nav.SetRegionLookup([](LatLon const &) { return std::optional<std::string>("Arkansas"); });
    nav.SetTrafficLightLookup([](LatLon const &) { return false; });
    nav.SetRoadDistanceLookup([](LatLon const &) { return 5.0; });
    nav.SetMusicLookup([] { return false; });
    nav.SetBridgeLookup([](LatLon const &, double) { return Bridge::None; });
  }

  SimPhone phone;
  double t = startMs;
  double speed = 0;
  size_t leg = 0;
  double left = legs[0].lengthM;
  uint32_t turnId = 1;
  double lightWaitUntil = -1;
  bool offRoute = false;
  double offSince = 0;
  bool finished = false;
  double finishedAt = 0;

  auto snapshot = [&]()
  {
    RouteSnapshot r;
    r.state = finished ? RouteState::Finished : offRoute ? RouteState::OffRoute : RouteState::OnRoute;
    r.turnId = turnId;
    r.turn = legs[leg].turn;
    r.nextStreet = legs[leg].street;
    r.distToTurnM = std::max(0.0, left);
    if (leg + 1 < legs.size())
    {
      r.thenTurn = legs[leg + 1].turn;
      r.thenStreet = legs[leg + 1].street;
      r.thenGapM = legs[leg + 1].lengthM;
    }
    r.destination = "Grandma's";
    double eta = 0;
    for (size_t i = leg; i < legs.size(); ++i)
      eta += legs[i].lengthM / legs[i].limitMps;
    r.etaSeconds = eta;
    for (auto const & l : legs)
      r.plannedTurns.push_back(l.turn);
    r.origin = {36.3725, -94.2088};
    r.destinationPoint = {lat + 0.01, lon + 0.01};
    r.routeBearingDeg = bearing;
    return r;
  };

  auto take = [&]()
  {
    auto lines = nav.TakeLines();
    for (auto const & l : lines)
      if (l.priority == Priority::Nav && l.category == Category::Instruction && leg < res.maneuvers.size())
        res.maneuvers[leg].directed = true;
    phone.Queue(std::move(lines), t, leg);
  };

  for (int step = 0; step < 6 * 3600; ++step)
  {
    t += 1000;
    SimLeg const & L = legs[leg];
    // The driver: a bit over the limit, slowing for the turn, waiting at red lights.
    double target = L.limitMps * 1.06;
    double const turnSpeed = L.turn == TurnKind::Destination ? 3 : L.turn == TurnKind::ExitRight ? 15 : 7;
    if (left < 120)
      target = std::min(target, turnSpeed + (left / 120) * (target - turnSpeed));
    if (L.light && left < 25 && lightWaitUntil < 0 && !offRoute)
      lightWaitUntil = t + uni(0, 35000);
    if (lightWaitUntil > t)
      target = 0;
    double const accel = target > speed ? 2.0 : -3.0;
    speed = accel > 0 ? std::min(target, speed + accel) : std::max(target, speed + accel);
    double const moved = speed;
    move(moved);

    if (offRoute)
    {
      // The router comes back with a new way after a few seconds; it starts with this leg again.
      if (t - offSince > 6000)
      {
        offRoute = false;
        left = 300;
        turnId += 100;
      }
    }
    else if (!finished)
    {
      left -= moved;
      if (left <= 0)
      {
        auto & m = res.maneuvers[leg];
        if (m.reachedMs < 0)
        {
          m.reachedMs = t;
          m.speedMps = speed;
        }
        if (L.turn == TurnKind::Destination)
        {
          finished = true;
          finishedAt = t;
        }
        else if (o.missTurns && L.turn != TurnKind::ExitRight && chance(0.05))
        {
          offRoute = true;
          offSince = t;
          m.missed = true;
        }
        else
        {
          bearing = bearingAfter(L.turn, bearing);
          ++leg;
          ++turnId;
          left = legs[leg].lengthM + left;
          lightWaitUntil = -1;
        }
      }
    }

    Fix f;
    f.tMs = t;
    f.lat = lat;
    f.lon = lon;
    f.speedMps = speed;
    f.bearingDeg = bearing;
    f.speedLimitMps = legs[leg].limitMps;
    f.accuracyM = 5;
    RouteSnapshot const snap = snapshot();
    nav.OnFix(f, snap);
    take();
    // Directions also go out between fixes, right before the phone speaks.
    nav.Poll(snap);
    take();
    phone.Advance(t, res.spoken);

    if (finished && t - finishedAt > 30000)
    {
      res.arrived = true;
      nav.OnFix(f, RouteSnapshot{});
      take();
      break;
    }
  }
  phone.Advance(1e18, res.spoken);
  res.endMs = t;

  // Which "Now, ..." went with which maneuver: the first one handed over while it was next.
  for (auto const & s : res.spoken)
    if (s.line.text.rfind("Now, ", 0) == 0 && s.leg < res.maneuvers.size() && !res.maneuvers[s.leg].finalEndMs)
      res.maneuvers[s.leg].finalEndMs = s.endMs;
  return res;
}
}  // namespace backseat_tests
