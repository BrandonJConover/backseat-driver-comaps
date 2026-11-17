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

Fix PlateFix(double tMs, double speedMps = 13)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.745;
  f.lon = -104.96;
  f.speedMps = speedMps;
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

TEST(PlateGameHearsAHedgedNoAsANo)
{
  // "Definitely not" was a yes because of "definitely" (loop B123).
  PlateGame game(SeededRng(2));
  game.NewTrip("home>work");
  game.Offer(1);
  CHECK(!game.OnDriverSaid("yes and no", 1));
  auto no = game.OnDriverSaid("definitely not", 1);
  CHECK(no);
  CHECK_EQUAL(no->key, std::string("plateGame.declined"));
  CHECK_EQUAL(game.Log().grudges, 1);

  PlateGame keen(SeededRng(2));
  keen.NewTrip("home>work");
  keen.Offer(1);
  CHECK_EQUAL(keen.OnDriverSaid("sure, why not", 1)->key, std::string("plateGame.accepted"));
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

TEST(PlateGameHearsCurlyApostrophes)
{
  // Dictation types "don’t": it must still be a no, not a shrug.
  CHECK(ParseYesNo("I don\xE2\x80\x99t want to") == YesNo::No);
  CHECK(ParseYesNo("I don\xE2\x80\x99t know") == YesNo::Unclear);
  CHECK(ParseYesNo("okay let\xE2\x80\x99s do it") == YesNo::Yes);
  CHECK(ParseYesNo("I\xE2\x80\x99m in") == YesNo::Yes);
}

// 500 ft is five seconds at 70 mph, inside the "now" call; the hold is 12 s at the current speed instead.
TEST(PlateHoldScalesWithSpeed)
{
  for (bool highway : {true, false})
  {
    EngineConfig c;
    c.rng = SeededRng(4);
    Navigator nav(c, Commutes(3));
    nav.SetRegionLookup([](LatLon const &) { return std::optional<std::string>("Colorado"); });
    nav.OnFix(PlateFix(0), PlateRoute(5000));
    nav.OnFix(PlateFix(70000), PlateRoute(4800));
    CHECK_EQUAL(PlateLines(nav.TakeLines()).size(), size_t(1));
    nav.OnDriverSaid(85000, "sure");
    nav.TakeLines();
    double const speed = highway ? 31 : 10;
    nav.OnFix(PlateFix(95000, speed), PlateRoute(highway ? 300 : 200));
    nav.TakeLines();
    nav.OnDriverSaid(96000, "Texas");
    nav.OnFix(PlateFix(97000, speed), PlateRoute(highway ? 269 : 190));
    auto near = PlateLines(nav.TakeLines());
    if (highway)
    {
      CHECK(near.empty());
      RouteSnapshot next = PlateRoute(3000);
      next.turnId = 2;
      nav.OnFix(PlateFix(100000, speed), next);
      auto released = PlateLines(nav.TakeLines());
      CHECK_EQUAL(released.size(), size_t(1));
      CHECK(released[0].text.find("Texas") != std::string::npos);
    }
    else
    {
      CHECK_EQUAL(near.size(), size_t(1));
      CHECK(near[0].text.find("Texas") != std::string::npos);
    }
  }
}

namespace
{
/// A game on, the driver already playing, at t = 0.
PlateGame PlayingGame()
{
  PlateGame game(SeededRng(3));
  game.NewTrip("home>work");
  game.SetHere(Code("CO"));
  game.Offer(0);
  game.OnDriverSaid("yes", 0, false, 0.0);
  return game;
}
}  // namespace

TEST(PlateGameCanBeQuit)
{
  for (char const * cue : {"stop playing", "I'm done", "no more plates", "enough plates", "game over",
                           "stop the game", "ok I quit", "Im done with this"})
  {
    PlateGame game = PlayingGame();
    CHECK_EQUAL(game.OnDriverSaid("Texas", 0, false, 1000.0)->key, std::string("plateGame.newFar"));
    auto r = game.OnDriverSaid(cue, 0, false, 2000.0);
    CHECK(r.has_value());
    CHECK_EQUAL(r->key, std::string("plateGame.wrapUp"));
    CHECK(r->text.find("Texas") != std::string::npos);
    CHECK(game.GetState() == PlateGame::State::Over);
    // Nothing more about plates this trip, and no second tally at arrival.
    CHECK(!game.OnDriverSaid("Ohio", 0, false, 3000.0));
    CHECK(!game.OnDriverSaid("which state", 0, false, 3500.0));
    CHECK(!game.WrapUp(0));
    CHECK(game.Log().lifetime == std::set<std::string>{"TX"});
    // A new trip starts fresh.
    game.NewTrip("home>work");
    CHECK(game.GetState() == PlateGame::State::Idle);
  }
  // Quitting before a single plate: the empty tally.
  PlateGame none = PlayingGame();
  CHECK_EQUAL(none.OnDriverSaid("game over", 0, false, 1000.0)->key, std::string("plateGame.wrapUpNone"));
  // "Done" alone, mid-sentence about something else, isn't quitting.
  CHECK(!QuitsPlateGame("are we done with the highway yet"));
  CHECK(!QuitsPlateGame("Texas"));
}

TEST(PlateGameIgnoresLaterChatter)
{
  PlateGame game = PlayingGame();
  CHECK_EQUAL(game.OnDriverSaid("Texas", 0, false, 10000.0)->key, std::string("plateGame.newFar"));
  // Right after an exchange, a mumble is probably a plate we missed.
  CHECK_EQUAL(game.OnDriverSaid("hmm what", 0, false, 25000.0)->key, std::string("plateGame.unheard"));
  // Five minutes later, chatter isn't for the game.
  CHECK(!game.OnDriverSaid("ugh this traffic", 0, false, 25000.0 + 5 * 60000.0));
  CHECK(!game.OnDriverSaid("what time is it", 0, false, 25000.0 + 6 * 60000.0));
  // Unless it sounds like a sighting.
  CHECK_EQUAL(game.OnDriverSaid("I saw one", 0, false, 25000.0 + 7 * 60000.0)->key, std::string("plateGame.unheard"));
  CHECK_EQUAL(game.OnDriverSaid("there's one", 0, false, 25000.0 + 9 * 60000.0)->key,
              std::string("plateGame.unheard"));
  // Plates themselves still count any time.
  CHECK_EQUAL(game.OnDriverSaid("Ohio", 0, false, 25000.0 + 20 * 60000.0)->key, std::string("plateGame.newFar"));
  CHECK_EQUAL(game.OnDriverSaid("a Narnia plate", 0, false, 25000.0 + 40 * 60000.0)->key,
              std::string("plateGame.madeUp"));
  // Without a time (old callers) every mumble still gets "which state?".
  CHECK_EQUAL(game.OnDriverSaid("hmm", 0)->key, std::string("plateGame.unheard"));
}

TEST(PlateGameDoesntCountNegatedSightings)
{
  for (char const * said : {"no Texas plates yet", "haven't seen Ohio", "not a Georgia plate",
                            "I didn't see a single Utah", "still no Nevada", "never seen an Alaska plate",
                            "no Hawaii so far"})
  {
    CHECK(NegatedSighting(said));
    PlateGame game = PlayingGame();
    CHECK(!game.OnDriverSaid(said, 0, false, 30 * 60000.0));
    CHECK(game.Log().lifetime.empty());
  }
  for (char const * said : {"Texas!", "I see a Texas plate", "no way, Hawaii!", "No! Texas!", "nope, Ohio",
                            "not bad, an Oregon plate", "there's a Utah one"})
  {
    CHECK(!NegatedSighting(said));
    PlateGame game = PlayingGame();
    auto r = game.OnDriverSaid(said, 0, false, 1000.0);
    CHECK(r.has_value());
    CHECK_EQUAL(game.Log().lifetime.size(), size_t(1));
  }
  // Negated in the offer: neither a yes nor a plate.
  PlateGame offered(SeededRng(3));
  offered.NewTrip("home>work");
  offered.Offer(0);
  CHECK(!offered.OnDriverSaid("I haven't seen Texas yet", 0, false, 1000.0));
  CHECK(offered.GetState() == PlateGame::State::Offered);
}

TEST(NavigatorPlateGameQuitFreesThanks)
{
  EngineConfig c;
  c.rng = SeededRng(5);
  Navigator nav(c, Commutes(3));
  nav.SetRegionLookup([](LatLon const &) { return std::optional<std::string>("Colorado"); });
  nav.OnFix(PlateFix(0), PlateRoute(5000));
  nav.OnFix(PlateFix(70000), PlateRoute(4800));
  CHECK_EQUAL(PlateLines(nav.TakeLines()).size(), size_t(1));
  nav.OnDriverSaid(85000, "sure");
  nav.OnDriverSaid(90000, "Texas");
  nav.TakeLines();
  nav.OnDriverSaid(95000, "I'm done");
  auto quit = PlateLines(nav.TakeLines());
  CHECK_EQUAL(quit.size(), size_t(1));
  CHECK(quit[0].text.find("Texas") != std::string::npos);
  // Five minutes on, chatter gets nothing, and "thanks" is thanks again.
  nav.OnFix(PlateFix(400000), PlateRoute(4000));
  nav.TakeLines();
  nav.OnDriverSaid(400500, "Ohio");
  CHECK(PlateLines(nav.TakeLines()).empty());
  CHECK(nav.GetPlateLog().lifetime == std::set<std::string>{"TX"});
  nav.OnDriverSaid(401000, "thanks");
  bool thanked = false;
  for (auto const & l : nav.TakeLines())
    thanked = thanked || l.key == "thanked";
  CHECK(thanked);
  // Arrival doesn't tally a second time.
  RouteSnapshot done = PlateRoute(4000);
  done.state = RouteState::Finished;
  nav.OnFix(PlateFix(500000), done);
  CHECK(PlateLines(nav.TakeLines()).empty());
}

TEST(PlateGameHearsNicknamesAndShortNames)
{
  CHECK(MatchPlateRegion("a Mass plate") == Code("MA"));
  CHECK(MatchPlateRegion("Massachusetts") == Code("MA"));
  CHECK(MatchPlateRegion("one from the Lone Star State") == Code("TX"));
  CHECK(MatchPlateRegion("Sunshine State!") == Code("FL"));
  CHECK(MatchPlateRegion("the Garden State") == Code("NJ"));
  CHECK(MatchPlateRegion("Green Mountain State") == Code("VT"));
  CHECK(MatchPlateRegion("land of ten thousand lakes") == Code("MN"));
  CHECK(MatchPlateRegion("show me state") == Code("MO"));
  // A nickname that is also a word stays out of chatter that isn't one.
  CHECK(!MatchPlateRegion("this is a mountain state"));
  CHECK(!MatchPlateRegion("massive truck"));
  CHECK(!MatchPlateRegion("what a state this road is in"));
  PlateGame game = PlayingGame();
  CHECK_EQUAL(game.OnDriverSaid("Lone Star State plate", 0, false, 1000.0)->key, std::string("plateGame.newFar"));
  CHECK(game.Log().lifetime == std::set<std::string>{"TX"});
  CHECK(NegatedSighting("no Lone Star State plates yet"));
}

TEST(PlateGameCountsEveryPlateInOneSentence)
{
  CHECK(MatchPlateRegions("a Florida and a Georgia") == (std::vector<std::string>{"FL", "GA"}));
  CHECK(MatchPlateRegions("Florida Georgia") == (std::vector<std::string>{"FL", "GA"}));
  CHECK(MatchPlateRegions("West Virginia and Virginia") == (std::vector<std::string>{"WV", "VA"}));
  CHECK(MatchPlateRegions("Texas, Texas, and Ohio") == (std::vector<std::string>{"TX", "OH"}));
  CHECK(MatchPlateRegions("a Florida but no Georgia") == (std::vector<std::string>{"FL"}));
  CHECK(MatchPlateRegions("no Texas or Ohio plates yet").empty());
  CHECK(MatchPlateRegions("haven't seen Ohio but there's a Texas") == (std::vector<std::string>{"TX"}));
  CHECK(MatchPlateRegions("no way, Hawaii!") == (std::vector<std::string>{"HI"}));

  PlateGame game = PlayingGame();
  auto r = game.OnDriverSaid("a Florida and a Georgia", 0, false, 1000.0);
  CHECK(r.has_value());
  CHECK(game.Log().lifetime == (std::set<std::string>{"FL", "GA"}));
  // The answer is about one of them and counts both.
  CHECK(r->text.find("Florida") != std::string::npos || r->text.find("Georgia") != std::string::npos);

  // The most exciting one gets the answer; the repeat is logged quietly, so the first repeat
  // later still gets Brandon's line.
  auto rare = game.OnDriverSaid("Florida and Hawaii", 0, false, 2000.0);
  CHECK_EQUAL(rare->key, std::string("plateGame.rare"));
  CHECK(rare->text.find("Hawaii") != std::string::npos);
  CHECK_EQUAL(game.OnDriverSaid("Georgia", 0, false, 3000.0)->key, std::string("plateGame.repeatFirst"));

  // Negated in the offer: neither a yes nor a plate; one real plate in the list is a yes.
  PlateGame offered(SeededRng(3));
  offered.NewTrip("home>work");
  offered.Offer(0);
  CHECK(!offered.OnDriverSaid("no Texas or Ohio plates yet", 0, false, 1000.0));
  CHECK(offered.GetState() == PlateGame::State::Offered);
  CHECK(offered.OnDriverSaid("not Texas, but an Ohio", 0, false, 2000.0).has_value());
  CHECK(offered.GetState() == PlateGame::State::Playing);
  CHECK(offered.Log().lifetime == std::set<std::string>{"OH"});
}
