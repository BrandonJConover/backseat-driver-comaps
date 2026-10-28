#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"
#include "backseat/road_bits.hpp"

#include <algorithm>
#include <cmath>

using namespace backseat;

namespace
{
double constexpr kRoadStartMs = 1793156400000.0 + 16 * 3600000.0;  // 1 pm in Denver
double constexpr kRoadLat = 39.74;
/// Meters per degree of longitude at kRoadLat.
double const kRoadMPerLonDeg = 111320 * std::cos(kRoadLat * M_PI / 180);

/// Drives RoadBits east along a straight road, one tick a second, collecting what it says.
struct RoadDrive
{
  RoadBits road;
  SaidLog said;
  double t = kRoadStartMs;
  double odoM = 0;
  double tripMs = 0;
  double sinceSpokenMs = 60000;
  double toTurnS = 600;
  bool night = false;
  int hour = 13;
  double r = 0.9;
  std::vector<RoadRemark> heard;

  std::vector<RoadRemark> Run(int seconds, double mps)
  {
    std::vector<RoadRemark> out;
    for (int i = 0; i < seconds; ++i)
    {
      RoadTick k;
      k.tMs = t;
      k.here = {kRoadLat, -104.99 + odoM / kRoadMPerLonDeg};
      k.speedMps = mps;
      k.bearingDeg = 90;
      k.steady = true;
      k.toTurnS = toTurnS;
      k.sinceSpokenMs = sinceSpokenMs;
      k.tripMs = tripMs;
      k.night = night;
      k.localHour = hour;
      auto const said_ = road.Tick(k, said, r);
      for (auto const & s : said_)
      {
        // Saying it, like the navigator does: the log remembers the key.
        said.Pick(s.key, LinesFor(s.key, 0), 0);
        out.push_back(s);
        heard.push_back(s);
        sinceSpokenMs = 0;
      }
      t += 1000;
      tripMs += 1000;
      sinceSpokenMs += 1000;
      odoM += mps;
    }
    return out;
  }
};

std::vector<std::string> Keys(std::vector<RoadRemark> const & v)
{
  std::vector<std::string> out;
  for (auto const & r : v)
    out.push_back(r.key);
  return out;
}

bool HasKey(std::vector<RoadRemark> const & v, std::string const & key)
{
  return std::any_of(v.begin(), v.end(), [&key](RoadRemark const & r) { return r.key == key; });
}

RoadRemark const & Find(std::vector<RoadRemark> const & v, std::string const & key)
{
  return *std::find_if(v.begin(), v.end(), [&key](RoadRemark const & r) { return r.key == key; });
}

/// A route-ahead lookup with stopovers at fixed odometer readings of the drive.
RouteAheadLookup Ahead(RoadDrive const & d, std::vector<std::pair<Stopover, double>> at, double routeEndM = 1e9)
{
  return [&d, at, routeEndM](double maxM)
  {
    RouteAhead a;
    a.searchedM = std::min(maxM, routeEndM - d.odoM);
    for (auto const & [kind, odo] : at)
      if (odo > d.odoM && odo - d.odoM <= a.searchedM)
        a.stops.push_back({kind, odo - d.odoM});
    return a;
  };
}
}  // namespace

TEST(WeatherFromNwsReadsTheDescription)
{
  Weather w = WeatherFromNws("Light Rain and Fog/Mist", 3, -1);
  CHECK(w.rain && w.fog && !w.snow && !w.clear && !w.windy);
  w = WeatherFromNws("Sunny", 2, -1);
  CHECK(w.clear && !w.rain);
  w = WeatherFromNws("Light Snow", -1, -1);
  CHECK(w.snow && !w.rain);
  w = WeatherFromNws("Thunderstorms and Rain", -1, -1);
  CHECK(w.rain);
  w = WeatherFromNws("Mostly Cloudy", 12, -1);
  CHECK(w.windy && !w.clear);
  w = WeatherFromNws("Clear", 2, 17);
  CHECK(w.windy && w.clear);
  w = WeatherFromNws("Cloudy", 2, 5);
  CHECK(!w.rain && !w.snow && !w.fog && !w.windy && !w.clear);
}

TEST(RestAreaAheadAfterALongDriveAndThenPassed)
{
  RoadDrive d;
  d.tripMs = 80 * 60000;
  d.road.SetRouteAheadLookup(Ahead(d, {{Stopover::RestArea, 6000}}));
  d.Run(400, 25);
  CHECK(HasKey(d.heard, "rest.ahead"));
  // Announced as soon as it's within 10 km: 6 km out.
  CHECK_EQUAL(Find(d.heard, "rest.ahead").vars.at("distance"), std::string("3.5 miles"));
  CHECK(HasKey(d.heard, "rest.passed"));
  CHECK(!HasKey(d.heard, "rest.survey"));
}

TEST(NoRestAreaCallBeforeAnHourAndAQuarter)
{
  RoadDrive d;
  d.tripMs = 30 * 60000;
  d.road.SetRouteAheadLookup(Ahead(d, {{Stopover::RestArea, 6000}}));
  d.Run(200, 25);
  CHECK(!HasKey(d.heard, "rest.ahead"));
}

TEST(RestStopSurveyAfterPassingOneQuietly)
{
  RoadDrive d;
  d.tripMs = 10 * 60000;
  d.road.SetRouteAheadLookup(Ahead(d, {{Stopover::RestArea, 1000}}));
  for (int i = 0; i < 200 && !HasKey(d.heard, "rest.survey"); ++i)
    d.Run(1, 25);
  CHECK(HasKey(d.heard, "rest.survey"));
  CHECK(d.road.AwaitingAnswer(d.t));
  auto const reply = d.road.OnAnswer(d.t, " like a seven ", d.said);
  CHECK(reply.has_value());
  CHECK_EQUAL(reply->key, std::string("rest.surveyAfter"));
  // Asked about 0.8 km past it, answered a moment later.
  // (Under a mile, said the natural way: "half a mile".)
  CHECK(reply->vars.at("distance").find("mile") != std::string::npos);
  CHECK(reply->vars.at("distance").find("miles") == std::string::npos);
  CHECK(!d.road.AwaitingAnswer(d.t));
}

TEST(RestStopSurveyPaysOffWithoutAnAnswerToo)
{
  RoadDrive d;
  d.tripMs = 10 * 60000;
  d.road.SetRouteAheadLookup(Ahead(d, {{Stopover::RestArea, 1000}}));
  d.Run(160, 25);
  auto const keys = Keys(d.heard);
  auto const ask = std::find(keys.begin(), keys.end(), "rest.survey");
  CHECK(ask != keys.end());
  CHECK(ask + 1 != keys.end() && *(ask + 1) == "rest.surveyAfter");
}

TEST(NoGasForFiftyMiles)
{
  RoadDrive d;
  d.tripMs = 5 * 60000;
  d.road.SetRouteAheadLookup(Ahead(d, {{Stopover::Coffee, 30000}}));
  d.Run(60, 25);
  CHECK(HasKey(d.heard, "gas.none"));
  CHECK_EQUAL(Find(d.heard, "gas.none").vars.at("distance"), std::string("50 miles"));
}

TEST(GasAheadMeansNoGasLine)
{
  RoadDrive d;
  d.tripMs = 5 * 60000;
  d.road.SetRouteAheadLookup(Ahead(d, {{Stopover::Fuel, 30000}}));
  d.Run(60, 25);
  CHECK(!HasKey(d.heard, "gas.none"));
}

TEST(ShortTripLeftDoesntClaimNoGas)
{
  RoadDrive d;
  d.tripMs = 5 * 60000;
  d.road.SetRouteAheadLookup(Ahead(d, {}, 20000));
  d.Run(60, 25);
  CHECK(!HasKey(d.heard, "gas.none"));
}

TEST(CoffeeIsFarOnlyInTheMorning)
{
  RoadDrive morning;
  morning.tripMs = 5 * 60000;
  morning.hour = 7;
  morning.road.SetRouteAheadLookup(Ahead(morning, {{Stopover::Fuel, 5000}, {Stopover::Coffee, 40000}}));
  morning.Run(60, 25);
  CHECK(HasKey(morning.heard, "coffee.far"));
  CHECK_EQUAL(Find(morning.heard, "coffee.far").vars.at("distance"), std::string("25 miles"));

  RoadDrive afternoon;
  afternoon.tripMs = 5 * 60000;
  afternoon.road.SetRouteAheadLookup(Ahead(afternoon, {{Stopover::Fuel, 5000}, {Stopover::Coffee, 40000}}));
  afternoon.Run(60, 25);
  CHECK(!HasKey(afternoon.heard, "coffee.far"));
}

TEST(ScenicOverlookTrapSpringsAfterPassingIt)
{
  RoadDrive d;
  d.tripMs = 5 * 60000;
  d.road.SetRouteAheadLookup(Ahead(d, {{Stopover::Fuel, 5000}, {Stopover::Viewpoint, 4000}}));
  d.Run(220, 25);
  auto const keys = Keys(d.heard);
  auto const ahead = std::find(keys.begin(), keys.end(), "viewpoint.ahead");
  auto const passed = std::find(keys.begin(), keys.end(), "viewpoint.passed");
  CHECK(ahead != keys.end());
  CHECK(passed != keys.end() && passed > ahead);
}

TEST(DriveThruInLongAndLeaving)
{
  RoadDrive d;
  d.tripMs = 10 * 60000;
  d.road.SetStopSpotLookup([](LatLon const &) { return StopSpot::DriveThru; });
  d.Run(5, 10);
  // Five minutes in line, inching up every minute.
  for (int i = 0; i < 6; ++i)
  {
    d.Run(55, 0);
    d.Run(3, 4.5);
  }
  CHECK(HasKey(d.heard, "driveThru.in"));
  CHECK(HasKey(d.heard, "driveThru.long"));
  CHECK(!HasKey(d.heard, "driveThru.leaving"));
  d.Run(40, 10);
  CHECK(HasKey(d.heard, "driveThru.leaving"));
  for (char const * key : {"driveThru.in", "driveThru.long", "driveThru.leaving"})
    CHECK_EQUAL(std::count_if(d.heard.begin(), d.heard.end(), [key](RoadRemark const & r) { return r.key == key; }), 1);
}

TEST(RestAreaStopQuickOrBack)
{
  RoadDrive quick;
  quick.tripMs = 10 * 60000;
  quick.road.SetStopSpotLookup([](LatLon const &) { return StopSpot::RestArea; });
  quick.Run(90, 0);
  quick.Run(20, 10);
  CHECK(HasKey(quick.heard, "rest.quick"));
  CHECK(!HasKey(quick.heard, "rest.back"));

  RoadDrive back;
  back.tripMs = 10 * 60000;
  back.road.SetStopSpotLookup([](LatLon const &) { return StopSpot::RestArea; });
  back.Run(300, 0);
  back.Run(20, 10);
  CHECK(HasKey(back.heard, "rest.back"));
}

TEST(SignalLostThenBack)
{
  RoadDrive d;
  d.tripMs = 10 * 60000;
  d.road.OnConnectivity(d.t, false);
  d.Run(70, 25);
  CHECK(HasKey(d.heard, "signal.lost"));
  d.Run(200, 25);
  d.road.OnConnectivity(d.t, true);
  d.Run(20, 25);
  CHECK(HasKey(d.heard, "signal.back"));
}

TEST(ShortSignalDropsStayQuiet)
{
  RoadDrive d;
  d.tripMs = 10 * 60000;
  d.road.OnConnectivity(d.t, false);
  d.Run(30, 25);
  d.road.OnConnectivity(d.t, true);
  d.Run(60, 25);
  CHECK(d.heard.empty());
}

TEST(BathroomTrapCatchesTheDriverOnALaterTrip)
{
  RoadDrive d;
  auto const ask = d.road.StartTrap(d.t, d.said);
  CHECK(ask.has_value());
  CHECK_EQUAL(ask->key, std::string("trap.bathroom.ask"));
  auto const saved = d.road.OnAnswer(d.t + 3000, " rest stops for sure ", d.said);
  CHECK(saved.has_value());
  CHECK_EQUAL(saved->key, std::string("file.saved"));
  CHECK_EQUAL(saved->vars.at("answer"), std::string("Rest stops"));
  CHECK_EQUAL(d.said.Recalled("note.bathroom"), std::string("rest"));
  // It's kept with the log, across restarts.
  CHECK_EQUAL(SaidLog::Parse(d.said.Serialize()).Recalled("note.bathroom"), std::string("rest"));

  // Another trip: a stop at a gas station.
  d.road.Reset();
  d.tripMs = 20 * 60000;
  d.road.SetStopSpotLookup([](LatLon const &) { return StopSpot::Fuel; });
  d.Run(40, 0);
  CHECK(HasKey(d.heard, "trap.bathroom.caught"));
  CHECK_EQUAL(Find(d.heard, "trap.bathroom.caught").vars.at("said"), std::string("rest stops"));
  CHECK(d.said.Recalled("note.bathroom").empty());
}

TEST(BathroomTrapIgnoresAMuddledAnswer)
{
  RoadDrive d;
  d.road.StartTrap(d.t, d.said);
  CHECK(!d.road.OnAnswer(d.t + 2000, " both of them are gross rest stops and gas stations ", d.said));
  CHECK(d.said.Recalled("note.bathroom").empty());
}

TEST(RateTrapOnceTheBathroomIsAnswered)
{
  RoadDrive d;
  d.said.Remember("note.bathroom", "gas");
  auto const ask = d.road.StartTrap(d.t, d.said);
  CHECK(ask.has_value());
  CHECK_EQUAL(ask->key, std::string("trap.rate.ask"));
  // Only one trap a trip.
  CHECK(!d.road.StartTrap(d.t + 1000, d.said));
  // No answer: the punchline comes anyway.
  d.Run(20, 25);
  CHECK(HasKey(d.heard, "trap.rate.after"));
}

TEST(HighwayAfterTenMinutes)
{
  RoadDrive d;
  d.tripMs = 5 * 60000;
  d.road.SetSurroundingsLookup([](LatLon const &, double) { Surroundings s; s.motorway = true; return s; });
  d.Run(9 * 60, 30);
  CHECK(!HasKey(d.heard, "road.highway"));
  d.Run(90, 30);
  CHECK(HasKey(d.heard, "road.highway"));
}

TEST(FarmlandSaysWhichSide)
{
  RoadDrive d;
  d.tripMs = 5 * 60000;
  d.road.SetSurroundingsLookup([](LatLon const &, double) { Surroundings s; s.farmLeft = true; return s; });
  d.Run(30, 20);
  CHECK(HasKey(d.heard, "road.farm"));
  CHECK_EQUAL(Find(d.heard, "road.farm").vars.at("side"), std::string("left"));
}

TEST(GravelSmallTownAndNatureOncePerTrip)
{
  RoadDrive d;
  d.tripMs = 5 * 60000;
  d.road.SetSurroundingsLookup(
      [](LatLon const &, double)
      {
        Surroundings s;
        s.unpaved = true;
        s.wild = true;
        return s;
      });
  d.Run(20 * 60, 15);
  CHECK(HasKey(d.heard, "road.gravel"));
  CHECK(HasKey(d.heard, "road.nature"));
  CHECK_EQUAL(std::count_if(d.heard.begin(), d.heard.end(), [](RoadRemark const & r) { return r.key == "road.gravel"; }), 1);

  RoadDrive town;
  town.tripMs = 5 * 60000;
  bool inTown = false;
  town.road.SetSurroundingsLookup([&inTown](LatLon const &, double) { Surroundings s; s.village = inTown; return s; });
  town.Run(10, 12);
  inTown = true;
  town.Run(40, 12);
  CHECK(HasKey(town.heard, "road.smallTown"));
}

TEST(SunsetDuringTheTripGetsItsLine)
{
  RoadDrive d;
  d.tripMs = 20 * 60000;
  d.Run(10, 25);
  d.night = true;
  d.Run(30, 25);
  CHECK(HasKey(d.heard, "night.sunset"));
  d.Run(20 * 60, 25);
  CHECK(!HasKey(d.heard, "night.dark"));
}

TEST(DrivingInTheDarkLateAtNight)
{
  RoadDrive d;
  d.night = true;
  d.hour = 23;
  d.Run(11 * 60, 25);
  CHECK(HasKey(d.heard, "night.late"));
  CHECK(!HasKey(d.heard, "night.sunset"));
}

TEST(WeatherOnceAndClearOnlyInDaylight)
{
  RoadDrive d;
  d.tripMs = 5 * 60000;
  d.road.OnWeather(d.t, WeatherFromNws("Light Rain", -1, -1));
  d.Run(20 * 60, 25);
  CHECK_EQUAL(std::count_if(d.heard.begin(), d.heard.end(), [](RoadRemark const & r) { return r.key == "weather.wet"; }), 1);

  RoadDrive night;
  night.tripMs = 5 * 60000;
  night.night = true;
  night.road.OnWeather(night.t, WeatherFromNws("Clear", -1, -1));
  night.Run(5 * 60, 25);
  CHECK(!HasKey(night.heard, "weather.clear"));
}

TEST(AmbientRemarksAreSpacedAndCapped)
{
  RoadDrive d;
  d.tripMs = 80 * 60000;
  d.night = true;
  d.hour = 20;
  d.road.OnWeather(d.t, WeatherFromNws("Snow", -1, -1));
  d.road.SetRouteAheadLookup(Ahead(d, {{Stopover::Viewpoint, 5000}, {Stopover::RestArea, 9000}}));
  d.road.SetSurroundingsLookup(
      [](LatLon const &, double)
      {
        Surroundings s;
        s.motorway = true;
        s.unpaved = true;
        s.wild = true;
        s.farmRight = true;
        return s;
      });
  d.Run(60 * 60, 25);
  int ambient = 0;
  for (auto const & r : d.heard)
    if (r.key != "rest.passed" && r.key != "viewpoint.passed")
      ++ambient;
  CHECK(ambient <= kRoadAmbientPerTrip);
  CHECK(ambient >= 4);
}

TEST(EveryRoadKeyHasLinesInEveryTier)
{
  for (char const * key :
       {"quiet.aside", "partner.aside", "road.highway", "rest.ahead", "rest.passed", "rest.back", "rest.quick",
        "rest.survey", "rest.surveyAfter", "gas.none", "coffee.far", "viewpoint.ahead", "viewpoint.passed",
        "driveThru.in", "driveThru.long", "driveThru.leaving", "signal.lost", "signal.back", "road.smallTown",
        "road.nature", "road.farm", "road.gravel", "night.sunset", "night.dark", "night.late", "weather.wet",
        "weather.fog", "weather.snow", "weather.clear", "weather.wind", "traffic.cleared", "trap.bathroom.ask",
        "trap.bathroom.caught", "trap.rate.ask", "trap.rate.after"})
    for (int tier = 0; tier < 3; ++tier)
      CHECK(!LinesFor(key, tier).empty());
  // Brandon's picks lead their pools, word for word.
  CHECK_EQUAL(LinesFor("road.farm", 0).front(), std::string("Cows on the {side}. Don't wave. They've seen your driving."));
  CHECK_EQUAL(LinesFor("signal.lost", 0).front(),
              std::string("No signal out here. That means no one can hear you sing. Except me. Unfortunately."));
  CHECK_EQUAL(LinesFor("traffic.cleared", 0).front(),
              std::string("Traffic's clearing up. Try to act like you've done this before."));
}

namespace
{
RouteSnapshot RoadRoute(double dist)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Right;
  r.nextStreet = "Elm Street";
  r.distToTurnM = dist;
  r.destination = "Grandma's";
  r.etaSeconds = 3600;
  r.plannedTurns = {TurnKind::Right, TurnKind::Destination};
  return r;
}

Navigator RoadNavigator(SaidLog said)
{
  EngineConfig c;
  c.rng = SeededRng(5);
  Navigator nav(c);
  nav.SetDriverName("Brandon");
  nav.SetSaidLog(std::move(said), {});
  nav.SetLocalTime([](double) {
    std::tm tm{};
    tm.tm_year = 126;
    tm.tm_mon = 9;
    tm.tm_mday = 28;
    tm.tm_hour = 13;
    return tm;
  });
  return nav;
}

std::vector<Commentary> RoadNav(Navigator & nav, double & t, int seconds, double mps, double & dist)
{
  std::vector<Commentary> said;
  for (int i = 0; i < seconds; ++i, t += 1000, dist -= mps)
  {
    Fix f;
    f.tMs = t;
    f.lat = kRoadLat;
    f.lon = -104.99 + (60000 - dist) / kRoadMPerLonDeg;
    f.speedMps = mps;
    f.bearingDeg = 90;
    nav.OnFix(f, RoadRoute(dist));
    auto lines = nav.TakeLines();
    said.insert(said.end(), lines.begin(), lines.end());
  }
  return said;
}

bool SaidText(std::vector<Commentary> const & lines, std::string const & text)
{
  return std::any_of(lines.begin(), lines.end(), [&text](Commentary const & c) { return c.text == text; });
}

SaidLog RoadSaidBefore(std::vector<std::string> keys)
{
  std::string text;
  for (auto const & k : keys)
    text += k + "\tsomething\n";
  return SaidLog::Parse(text);
}
}  // namespace

TEST(QuietStretchAsideIsBrandonsFirstLine)
{
  Navigator nav = RoadNavigator(RoadSaidBefore({"call.script", "ispy.day", "fact.frame", "twenty.offer"}));
  double t = kRoadStartMs;
  double dist = 60000;
  auto const lines = RoadNav(nav, t, 600, 25, dist);
  CHECK(SaidText(lines, LinesFor("quiet.aside", 0).front()));
}

TEST(QuietStretchTrapAsksAndFilesTheAnswer)
{
  Navigator nav = RoadNavigator(RoadSaidBefore({"call.script", "ispy.day", "fact.frame", "twenty.offer", "quiet.aside"}));
  double t = kRoadStartMs;
  double dist = 60000;
  auto lines = RoadNav(nav, t, 600, 25, dist);
  CHECK(SaidText(lines, "For your file. Which has better bathrooms, rest stops or gas stations?"));
  // Answer right after it's asked: find when it was said and reply then.
  Navigator nav2 = RoadNavigator(RoadSaidBefore({"call.script", "ispy.day", "fact.frame", "twenty.offer", "quiet.aside"}));
  double t2 = kRoadStartMs;
  double dist2 = 60000;
  for (int i = 0; i < 600; ++i)
  {
    auto const got = RoadNav(nav2, t2, 1, 25, dist2);
    if (SaidText(got, "For your file. Which has better bathrooms, rest stops or gas stations?"))
    {
      nav2.OnDriverSaid(t2, "Gas stations, obviously");
      auto const reply = nav2.TakeLines();
      CHECK(!reply.empty());
      CHECK(reply.front().text.rfind("Gas stations", 0) == 0);
      return;
    }
  }
  CHECK(false);
}

TEST(RestAreaStopSkipsTheAbandonedRoast)
{
  Navigator nav = RoadNavigator({});
  nav.SetStopSpotLookup([](LatLon const &) { return StopSpot::RestArea; });
  double t = kRoadStartMs;
  double dist = 60000;
  RoadNav(nav, t, 300, 25, dist);
  RoadNav(nav, t, 240, 0, dist);
  auto const lines = RoadNav(nav, t, 60, 10, dist);
  bool back = false;
  for (auto const & l : lines)
  {
    CHECK(l.category != Category::Abandoned);
    back = back || l.category == Category::Road;
  }
  CHECK(back);
}

TEST(ParkedAtAFastFoodPlaceIsNotADriveThru)
{
  RoadDrive d;
  d.tripMs = 10 * 60000;
  d.road.SetStopSpotLookup([](LatLon const &) { return StopSpot::DriveThru; });
  d.Run(5, 10);
  d.Run(10 * 60, 0);
  d.Run(40, 10);
  CHECK(d.heard.empty());
}
