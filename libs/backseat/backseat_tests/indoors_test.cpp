#include "check.hpp"

#include "backseat/history.hpp"
#include "backseat/indoors.hpp"
#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"

#include <string>

using namespace backseat;

namespace
{
// About 1.1 m per 0.00001 degrees of latitude; at 39.7 N, 0.00001 of longitude is about 0.86 m.
LatLon Off(double northM, double eastM) { return {39.7 + northM / 111320, -105 + eastM / (111320 * 0.7694)}; }

/// A 20 m x 12 m house as two triangles, like the map stores it.
std::vector<FootprintTriangle> House()
{
  LatLon const sw = Off(0, 0), se = Off(0, 20), ne = Off(12, 20), nw = Off(12, 0);
  return {{sw, se, ne}, {sw, ne, nw}};
}

EngineConfig IndoorConfig()
{
  EngineConfig c;
  c.rng = SeededRng(5);
  return c;
}

RouteSnapshot IndoorRoute()
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Left;
  r.distToTurnM = 900;
  r.nextStreet = "Elm Street";
  r.destination = "Grandma's";
  r.etaSeconds = 600;
  r.plannedTurns = {TurnKind::Left, TurnKind::Destination};
  r.origin = Off(6, 10);
  return r;
}

Fix IndoorFix(double tMs, LatLon at, double mps)
{
  Fix f;
  f.tMs = tMs;
  f.lat = at.lat;
  f.lon = at.lon;
  f.speedMps = mps;
  f.accuracyM = 15;
  return f;
}

/// The texts spoken, all joined, for looking up a line.
std::string Spoken(std::vector<Commentary> const & lines)
{
  std::string all;
  for (auto const & l : lines)
    all += l.text + "\n";
  return all;
}

BuildingLookup HouseLookup(bool garage = false)
{
  return [garage](LatLon const & p) -> std::optional<Building>
  {
    auto const depth = DepthInsideM(House(), p);
    if (!depth)
      return std::nullopt;
    return Building{*depth, garage};
  };
}
}  // namespace

TEST(DepthInsideMeasuresToTheNearestOutsideWall)
{
  auto const middle = DepthInsideM(House(), Off(6, 10));
  CHECK(middle);
  CHECK(std::abs(*middle - 6) < 0.2);  // 6 m to the north and south walls
  // The diagonal shared by the two triangles is not a wall.
  auto const nearDiagonal = DepthInsideM(House(), Off(6, 10.3));
  CHECK(nearDiagonal && *nearDiagonal > 5.5);
  auto const byWall = DepthInsideM(House(), Off(1, 10));
  CHECK(byWall && std::abs(*byWall - 1) < 0.2);
  CHECK(!DepthInsideM(House(), Off(-3, 10)));
  CHECK(!DepthInsideM({}, Off(6, 10)));
}

TEST(IndoorStartNeedsAGoodFixWellInsideAndStill)
{
  Building const house{6, false};
  CHECK(JudgeIndoorStart(house, 15, 0, false) == IndoorStart::Building);
  CHECK(JudgeIndoorStart(house, -1, -1, false) == IndoorStart::Building);
  CHECK(JudgeIndoorStart(house, 15, 0, true) == IndoorStart::Garage);
  CHECK(JudgeIndoorStart(Building{6, true}, 15, 0, false) == IndoorStart::Garage);
  CHECK(JudgeIndoorStart(std::nullopt, 15, 0, true) == IndoorStart::No);
  CHECK(JudgeIndoorStart(Building{1, false}, 15, 0, false) == IndoorStart::No);  // the driveway, give or take
  CHECK(JudgeIndoorStart(house, 120, 0, false) == IndoorStart::No);              // fix too vague
  CHECK(JudgeIndoorStart(house, 15, 12, false) == IndoorStart::No);              // driving through
}

TEST(NavigatorRoastsATripStartedInsideABuilding)
{
  Navigator nav(IndoorConfig());
  nav.SetBuildingLookup(HouseLookup());
  nav.OnFix(IndoorFix(1000, Off(6, 10), 0), IndoorRoute());
  std::string const said = Spoken(nav.TakeLines());
  // Brandon's line comes first, the first time.
  CHECK(said.find("You drove into a building and then decided it was time for directions.") != std::string::npos);
}

TEST(NavigatorSaysNothingAboutBuildingsFromTheDriveway)
{
  Navigator nav(IndoorConfig());
  nav.SetBuildingLookup(HouseLookup());
  nav.OnFix(IndoorFix(1000, Off(-5, 10), 0), IndoorRoute());
  std::string const said = Spoken(nav.TakeLines());
  CHECK(said.find("building") == std::string::npos);
  CHECK(said.find("garage") == std::string::npos);
}

TEST(NavigatorRemindsAboutTheGarageDoorAtHome)
{
  // Two trips home, ending at the house: that's where home is.
  LatLon const home = Off(6, 10);
  LatLon const store{39.75, -105.05};
  std::vector<TripRecord> history;
  for (int i = 0; i < 3; ++i)
  {
    TripRecord there;
    there.routeKey = RouteKey(home, store);
    there.to = store;
    there.endedAt = 1000.0 * i;
    TripRecord back;
    back.routeKey = RouteKey(store, home);
    back.to = home;
    back.endedAt = 1000.0 * i + 500;
    history.push_back(there);
    history.push_back(back);
  }
  Navigator nav(IndoorConfig(), history);
  nav.SetBuildingLookup(HouseLookup());
  nav.OnFix(IndoorFix(1000, home, 0), IndoorRoute());
  std::string const said = Spoken(nav.TakeLines());
  CHECK(said.find("Make sure to open the garage door before you leave. It might be a bit loud otherwise.") !=
        std::string::npos);
}

TEST(NavigatorDoesNotRepeatTheBuildingLineEveryTrip)
{
  // After the first time, it comes back on about a third of indoor starts.
  int said = 0;
  SaidLog log;
  for (int trip = 0; trip < 30; ++trip)
  {
    EngineConfig c;
    c.rng = SeededRng(100 + trip);
    Navigator nav(c);
    nav.SetSaidLog(log, [&](SaidLog const & l) { log = l; });
    nav.SetBuildingLookup(HouseLookup());
    nav.OnFix(IndoorFix(1000, Off(6, 10), 0), IndoorRoute());
    auto const lines = nav.TakeLines();
    for (auto const & l : lines)
      if (l.category == Category::LongStop)
        ++said;
  }
  CHECK(said >= 5);
  CHECK(said <= 20);
}
