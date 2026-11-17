// Loop B147 (BI-131): fixes with no GPS speed (tunnel extrapolation, network fixes, iOS invalid speed) used
// to read as a parked car once the 10 s carried speed ran out: a 150 s tunnel at 30 m/s got longStop,
// traffic.start, traffic.standstill and "why did you go this way". Now the car's progress (the route
// distance to the same turn shrinking, or a move between accurate fixes) is its speed; progress frozen
// still counts as a stop, and a fix with no evidence either way is skipped by the stop/slow timers.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"

#include <optional>
#include <vector>

using namespace backseat;

namespace
{
double constexpr kT0 = 1.7e12;
double constexpr kDegPerM = 1.0 / 111320;

EngineConfig UsSeeded()
{
  EngineConfig c;
  c.rng = SeededRng(11);
  return c;
}

struct UsDrive
{
  Navigator nav{UsSeeded()};
  double t = kT0;
  double lat = 36.1;
  double toTurnM = 60000;
  bool routeHasTurn = true;
  std::optional<double> limitMps = 31;
  double accuracyM = -1;
  std::vector<Commentary> said;

  RouteSnapshot Route() const
  {
    RouteSnapshot r;
    r.state = RouteState::OnRoute;
    if (routeHasTurn)
      r.turnId = 1;
    r.turn = TurnKind::ExitRight;
    r.nextStreet = "Highway 412";
    r.distToTurnM = toTurnM;
    r.destination = "Home";
    r.etaSeconds = 3600;
    r.plannedTurns = {TurnKind::ExitRight, TurnKind::Destination};
    r.origin = {36.1, -94.1};
    r.destinationPoint = {37.6, -94.1};
    return r;
  }

  /// One fix a second for `seconds`; the car covers `movingMps` whatever the GPS says its speed is.
  void Fixes(double seconds, double movingMps, bool speedKnown)
  {
    for (double end = t + seconds * 1000; t < end; t += 1000)
    {
      Fix f;
      f.tMs = t;
      f.lat = lat;
      f.lon = -94.1;
      f.speedMps = speedKnown ? movingMps : -1;
      f.bearingDeg = 0;
      f.speedLimitMps = limitMps;
      f.accuracyM = accuracyM;
      nav.OnFix(f, Route());
      toTurnM -= movingMps;
      lat += movingMps * kDegPerM;
      for (auto & l : nav.TakeLines())
        said.push_back(std::move(l));
    }
  }

  int Count(Category c, double fromT) const
  {
    int n = 0;
    for (auto const & l : said)
      n += l.category == c && l.t >= fromT;
    return n;
  }

  int Key(std::string const & key) const
  {
    int n = 0;
    for (auto const & l : said)
      n += l.key == key;
    return n;
  }

  /// Lines a parked or crawling car would get.
  int StopLines(double fromT) const
  {
    return Count(Category::LongStop, fromT) + Count(Category::Traffic, fromT) + Count(Category::TooSlow, fromT);
  }

  int Stops() const { return nav.Engine() ? nav.Engine()->StopCount() : -1; }
  double DistanceM() const { return nav.Engine() ? nav.Engine()->Stats().distanceM : 0; }
};
}  // namespace

TEST(UnknownSpeedTunnelWithRouteProgressIsDriving)
{
  UsDrive d;
  d.Fixes(120, 30, true);
  int const stops = d.Stops();
  double const before = d.DistanceM();
  double const tunnel = d.t;
  d.Fixes(150, 30, false);
  CHECK_EQUAL(d.StopLines(tunnel), 0);
  CHECK_EQUAL(d.Stops(), stops);
  // The tunnel's miles count (150 s at 30 m/s), not 10 s of them.
  CHECK(d.DistanceM() - before > 4000);
  d.Fixes(40, 30, true);
  CHECK_EQUAL(d.StopLines(tunnel), 0);
  CHECK_EQUAL(d.Stops(), stops);
  CHECK_EQUAL(d.Key("gps.back"), 1);
}

TEST(UnknownSpeedTunnelWithAccurateFixesIsDriving)
{
  // No turn to measure against, but accurate fixes that keep moving.
  UsDrive d;
  d.routeHasTurn = false;
  d.accuracyM = 10;
  d.Fixes(120, 30, true);
  int const stops = d.Stops();
  double const tunnel = d.t;
  d.Fixes(150, 30, false);
  CHECK_EQUAL(d.StopLines(tunnel), 0);
  CHECK_EQUAL(d.Stops(), stops);
}

TEST(UnknownSpeedFrozenPositionStillCountsAsAStop)
{
  // City street (no limit to judge traffic on): parked with the GPS giving no speed, and the position
  // and route distance frozen for two minutes. That is a stop, and longStop may say so.
  UsDrive d;
  d.limitMps.reset();
  d.Fixes(60, 12, true);
  int const stops = d.Stops();
  double const parked = d.t;
  d.Fixes(120, 0, false);
  CHECK_EQUAL(d.Stops(), stops + 1);
  CHECK(d.Count(Category::LongStop, parked) >= 1);
}

TEST(UnknownSpeedFrozenOnAHighwayIsAStopOrAJam)
{
  UsDrive d;
  d.Fixes(120, 30, true);
  int const stops = d.Stops();
  double const parked = d.t;
  d.Fixes(120, 0, false);
  CHECK_EQUAL(d.Stops(), stops + 1);
  CHECK(d.Count(Category::LongStop, parked) + d.Count(Category::Traffic, parked) >= 1);
}

TEST(UnknownSpeedWithNoEvidenceIsNotAStop)
{
  // No turn on the route and no accuracy: nothing says whether the car moves. Skipped, not parked.
  UsDrive d;
  d.routeHasTurn = false;
  d.Fixes(120, 30, true);
  int const stops = d.Stops();
  double const gap = d.t;
  d.Fixes(150, 30, false);
  CHECK_EQUAL(d.StopLines(gap), 0);
  CHECK_EQUAL(d.Stops(), stops);
}

TEST(UnknownSpeedJitterBetweenAccurateFixesIsParked)
{
  // Accurate fixes wobbling a few meters around one spot are a parked car, not a moving one.
  UsDrive d;
  d.limitMps.reset();
  d.routeHasTurn = false;
  d.accuracyM = 10;
  d.Fixes(60, 12, true);
  int const stops = d.Stops();
  double const parked = d.t;
  for (int i = 0; i < 120; ++i)
  {
    d.lat += (i % 2 ? 4 : -4) * kDegPerM;
    d.Fixes(1, 0, false);
  }
  CHECK_EQUAL(d.Stops(), stops + 1);
  CHECK(d.Count(Category::LongStop, parked) >= 1);
}
