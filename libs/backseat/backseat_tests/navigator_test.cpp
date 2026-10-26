#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/lines.hpp"
#include "backseat/units.hpp"

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
  CHECK(lines[1].text.find("turn left") != std::string::npos);
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
