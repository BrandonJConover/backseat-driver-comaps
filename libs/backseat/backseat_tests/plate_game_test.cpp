#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/plate_game.hpp"

#include <algorithm>
#include <cctype>

using namespace backseat;

namespace
{
std::optional<std::string> Code(char const * s) { return std::string(s); }

/// A commute: home to work, driven three times before.
LatLon const kPlateHome{39.74, -104.99};
LatLon const kPlateWork{39.75, -104.94};

RouteSnapshot PlateRoute(double distToTurnM)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Left;
  r.distToTurnM = distToTurnM;
  r.destination = "Work";
  r.etaSeconds = 900;
  r.origin = kPlateHome;
  r.destinationPoint = kPlateWork;
  return r;
}

Fix PlateFix(double tMs)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.745;
  f.lon = -104.96;
  f.speedMps = 13;
  return f;
}

std::vector<TripRecord> Commutes(int n)
{
  std::vector<TripRecord> trips(n);
  for (auto & t : trips)
    t.routeKey = RouteKey(kPlateHome, kPlateWork);
  return trips;
}

std::vector<Commentary> PlateLines(std::vector<Commentary> const & lines)
{
  std::vector<Commentary> out;
  for (auto const & l : lines)
    if (l.category == Category::PlateGame)
      out.push_back(l);
  return out;
}
}  // namespace

TEST(PlateRegionsMatchWhatPeopleSay)
{
  CHECK(MatchPlateRegion("I see a Texas plate") == Code("TX"));
  CHECK(MatchPlateRegion("Arkansas!") == Code("AR"));
  CHECK(MatchPlateRegion("kansas") == Code("KS"));
  CHECK(MatchPlateRegion("west virginia") == Code("WV"));
  CHECK(MatchPlateRegion("Virginia") == Code("VA"));
  CHECK(MatchPlateRegion("New Mexico plate") == Code("NM"));
  CHECK(MatchPlateRegion("a Mexican plate") == Code("MX"));
  CHECK(MatchPlateRegion("Washington DC") == Code("DC"));
  CHECK(MatchPlateRegion("washington") == Code("WA"));
  CHECK(MatchPlateRegion("British Columbia, eh") == Code("BC"));
  CHECK(!MatchPlateRegion("ok, sounds good"));
  CHECK(!MatchPlateRegion("I'm in"));
  CHECK(PlateRegionCode("Colorado") == Code("CO"));
  CHECK_EQUAL(PlateRegionName("HI"), std::string("Hawaii"));
}

TEST(PlateNeighborsGoBothWays)
{
  CHECK(PlateNeighbors("CO", "NM"));
  CHECK(PlateNeighbors("NM", "CO"));
  CHECK(PlateNeighbors("WA", "BC"));
  CHECK(!PlateNeighbors("CO", "CA"));
  CHECK(!PlateNeighbors("HI", "CA"));
  CHECK(RarePlate("HI") && RarePlate("AK") && !RarePlate("TX"));
}

TEST(PlateGameHearsYesAndNo)
{
  CHECK(ParseYesNo("Yeah sure") == YesNo::Yes);
  CHECK(ParseYesNo("okay let's do it") == YesNo::Yes);
  CHECK(ParseYesNo("no thanks") == YesNo::No);
  CHECK(ParseYesNo("I don't want to") == YesNo::No);
  CHECK(ParseYesNo("what was that") == YesNo::Unclear);
}

TEST(PlateLogSurvivesASaveAndLoad)
{
  PlateLog log;
  log.lifetime = {"HI", "TX"};
  log.byRoute["a>b"] = {"TX"};
  log.grudges = 2;
  auto const back = PlateLog::Parse(log.Serialize());
  CHECK(back.lifetime == log.lifetime);
  CHECK(back.byRoute == log.byRoute);
  CHECK_EQUAL(back.grudges, 2);
  CHECK(PlateLog::Parse("garbage\nlifetime ZZ CO\n").lifetime == std::set<std::string>{"CO"});
}

TEST(PlateGameOnlyOnABoringCommute)
{
  CHECK(PlateGame::ShouldOffer(3, 4000, 10));
  CHECK(!PlateGame::ShouldOffer(2, 4000, 10));    // not a commute yet
  CHECK(!PlateGame::ShouldOffer(3, 1000, 10));    // a turn is coming up
  CHECK(!PlateGame::ShouldOffer(3, 4000, 50));    // already fed up
}

TEST(PlateGameJudgesEveryPlate)
{
  PlateGame game(SeededRng(1));
  game.NewTrip("home>work");
  game.SetHere(Code("CO"));
  CHECK(!game.OnDriverSaid("Texas", 0));  // nobody asked yet
  CHECK_EQUAL(game.Offer(0).key, std::string("plateGame.offer"));
  CHECK_EQUAL(game.OnDriverSaid("yeah okay", 0)->key, std::string("plateGame.accepted"));

  auto same = game.OnDriverSaid("I see a Colorado plate", 0);
  CHECK_EQUAL(same->key, std::string("plateGame.sameState"));
  CHECK(game.Log().lifetime.empty());

  CHECK_EQUAL(game.OnDriverSaid("New Mexico", 0)->key, std::string("plateGame.neighbor"));
  auto again = game.OnDriverSaid("another new mexico one", 0);
  CHECK_EQUAL(again->key, std::string("plateGame.repeatFirst"));
  CHECK_EQUAL(again->text, std::string("We already have that one champ. Sometimes I wish I wasn't in this car."));
  CHECK_EQUAL(game.OnDriverSaid("New Mexico", 0)->key, std::string("plateGame.repeat"));
  CHECK_EQUAL(game.OnDriverSaid("Ohio", 0)->key, std::string("plateGame.newFar"));
  auto rare = game.OnDriverSaid("no way, Hawaii!", 0);
  CHECK_EQUAL(rare->key, std::string("plateGame.rare"));
  CHECK(rare->text.find("Hawaii") != std::string::npos);

  auto fake = game.OnDriverSaid("a Narnia plate", 0);
  CHECK_EQUAL(fake->key, std::string("plateGame.madeUp"));
  CHECK(fake->grudge);
  CHECK_EQUAL(game.OnDriverSaid("hmm", 0)->key, std::string("plateGame.unheard"));

  auto wrap = game.WrapUp(0);
  CHECK_EQUAL(wrap->key, std::string("plateGame.wrapUp"));
  CHECK(wrap->text.find("Hawaii") != std::string::npos);
  CHECK(wrap->text.find("3") != std::string::npos);
  CHECK(game.Log().byRoute.at("home>work") == (std::set<std::string>{"HI", "NM", "OH"}));
  CHECK_EQUAL(game.Log().grudges, 1);

  // Next trip on the same commute: New Mexico is old news.
  game.NewTrip("home>work");
  game.Offer(0);
  CHECK_EQUAL(game.OnDriverSaid("New Mexico", 0)->key, std::string("plateGame.repeatFirst"));
}

TEST(PlateGameRemembersBeingTurnedDown)
{
  PlateGame game(SeededRng(2));
  game.NewTrip("home>work");
  game.Offer(1);
  auto no = game.OnDriverSaid("nah", 1);
  CHECK_EQUAL(no->key, std::string("plateGame.declined"));
  CHECK(no->grudge);
  CHECK(!game.OnDriverSaid("Texas", 1));
  CHECK(!game.WrapUp(1));
  CHECK_EQUAL(game.Log().grudges, 1);
}

TEST(NavigatorOffersThePlateGameOnACommute)
{
  EngineConfig c;
  c.rng = SeededRng(4);
  Navigator nav(c, Commutes(3));
  int saves = 0;
  nav.SetPlateLog({}, [&](PlateLog const &) { ++saves; });
  nav.SetRegionLookup([](LatLon const &) { return std::optional<std::string>("Colorado"); });

  nav.OnFix(PlateFix(0), PlateRoute(5000));
  CHECK(PlateLines(nav.TakeLines()).empty());  // not during the opening rundown
  nav.OnFix(PlateFix(70000), PlateRoute(4800));
  auto offer = PlateLines(nav.TakeLines());
  CHECK_EQUAL(offer.size(), size_t(1));
  nav.OnFix(PlateFix(80000), PlateRoute(4700));
  CHECK(PlateLines(nav.TakeLines()).empty());  // once a trip

  nav.OnDriverSaid(85000, "sure");
  nav.OnDriverSaid(90000, "Colorado plate");
  auto lines = PlateLines(nav.TakeLines());
  CHECK_EQUAL(lines.size(), size_t(2));
  CHECK(lines[1].text.find("Colorado") != std::string::npos);
  CHECK_EQUAL(saves, 2);

  // Near a maneuver the answer waits until the directions are out of the way.
  nav.OnFix(PlateFix(95000), PlateRoute(120));
  nav.TakeLines();
  nav.OnDriverSaid(96000, "Texas");
  CHECK(PlateLines(nav.TakeLines()).empty());
  RouteSnapshot next = PlateRoute(3000);
  next.turnId = 2;
  nav.OnFix(PlateFix(100000), next);
  auto released = PlateLines(nav.TakeLines());
  CHECK_EQUAL(released.size(), size_t(1));
  CHECK(released[0].text.find("Texas") != std::string::npos);
  CHECK(nav.GetPlateLog().lifetime == std::set<std::string>{"TX"});

  // Arrival tallies it up.
  RouteSnapshot done = next;
  done.state = RouteState::Finished;
  nav.OnFix(PlateFix(200000), done);
  auto wrap = PlateLines(nav.TakeLines());
  CHECK_EQUAL(wrap.size(), size_t(1));
  CHECK(wrap[0].text.find("Texas") != std::string::npos);
}

TEST(NavigatorSkipsThePlateGameOnNewRoutes)
{
  EngineConfig c;
  c.rng = SeededRng(5);
  Navigator nav(c, Commutes(1));
  nav.OnFix(PlateFix(0), PlateRoute(5000));
  nav.OnFix(PlateFix(90000), PlateRoute(4800));
  CHECK(PlateLines(nav.TakeLines()).empty());
  nav.OnDriverSaid(95000, "Texas");
  CHECK(PlateLines(nav.TakeLines()).empty());
}

TEST(PlateGameCallsParkingLotsCheating)
{
  PlateGame game(SeededRng(6));
  game.NewTrip("home>work");
  game.Offer(0);
  game.OnDriverSaid("yes", 0);
  auto r = game.OnDriverSaid("Texas, Ohio, Oregon", 0, true /* parked */);
  // Brandon's "Hey that's cheating. New rule, no parking lots." or one of its variants.
  std::string lower = r->text;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  CHECK(lower.find("cheat") != std::string::npos);
  CHECK(game.Log().lifetime.empty());
  CHECK_EQUAL(game.OnDriverSaid("Texas", 0)->key, std::string("plateGame.newFar"));
}

TEST(NavigatorKnowsWhenThePlatesComeFromAParkingLot)
{
  EngineConfig c;
  c.rng = SeededRng(7);
  Navigator nav(c, Commutes(3));
  bool inLot = false;
  nav.SetParkingLookup([&](LatLon const &) { return inLot; });
  nav.OnFix(PlateFix(0), PlateRoute(5000));
  nav.OnFix(PlateFix(70000), PlateRoute(4800));
  nav.OnDriverSaid(75000, "sure");
  nav.TakeLines();

  // Rolling through a parking lot.
  inLot = true;
  Fix slow = PlateFix(80000);
  slow.speedMps = 2;
  nav.OnFix(slow, PlateRoute(4700));
  nav.TakeLines();
  nav.OnDriverSaid(81000, "Texas");
  auto lines = PlateLines(nav.TakeLines());
  CHECK_EQUAL(lines.size(), size_t(1));
  CHECK(lines[0].text.find("cheating") != std::string::npos);
  CHECK(nav.GetPlateLog().lifetime.empty());

  // Back on the road it counts.
  inLot = false;
  nav.OnFix(PlateFix(90000), PlateRoute(4500));
  nav.TakeLines();
  nav.OnDriverSaid(91000, "Texas");
  CHECK(nav.GetPlateLog().lifetime == std::set<std::string>{"TX"});
}
