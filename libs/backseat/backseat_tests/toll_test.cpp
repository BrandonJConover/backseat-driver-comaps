// Loop B132 (BI-113): a toll stretch gets one "toll.start" line as it begins (within 200 m of its
// start, or on it), again only for a new stretch after more than 2 km of free road, and never when
// the driver said "avoid tolls" this trip. No prices or numbers. Junk distances say nothing.
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

RouteSnapshot Route(std::optional<double> tollAheadM, double distToTurnM = 5000)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Right;
  r.distToTurnM = distToTurnM;
  r.nextStreet = "Turnpike Road";
  r.destination = "the city";
  r.etaSeconds = 3600;
  r.plannedTurns = {TurnKind::Right, TurnKind::Destination};
  r.destinationPoint = {40.5, -105};
  r.tollAheadM = tollAheadM;
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

TEST(TollStartSaidOnceAsItBegins)
{
  Drive d;
  d.Run(30, 15, [](int) { return Route(5000); });
  CHECK_EQUAL(Count(d.said, "toll.start"), 0);
  d.Run(10, 15, [](int) { return Route(150); });
  // On the toll road for a while: still just the one line.
  d.Run(120, 25, [](int) { return Route(0); });
  CHECK_EQUAL(Count(d.said, "toll.start"), 1);
}

TEST(TollNotSaidFarAhead)
{
  Drive d;
  d.Run(90, 15, [](int) { return Route(600); });
  CHECK_EQUAL(Count(d.said, "toll.start"), 0);
}

TEST(TollSecondStretchAfterLongFreeRoad)
{
  Drive d;
  d.Run(60, 25, [](int) { return Route(0); });
  // Off the toll road with 3 km of free road to the next one.
  d.Run(60, 25, [](int s) { return Route(3000.0 - (s - 60) * 25.0); });
  d.Run(60, 25, [](int) { return Route(0); });
  CHECK_EQUAL(Count(d.said, "toll.start"), 2);
}

TEST(TollShortGapIsTheSameTollRoad)
{
  Drive d;
  d.Run(60, 25, [](int) { return Route(0); });
  // Only 1.5 km of free road between plazas: one toll road, one line.
  d.Run(50, 25, [](int s) { return Route(std::max(0.0, 1500.0 - (s - 60) * 30.0)); });
  d.Run(60, 25, [](int) { return Route(0); });
  CHECK_EQUAL(Count(d.said, "toll.start"), 1);
}

TEST(TollSkippedAfterAvoidTollsSaid)
{
  Drive d;
  d.Run(30, 15, [](int) { return Route(5000); });
  d.nav.OnDriverSaid(30000, "avoid tolls");
  d.Run(120, 15, [](int) { return Route(0); });
  CHECK_EQUAL(Count(d.said, "toll.start"), 0);
}

TEST(TollSaidAgainAfterAllowTolls)
{
  Drive d;
  d.Run(30, 15, [](int) { return Route(5000); });
  d.nav.OnDriverSaid(30000, "avoid tolls");
  d.nav.OnDriverSaid(31000, "actually tolls are fine");
  d.Run(120, 15, [](int) { return Route(0); });
  CHECK_EQUAL(Count(d.said, "toll.start"), 1);
}

TEST(TollUnsetOrJunkSaysNothing)
{
  for (std::optional<double> junk : {std::optional<double>(), std::optional<double>(std::numeric_limits<double>::quiet_NaN()),
                                     std::optional<double>(-50.0), std::optional<double>(1e12),
                                     std::optional<double>(std::numeric_limits<double>::infinity())})
  {
    Drive d;
    d.Run(120, 15, [&](int) { return Route(junk); });
    CHECK_EQUAL(Count(d.said, "toll.start"), 0);
  }
}

TEST(CleanRouteDropsJunkTollDistance)
{
  RouteSnapshot r = Route(std::numeric_limits<double>::quiet_NaN());
  CHECK(!CleanRoute(r, 0).tollAheadM);
  r.tollAheadM = -5;
  CHECK(!CleanRoute(r, 0).tollAheadM);
  r.tollAheadM = 0;
  CHECK(CleanRoute(r, 0).tollAheadM == 0.0);
  r.tollAheadM = 150;
  CHECK(CleanRoute(r, 0).tollAheadM == 150.0);
}

TEST(TollQuietInDirectionsOnly)
{
  Drive d;
  d.nav.SetTalkLevel(TalkLevel::DirectionsOnly);
  d.Run(120, 15, [](int) { return Route(0); });
  CHECK_EQUAL(Count(d.said, "toll.start"), 0);
}

TEST(TollLinesNoPricesNoTemptation)
{
  auto const it = LineData().find("toll.start");
  CHECK(it != LineData().end());
  if (it == LineData().end())
    return;
  int n = 0;
  for (auto const & tier : it->second)
  {
    CHECK(!tier.empty());
    for (auto line : tier)
    {
      ++n;
      CHECK(std::none_of(line.begin(), line.end(), [](unsigned char c) { return std::isdigit(c) || c == '$'; }));
      std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) { return std::tolower(c); });
      for (char const * bad : {"screen", "look at", "hurry", "floor it", "race", "beat the", "seatbelt", "crash"})
        CHECK(line.find(bad) == std::string::npos);
    }
  }
  CHECK(n >= 10);
}
