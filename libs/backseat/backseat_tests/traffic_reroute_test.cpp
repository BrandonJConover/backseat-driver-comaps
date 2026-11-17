// Traffic-aware routing from the phone's own GPS (no free live traffic feed exists for the US). Once a
// jam has lasted a bit, the navigator hands it to the host as a JamReport; the host marks it on the
// router's traffic map and rebuilds the route, then says what came of it (OnTrafficReroute).
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/navigator.hpp"
#include "backseat/units.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
constexpr double kRerouteT0 = 1.7e12;

struct JamDrive
{
  explicit JamDrive(uint32_t seed = 3)
  {
    EngineConfig c;
    c.rng = SeededRng(seed);
    nav = std::make_unique<Navigator>(c);
    nav->SetJamReporter([this](std::optional<JamReport> const & r) { reports.push_back(r); });
    route.state = RouteState::OnRoute;
    route.destinationPoint = {40.5, -105};
    route.distToTurnM = 50000;
    route.turn = TurnKind::Right;
    route.nextStreet = "Elm Street";
    route.turnId = 1;
  }

  void Feed(int fromS, int toS, double mps)
  {
    for (int i = fromS; i < toS; ++i)
    {
      Fix f;
      f.tMs = kRerouteT0 + i * 1000.0;
      lat += mps / 111000;
      route.distToTurnM = std::max(100.0, route.distToTurnM - mps);
      f.lat = lat;
      f.lon = -105;
      f.speedMps = mps;
      f.bearingDeg = 0;
      f.speedLimitMps = limit;
      f.accuracyM = 5;
      nav->OnFix(f, route);
      for (auto & l : nav->TakeLines())
        said.push_back(l);
    }
  }

  int Count(std::string const & key) const
  {
    int n = 0;
    for (auto const & l : said)
      n += l.key == key;
    return n;
  }
  /// Lines whose key starts with `prefix`: the jam call has route-blame variants ("traffic.start.onRoute").
  int CountPrefix(std::string const & prefix) const
  {
    int n = 0;
    for (auto const & l : said)
      n += l.key.rfind(prefix, 0) == 0;
    return n;
  }
  int Reports() const
  {
    int n = 0;
    for (auto const & r : reports)
      n += r.has_value();
    return n;
  }

  std::unique_ptr<Navigator> nav;
  RouteSnapshot route;
  double lat = 39.7;
  double limit = FromDisplay(65, Units::Mph);
  std::vector<Commentary> said;
  std::vector<std::optional<JamReport>> reports;
};
}  // namespace

// A jam is reported once it has lasted a little past the call, with a stretch behind the car that
// covers what it crept through and a guess ahead, and the speed it's moving at.
TEST(JamIsReportedForRouting)
{
  JamDrive d;
  d.Feed(0, 90, d.limit);
  d.Feed(90, 200, 2);
  CHECK(d.CountPrefix("traffic.start") + d.CountPrefix("traffic.stopGo") + d.CountPrefix("traffic.standstill") >= 1);
  CHECK_EQUAL(d.Reports(), 1);
  auto const & r = *d.reports.front();
  CHECK(r.behindM >= 200 && r.behindM <= 3000);
  CHECK(r.aheadM >= 1500 && r.aheadM <= 5000);
  CHECK(r.speedShare < 0.3);
}

// Cruising along: nothing to report.
TEST(NoJamNoReport)
{
  JamDrive d;
  d.Feed(0, 600, d.limit * 0.9);
  CHECK(d.reports.empty());
}

// A long jam is asked about again every five minutes with a longer reach ahead, three times a trip
// at most, and taken off the map once the car is moving again.
TEST(LongJamAsksAgainThenClears)
{
  JamDrive d;
  d.Feed(0, 90, d.limit);
  d.Feed(90, 1800, 1.5);
  CHECK_EQUAL(d.Reports(), 3);
  CHECK(d.reports[1]->aheadM > d.reports[0]->aheadM - 1);
  d.Feed(1800, 2000, d.limit);
  CHECK(!d.reports.back().has_value());
}

// Walking or biking never asks the router about traffic.
TEST(OnlyDrivesReportJams)
{
  JamDrive d;
  d.nav->SetTravelMode(TravelMode::Bike);
  // Slow next to a fast road's limit, which is a jam in a car (a car's speed would make it a drive).
  d.Feed(0, 400, 2);
  CHECK_EQUAL(d.Reports(), 0);
  SetLineTravelMode(TravelMode::Drive);
}

// A way around: said with the road and the minutes saved. The new route's first maneuver isn't a
// turn the driver made, and the swap isn't a reroute against their grade.
TEST(WayAroundIsAnnouncedAndNotHeldAgainstTheDriver)
{
  JamDrive d;
  d.Feed(0, 90, d.limit);
  d.Feed(90, 200, 2);
  int const reroutesBefore = d.nav->Engine()->Stats().reroutes;
  int const turnsBefore = d.nav->Engine()->Stats().turnsCompleted;
  TrafficReroute r;
  r.newRoute = true;
  r.savedS = 7 * 60;
  r.via = "Colfax Avenue";
  d.nav->OnTrafficReroute(kRerouteT0 + 200000, r);
  d.route.turnId = 9;
  d.route.turn = TurnKind::Left;
  d.route.nextStreet = "Colfax Avenue";
  d.route.distToTurnM = 300;
  d.Feed(200, 205, 2);
  CHECK_EQUAL(d.Count("traffic.reroute"), 1);
  bool named = false;
  for (auto const & l : d.said)
    named = named || (l.key == "traffic.reroute" && (l.text.find("Colfax") != std::string::npos ||
                                                      l.text.find("COLFAX") != std::string::npos));
  CHECK(named);
  CHECK_EQUAL(d.nav->Engine()->Stats().reroutes, reroutesBefore);
  CHECK_EQUAL(d.Count("rerouted"), 0);
  CHECK_EQUAL(d.nav->Engine()->Stats().turnsCompleted, turnsBefore);
  // The new direction is called.
  bool left = false;
  for (auto const & l : d.said)
    left = left || (l.priority == Priority::Nav && l.text.find("Colfax") != std::string::npos);
  CHECK(left);
}

// A small win just names the road.
TEST(EvenRerouteNamesTheRoad)
{
  JamDrive d;
  d.Feed(0, 90, d.limit);
  d.Feed(90, 200, 2);
  TrafficReroute r;
  r.newRoute = true;
  r.savedS = 30;
  r.via = "I-25";
  d.nav->OnTrafficReroute(kRerouteT0 + 200000, r);
  d.Feed(200, 202, 2);
  CHECK_EQUAL(d.Count("traffic.rerouteEven"), 1);
  CHECK_EQUAL(d.Count("traffic.reroute"), 0);
}

// No way around: how late it makes us, said once, and again only when the jam has grown by five
// minutes. The rebuilt route's renumbered maneuver isn't said a second time.
TEST(NoWayAroundSaysTheDelayOnce)
{
  JamDrive d;
  d.Feed(0, 90, d.limit);
  d.Feed(90, 200, 2);
  int const turnsBefore = d.nav->Engine()->Stats().turnsCompleted;
  size_t const navBefore = std::count_if(d.said.begin(), d.said.end(),
                                         [](Commentary const & l) { return l.priority == Priority::Nav; });
  TrafficReroute r;
  r.delayS = 9 * 60;
  d.nav->OnTrafficReroute(kRerouteT0 + 200000, r);
  d.route.turnId = 4;  // same right turn onto Elm Street, numbered anew
  d.Feed(200, 210, 2);
  CHECK_EQUAL(d.Count("traffic.noWayAround"), 1);
  size_t const navAfter = std::count_if(d.said.begin(), d.said.end(),
                                        [](Commentary const & l) { return l.priority == Priority::Nav; });
  CHECK_EQUAL(navAfter, navBefore);
  CHECK_EQUAL(d.nav->Engine()->Stats().turnsCompleted, turnsBefore);
  r.delayS = 11 * 60;
  d.nav->OnTrafficReroute(kRerouteT0 + 500000, r);
  d.Feed(210, 215, 2);
  CHECK_EQUAL(d.Count("traffic.noWayAround"), 1);
  r.delayS = 15 * 60;
  d.nav->OnTrafficReroute(kRerouteT0 + 800000, r);
  d.Feed(215, 220, 2);
  CHECK_EQUAL(d.Count("traffic.noWayAround"), 2);
}

// A jam that costs only a minute or two isn't worth a line.
TEST(SmallDelayStaysQuiet)
{
  JamDrive d;
  d.Feed(0, 90, d.limit);
  d.Feed(90, 200, 2);
  TrafficReroute r;
  r.delayS = 90;
  d.nav->OnTrafficReroute(kRerouteT0 + 200000, r);
  d.Feed(200, 205, 2);
  CHECK_EQUAL(d.Count("traffic.noWayAround"), 0);
}

// Brandon's line leads the no-way-around pool, word for word, and the new keys have three tiers
// with nothing left unfilled.
TEST(TrafficRerouteLinesAreComplete)
{
  auto const & table = LineData();
  for (char const * key : {"traffic.reroute", "traffic.rerouteEven", "traffic.noWayAround"})
  {
    auto const it = table.find(key);
    CHECK(it != table.end());
    if (it == table.end())
      continue;
    CHECK_EQUAL(it->second.size(), 3u);
    for (auto const & tier : it->second)
      CHECK(tier.size() >= 3);
  }
  CHECK_EQUAL(table.at("traffic.noWayAround")[0][0],
              std::string("I could reroute you, but actually I'm having a great time."));
  // Brandon's jam lines are untouched.
  CHECK_EQUAL(table.at("traffic.stopGo")[0][0],
              std::string("So indecisive. First you go. Then you stop. Make up your mind already."));
}
