#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/lines.hpp"
#include "backseat/units.hpp"

#include <algorithm>
#include <cmath>

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
  // Greeting, rundown, time prediction, then the first instruction.
  CHECK_EQUAL(lines.size(), size_t{4});
  CHECK(lines[0].text.find("didn't ask to be in this car") != std::string::npos);
  // First trip ever: the turns rattled off at speed, then Brandon's music line.
  CHECK(lines[1].text.find("Left, right.") != std::string::npos);
  CHECK(lines[1].text.find("put on some music") != std::string::npos);
  CHECK(lines[1].voiceSpeed > 1.2);
  CHECK(lines[3].text.find("turn left onto Elm Street") != std::string::npos);
}

TEST(NavigatorTurnsStepChangesIntoCompletedTurns)
{
  Navigator nav(Config());
  nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Left, 400));
  nav.TakeLines();
  nav.OnFix(At(1000, 10), OnRoute(1, TurnKind::Left, 140));
  CHECK(Has(nav.TakeLines(), Category::Instruction));  // the 500 ft nag
  nav.OnFix(At(30000, 10), OnRoute(2, TurnKind::Right, 300, "Oak Avenue"));
  auto const lines = nav.TakeLines();
  CHECK(Has(lines, Category::TurnCompleted));
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
  CHECK(Has(lines, Category::Rerouted));
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
  nav.OnFix(At(1000, FromDisplay(51, Units::Mph), FromDisplay(50, Units::Mph)), OnRoute(1, TurnKind::Left, 880));
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
    for (auto const & line : LinesFor(key, tier))
    {
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
  auto said = LinesOf(nav.TakeLines(), Category::Autopilot);
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK_EQUAL(said[0].text, lemon);
  nav.OnFix(At(20000, 10), Far());
  nav.OnFix(At(30000, 10), off);
  CHECK(LinesOf(nav.TakeLines(), Category::Autopilot).empty());  // once a trip

  // Claimed after getting lost: right away.
  Navigator late(Config());
  late.OnFix(At(0, 10), Far());
  late.OnFix(At(10000, 10), off);
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
  nav.OnFix(At(0, 10), OnRoute(1, TurnKind::Roundabout, 300));
  auto said = LinesOf(nav.TakeLines(), Category::Music);
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK_EQUAL(said[0].text, std::string("Oh look, a roundabout! Do you know what else in this car is round?"));
  // ...awkward pause...
  nav.OnFix(At(4000, 10), OnRoute(1, TurnKind::Roundabout, 260));
  CHECK(LinesOf(nav.TakeLines(), Category::Music).empty());
  nav.OnFix(At(8000, 10), OnRoute(1, TurnKind::Roundabout, 220));
  said = LinesOf(nav.TakeLines(), Category::Music);
  CHECK_EQUAL(said.size(), size_t(1));
  CHECK_EQUAL(said[0].text, std::string("The volume knob."));
  nav.OnFix(At(30000, 10), OnRoute(2, TurnKind::Roundabout, 300));
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
