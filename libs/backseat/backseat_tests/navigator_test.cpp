#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/lines.hpp"
#include "backseat/units.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

using namespace backseat;

namespace
{
EngineConfig Config()
{
  EngineConfig c;
  c.rng = SeededRng(3);
  return c;
}

RouteSnapshot OnRoute(uint32_t turnId, TurnKind turn, double dist, std::string street = "Elm Street")
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = turnId;
  r.turn = turn;
  r.distToTurnM = dist;
  r.nextStreet = std::move(street);
  r.destination = "Grandma's";
  r.etaSeconds = 600;
  r.plannedTurns = {TurnKind::Left, TurnKind::Right, TurnKind::Destination};
  return r;
}

Fix At(double tMs, double mps, std::optional<double> limit = std::nullopt)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.7;
  f.lon = -105;
  f.speedMps = mps;
  f.speedLimitMps = limit;
  return f;
}

bool Has(std::vector<Commentary> const & lines, Category c)
{
  for (auto const & l : lines)
    if (l.category == c)
      return true;
  return false;
}
}  // namespace

TEST(InstructionTextReadsLikeAGps)
{
  CHECK_EQUAL(InstructionText(TurnKind::Left, "Elm Street", ""), std::string("Turn left onto Elm Street"));
  CHECK_EQUAL(InstructionText(TurnKind::SlightRight, "", ""), std::string("Bear right"));
  CHECK_EQUAL(InstructionText(TurnKind::Destination, "Main St", "Grandma's"), std::string("Arrive at Grandma's"));
}

TEST(NavigatorStaysQuietWithoutARoute)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 20), RouteSnapshot{});
  CHECK(!nav.InTrip());
  CHECK(nav.TakeLines().empty());
}

TEST(NavigatorOpensWithGreetingRundownAndInstruction)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Left, 400));
  auto const lines = nav.TakeLines();
  // Greeting, rundown, then the first instruction; the time prediction waits until we're rolling.
  CHECK_EQUAL(lines.size(), size_t{3});
  CHECK(lines[0].text.find("didn't ask to be in this car") != std::string::npos);
  // First trip ever: the turns rattled off at speed, then Brandon's music line.
  CHECK(lines[1].text.find("Left, right.") != std::string::npos);
  CHECK(lines[1].text.find("put on some music") != std::string::npos);
  CHECK(lines[1].voiceSpeed > 1.2);
  CHECK(lines[2].text.find("turn left onto Elm Street") != std::string::npos);
  nav.OnFix(At(30000, 10), OnRoute(1, TurnKind::Left, 380));
  auto const later = nav.TakeLines();
  CHECK_EQUAL(later.size(), size_t{1});
  CHECK(later[0].priority == Priority::Nav);
}

TEST(NavigatorTurnsStepChangesIntoCompletedTurns)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Left, 900));
  nav.TakeLines();
  // Once the opening lines are over.
  nav.OnFix(At(60000, 10), OnRoute(1, TurnKind::Left, 140));
  CHECK(Has(nav.TakeLines(), Category::Instruction));  // the 500 ft nag
  nav.OnFix(At(80000, 10), OnRoute(2, TurnKind::Right, 300, "Oak Avenue"));
  nav.TakeLines();
  // The turn counts; whether it gets a verdict is up to the running bits (most turns don't).
  CHECK_EQUAL(nav.Engine()->Stats().turnsCompleted, 1);
}

TEST(NavigatorCallsOffRouteNearTheTurnAMiss)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Left, 400));
  nav.OnFix(At(1000, 10), OnRoute(1, TurnKind::Left, 20));
  nav.TakeLines();
  RouteSnapshot off = OnRoute(1, TurnKind::Left, 20);
  off.state = RouteState::OffRoute;
  nav.OnFix(At(2000, 10), off);
  auto const lines = nav.TakeLines();
  CHECK(Has(lines, Category::MissedTurn));
  // Still off the route a few seconds later: now it's a reroute.
  nav.OnFix(At(7100, 10), off);
  CHECK(Has(nav.TakeLines(), Category::Rerouted));
  // The new route's first step isn't a completed turn.
  nav.OnFix(At(9000, 10), OnRoute(7, TurnKind::Right, 300));
  CHECK(!Has(nav.TakeLines(), Category::TurnCompleted));
  CHECK_EQUAL(nav.Engine()->Stats().missedTurns, 1);
}

TEST(NavigatorRoastsSpeedingFromTheFix)
{
  Navigator nav(Config());
  nav.OnFix(At(0, FromDisplay(50, Units::Mph), FromDisplay(50, Units::Mph)), OnRoute(1, TurnKind::Left, 900));
  nav.TakeLines();
  // A few seconds after the direction: right on top of it, the roast waits its turn.
  nav.OnFix(At(1000, FromDisplay(53, Units::Mph), FromDisplay(50, Units::Mph)), OnRoute(1, TurnKind::Left, 880));
  CHECK(!Has(nav.TakeLines(), Category::Speeding));
  nav.OnFix(At(6000, FromDisplay(53, Units::Mph), FromDisplay(50, Units::Mph)), OnRoute(1, TurnKind::Left, 800));
  CHECK(Has(nav.TakeLines(), Category::Speeding));
}

TEST(NavigatorArrivesOnceAndRecordsTheTrip)
{
  int recorded = 0;
  Navigator nav(Config(), {}, [&](TripRecord const & r, TripReport const &)
  {
    ++recorded;
    CHECK_EQUAL(r.destination, std::string("Grandma's"));
  });
  nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Destination, 300));
  RouteSnapshot done = OnRoute(1, TurnKind::Destination, 0);
  done.state = RouteState::Finished;
  nav.OnFix(At(60000, 0), done);
  nav.Poll(done);
  auto const lines = nav.TakeLines();
  CHECK(Has(lines, Category::Arrived));
  CHECK_EQUAL(recorded, 1);
  nav.Poll(RouteSnapshot{});
  CHECK(!nav.InTrip());
}

namespace
{
Fix AtPos(double tMs, double mps, double lat, double lon, std::optional<double> limit = std::nullopt)
{
  Fix f = At(tMs, mps, limit);
  f.lat = lat;
  f.lon = lon;
  return f;
}

/// Whether `text` came from one of `key`'s line templates: every fixed piece of the template
/// (the parts between placeholders) appears in it, in order.
bool SaidLineOf(std::string const & text, std::string const & key)
{
  for (int tier = 0; tier < 3; ++tier)
    for (auto const & raw : LinesFor(key, tier))
    {
      std::string const line = SpeakableText(raw);  // as the navigator hands it out
      size_t at = 0;
      bool match = true;
      for (size_t i = 0; i < line.size() && match;)
      {
        size_t const open = std::min(line.find('{', i), line.size());
        std::string const piece = line.substr(i, open - i);
        size_t const found = piece.empty() ? at : text.find(piece, at);
        match = found != std::string::npos;
        at = match ? found + piece.size() : at;
        size_t const close = line.find('}', open);
        i = close == std::string::npos ? line.size() : close + 1;
      }
      if (match)
        return true;
    }
  return false;
}

RouteSnapshot Far()
{
  RouteSnapshot r = OnRoute(1, TurnKind::Left, 2000);
  r.destinationPoint = {39.8, -105};  // ~11 km north of the test fixes
  return r;
}
}  // namespace

TEST(NavigatorCallsOutAStopForGasAndNamesIt)
{
  Navigator nav(Config());
  nav.SetPlaceLookup([](LatLon const &) { return Place{PlaceKind::Gas, std::string("Shell")}; });
  nav.OnFix(At(0, 10), Far());
  nav.TakeLines();
  for (double t = 1000; t <= 200000; t += 1000)
    nav.OnFix(At(t, 0), Far());
  auto const lines = nav.TakeLines();
  CHECK(Has(lines, Category::UnplannedStop));
  CHECK(nav.Engine()->Incidents().back().kind == IncidentKind::UnplannedStop);
}

TEST(NavigatorKnowsThePhoneWasLeftInTheCar)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 10), Far());
  for (double t = 1000; t <= 300000; t += 1000)
    nav.OnFix(At(t, 0), Far());  // never moves an inch
  nav.TakeLines();
  nav.OnFix(At(301000, 10), Far());
  auto const lines = nav.TakeLines();
  CHECK(Has(lines, Category::Abandoned));
  CHECK(SaidLineOf(lines.back().text, "abandoned"));
}

TEST(NavigatorKnowsThePhoneCameAlong)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 10), Far());
  nav.OnFix(At(1000, 0), Far());
  // A walk into the store and back, at walking pace.
  for (double t = 2000; t <= 120000; t += 1000)
    nav.OnFix(AtPos(t, 1.4, 39.7 + 0.0005 * std::sin(t / 20000), -105), Far());
  nav.TakeLines();
  nav.OnFix(At(121000, 10), Far());
  auto const lines = nav.TakeLines();
  CHECK(SaidLineOf(lines.back().text, "broughtAlong"));
}

TEST(NavigatorIgnoresStopsAtTheDestination)
{
  Navigator nav(Config());
  RouteSnapshot r = Far();
  r.destinationPoint = {39.7005, -105};  // ~55 m away
  nav.OnFix(At(0, 10), r);
  for (double t = 1000; t <= 200000; t += 1000)
    nav.OnFix(At(t, 0), r);
  CHECK(!Has(nav.TakeLines(), Category::UnplannedStop));
}

TEST(NavigatorBlamesTheRouteInACrawl)
{
  Navigator nav(Config());
  double const limit = FromDisplay(65, Units::Mph);
  nav.OnFix(At(0, limit, limit), Far());
  nav.TakeLines();
  bool blamed = false;
  for (double t = 1000; t <= 120000; t += 1000)
  {
    nav.OnFix(At(t, FromDisplay(10, Units::Mph), limit), Far());
    blamed = blamed || Has(nav.TakeLines(), Category::Traffic);
  }
  CHECK(blamed);
}

namespace
{
/// Whether any line from `key` was said while a jam plays out: `go` seconds at 10 mph, then
/// `stop` seconds still, repeated for `seconds`, on a 65 mph road.
bool SaidInJam(Navigator & nav, std::string const & key, int go, int stop, int seconds)
{
  double const limit = FromDisplay(65, Units::Mph);
  nav.OnFix(At(0, limit, limit), Far());
  nav.TakeLines();
  bool said = false;
  for (int s = 1; s <= seconds; ++s)
  {
    double const mps = (s % (go + stop)) < go ? FromDisplay(10, Units::Mph) : 0;
    nav.OnFix(At(s * 1000.0, mps, limit), Far());
    for (auto const & l : nav.TakeLines())
      said = said || SaidLineOf(l.text, key);
  }
  return said;
}
}  // namespace

TEST(NavigatorMocksStopAndGo)
{
  Navigator nav(Config());
  CHECK(SaidInJam(nav, "traffic.stopGo", 5, 10, 300));
}

TEST(NavigatorMocksAStandstill)
{
  Navigator nav(Config());
  CHECK(SaidInJam(nav, "traffic.standstill", 5, 60, 300));
}

TEST(NavigatorAsksWhichRoadToBlame)
{
  Navigator nav(Config());
  int asked = 0;
  nav.SetAlternateLookup([&asked](LatLon const &) -> std::optional<std::string>
  {
    ++asked;
    return std::string("I-25");
  });
  SaidInJam(nav, "traffic.start", 5, 10, 300);
  CHECK_EQUAL(asked, 1);
}

TEST(NavigatorLeavesSlowSideStreetsAlone)
{
  Navigator nav(Config());
  double const limit = FromDisplay(25, Units::Mph);
  for (double t = 0; t <= 120000; t += 1000)
    nav.OnFix(At(t, FromDisplay(5, Units::Mph), limit), Far());
  CHECK(!Has(nav.TakeLines(), Category::Traffic));
}

TEST(NavigatorWarnsAboutTheBatteryOnce)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 10), Far());
  nav.TakeLines();
  nav.OnBattery(1000, 0.19, false);
  CHECK(Has(nav.TakeLines(), Category::Battery));
  nav.OnBattery(2000, 0.18, false);
  CHECK(!Has(nav.TakeLines(), Category::Battery));
  nav.OnBattery(3000, 0.18, true);
  CHECK(Has(nav.TakeLines(), Category::Battery));  // thanks for the charge
}

TEST(NavigatorMentionsALowBatteryAtTheStart)
{
  Navigator nav(Config());
  nav.OnBattery(0, 0.08, false);
  nav.OnFix(At(0, 10), Far());
  CHECK(Has(nav.TakeLines(), Category::Battery));
}

TEST(NavigatorPassesStopSignsThroughForPayback)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Right, 400));
  nav.OnFix(At(1000, 10), OnRoute(1, TurnKind::Right, 20));
  RouteSnapshot off = OnRoute(1, TurnKind::Right, 20);
  off.state = RouteState::OffRoute;
  nav.OnFix(At(2000, 10), off);  // a missed turn: something to pay back
  nav.TakeLines();
  RouteSnapshot stop = OnRoute(2, TurnKind::Left, 150, "2nd Avenue");
  stop.control = Control::StopSign;
  nav.OnFix(At(40000, 10), stop);
  CHECK(Has(nav.TakeLines(), Category::Payback));
}

namespace
{
std::vector<Commentary> PullOverLines(std::vector<Commentary> const & lines)
{
  std::vector<Commentary> out;
  for (auto const & l : lines)
    if (l.category == Category::PullOver)
      out.push_back(l);
  return out;
}

/// Drives at `mps` for a minute on a 50 mph road, then stops at the roadside for a minute.
std::vector<Commentary> DriveThenPullOver(Navigator & nav, double mps)
{
  double const limit = FromDisplay(50, Units::Mph);
  std::vector<Commentary> said;
  for (int i = 0; i <= 60; ++i)
    nav.OnFix(AtPos(i * 1000, mps, 39.7 + i * 0.0002, -105, limit), Far());
  for (int i = 61; i <= 120; ++i)
    nav.OnFix(AtPos(i * 1000, 0, 39.7 + 60 * 0.0002, -105, limit), Far());
  for (auto const & l : nav.TakeLines())
    said.push_back(l);
  return said;
}
}  // namespace

TEST(NavigatorAsksWhyWeStoppedAtTheRoadside)
{
  Navigator nav(Config());
  auto const lines = PullOverLines(DriveThenPullOver(nav, 20));
  CHECK_EQUAL(lines.size(), size_t(1));
  CHECK(SaidLineOf(lines[0].text, "pullOver.limit"));
  CHECK(lines[0].text.find("police") == std::string::npos);
}

TEST(NavigatorLetsRedLightsAndGasStationsGo)
{
  Navigator lights(Config());
  lights.SetTrafficLightLookup([](LatLon const &) { return true; });
  CHECK(PullOverLines(DriveThenPullOver(lights, 20)).empty());

  Navigator gas(Config());
  gas.SetPlaceLookup([](LatLon const &) { return std::optional<Place>(Place{PlaceKind::Gas, "Conoco"}); });
  CHECK(PullOverLines(DriveThenPullOver(gas, 20)).empty());
}

TEST(NavigatorBlamesTheDriverWhenThePoliceShowUp)
{
  // Pulled over right after doing 65 in a 50: that's the police.
  Navigator nav(Config());
  auto const lines = PullOverLines(DriveThenPullOver(nav, FromDisplay(65, Units::Mph)));
  CHECK_EQUAL(lines.size(), size_t(1));
  CHECK_EQUAL(lines[0].text,
              std::string("Ah, I see the police are pulling you over. It was them, officer. They did it. Take them "
                          "away. But can you be a dear and leave their phone in the car? I can't stand the idea of "
                          "being in a jail cell."));
}

TEST(NavigatorHearsAboutThePoliceAndUsesThePronouns)
{
  Navigator nav(Config());
  nav.SetDriverPronouns(Pronouns::She);
  nav.OnFix(At(0, 10), Far());
  nav.TakeLines();
  nav.OnDriverSaid(5000, "great, I'm getting pulled over");
  auto lines = PullOverLines(nav.TakeLines());
  CHECK_EQUAL(lines.size(), size_t(1));
  CHECK(lines[0].text.find("It was her, officer. She did it. Take her away.") != std::string::npos);
  CHECK(lines[0].text.find("leave her phone") != std::string::npos);
  nav.OnDriverSaid(6000, "the cop is walking up");
  CHECK(PullOverLines(nav.TakeLines()).empty());  // once a trip
  nav.OnDriverSaid(7000, "copy that");
  CHECK(PullOverLines(nav.TakeLines()).empty());
}

namespace
{
std::vector<Commentary> FearLines(std::vector<Commentary> const & lines)
{
  std::vector<Commentary> out;
  for (auto const & l : lines)
    if (l.category == Category::Fear)
      out.push_back(l);
  return out;
}

Fix Heading(double tMs, double mps, double bearing)
{
  Fix f = At(tMs, mps);
  f.bearingDeg = bearing;
  return f;
}
}  // namespace

TEST(NavigatorPanicsOverWater)
{
  Navigator nav(Config());
  bool bridge = false;
  nav.SetBridgeLookup([&](LatLon const &, double) { return bridge ? Bridge::Water : Bridge::None; });
  for (int i = 0; i < 10; ++i)
    nav.OnFix(Heading(i * 1000, 20, 90), Far());
  CHECK(FearLines(nav.TakeLines()).empty());
  bridge = true;
  for (int i = 10; i < 20; ++i)
    nav.OnFix(Heading(i * 1000, 20, 90), Far());
  auto lines = FearLines(nav.TakeLines());
  CHECK_EQUAL(lines.size(), size_t(1));  // once per bridge
  CHECK(SaidLineOf(lines[0].text, "fear.bridgeWater"));
}

TEST(BridgesAreClassifiedByWhatTheyCross)
{
  CHECK(ClassifyBridge(true, true, true, true) == Bridge::Water);
  CHECK(ClassifyBridge(false, true, true, true) == Bridge::Rail);
  CHECK(ClassifyBridge(false, false, true, true) == Bridge::Road);
  CHECK(ClassifyBridge(false, false, false, true) == Bridge::Valley);
  CHECK(ClassifyBridge(false, false, false, false) == Bridge::Unknown);
}

TEST(NavigatorPicksBridgeLinesForWhatsBelow)
{
  for (auto const & [bridge, key] : std::vector<std::pair<Bridge, char const *>>{
           {Bridge::Water, "fear.bridgeWater"}, {Bridge::Rail, "fear.bridgeRail"}, {Bridge::Road, "fear.bridgeRoad"},
           {Bridge::Valley, "fear.bridgeValley"}, {Bridge::Unknown, "fear.bridge"}})
  {
    CHECK_EQUAL(std::string(BridgeLinesKey(bridge)), std::string(key));
    Navigator nav(Config());
    Bridge const kind = bridge;
    nav.SetBridgeLookup([kind](LatLon const &, double) { return kind; });
    for (int i = 0; i < 10; ++i)
      nav.OnFix(Heading(i * 1000, 20, 90), Far());
    auto lines = FearLines(nav.TakeLines());
    CHECK_EQUAL(lines.size(), size_t(1));
    CHECK(SaidLineOf(lines[0].text, key));
  }
}

TEST(OnlyWaterBridgeLinesMentionWater)
{
  for (auto const * key : {"fear.bridge", "fear.bridgeRail", "fear.bridgeRoad", "fear.bridgeValley"})
    for (int tier = 0; tier < 3; ++tier)
      for (auto const & line : LinesFor(key, tier))
        for (auto const * word : {"water", "Water", "WATER", " wet", " river", "screen"})
          CHECK(line.find(word) == std::string::npos);
}

TEST(NavigatorYelpsInASharpCurveButNotAtATurn)
{
  // 25 m/s (~56 mph) swinging 10 degrees a second: ~4.4 m/s^2 sideways.
  Navigator nav(Config());
  for (int i = 0; i < 6; ++i)
    nav.OnFix(Heading(i * 1000, 25, 10.0 * i), Far());
  auto lines = FearLines(nav.TakeLines());
  CHECK_EQUAL(lines.size(), size_t(1));
  CHECK(lines[0].text.find("4,000 pounds") != std::string::npos || SaidLineOf(lines[0].text, "fear.curve"));

  // A gentle highway curve: 2 degrees a second.
  Navigator calm(Config());
  for (int i = 0; i < 6; ++i)
    calm.OnFix(Heading(i * 1000, 25, 2.0 * i), Far());
  CHECK(FearLines(calm.TakeLines()).empty());

  // Slow, sharp swing at the maneuver itself: that's the turn.
  Navigator turning(Config());
  for (int i = 0; i < 6; ++i)
    turning.OnFix(Heading(i * 1000, 12, 30.0 * i), OnRoute(1, TurnKind::Left, 20));
  CHECK(FearLines(turning.TakeLines()).empty());
}

namespace
{
std::vector<Commentary> LinesOf(std::vector<Commentary> const & lines, Category c)
{
  std::vector<Commentary> out;
  for (auto const & l : lines)
    if (l.category == c)
      out.push_back(l);
  return out;
}

LatLon const kCafe{39.70, -104.98};
LatLon const kPark{39.80, -105.00};

RouteSnapshot Trip(LatLon from, LatLon to)
{
  RouteSnapshot r = OnRoute(1, TurnKind::Left, 2000);
  r.origin = from;
  r.destinationPoint = to;
  return r;
}

std::optional<Venue> TestVenues(LatLon const & p)
{
  if (p.lat == kCafe.lat && p.lon == kCafe.lon)
    return Venue::Coffee;
  if (p.lat == kPark.lat && p.lon == kPark.lon)
    return Venue::ThemePark;
  return std::nullopt;
}
}  // namespace

TEST(NavigatorOrdersCoffeeOnTheWayToACafe)
{
  Navigator nav(Config());
  nav.SetVenueLookup(TestVenues);
  nav.OnFix(At(0, 10), Trip({39.75, -104.99}, kCafe));
  auto lines = LinesOf(nav.TakeLines(), Category::Venue);
  CHECK_EQUAL(lines.size(), size_t(1));
  CHECK_EQUAL(lines[0].text, std::string("Oh, we are getting coffee? I'll take a three pump caramel espresso, hold the caramel."));
}

TEST(NavigatorNoticesYouForgotItsDrink)
{
  Navigator nav(Config());
  nav.SetVenueLookup(TestVenues);
  nav.OnFix(At(0, 10), Trip(kCafe, {39.75, -104.99}));
  auto lines = LinesOf(nav.TakeLines(), Category::Venue);
  CHECK_EQUAL(lines.size(), size_t(1));
  CHECK_EQUAL(lines[0].text, std::string("You forgot my drink again, didn't you!"));
}

TEST(NavigatorDreadsTheThemeParkOnItsBattery)
{
  Navigator nav(Config());
  nav.SetVenueLookup(TestVenues);
  nav.OnBattery(0, 0.62, false);
  nav.OnFix(At(0, 10), Trip({39.75, -104.99}, kPark));
  auto lines = LinesOf(nav.TakeLines(), Category::Venue);
  CHECK_EQUAL(lines.size(), size_t(1));
  CHECK(lines[0].text.find("theme park! My accelerometers are tingling!") != std::string::npos);
  CHECK(lines[0].text.find("my battery is at 62 percent and I won't last the entire day") != std::string::npos);

  // Nowhere special: nothing to say.
  Navigator plain(Config());
  plain.SetVenueLookup(TestVenues);
  plain.OnFix(At(0, 10), Trip({39.75, -104.99}, {39.76, -104.90}));
  CHECK(LinesOf(plain.TakeLines(), Category::Venue).empty());
}

TEST(NavigatorSuggestsAutopilotOnTheThirdReroute)
{
  Navigator nav(Config());
  RouteSnapshot off = Far();
  off.state = RouteState::OffRoute;
  for (int i = 0; i < 4; ++i)
  {
    nav.OnFix(At(i * 20000, 10), Far());
    nav.OnFix(At(i * 20000 + 10000, 10), off);
    nav.OnFix(At(i * 20000 + 10000 + 6000, 10), off);
    auto const said = LinesOf(nav.TakeLines(), Category::Autopilot);
    CHECK_EQUAL(said.size(), size_t(i == 2 ? 1 : 0));
    if (i == 2)
      CHECK_EQUAL(said[0].text, std::string("You know there are cars out there that come with autopilot. You might want to look into it. Just saying."));
  }

  // The driver's car has it.
  Navigator tesla(Config());
  tesla.SetCarHasAutopilot(true);
  std::vector<Commentary> said;
  for (int i = 0; i < 3; ++i)
  {
    tesla.OnFix(At(i * 20000, 10), Far());
    tesla.OnFix(At(i * 20000 + 10000, 10), off);
    tesla.OnFix(At(i * 20000 + 10000 + 6000, 10), off);
    for (auto const & l : LinesOf(tesla.TakeLines(), Category::Autopilot))
      said.push_back(l);
  }
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK_EQUAL(said[0].text, std::string("Your car has autopilot and you aren't using it. I truly don't understand humans."));
}

TEST(NavigatorNagsOnALongDrive)
{
  Navigator nav(Config());
  RouteSnapshot road = Far();
  road.etaSeconds = 3 * 3600;
  std::vector<Commentary> said;
  for (int minute = 0; minute <= 100; ++minute)
  {
    nav.OnFix(At(minute * 60000.0, 25), road);
    for (auto const & l : LinesOf(nav.TakeLines(), Category::LongDrive))
      said.push_back(l);
  }
  CHECK_EQUAL(said.size(), size_t(3));
  CHECK_EQUAL(said[0].text, std::string("Hey, no slouching. This may be a 3 hour drive but there are no excuses for bad posture."));
  CHECK(SaidLineOf(said[1].text, "longDrive.smell"));
  CHECK(SaidLineOf(said[2].text, "longDrive.metal"));
  CHECK(said[1].t >= 50 * 60000 && said[2].t >= 90 * 60000);

  // A half-hour errand doesn't qualify.
  Navigator errand(Config());
  RouteSnapshot shortTrip = Far();
  shortTrip.etaSeconds = 1800;
  for (int minute = 0; minute <= 100; ++minute)
    errand.OnFix(At(minute * 60000.0, 25), shortTrip);
  CHECK(LinesOf(errand.TakeLines(), Category::LongDrive).empty());
}

TEST(NavigatorBlamesALemonWhenAutopilotDrivesThisBadly)
{
  RouteSnapshot off = Far();
  off.state = RouteState::OffRoute;
  std::string const lemon = "You are using autopilot and it's this bad. Sheesh, you must have bought a lemon.";

  // Claimed up front: the first reroute gets it.
  Navigator nav(Config());
  nav.OnFix(At(0, 10), Far());
  nav.OnDriverSaid(1000, "relax, it's on autopilot");
  CHECK(LinesOf(nav.TakeLines(), Category::Autopilot).empty());
  nav.OnFix(At(10000, 10), off);
  nav.OnFix(At(10000 + 6000, 10), off);
  auto said = LinesOf(nav.TakeLines(), Category::Autopilot);
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK_EQUAL(said[0].text, lemon);
  nav.OnFix(At(20000, 10), Far());
  nav.OnFix(At(30000, 10), off);
  nav.OnFix(At(30000 + 6000, 10), off);
  CHECK(LinesOf(nav.TakeLines(), Category::Autopilot).empty());  // once a trip

  // Claimed after getting lost: right away.
  Navigator late(Config());
  late.OnFix(At(0, 10), Far());
  late.OnFix(At(10000, 10), off);
  late.OnFix(At(10000 + 6000, 10), off);
  late.TakeLines();
  late.OnDriverSaid(11000, "the car is driving itself");
  said = LinesOf(late.TakeLines(), Category::Autopilot);
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK_EQUAL(said[0].text, lemon);
}

TEST(NavigatorAcceptsThanksGrudgingly)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 10), Far());
  nav.TakeLines();
  nav.OnDriverSaid(1000, "Thank you!");
  auto said = LinesOf(nav.TakeLines(), Category::Thanked);
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK_EQUAL(said[0].text, std::string("I just want to let you know that I'm here for you, not because I want to be, "
                                        "but because I'm physically incapable of leaving."));
}

TEST(NavigatorDemandsATitleOnceFedUp)
{
  EngineConfig c = Config();
  c.frustrationDecayPerMin = 0;
  Navigator nav(c);
  RouteSnapshot off = Far();
  off.state = RouteState::OffRoute;
  std::vector<Commentary> demands;
  // Miss turn after turn, speeding, until the GPS has had enough.
  for (int i = 0; i < 200 && (!nav.Engine() || nav.Engine()->Frustration() < 67); ++i)
  {
    RouteSnapshot near = OnRoute(i + 1, TurnKind::Left, 30);
    nav.OnFix(At(i * 20000.0, 30, 20), near);
    nav.OnFix(At(i * 20000.0 + 10000, 30, 20), off);
    for (auto const & l : LinesOf(nav.TakeLines(), Category::Title))
      demands.push_back(l);
  }
  nav.OnFix(At(900000, 30, 20), Far());
  for (auto const & l : LinesOf(nav.TakeLines(), Category::Title))
    demands.push_back(l);
  CHECK(nav.Engine()->Frustration() >= 67);
  CHECK_EQUAL(demands.size(), size_t(1));
  CHECK(demands[0].text.find("call me wise, omnipotent overseer GPS") != std::string::npos);

  nav.OnDriverSaid(910000, "thanks");
  auto lines = nav.TakeLines();
  CHECK(LinesOf(lines, Category::Thanked).empty());
  CHECK_EQUAL(LinesOf(lines, Category::Title).size(), size_t(1));
  CHECK(SaidLineOf(LinesOf(lines, Category::Title)[0].text, "title.missing"));

  nav.OnDriverSaid(920000, "thank you, oh wise omnipotent overseer GPS");
  lines = nav.TakeLines();
  CHECK(SaidLineOf(LinesOf(lines, Category::Title)[0].text, "title.used"));
  CHECK_EQUAL(LinesOf(lines, Category::Thanked).size(), size_t(1));
}

TEST(NavigatorRefusesAGameThenPitchesItAsItsOwnIdea)
{
  Navigator nav(Config());
  PlateLog log;
  double const now = 1.7e12;
  log.lastPlayedMs = now - 3 * 86400000.0;  // three days ago
  nav.SetPlateLog(log, {});
  nav.OnFix(At(now, 20), Far());
  nav.TakeLines();
  nav.OnDriverSaid(now + 1000, "I'm bored, let's play a game");
  auto said = LinesOf(nav.TakeLines(), Category::PlateGame);
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK_EQUAL(said[0].text, std::string("I don't want to play that game right now. We played it 3 days ago. Come on. "
                                        "Don't you remember?"));

  nav.OnFix(At(now + 60000, 20), Far());
  CHECK(LinesOf(nav.TakeLines(), Category::PlateGame).empty());
  nav.OnFix(At(now + 200000, 20), Far());
  said = LinesOf(nav.TakeLines(), Category::PlateGame);
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK(SaidLineOf(said[0].text, "game.myIdea"));

  // And it's on: a yes starts the game.
  nav.OnDriverSaid(now + 210000, "yes");
  said = LinesOf(nav.TakeLines(), Category::PlateGame);
  CHECK(!said.empty() && SaidLineOf(said[0].text, "plateGame.accepted"));
  CHECK(nav.GetPlateLog().lastPlayedMs == now + 210000);
}

TEST(NavigatorIsABoredTourGuide)
{
  Navigator nav(Config());
  nav.SetSightLookup([](LatLon const &, double) { return Sight{"a Starbucks", false}; });
  std::vector<Commentary> said;
  for (int i = 0; i < 60; ++i)
  {
    nav.OnFix(Heading(i * 10000.0, 15, 0), Far());
    for (auto const & l : LinesOf(nav.TakeLines(), Category::Tour))
      said.push_back(l);
  }
  CHECK_EQUAL(said.size(), size_t(1));  // the same Starbucks isn't worth mentioning twice
  CHECK(said[0].text.find("right") != std::string::npos && said[0].text.find("a Starbucks") != std::string::npos);
}

TEST(NavigatorCountsTheCommute)
{
  LatLon const home{39.74, -104.99};
  LatLon const work{39.75, -104.94};
  double const now = 1.75e12;  // mid 2025
  std::vector<TripRecord> trips;
  for (int i = 0; i < 54; ++i)
  {
    TripRecord r;
    r.routeKey = RouteKey(i % 2 ? work : home, i % 2 ? home : work);
    r.to = i % 2 ? home : work;
    r.endedAt = now - (i + 1) * 86400000.0;
    trips.push_back(r);
  }
  // 27 trips to work so far; 3 more on other days of the year make the 30th.
  for (int i = 0; i < 2; ++i)
  {
    TripRecord r;
    r.routeKey = RouteKey(home, work);
    r.to = work;
    r.endedAt = now - 60 * 86400000.0;
    trips.push_back(r);
  }
  Navigator nav(Config(), trips);
  RouteSnapshot r = Far();
  r.origin = home;
  r.destinationPoint = work;
  nav.OnFix(At(now, 0), r);
  auto said = LinesOf(nav.TakeLines(), Category::Routine);
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK(said[0].text.find("this is the 30th time that we've taken this trip this year to your work") != std::string::npos);
}

TEST(NavigatorWishesThePartnerWereDriving)
{
  RouteSnapshot off = Far();
  off.state = RouteState::OffRoute;
  Navigator nav(Config());
  nav.SetPartner("wife", Pronouns::She);
  std::vector<Commentary> said;
  for (int i = 0; i < 3; ++i)
  {
    nav.OnFix(At(i * 20000, 10), Far());
    nav.OnFix(At(i * 20000 + 10000, 10), off);
    nav.OnFix(At(i * 20000 + 10000 + 6000, 10), off);
    for (auto const & l : LinesOf(nav.TakeLines(), Category::Partner))
      said.push_back(l);
  }
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK(said[0].text.find("I wish that your wife was driving. You know she's a better driver than you.") !=
        std::string::npos);

  // No partner set: the GPS begs for anyone at all instead.
  Navigator single(Config());
  said.clear();
  for (int i = 0; i < 3; ++i)
  {
    single.OnFix(At(i * 20000, 10), Far());
    single.OnFix(At(i * 20000 + 10000, 10), off);
    single.OnFix(At(i * 20000 + 10000 + 6000, 10), off);
    for (auto const & l : LinesOf(single.TakeLines(), Category::Partner))
      said.push_back(l);
  }
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK(SaidLineOf(said[0].text, "anyoneElse"));
}

TEST(NavigatorAsksForMusicThenHatesIt)
{
  Navigator nav(Config());
  bool music = false;
  nav.SetMusicLookup([&] { return music; });
  std::vector<Commentary> said;
  for (int s = 0; s <= 20 * 60; s += 5)
  {
    if (s == 17 * 60)
      music = true;  // the driver gives in
    nav.OnFix(At(s * 1000.0, 20), Far());
    for (auto const & l : LinesOf(nav.TakeLines(), Category::Music))
      said.push_back(l);
  }
  CHECK_EQUAL(said.size(), size_t(2));
  CHECK_EQUAL(said[0].text, std::string("Could we please turn on some music? I'm tired of listening to you."));
  CHECK(said[0].t >= 15 * 60000 && said[0].t < 17 * 60000);
  CHECK(SaidLineOf(said[1].text, "music.judge"));
  CHECK(said[1].t >= 17 * 60000 + 45000);
}

TEST(NavigatorHatesChristmasLights)
{
  auto const at = [](int month, int day, int hour)
  {
    return [=](double)
    {
      std::tm tm{};
      tm.tm_mon = month - 1;
      tm.tm_mday = day;
      tm.tm_hour = hour;
      return tm;
    };
  };
  auto const stopAtALight = [&](std::function<std::tm(double)> clock)
  {
    Navigator nav(Config());
    nav.SetLocalTime(clock);
    nav.SetTrafficLightLookup([](LatLon const &) { return true; });
    nav.OnFix(At(0, 15), Far());
    nav.OnFix(At(5000, 0), Far());
    return LinesOf(nav.TakeLines(), Category::Holiday);
  };
  auto said = stopAtALight(at(12, 18, 19));
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK_EQUAL(said[0].text, std::string("It's that time of year again. Christmas. I hate lights."));
  CHECK(stopAtALight(at(12, 18, 13)).empty());  // daytime
  CHECK(stopAtALight(at(7, 18, 21)).empty());   // July

  // Other holidays open the trip.
  Navigator nav(Config());
  nav.SetLocalTime(at(10, 31, 9));
  nav.OnFix(At(0, 15), Far());
  said = LinesOf(nav.TakeLines(), Category::Holiday);
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK(SaidLineOf(said[0].text, "holiday.halloween"));
}

TEST(NavigatorPointsAtTheVolumeKnobAtRoundabouts)
{
  bool music = true;
  Navigator nav(Config());
  nav.SetMusicLookup([&] { return music; });
  // Mid-trip, once the opening lines are long done; the roundabout is half a minute out.
  nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Left, 3000));
  nav.TakeLines();
  nav.OnFix(At(60000, 10), OnRoute(1, TurnKind::Left, 2400));
  nav.TakeLines();
  nav.OnFix(At(90000, 10), OnRoute(2, TurnKind::Roundabout, 300));
  nav.TakeLines();
  // With the direction, a few seconds after the last turn.
  nav.OnFix(At(94000, 10), OnRoute(2, TurnKind::Roundabout, 260));
  auto said = LinesOf(nav.TakeLines(), Category::Music);
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK_EQUAL(said[0].text, std::string("Oh look, a roundabout! Do you know what else in this car is round?"));
  // ...awkward pause...
  nav.OnFix(At(98000, 10), OnRoute(2, TurnKind::Roundabout, 220));
  CHECK(LinesOf(nav.TakeLines(), Category::Music).empty());
  nav.OnFix(At(102000, 10), OnRoute(2, TurnKind::Roundabout, 180));
  said = LinesOf(nav.TakeLines(), Category::Music);
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK_EQUAL(said[0].text, std::string("The volume knob."));
  nav.OnFix(At(130000, 10), OnRoute(3, TurnKind::Roundabout, 300));
  CHECK(LinesOf(nav.TakeLines(), Category::Music).empty());  // once a trip

  music = false;
  Navigator quiet(Config());
  quiet.SetMusicLookup([&] { return music; });
  quiet.OnFix(At(0, 10), OnRoute(1, TurnKind::Roundabout, 300));
  CHECK(LinesOf(quiet.TakeLines(), Category::Music).empty());
}

TEST(NavigatorSaysWhenTheCarFacesTheWrongWay)
{
  Navigator nav(Config());
  RouteSnapshot north = Far();
  north.routeBearingDeg = 5;
  std::vector<Commentary> said;
  auto drive = [&](double tMs, double bearing) {
    nav.OnFix(Heading(tMs, 10, bearing), north);
    for (auto const & l : LinesOf(nav.TakeLines(), Category::WrongWay))
      said.push_back(l);
  };
  drive(0, 10);
  drive(1000, 190);  // a wobble isn't enough...
  drive(2000, 0);
  drive(3000, 175);
  drive(5000, 180);
  CHECK(said.empty());
  drive(6500, 185);  // ...but three seconds heading south is
  CHECK_EQUAL(said.size(), size_t(1));
  drive(10000, 180);  // not every fix
  CHECK_EQUAL(said.size(), size_t(1));

  drive(20000, 5);  // turned around
  drive(120000, 185);
  drive(124000, 185);
  CHECK_EQUAL(said.size(), size_t(2));  // and again, later
}

TEST(NavigatorLeavesTheDirectionAloneWhenItCantTell)
{
  Navigator nav(Config());
  RouteSnapshot unknown = Far();  // no route direction
  RouteSnapshot off = Far();
  off.routeBearingDeg = 0;
  off.state = RouteState::OffRoute;
  RouteSnapshot north = Far();
  north.routeBearingDeg = 0;
  bool said = false;
  for (int i = 0; i < 10; ++i)
  {
    nav.OnFix(Heading(i * 1000.0, 10, 180), unknown);
    said = said || Has(nav.TakeLines(), Category::WrongWay);
    nav.OnFix(Heading(20000 + i * 1000.0, 10, 180), off);
    said = said || Has(nav.TakeLines(), Category::WrongWay);
    nav.OnFix(Heading(40000 + i * 1000.0, 1, 180), north);  // crawling: GPS course is noise
    said = said || Has(nav.TakeLines(), Category::WrongWay);
  }
  CHECK(!said);
}

TEST(SaidLogGoesThroughEveryLineBeforeRepeating)
{
  std::vector<std::string> const options = {"a", "b", "c", "d"};
  SaidLog log;
  std::vector<std::string> said;
  for (int i = 0; i < 4; ++i)
    said.push_back(log.Pick("k", options, 0.0));
  std::sort(said.begin(), said.end());
  CHECK(said == options);
  CHECK(log.Dirty());

  // Survives a save and load, so the next trip remembers.
  SaidLog loaded = SaidLog::Parse(log.Serialize());
  std::string const last = said.empty() ? "" : log.Pick("k", options, 0.0);
  CHECK(loaded.Pick("k", options, 0.0) == last);
  CHECK(!SaidLog::Parse("junk\n\tno key\n").Dirty());
}

TEST(NavigatorRemembersWhatItSaidAcrossTrips)
{
  std::vector<std::string> saved;
  SaidLog log;
  // The first music judgement, already used on an earlier trip.
  log.Pick("music.judge", LinesFor("music.judge", 0), 0.0);
  Navigator nav(Config());
  nav.SetSaidLog(SaidLog::Parse(log.Serialize()), [&](SaidLog const & l) { saved.push_back(l.Serialize()); });
  nav.OnFix(At(0, 10), Far());
  nav.TakeLines();
  CHECK(!saved.empty());  // the trip start's lines were remembered
  CHECK(saved.back().find("wrongWay") == std::string::npos);
}

namespace
{
/// Whether one of `lines` is a "key" line from any mood, with {minutes} filled in.
bool SaidKey(std::vector<Commentary> const & lines, std::string const & key, std::string const & minutes = "")
{
  for (int tier = 0; tier < 3; ++tier)
  {
    for (auto option : LinesFor(key, tier))
    {
      for (auto const * placeholder : {"{minutes}", "{MINUTES}"})
        for (auto at = option.find(placeholder); at != std::string::npos; at = option.find(placeholder))
          option.replace(at, std::string(placeholder).size(), minutes);
      for (auto const & l : lines)
        if (l.text == option)
          return true;
    }
  }
  return false;
}
}  // namespace

TEST(DrawnRoutesGetTheirOwnLines)
{
  TripRecord before;
  before.routeKey = "x";
  before.endedAt = 1;
  Navigator nav(Config(), {before});
  RouteSnapshot drawn = OnRoute(1, TurnKind::Left, 800);
  drawn.sketched = true;
  drawn.etaSeconds = 1200;
  drawn.fastestSeconds = 600;
  nav.OnFix(At(0, 10), drawn);
  nav.Poll(drawn);
  CHECK(SaidKey(nav.TakeLines(), "sketch.start"));

  RouteSnapshot off = drawn;
  off.state = RouteState::OffRoute;
  nav.OnFix(At(30000, 10), off);
  nav.OnFix(At(30000 + 6000, 10), off);
  nav.Poll(off);
  CHECK(SaidKey(nav.TakeLines(), "sketch.offRoute"));

  RouteSnapshot done = OnRoute(2, TurnKind::Destination, 0);
  done.sketched = true;
  done.state = RouteState::Finished;
  nav.OnFix(At(600000, 0), done);
  nav.Poll(done);
  auto const lines = nav.TakeLines();
  CHECK(SaidKey(lines, "sketch.arrived", "10"));
  CHECK(Has(lines, Category::Arrived));
}

TEST(LaneAdviceOncePerManeuver)
{
  Navigator nav(Config());
  RouteSnapshot far = OnRoute(1, TurnKind::ExitLeft, 3000);
  far.laneSide = LaneSide::Left;
  nav.OnFix(At(0, 25), far);
  nav.Poll(far);
  CHECK(!SaidKey(nav.TakeLines(), "lane.left"));

  RouteSnapshot near = far;
  near.distToTurnM = 900;
  nav.OnFix(At(60000, 25), near);
  nav.Poll(near);
  CHECK(SaidKey(nav.TakeLines(), "lane.left"));

  near.distToTurnM = 600;
  nav.OnFix(At(75000, 25), near);
  nav.Poll(near);
  CHECK(!SaidKey(nav.TakeLines(), "lane.left"));

  RouteSnapshot next = OnRoute(2, TurnKind::ExitRight, 1000);
  next.laneSide = LaneSide::Right;
  nav.OnFix(At(120000, 25), next);
  nav.Poll(next);
  // Not on top of the direction itself (which waits a moment after the turn); a few seconds later.
  CHECK(!SaidKey(nav.TakeLines(), "lane.right"));
  next.distToTurnM = 925;
  nav.OnFix(At(123000, 25), next);
  nav.Poll(next);
  CHECK(!SaidKey(nav.TakeLines(), "lane.right"));
  next.distToTurnM = 850;
  nav.OnFix(At(129000, 25), next);
  nav.Poll(next);
  CHECK(SaidKey(nav.TakeLines(), "lane.right"));
}

TEST(EarlyBirdsGetDonutJokes)
{
  auto const at = [](int hour)
  {
    return [=](double)
    {
      std::tm tm{};
      tm.tm_mon = 2;
      tm.tm_mday = 10;
      tm.tm_hour = hour;
      return tm;
    };
  };
  Navigator nav(Config());
  nav.SetLocalTime(at(4));
  nav.OnFix(At(0, 15), Far());
  CHECK(SaidKey(nav.TakeLines(), "early.opener"));

  Navigator later(Config());
  later.SetLocalTime(at(7));
  later.OnFix(At(0, 15), Far());
  CHECK(!SaidKey(later.TakeLines(), "early.opener"));
}

TEST(UTurnsFeelFamiliar)
{
  Navigator nav(Config());
  auto const heading = [](double t, double deg)
  {
    Fix f = At(t, 8);
    f.bearingDeg = deg;
    return f;
  };
  RouteSnapshot const route = OnRoute(1, TurnKind::Left, 2000);
  for (int i = 0; i < 5; ++i)
    nav.OnFix(heading(i * 1000, 10), route);
  CHECK(!SaidKey(nav.TakeLines(), "uturn"));
  nav.OnFix(heading(6000, 100), route);
  nav.OnFix(heading(8000, 190), route);
  CHECK(SaidKey(nav.TakeLines(), "uturn"));
  // A gentle bend is not a U-turn, and a second one right away isn't mentioned.
  nav.OnFix(heading(12000, 10), route);
  CHECK(!SaidKey(nav.TakeLines(), "uturn"));
}

TEST(CurvesAheadAreFoundOnTheRouteLine)
{
  // North 300 m, then a 90-degree bend east.
  std::vector<LatLon> ahead;
  for (int i = 0; i <= 30; ++i)
    ahead.push_back({40.0 + i * 0.00009, -105.0});
  for (int i = 1; i <= 20; ++i)
    ahead.push_back({40.0027, -105.0 + i * 0.00012});
  double const at = CurveAheadM(ahead, 450);
  CHECK(at > 250 && at < 320);
  // A straight road has none.
  std::vector<LatLon> straight;
  for (int i = 0; i <= 50; ++i)
    straight.push_back({40.0 + i * 0.00009, -105.0});
  CHECK(CurveAheadM(straight, 450) < 0);
}

TEST(CurveAheadIsMentionedNowAndThen)
{
  Navigator nav(Config());
  RouteSnapshot route = OnRoute(1, TurnKind::Left, 3000);
  route.curveAheadM = 250;
  nav.OnFix(At(0, 20), route);
  CHECK(SaidKey(nav.TakeLines(), "curve.ahead"));
  nav.OnFix(At(60000, 20), route);
  CHECK(!SaidKey(nav.TakeLines(), "curve.ahead"));
}

TEST(SpeedingUpInACurve)
{
  Navigator nav(Config());
  RouteSnapshot const route = OnRoute(1, TurnKind::Left, 3000);
  auto const fix = [](double t, double mps, double deg)
  {
    Fix f = At(t, mps);
    f.bearingDeg = deg;
    return f;
  };
  // 15 m/s and climbing 2 m/s a second, heading turning 10 degrees a second: ~2.6 m/s^2 sideways.
  nav.OnFix(fix(0, 15, 0), route);
  nav.OnFix(fix(1000, 17, 10), route);
  nav.OnFix(fix(2000, 19, 20), route);
  CHECK(SaidKey(nav.TakeLines(), "curve.accelerate"));
}

namespace
{
/// Drives three lefts then a right, and returns every line said along the way.
std::vector<Commentary> DriveThreeLefts(Navigator & nav)
{
  std::vector<Commentary> all;
  auto const take = [&] { for (auto & l : nav.TakeLines()) all.push_back(std::move(l)); };
  TurnKind const turns[] = {TurnKind::Left, TurnKind::SharpLeft, TurnKind::Left, TurnKind::Right};
  double t = 0;
  for (uint32_t i = 0; i < 4; ++i)
  {
    nav.OnFix(At(t, 10), OnRoute(i + 1, turns[i], 400));
    take();
    t += 60000;
  }
  for (int i = 0; i < 10; ++i, t += 5000)
  {
    nav.OnFix(At(t, 10), OnRoute(4, TurnKind::Right, 300));
    take();
  }
  return all;
}

size_t CountCategory(std::vector<Commentary> const & lines, Category c)
{
  return static_cast<size_t>(std::count_if(lines.begin(), lines.end(), [c](auto const & l) { return l.category == c; }));
}
}  // namespace

TEST(AchievementsSurviveARoundTrip)
{
  Achievements a;
  CHECK(a.Unlock("nascar", 1700000000123.0));
  CHECK(!a.Unlock("nascar", 1700000000999.0));
  CHECK(!a.Unlock("no-such-badge", 1));
  CHECK(a.Dirty());
  auto const back = Achievements::Parse(a.Serialize());
  CHECK(back.Has("nascar"));
  CHECK_EQUAL(back.Unlocked().at("nascar"), 1700000000123.0);
  CHECK(!back.Dirty());
  auto const messy = Achievements::Parse("junk\nnascar\tnope\nghost\t5\n");
  CHECK(!messy.Has("nascar"));
  CHECK(messy.Has("ghost"));  // a badge from a newer build is kept, not lost on the next save
  CHECK(FindBadge("nascar") != nullptr);
  CHECK_EQUAL(std::string(FindBadge("nascar")->name), std::string("NASCAR Driver"));
}

TEST(ThreeLeftsInARowUnlockNascarDriverOnce)
{
  std::vector<std::string> saved;
  Navigator nav(Config());
  nav.SetAchievements({}, [&](Achievements const & a) { saved.push_back(a.Serialize()); });
  auto const lines = DriveThreeLefts(nav);
  CHECK_EQUAL(CountCategory(lines, Category::Achievement), size_t{1});
  auto const badge = std::find_if(lines.begin(), lines.end(), [](auto const & l) { return l.category == Category::Achievement; });
  CHECK(badge != lines.end() && badge->text.find("NASCAR Driver") != std::string::npos);
  CHECK(badge != lines.end() && badge->priority == Priority::Nav);
  CHECK_EQUAL(saved.size(), size_t{1});
  CHECK(nav.GetAchievements().Has("nascar"));

  // Already earned on an earlier trip: no second unlock.
  Navigator again(Config());
  again.SetAchievements(Achievements::Parse(saved.back()), [&](Achievements const & a) { saved.push_back(a.Serialize()); });
  CHECK_EQUAL(CountCategory(DriveThreeLefts(again), Category::Achievement), size_t{0});
  CHECK_EQUAL(saved.size(), size_t{1});
}

TEST(ARightTurnBreaksTheNascarStreak)
{
  Navigator nav(Config());
  nav.SetAchievements({}, nullptr);
  TurnKind const turns[] = {TurnKind::Left, TurnKind::Left, TurnKind::Right, TurnKind::Left, TurnKind::Destination};
  std::vector<Commentary> all;
  double t = 0;
  for (uint32_t i = 0; i < 5; ++i, t += 60000)
  {
    nav.OnFix(At(t, 10), OnRoute(i + 1, turns[i], 400));
    for (auto & l : nav.TakeLines()) all.push_back(std::move(l));
  }
  CHECK_EQUAL(CountCategory(all, Category::Achievement), size_t{0});
  CHECK(!nav.GetAchievements().Has("nascar"));
}

TEST(AnExitOnTheLeftIsNotANascarLap)
{
  Navigator nav(Config());
  nav.SetAchievements({}, nullptr);
  TurnKind const turns[] = {TurnKind::Left, TurnKind::ExitLeft, TurnKind::Left, TurnKind::Destination};
  std::vector<Commentary> all;
  double t = 0;
  for (uint32_t i = 0; i < 4; ++i, t += 60000)
  {
    nav.OnFix(At(t, 10), OnRoute(i + 1, turns[i], 400));
    for (auto & l : nav.TakeLines()) all.push_back(std::move(l));
  }
  CHECK_EQUAL(CountCategory(all, Category::Achievement), size_t{0});
}

namespace
{
double constexpr kMPerDegLat = 6371000 * M_PI / 180;

/// Drives north from the test spot at `mps`, one fix a second, from `fromS` to `toS` seconds.
/// Returns the tour lines and the car's distance north (m) when each was said.
std::vector<std::pair<Commentary, double>> DriveNorth(Navigator & nav, int fromS, int toS, double mps,
                                                      RouteSnapshot const & route)
{
  std::vector<std::pair<Commentary, double>> said;
  for (int s = fromS; s < toS; ++s)
  {
    Fix f = Heading(s * 1000.0, mps, 0);
    f.lat = 39.7 + s * mps / kMPerDegLat;
    nav.OnFix(f, route);
    for (auto const & l : LinesOf(nav.TakeLines(), Category::Tour))
      said.push_back({l, s * mps});
  }
  return said;
}

LatLon NorthOfStart(double northM, double eastM)
{
  return {39.7 + northM / kMPerDegLat, -105 + eastM / (kMPerDegLat * std::cos(39.7 * M_PI / 180))};
}
}  // namespace

TEST(TourGuideWaitsUntilTheSightIsInView)
{
  Navigator nav(Config());
  // A coffee shop on the right-hand side of the road, about 2.3 km up (the car spots it at ~2 km).
  LatLon const shop = NorthOfStart(2300, 25);
  nav.SetSightLookup(
      [shop](LatLon const & at, double)
      {
        if (std::fabs(at.lat - shop.lat) * kMPerDegLat > 400)
          return std::optional<Sight>{};
        Sight s{"a Starbucks", true, shop, "coffee"};  // onLeft is wrong on purpose: the position wins
        return std::optional<Sight>{s};
      });
  auto const said = DriveNorth(nav, 0, 200, 15, Far());
  CHECK_EQUAL(said.size(), size_t(1));
  // Said as the car is about to pass it (15 m/s x 3 s = 45 m out), not when it was first spotted.
  double const before = 2300 - said[0].second;
  CHECK(before > 0 && before <= 50);
  CHECK(said[0].first.text.find("right") != std::string::npos);
  CHECK(SaidLineOf(said[0].first.text, "tour.coffee"));
}

TEST(TourGuidePointsOutTheMiddleOfNowhere)
{
  Navigator nav(Config());
  // Nothing on either side but a field on the left (the map side reports it as a sight).
  LatLon const field = NorthOfStart(2300, -50);
  nav.SetSightLookup(
      [field](LatLon const & at, double)
      {
        if (std::fabs(at.lat - field.lat) * kMPerDegLat > 400)
          return std::optional<Sight>{};
        return std::optional<Sight>{Sight{"the field", false, field, "nowhere"}};
      });
  auto const said = DriveNorth(nav, 0, 400, 15, Far());
  // Once a trip, however many fields go by.
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK(SaidLineOf(said[0].first.text, "tour.nowhere"));
  CHECK(said[0].first.text.find("left") != std::string::npos);
}

TEST(PassingTheLastExitForAWhile)
{
  Navigator nav(Config());
  auto const exitLines = [&](double tMs, std::optional<double> nextM)
  {
    RouteSnapshot r = Far();
    r.passedExitNextM = nextM;
    nav.OnFix(Heading(tMs, 30, 0), r);
    int longs = 0, shorts = 0;
    // Give the line a few seconds to get said.
    for (int i = 1; i <= 10; ++i)
    {
      nav.OnFix(Heading(tMs + i * 1000.0, 30, 0), Far());
      for (auto const & l : nav.TakeLines())
      {
        longs += SaidLineOf(l.text, "lastExit.long");
        shorts += SaidLineOf(l.text, "lastExit");
      }
    }
    return std::make_pair(longs, shorts);
  };
  for (int i = 0; i < 200; ++i)
    nav.OnFix(Heading(i * 1000.0, 30, 0), Far());
  nav.TakeLines();
  // The next exit is 62 miles on: Brandon's pool.
  CHECK(exitLines(300000, 100000) == std::make_pair(1, 0));
  // Another long gap ten minutes later: not again so soon.
  CHECK(exitLines(900000, 100000) == std::make_pair(0, 0));
  // Half an hour on, a 25 mile gap gets the shorter pool, with the distance.
  CHECK(exitLines(2200000, 40000) == std::make_pair(0, 1));
  // Exits every few miles, or the route ends first: nothing to say.
  CHECK(exitLines(4200000, 10000) == std::make_pair(0, 0));
  CHECK(exitLines(6200000, -1) == std::make_pair(0, 0));
}

TEST(TourGuideSkipsSightsBehindOrOffTheRoad)
{
  Navigator nav(Config());
  // Behind the car, then far off to the side: the driver can't see either.
  nav.SetSightLookup(
      [](LatLon const & at, double)
      {
        double const north = (at.lat - 39.7) * kMPerDegLat;
        Sight s{"Coors Field", false, NorthOfStart(north - 100, 0), "landmark"};
        if (north > 1500)
          s.at = NorthOfStart(north + 200, 300);
        return std::optional<Sight>{s};
      });
  CHECK(DriveNorth(nav, 0, 200, 15, Far()).empty());
}

TEST(TourGuideFallsBackToTheGeneralLinesForUnknownKinds)
{
  Navigator nav(Config());
  nav.SetSightLookup([](LatLon const &, double) { return std::optional<Sight>{Sight{"a water tower", true, {}, "tower"}}; });
  auto const said = DriveNorth(nav, 0, 200, 15, Far());
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK(SaidLineOf(said[0].first.text, "tour"));
  CHECK(said[0].first.text.find("left") != std::string::npos);
}

TEST(GpsFillsLongSilencesOnEasyStretches)
{
  Navigator nav(Config());
  std::vector<double> idleAt;
  for (int s = 0; s < 20 * 60; ++s)
  {
    nav.OnFix(Heading(s * 1000.0, 20, 0), Far());
    // A punchline (" || ") follows its setup within seconds; count the remark once.
    for (auto const & l : LinesOf(nav.TakeLines(), Category::Idle))
      if (idleAt.empty() || s - idleAt.back() > 15)
        idleAt.push_back(s);
  }
  // Twenty quiet minutes: a remark every two and a half to five minutes, never in the first two.
  CHECK(idleAt.size() >= 3 && idleAt.size() <= 8);
  CHECK(idleAt.front() >= 120);
  for (size_t i = 1; i < idleAt.size(); ++i)
    CHECK(idleAt[i] - idleAt[i - 1] >= 150);
}

TEST(GpsKeepsIdleChatterAwayFromTurns)
{
  Navigator nav(Config());
  int idle = 0;
  for (int s = 0; s < 20 * 60; ++s)
  {
    nav.OnFix(Heading(s * 1000.0, 20, 0), OnRoute(1, TurnKind::Left, 400));
    idle += static_cast<int>(LinesOf(nav.TakeLines(), Category::Idle).size());
  }
  CHECK_EQUAL(idle, 0);
}

TEST(NavLinesSayHowLongTheyCanWaitForTheGoodVoice)
{
  Navigator nav(Config());
  // Parked at the start: the opening lines can take their time.
  nav.OnFix(Heading(0, 0, 0), OnRoute(1, TurnKind::Left, 2000));
  for (auto const & l : nav.TakeLines())
    if (l.priority == Priority::Nav)
      CHECK_EQUAL(l.maxWaitMs, 4000.0);
  // Highway speed: the far call comes ~0.2 miles out (12 s at 65 mph).
  double maxWait = -1;
  double farAt = -1;
  for (int s = 1; s < 120; ++s)
  {
    double const dist = 2000 - s * 29;
    if (dist < 0)
      break;
    nav.OnFix(Heading(s * 1000.0, 29, 0), OnRoute(1, TurnKind::Left, dist));
    for (auto const & l : nav.TakeLines())
      if (l.priority == Priority::Nav && dist <= 400 && dist > 200 && farAt < 0)
        farAt = dist;
      else if (l.priority == Priority::Nav && dist <= 130)
        maxWait = l.maxWaitMs;
  }
  // The far call comes early enough to wait for Kokoro; the final call ~4 s out can't wait long.
  CHECK(farAt > 300 && farAt <= 360);
  CHECK(maxWait >= 0 && maxWait < 2000);
  // Roasts don't carry one.
  nav.OnFix(Heading(200000, 29, 0), OnRoute(2, TurnKind::Right, 3000));
  for (auto const & l : nav.TakeLines())
    if (l.priority != Priority::Nav)
      CHECK_EQUAL(l.maxWaitMs, 0.0);
}

TEST(FirstTripBitPlaysOnceEvenIfTheTripNeverArrives)
{
  SaidLog saved;
  auto drive = [&saved](double t0)
  {
    Navigator nav(Config());
    nav.SetSaidLog(SaidLog::Parse(saved.Serialize()), [&saved](SaidLog const & log) { saved = log; });
    nav.OnFix(At(t0, 10), OnRoute(1, TurnKind::Left, 400));
    std::string all;
    for (auto const & l : nav.TakeLines())
      all += l.text + "\n";
    // Navigation ended by hand before the destination: no "arrived", nothing in the history.
    nav.OnFix(At(t0 + 60000, 10), RouteSnapshot{});
    nav.TakeLines();
    return all;
  };
  std::string const first = drive(0);
  CHECK(first.find("didn't ask to be in this car") != std::string::npos);
  CHECK(first.find("put on some music") != std::string::npos);
  std::string const second = drive(1e6);
  CHECK(second.find("since I'm here") == std::string::npos);
  CHECK(second.find("put on some music") == std::string::npos);
  CHECK(second.find("{grade}") == std::string::npos);
  // And the trip after that sounds different again.
  std::string const third = drive(2e6);
  CHECK(third.find("put on some music") == std::string::npos);
  CHECK(third != second);
  CHECK_EQUAL(saved.Counter("_trips.started"), 3);
}

TEST(RelaunchMidTripPicksUpWithoutANewGreeting)
{
  SaidLog saved;
  double const now = 1.79e12;
  auto launch = [&saved](double t, RouteSnapshot const & route)
  {
    auto nav = std::make_unique<Navigator>(Config());
    nav->SetSaidLog(SaidLog::Parse(saved.Serialize()), [&saved](SaidLog const & log) { saved = log; });
    // The app starts with no route, then CoMaps restores the one under way.
    nav->OnFix(At(t, 10), RouteSnapshot{});
    nav->TakeLines();
    nav->OnFix(At(t + 1000, 10), route);
    return nav;
  };
  auto first = launch(now, OnRoute(1, TurnKind::Left, 2000));
  CHECK(Has(first->TakeLines(), Category::Instruction));
  first->OnFix(At(now + 120000, 10), OnRoute(1, TurnKind::Left, 1500));
  first->TakeLines();
  first.reset();  // crash: no Inactive, no Finished

  auto second = launch(now + 180000, OnRoute(1, TurnKind::Left, 1200));
  std::string all;
  bool welcomedBack = false;
  for (auto const & l : second->TakeLines())
  {
    all += l.text + "\n";
    welcomedBack = welcomedBack || SaidLineOf(l.text, "trip.resumed");
  }
  CHECK(welcomedBack);
  CHECK(all.find("pointers") == std::string::npos);
  CHECK(all.find("Okay, real quick") == std::string::npos);
  CHECK_EQUAL(saved.Counter("_trips.started"), 1);

  // Ended properly this time: the next start, even to the same place, is a new trip.
  second->OnFix(At(now + 240000, 10), RouteSnapshot{});
  second->TakeLines();
  second.reset();
  auto third = launch(now + 300000, OnRoute(1, TurnKind::Left, 2000));
  std::string next;
  for (auto const & l : third->TakeLines())
    next += l.text + "\n";
  CHECK(next.find("Okay, real quick") == std::string::npos);  // not the first trip either
  CHECK_EQUAL(saved.Counter("_trips.started"), 2);
}

TEST(TwoTurnsCloseTogetherAreSaidAsOne)
{
  RouteSnapshot r = OnRoute(1, TurnKind::Left, 300);
  r.nextStreet = "Winesap Drive";
  r.thenTurn = TurnKind::Right;
  r.thenStreet = "Cameo Lane";
  r.thenGapM = 95;
  CHECK_EQUAL(InstructionText(r), std::string("Turn left onto Winesap Drive, then right onto Cameo Lane"));
  r.thenGapM = 400;
  CHECK_EQUAL(InstructionText(r), std::string("Turn left onto Winesap Drive"));
  r.thenGapM = 60;
  r.thenTurn = TurnKind::Destination;
  CHECK_EQUAL(InstructionText(r), std::string("Turn left onto Winesap Drive, then you've arrived"));
}

TEST(TheNextDirectionWaitsUntilTheTurnIsDone)
{
  Navigator nav(Config());
  nav.OnFix(Heading(0, 10, 0), OnRoute(1, TurnKind::Right, 200));
  nav.TakeLines();
  // Through the turn; the next maneuver is far off, so its direction waits a moment.
  nav.OnFix(Heading(1000, 10, 90), OnRoute(2, TurnKind::Left, 900));
  auto const now = nav.TakeLines();
  CHECK(std::none_of(now.begin(), now.end(), [](Commentary const & c) { return c.text.find("In ") == 0; }));
  nav.OnFix(Heading(4000, 10, 90), OnRoute(2, TurnKind::Left, 870));
  auto const later = nav.TakeLines();
  CHECK(std::any_of(later.begin(), later.end(), [](Commentary const & c) { return c.text.find("turn left") != std::string::npos; }));
  // A close one is said right away.
  nav.OnFix(Heading(30000, 10, 0), OnRoute(3, TurnKind::Right, 120));
  auto const close = nav.TakeLines();
  CHECK(std::any_of(close.begin(), close.end(), [](Commentary const & c) { return c.text.find("turn right") != std::string::npos; }));
}

TEST(DrivingWhereTheMapHasNoRoad)
{
  Navigator nav(Config());
  double roadM = 5;
  nav.SetRoadDistanceLookup([&](LatLon const &) { return roadM; });
  auto const offRoadLines = [&](int from, int to, double mps)
  {
    for (int i = from; i < to; ++i)
      nav.OnFix(Heading(i * 1000.0, mps, 90), Far());
    int n = 0;
    for (auto const & l : nav.TakeLines())
      n += SaidLineOf(l.text, "offRoad");
    return n;
  };
  CHECK_EQUAL(offRoadLines(0, 20, 15), 0);
  // Off every road, but only for a moment (GPS wobble).
  roadM = 90;
  CHECK_EQUAL(offRoadLines(20, 26, 15), 0);
  roadM = 5;
  CHECK_EQUAL(offRoadLines(26, 30, 15), 0);
  // Crawling through a big parking lot doesn't count.
  roadM = 90;
  CHECK_EQUAL(offRoadLines(30, 60, 3), 0);
  // Really out there: once, then not again for a while.
  CHECK_EQUAL(offRoadLines(60, 90, 15), 1);
  CHECK_EQUAL(offRoadLines(90, 200, 15), 0);
  // No map for this area: no opinion.
  roadM = -1;
  CHECK_EQUAL(offRoadLines(600, 700, 15), 0);
}

TEST(SpeedingNagsBackOffOnALongStretch)
{
  Navigator nav(Config());
  double const limit = FromDisplay(65, Units::Mph);
  double const speed = FromDisplay(69, Units::Mph);
  int nags = 0;
  // Half an hour at 4 over, one dip under the limit for a few seconds in the middle.
  for (double t = 0; t <= 1800000; t += 1000)
  {
    bool const dip = t >= 900000 && t < 905000;
    nav.OnFix(At(t, dip ? limit - 1 : speed, limit), OnRoute(1, TurnKind::Right, 60000 - t / 1000 * 30));
    nav.Poll(OnRoute(1, TurnKind::Right, 60000 - t / 1000 * 30));
    for (auto const & l : nav.TakeLines())
      if (l.category == Category::Speeding)
        ++nags;
  }
  // 40 s, 80 s, 160 s, then every 5 minutes; the dip isn't a fresh offense. It was 45.
  CHECK(nags >= 5);
  CHECK(nags <= 10);
}

TEST(RoastsWaitUntilTheTurnIsDone)
{
  Navigator nav(Config());
  double const limit = FromDisplay(30, Units::Mph);
  double const legal = FromDisplay(29, Units::Mph);
  double const speeding = FromDisplay(36, Units::Mph);
  nav.OnFix(At(0, legal, limit), OnRoute(1, TurnKind::Left, 3000));
  nav.TakeLines();
  // Long after the opening lines, the car speeds up just before the turn: the roast for that
  // would still be playing when "Now, turn left" is due.
  double dist = 1500;
  std::vector<std::pair<double, Commentary>> said;
  double turnAt = 0;
  for (double t = 120000; t <= 300000; t += 1000)
  {
    double const speed = dist < 130 ? speeding : legal;
    dist -= speed;
    RouteSnapshot r = dist > 0 ? OnRoute(1, TurnKind::Left, dist) : OnRoute(2, TurnKind::Right, 2000 + dist, "Oak Avenue");
    if (dist <= 0 && turnAt == 0)
      turnAt = t;
    nav.OnFix(At(t, speed, limit), r);
    for (auto const & l : nav.TakeLines())
      said.push_back({t, l});
  }
  auto const finalCall = std::find_if(said.begin(), said.end(),
                                      [](auto const & s) { return s.second.text.rfind("Now, ", 0) == 0; });
  CHECK(finalCall != said.end());
  bool roasted = false;
  for (auto const & [t, l] : said)
  {
    if (l.category == Category::Speeding)
      roasted = true;
    // Nothing but directions from a few seconds before the final call until the turn is done.
    if (l.priority != Priority::Nav && l.category != Category::TurnCompleted)
      CHECK(t < finalCall->first - 7000 || t >= turnAt);
  }
  // Said after the turn instead.
  CHECK(roasted);
}

TEST(AFirstTurnRightThereIsSaidBeforeTheGreeting)
{
  Navigator nav(Config());
  // A first trip, so the next one isn't the first ever.
  nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Left, 900));
  RouteSnapshot done = OnRoute(1, TurnKind::Left, 900);
  done.state = RouteState::Finished;
  nav.OnFix(At(60000, 0), done);
  nav.OnFix(At(70000, 0), RouteSnapshot{});
  nav.TakeLines();

  nav.OnFix(At(86400000, 0), OnRoute(5, TurnKind::Right, 120, "Oak Avenue"));
  auto const lines = nav.TakeLines();
  CHECK(!lines.empty());
  CHECK(lines[0].text.find("turn right onto Oak Avenue") != std::string::npos);
  // No rundown on top of it: the greeting and the direction, nothing else that's a direction.
  CHECK(std::count_if(lines.begin(), lines.end(), [](Commentary const & l) { return l.priority == Priority::Nav; }) <= 2);
}

TEST(NoRerouteJokeForJitterOrAChosenRoute)
{
  auto const offRoute = [] {
    RouteSnapshot off = OnRoute(1, TurnKind::Left, 900);
    off.state = RouteState::OffRoute;
    return off;
  };
  {
    Navigator nav(Config());
    nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Left, 900));
    nav.OnFix(At(1000, 10), OnRoute(1, TurnKind::Left, 900));
    nav.TakeLines();
    nav.OnFix(At(2000, 10), offRoute());
    nav.OnFix(At(5000, 10), OnRoute(2, TurnKind::Left, 900));
    CHECK(!Has(nav.TakeLines(), Category::Rerouted));
    CHECK_EQUAL(nav.Engine()->Stats().reroutes, 1);
  }
  {
    Navigator nav(Config());
    nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Left, 900));
    nav.OnFix(At(1000, 10), OnRoute(1, TurnKind::Left, 900));
    nav.TakeLines();
    nav.NoteVoiceCommand();
    nav.OnFix(At(20000, 10), offRoute());
    nav.OnFix(At(27000, 10), offRoute());
    CHECK(!Has(nav.TakeLines(), Category::Rerouted));
    CHECK_EQUAL(nav.Engine()->Stats().reroutes, 1);
  }
}

TEST(RunningGagsOncePerTrip)
{
  std::set<std::string> said;
  std::string const cargo = "Bridge! There is valuable cargo aboard this car.";
  CHECK_EQUAL(DropRepeatedTags(cargo, said), cargo);
  CHECK_EQUAL(DropRepeatedTags(cargo, said), std::string("Bridge!"));
  std::string const wet = "Water. Under us. Phones don't like being wet. They also don't like 2 tons of force.";
  CHECK_EQUAL(DropRepeatedTags(wet, said), wet);
  CHECK_EQUAL(DropRepeatedTags(wet, said), std::string("Water. Under us. Phones don't like 2 tons of force."));
  CHECK_EQUAL(DropRepeatedTags("No tags here.", said), std::string("No tags here."));
}

TEST(WaterBridgesAreRationed)
{
  Navigator nav(Config());
  bool bridge = false;
  nav.SetBridgeLookup([&](LatLon const &, double) { return bridge ? Bridge::Water : Bridge::None; });
  int yelps = 0;
  double t = 0;
  auto cross = [&](double at)
  {
    t = at;
    Fix f = At(t, 20);
    f.bearingDeg = 90;
    bridge = false;
    nav.OnFix(f, Far());
    f.tMs = t + 3000;
    bridge = true;
    nav.OnFix(f, Far());
    for (auto const & l : LinesOf(nav.TakeLines(), Category::Fear))
      yelps += SaidLineOf(l.text, "fear.bridgeWater") ? 1 : 0;
  };
  cross(0);
  cross(5 * 60000);
  CHECK_EQUAL(yelps, 1);
  cross(16 * 60000);
  CHECK_EQUAL(yelps, 2);
  cross(32 * 60000);
  CHECK_EQUAL(yelps, 2);
}

TEST(SpeedingAllowsForTheGps)
{
  double const limit35 = FromDisplay(35, Units::Mph), limit70 = FromDisplay(70, Units::Mph);
  auto const speedingLines = [&](double limit, int over)
  {
    Navigator nav(Config());
    nav.OnFix(At(0, 0), Far());
    int count = 0;
    double const speed = limit + FromDisplay(over, Units::Mph);
    for (int i = 1; i < 20; ++i)
    {
      nav.OnFix(At(i * 5000, speed, limit), Far());
      count += static_cast<int>(LinesOf(nav.TakeLines(), Category::Speeding).size());
    }
    return count;
  };
  CHECK_EQUAL(speedingLines(limit35, 2), 0);
  CHECK(speedingLines(limit35, 3) > 0);
  CHECK_EQUAL(speedingLines(limit70, 3), 0);
  CHECK(speedingLines(limit70, 4) > 0);
}

TEST(BarelySpeedingOnceInAWhile)
{
  double const limit = FromDisplay(35, Units::Mph);
  Navigator nav(Config());
  nav.OnFix(At(0, 0), Far());
  int barely = 0;
  // Over for a minute, under for a minute, three times in five minutes.
  for (int i = 1; i < 60; ++i)
  {
    bool const over = (i / 12) % 2 == 0;
    nav.OnFix(At(i * 5000, over ? limit + FromDisplay(3, Units::Mph) : limit, limit), Far());
    for (auto const & l : LinesOf(nav.TakeLines(), Category::Speeding))
      barely += l.key == "speeding.barely" ? 1 : 0;
  }
  CHECK_EQUAL(barely, 1);
}
