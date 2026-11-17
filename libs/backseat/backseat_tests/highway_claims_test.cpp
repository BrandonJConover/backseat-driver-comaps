// Loop B144 (BI-127): "Still on this highway. I've started counting the lines on the road." and
// "Fun fact: we are technically parked on a highway." went out on any road, a city avenue included.
// When the map says the car is not on a motorway, straight.later and traffic.standstill skip the lines
// naming a highway; on a motorway, or with no surroundings lookup (unknown), nothing changes.
// BACKSEAT_SOAK_VERBOSE=1 prints every straight.later / traffic.standstill line picked.
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"
#include "backseat/road_bits.hpp"
#include "backseat/units.hpp"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

using namespace backseat;
using namespace backseat_tests;

namespace
{
constexpr double kStartMs = 1.7e12;

enum class Road
{
  Unknown,
  City,
  Motorway
};

struct Picks
{
  int lines = 0;
  int highway = 0;
  std::string first;
};

void Wire(Navigator & nav, Road road)
{
  if (road == Road::Unknown)
    return;
  bool const motorway = road == Road::Motorway;
  nav.SetSurroundingsLookup([motorway](LatLon const &, double)
  {
    Surroundings s;
    s.motorway = motorway;
    return s;
  });
}

void Count(Picks & p, Commentary const & l, char const * key)
{
  if (l.key != key)
    return;
  if (p.lines == 0)
    p.first = l.text;
  ++p.lines;
  p.highway += LineClaimsHighway(l.text);
  if (std::getenv("BACKSEAT_SOAK_VERBOSE"))
    std::fprintf(stderr, "  [%s] %s\n", key, l.text.c_str());
}

/// One straight hour and a half at 25 m/s toward a turn 150 km away, with a five minute dead stop in
/// the lane (traffic) every quarter hour.
void Drive(Road road, uint32_t seed, Picks & later, Picks & standstill)
{
  EngineConfig c;
  c.rng = SeededRng(seed);
  Navigator nav(c);
  Wire(nav, road);
  double const limit = FromDisplay(55, Units::Mph);
  RouteSnapshot route;
  route.state = RouteState::OnRoute;
  route.destinationPoint = {41.5, -105};
  route.turn = TurnKind::Right;
  route.turnId = 1;
  double lat = 39.7, left = 150000;
  for (int i = 0; i < 5400; ++i)
  {
    int const q = i % 900;
    bool const stopped = i >= 600 && q >= 500 && q < 800;
    bool const crawl = i >= 600 && ((q >= 470 && q < 500) || (q >= 800 && q < 830));
    double const mps = stopped ? 0 : crawl ? 3 : 25;
    lat += mps / 111000;
    left -= mps;
    route.distToTurnM = left;
    Fix f;
    f.tMs = kStartMs + i * 1000.0;
    f.lat = lat;
    f.lon = -105;
    f.speedMps = mps;
    f.bearingDeg = 0;
    f.speedLimitMps = limit;
    f.accuracyM = 5;
    nav.OnFix(f, route);
    for (auto const & l : nav.TakeLines())
    {
      Count(later, l, "straight.later");
      Count(standstill, l, "traffic.standstill");
    }
  }
}
}  // namespace

TEST(LineClaimsHighwayFindsWholeWords)
{
  CHECK(LineClaimsHighway("Still on this highway. I've started counting the lines on the road."));
  CHECK(LineClaimsHighway("Fun fact: we are technically parked on a highway."));
  CHECK(LineClaimsHighway("Two interstates and a FREEWAY."));
  CHECK(LineClaimsHighway("The motorway again."));
  CHECK(!LineClaimsHighway("Same road, many miles later."));
  CHECK(!LineClaimsHighway("Highwayman is a song."));
}

// Both pools keep plenty to say off the highway.
TEST(HighwayFreePoolsStayFull)
{
  for (char const * key : {"straight.later", "traffic.standstill"})
    for (int tier = 0; tier <= 2; ++tier)
    {
      int fit = 0;
      for (auto const & l : LinesFor(key, tier))
        fit += !LineClaimsHighway(l);
      CHECK(fit >= 2);
    }
}

// City, motorway and unknown roads, 30 long straight drives each with jams along the way.
TEST(HighwayLinesOnlyOnAHighway)
{
  Picks cityLater, cityStill, mwLater, mwStill, unkLater, unkStill;
  for (uint32_t seed = 1; seed <= 30; ++seed)
  {
    Picks a, b, c, d, e, f;
    Drive(Road::City, seed, a, b);
    Drive(Road::Motorway, seed, c, d);
    Drive(Road::Unknown, seed, e, f);
    // A key's first ever line is option 0: "Still on this highway" on a highway or unknown road,
    // something else in town.
    if (seed == 1)
    {
      CHECK(!a.first.empty() && !LineClaimsHighway(a.first));
      CHECK(LineClaimsHighway(c.first));
      CHECK_EQUAL(c.first, e.first);
    }
    for (auto [to, from] : {std::pair{&cityLater, &a}, {&cityStill, &b}, {&mwLater, &c}, {&mwStill, &d},
                            {&unkLater, &e}, {&unkStill, &f}})
    {
      to->lines += from->lines;
      to->highway += from->highway;
    }
  }
  std::fprintf(stderr, "  highway claims: city later %d/%d still %d/%d, motorway later %d/%d still %d/%d, "
                       "unknown later %d/%d still %d/%d\n",
               cityLater.highway, cityLater.lines, cityStill.highway, cityStill.lines, mwLater.highway,
               mwLater.lines, mwStill.highway, mwStill.lines, unkLater.highway, unkLater.lines, unkStill.highway,
               unkStill.lines);
  CHECK(cityLater.lines >= 30);
  CHECK(cityStill.lines >= 60);
  CHECK_EQUAL(cityLater.highway, 0);
  CHECK_EQUAL(cityStill.highway, 0);
  // On a motorway and on an unknown road the highway lines still come up.
  CHECK(mwLater.highway > 0);
  CHECK(unkLater.highway > 0);
  CHECK(mwStill.highway + unkStill.highway > 0);
}
