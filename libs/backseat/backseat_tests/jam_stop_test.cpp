// Stuck in a jam, the driver never left the car. Before loop B52, sitting still in a lane for three
// minutes or more (the navigator already calling it traffic) ended with "You left me in the car!
// Phones have needs too" as the jam cleared, and at the three-minute mark "Why are we stopping
// here?" landed on top of the traffic lines. The roadside pull-over line already knew better.
// BACKSEAT_SOAK_VERBOSE=1 prints every remark from the stop to a minute after it.
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/navigator.hpp"
#include "backseat/units.hpp"

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

using namespace backseat;
using namespace backseat_tests;

namespace
{
constexpr double kJamStartMs = 1.7e12;
bool JamVerbose() { return std::getenv("BACKSEAT_SOAK_VERBOSE") != nullptr; }
bool BlamesTheStop(Category c)
{
  return c == Category::Abandoned || c == Category::UnplannedStop || c == Category::PullOver ||
         c == Category::LongStop;
}
}  // namespace

// Stopped dead in a lane for 3, 10 and 15 minutes, 40 seeds each. Whenever the navigator called it
// traffic during the stop, nothing after that says the driver stopped on purpose or left the car.
TEST(JamStopIsNotAbandonment)
{
  int jammed = 0, failed = 0;
  for (double const len : {180.0, 600.0, 900.0})
    for (uint32_t seed = 1; seed <= 40; ++seed)
    {
      SimOptions o{TripShape::Town, seed};
      o.missTurns = false;
      o.stopFromS = 50 + seed * 5;
      o.stopS = len;
      o.stopParked = false;
      EngineConfig c;
      c.rng = SeededRng(seed);
      Navigator nav(c);
      auto const r = SimulateTrip(nav, o);
      double const back = o.stopFromS + o.stopS;
      double trafficAt = -1;
      int blamed = 0;
      std::vector<double> longStops;
      for (auto const & s : r.spoken)
      {
        double const q = (s.queuedMs - kJamStartMs) / 1000;
        if (q < o.stopFromS || q > back + 60)
          continue;
        if (trafficAt < 0 && s.line.category == Category::Traffic && q < back)
          trafficAt = q;
        if (trafficAt >= 0 && BlamesTheStop(s.line.category))
          ++blamed;
        if (s.line.category == Category::LongStop && q < back)
          longStops.push_back(q);
        if (JamVerbose())
          std::fprintf(stderr, "  jam len=%.0f seed=%u +%.0fs [%s] %s\n", len, seed, q - o.stopFromS,
                       s.line.key.c_str(), s.line.text.c_str());
      }
      if (trafficAt < 0)
        continue;
      ++jammed;
      // BI-056: "We've been stopped for a while" never comes just before the jam is called.
      for (double const q : longStops)
        blamed += q < trafficAt;
      bool const bad = !r.arrived || blamed;
      failed += bad;
      if (bad)
        std::fprintf(stderr, "  jam len=%.0f seed=%u arrived=%d blamed=%d\n", len, seed, r.arrived, blamed);
    }
  // Enough of the seeds stop on a road fast enough to judge traffic on.
  CHECK(jammed >= 20);
  CHECK_EQUAL(failed, 0);
}

// A stop in a parking lot is still the driver's doing: walking off without the phone still gets
// "You left me in the car!" when the car moves again.
TEST(ParkedStopStillAbandons)
{
  int abandoned = 0;
  for (uint32_t seed = 1; seed <= 20; ++seed)
  {
    SimOptions o{TripShape::Town, seed};
    o.missTurns = false;
    o.stopFromS = 50 + seed * 5;
    o.stopS = 600;
    o.stopParked = true;
    EngineConfig c;
    c.rng = SeededRng(seed);
    Navigator nav(c);
    auto const r = SimulateTrip(nav, o);
    for (auto const & s : r.spoken)
      abandoned += s.line.category == Category::Abandoned;
  }
  CHECK(abandoned >= 18);
}

// BI-052: a jam that crawls before it stops is called traffic before the roadside line's 45 s, so
// "Why are we stopping? It's 50 not 0." never lands in it. (A dead stop straight from cruising speed
// is exactly what pulling over looks like, so that one still asks: NavigatorAsksWhyWeStoppedAtTheRoadside.)
TEST(CrawlingIntoAJamNeverHearsTheRoadsideLine)
{
  EngineConfig c;
  c.rng = SeededRng(7);
  Navigator nav(c);
  double const limit = FromDisplay(50, Units::Mph);
  double lat = 39.7;
  RouteSnapshot route;
  route.state = RouteState::OnRoute;
  route.destinationPoint = {40.5, -105};
  route.distToTurnM = 50000;
  route.turn = TurnKind::Right;
  route.turnId = 1;
  std::vector<Commentary> said;
  auto feed = [&](int fromS, int toS, double mps)
  {
    for (int i = fromS; i < toS; ++i)
    {
      Fix f;
      f.tMs = kJamStartMs + i * 1000.0;
      lat += mps / 111000;
      f.lat = lat;
      f.lon = -105;
      f.speedMps = mps;
      f.bearingDeg = 0;
      f.speedLimitMps = limit;
      f.accuracyM = 5;
      nav.OnFix(f, route);
      for (auto & l : nav.TakeLines())
        said.push_back(l);
    }
  };
  feed(0, 90, limit);
  feed(90, 130, 3);
  feed(130, 330, 0);
  bool traffic = false;
  int roadside = 0;
  for (auto const & l : said)
  {
    traffic = traffic || l.category == Category::Traffic;
    roadside += l.category == Category::PullOver;
  }
  CHECK(traffic);
  CHECK_EQUAL(roadside, 0);
}

// BI-052: three, five and eight minutes into a jam the car is still not parked, so the plate game
// counts a plate called out then instead of "Hey that's cheating. New rule, no parking lots."
// The same stop in a parking lot is parked from the three-minute mark.
TEST(JamIsNotParked)
{
  int jammed = 0, jamParked = 0, lots = 0, lotParked = 0;
  for (bool const lot : {false, true})
    for (uint32_t seed = 1; seed <= 30; ++seed)
    {
      SimOptions o{TripShape::Town, seed};
      o.missTurns = false;
      o.stopFromS = 50 + seed * 5;
      o.stopS = 600;
      o.stopParked = lot;
      int checks = 0, parked = 0;
      o.afterFix = [&](Navigator & nav, double, double tripS)
      {
        double const in = tripS - o.stopFromS;
        if (in == 180 || in == 300 || in == 480)
        {
          ++checks;
          parked += nav.Parked();
        }
      };
      EngineConfig c;
      c.rng = SeededRng(seed);
      Navigator nav(c);
      auto const r = SimulateTrip(nav, o);
      bool traffic = false;
      for (auto const & s : r.spoken)
      {
        double const q = (s.queuedMs - kJamStartMs) / 1000;
        traffic = traffic || (s.line.category == Category::Traffic && q >= o.stopFromS && q < o.stopFromS + 180);
      }
      if (checks != 3)
        continue;
      if (lot)
      {
        ++lots;
        lotParked += parked == 3;
      }
      else if (traffic)
      {
        ++jammed;
        jamParked += parked;
      }
    }
  CHECK(jammed >= 15);
  CHECK_EQUAL(jamParked, 0);
  CHECK(lots >= 25);
  CHECK_EQUAL(lotParked, lots);
}

namespace
{
/// Cruise at the limit for 90 s, then stop dead in the lane for `stopS`; every line said, with its second.
std::vector<std::pair<double, Commentary>> DeadStop(double limitMph, int stopS, uint32_t seed)
{
  EngineConfig c;
  c.rng = SeededRng(seed);
  Navigator nav(c);
  double const limit = FromDisplay(limitMph, Units::Mph);
  double lat = 39.7;
  RouteSnapshot route;
  route.state = RouteState::OnRoute;
  route.destinationPoint = {40.5, -105};
  route.distToTurnM = 50000;
  route.turn = TurnKind::Right;
  route.turnId = 1;
  std::vector<std::pair<double, Commentary>> said;
  for (int i = 0; i < 90 + stopS; ++i)
  {
    double const mps = i < 90 ? limit : 0;
    Fix f;
    f.tMs = kJamStartMs + i * 1000.0;
    lat += mps / 111000;
    f.lat = lat;
    f.lon = -105;
    f.speedMps = mps;
    f.bearingDeg = 0;
    f.speedLimitMps = limit;
    f.accuracyM = 5;
    nav.OnFix(f, route);
    for (auto & l : nav.TakeLines())
      said.emplace_back(i - 90.0, l);
  }
  return said;
}
}  // namespace

// BI-056: stopped dead from cruising speed on a 50 mph road, the engine's "We've been stopped for a
// while" used to land at +45 s and the traffic call at about +57 s. Now the longStop waits for the
// traffic window, and once traffic is called the jam's own lines take over.
TEST(DeadStopOnAFastRoadWaitsForTheTrafficCall)
{
  int called = 0;
  for (uint32_t seed = 1; seed <= 20; ++seed)
  {
    double trafficAt = -1;
    int early = 0;
    for (auto const & [at, line] : DeadStop(50, 240, seed))
    {
      if (trafficAt < 0 && line.category == Category::Traffic)
        trafficAt = at;
      if (line.category == Category::LongStop && (trafficAt < 0 || at < trafficAt + 20))
        ++early;
      if (JamVerbose())
        std::fprintf(stderr, "  dead stop seed=%u +%.0fs [%s] %s\n", seed, at, line.key.c_str(), line.text.c_str());
    }
    called += trafficAt >= 0;
    CHECK_EQUAL(early, 0);
  }
  CHECK_EQUAL(called, 20);
}

// A stop on a slow street (too slow to judge traffic on) is still remarked on at 45 s.
TEST(DeadStopOnASlowStreetStillHearsLongStop)
{
  int heard = 0;
  for (uint32_t seed = 1; seed <= 20; ++seed)
    for (auto const & [at, line] : DeadStop(25, 120, seed))
      if (line.category == Category::LongStop && at >= 44 && at <= 60)
      {
        ++heard;
        break;
      }
  CHECK(heard >= 18);
}
