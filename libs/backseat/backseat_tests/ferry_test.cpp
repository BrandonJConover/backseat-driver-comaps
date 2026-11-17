// Loop B131 (BI-112): a ferry on the route gets one "ferry.ahead" line when it's 300-1500 m out
// (held while a turn call is due), and one "ferry.aboard" line once parked on the boat. The boat
// explains the stop: no longStop, pull-over or "moving again" lines. Junk distances say nothing.
#include "check.hpp"

#include "backseat/lines_data.hpp"
#include "backseat/navigator.hpp"
#include "backseat/units.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
EngineConfig Config()
{
  EngineConfig c;
  c.rng = SeededRng(11);
  return c;
}

RouteSnapshot Route(std::optional<double> ferryAheadM, double distToTurnM = 5000, bool onFerry = false)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Right;
  r.distToTurnM = distToTurnM;
  r.nextStreet = "Harbor Road";
  r.destination = "the island";
  r.etaSeconds = 3600;
  r.plannedTurns = {TurnKind::Right, TurnKind::Destination};
  r.destinationPoint = {40.5, -105};
  r.ferryAheadM = ferryAheadM;
  r.onFerry = onFerry;
  return r;
}

Fix At(double tMs, double mps, double lat)
{
  Fix f;
  f.tMs = tMs;
  f.lat = lat;
  f.lon = -105;
  f.speedMps = mps;
  return f;
}

int Count(std::vector<Commentary> const & lines, std::string const & key)
{
  return static_cast<int>(std::count_if(lines.begin(), lines.end(), [&](Commentary const & c) { return c.key == key; }));
}

struct Drive
{
  Navigator nav{Config()};
  std::vector<Commentary> said;
  double lat = 39.7;
  int t = 0;

  /// `seconds` of fixes at `mps` with the route from `route(second)`.
  template <class F>
  void Run(int seconds, double mps, F route)
  {
    for (int end = t + seconds; t < end; ++t)
    {
      nav.OnFix(At(t * 1000.0, mps, lat), route(t));
      if (mps > 0)
        lat += mps / 111000.0;
      for (auto & l : nav.TakeLines())
        said.push_back(std::move(l));
    }
  }
};
}  // namespace

TEST(FerryAheadSaidOnceInRange)
{
  Drive d;
  d.Run(120, 15, [](int) { return Route(900); });
  CHECK_EQUAL(Count(d.said, "ferry.ahead"), 1);
}

TEST(FerryAheadWaitsUntilInRange)
{
  Drive d;
  d.Run(60, 15, [](int) { return Route(5000); });
  CHECK_EQUAL(Count(d.said, "ferry.ahead"), 0);
  // Closing in: said once it's within 1500 m, not before.
  d.Run(120, 15, [](int s) { return Route(std::max(400.0, 1600.0 - (s - 60) * 15.0)); });
  CHECK_EQUAL(Count(d.said, "ferry.ahead"), 1);
}

TEST(FerryAheadHeldForATurnCall)
{
  Drive d;
  d.Run(90, 15, [](int) { return Route(900, 400); });
  CHECK_EQUAL(Count(d.said, "ferry.ahead"), 0);
  // The turn's done and the next one is far: the ferry line can go now.
  d.Run(60, 15, [](int) { return Route(700, 4000); });
  CHECK_EQUAL(Count(d.said, "ferry.ahead"), 1);
}

TEST(FerryAheadJunkDistancesSayNothing)
{
  for (double junk : {std::numeric_limits<double>::quiet_NaN(), -800.0, 1e12,
                      std::numeric_limits<double>::infinity()})
  {
    Drive d;
    d.Run(90, 15, [&](int) { return Route(junk); });
    CHECK_EQUAL(Count(d.said, "ferry.ahead"), 0);
  }
  Drive unset;
  unset.Run(90, 15, [](int) { return Route(std::nullopt); });
  CHECK_EQUAL(Count(unset.said, "ferry.ahead"), 0);
  CHECK_EQUAL(Count(unset.said, "ferry.aboard"), 0);
}

TEST(CleanRouteDropsJunkFerryDistance)
{
  RouteSnapshot r = Route(std::numeric_limits<double>::quiet_NaN());
  CHECK(!CleanRoute(r, 0).ferryAheadM);
  r.ferryAheadM = -5;
  CHECK(!CleanRoute(r, 0).ferryAheadM);
  r.ferryAheadM = 1e12;
  CHECK(!CleanRoute(r, 0).ferryAheadM);
  r.ferryAheadM = 900;
  CHECK(CleanRoute(r, 0).ferryAheadM == 900.0);
}

TEST(FerryAboardOnceParkedAndTheBoatExplainsTheStop)
{
  Drive d;
  d.Run(60, 15, [](int) { return Route(900); });
  CHECK_EQUAL(Count(d.said, "ferry.ahead"), 1);
  // Rolling onto the boat: no aboard line yet.
  d.Run(10, 3, [](int) { return Route(std::nullopt, 5000, true); });
  CHECK_EQUAL(Count(d.said, "ferry.aboard"), 0);
  // Parked for a 10-minute crossing.
  d.Run(600, 0, [](int) { return Route(std::nullopt, 5000, true); });
  CHECK_EQUAL(Count(d.said, "ferry.aboard"), 1);
  CHECK_EQUAL(Count(d.said, "pullOver"), 0);
  CHECK_EQUAL(Count(d.said, "pullOver.limit"), 0);
  for (auto const & c : d.said)
    CHECK(c.category != Category::LongStop && c.category != Category::PullOver &&
          c.category != Category::UnplannedStop);
  std::size_t const before = d.said.size();
  // Driving off the ramp: no "you abandoned me for ten minutes" either.
  d.Run(30, 12, [](int) { return Route(std::nullopt); });
  for (std::size_t i = before; i < d.said.size(); ++i)
    CHECK(d.said[i].category != Category::Abandoned);
  CHECK_EQUAL(Count(d.said, "ferry.ahead"), 1);
}

TEST(FerryNextFerryGetsItsOwnLines)
{
  Drive d;
  d.Run(60, 15, [](int) { return Route(900); });
  d.Run(60, 0, [](int) { return Route(std::nullopt, 5000, true); });
  d.Run(120, 15, [](int) { return Route(std::nullopt); });
  d.Run(60, 15, [](int) { return Route(1200); });
  d.Run(60, 0, [](int) { return Route(std::nullopt, 5000, true); });
  CHECK_EQUAL(Count(d.said, "ferry.ahead"), 2);
  CHECK_EQUAL(Count(d.said, "ferry.aboard"), 2);
}

TEST(FerryQuietInDirectionsOnly)
{
  Drive d;
  d.nav.SetTalkLevel(TalkLevel::DirectionsOnly);
  d.Run(60, 15, [](int) { return Route(900); });
  d.Run(60, 0, [](int) { return Route(std::nullopt, 5000, true); });
  CHECK_EQUAL(Count(d.said, "ferry.ahead"), 0);
  CHECK_EQUAL(Count(d.said, "ferry.aboard"), 0);
}

TEST(FerryLinesNeverTemptTheDriver)
{
  int n = 0;
  for (char const * key : {"ferry.ahead", "ferry.aboard"})
  {
    auto const it = LineData().find(key);
    CHECK(it != LineData().end());
    if (it == LineData().end())
      continue;
    for (auto const & tier : it->second)
    {
      CHECK(!tier.empty());
      for (auto line : tier)
      {
        ++n;
        std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) { return std::tolower(c); });
        for (char const * bad : {"screen", "look at", "hurry", "floor it", "race", "beat the", "seatbelt"})
          CHECK(line.find(bad) == std::string::npos);
      }
    }
  }
  CHECK(n >= 16);
}
