#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"
#include "backseat/twenty_questions.hpp"

#include <algorithm>

using namespace backseat;

namespace
{
using Kind = TwentyHeard::Kind;

/// A game where the GPS is thinking of `name`: the rng is steered until it picks it.
TwentyQuestions TqGameWith(char const * name, std::set<std::string> const & nearby = {}, bool night = false)
{
  for (uint32_t seed = 1; seed < 5000; ++seed)
  {
    TwentyQuestions g(SeededRng(seed));
    g.Start(nearby, night, 0, 0);
    if (std::string(g.Thing()->name) == name)
      return g;
  }
  throw std::runtime_error(std::string("never picked ") + name);
}

bool TqNoPlaceholders(std::string const & s) { return s.find('{') == std::string::npos; }

// 2026-10-28 19:00 UTC: 1 pm in Denver.
double constexpr kTqDayMs = 1793156400000.0 + 16 * 3600000.0;

RouteSnapshot TqRoute(uint32_t turnId, double dist)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = turnId;
  r.turn = TurnKind::Right;
  r.nextStreet = "Elm Street";
  r.distToTurnM = dist;
  r.destination = "Grandma's";
  r.etaSeconds = 3600;
  r.plannedTurns = {TurnKind::Right, TurnKind::Destination};
  return r;
}

Fix TqFix(double tMs)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.74;
  f.lon = -104.99;
  f.speedMps = 25;
  f.bearingDeg = 90;
  return f;
}

Navigator TqNavigator(std::vector<std::string> saidBefore = {})
{
  EngineConfig c;
  c.rng = SeededRng(11);
  Navigator nav(c);
  nav.SetDriverName("Brandon");
  nav.SetDriverPronouns(Pronouns::He);
  std::string said;
  for (auto const & k : saidBefore)
    said += k + "\tsomething\n";
  nav.SetSaidLog(SaidLog::Parse(said), {});
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

/// One fix a second toward maneuver `turnId`, `dist` metres away and closing at 25 m/s.
std::vector<Commentary> TqDrive(Navigator & nav, double & t, int seconds, uint32_t turnId, double & dist)
{
  std::vector<Commentary> said;
  for (int i = 0; i < seconds; ++i, t += 1000, dist -= 25)
  {
    nav.OnFix(TqFix(t), TqRoute(turnId, dist));
    auto lines = nav.TakeLines();
    said.insert(said.end(), lines.begin(), lines.end());
  }
  return said;
}

std::vector<Commentary> TqGame(std::vector<Commentary> const & lines)
{
  std::vector<Commentary> out;
  for (auto const & l : lines)
    if (l.category == Category::TwentyQuestions)
      out.push_back(l);
  return out;
}
}  // namespace

TEST(TwentyParsesQuestions)
{
  struct Case
  {
    char const * said;
    Trait trait;
    bool flipped;
  };
  for (auto const & c : std::vector<Case>{
           {"Is it alive?", Trait::Alive, false},
           {"is it an animal", Trait::Animal, false},
           {"Is it man-made?", Trait::ManMade, false},
           {"Is it bigger than a breadbox?", Trait::Small, true},
           {"Is it bigger than a car?", Trait::BiggerThanCar, false},
           {"Is it smaller than a car?", Trait::BiggerThanCar, true},
           {"Could I hold it in my hand?", Trait::Small, false},
           {"Can you eat it?", Trait::Food, false},
           {"Is it in the car?", Trait::InCar, false},
           {"Does it have wheels?", Trait::Wheels, false},
           {"Is it on the side of the road?", Trait::Roadside, false},
           {"Is it on the road?", Trait::OnRoad, false},
           {"Is it red?", Trait::Red, false},
           {"Is it grey?", Trait::Gray, false},
           {"Does it make noise?", Trait::Noise, false},
           {"Does it light up?", Trait::Lights, false},
           {"Can it fly?", Trait::Flies, false},
           {"Is it in the sky?", Trait::Sky, false},
           {"Is it a building?", Trait::Building, false},
           {"Can I buy something there?", Trait::Business, false},
           {"Is it a sign?", Trait::Sign, false},
           {"Is it safe?", Trait::Dangerous, true},
           {"Is it made of metal?", Trait::Metal, false},
           {"Is it a person?", Trait::Person, false},
           {"Is it a vehicle?", Trait::Vehicle, false},
           {"Is it tall?", Trait::Tall, false},
           {"Is it outside?", Trait::InCar, true},
       })
  {
    auto const h = ParseTwentyHeard(c.said);
    CHECK_EQUAL(static_cast<int>(h.kind), static_cast<int>(Kind::Question));
    CHECK_EQUAL(static_cast<int>(h.trait), static_cast<int>(c.trait));
    CHECK_EQUAL(h.flipped, c.flipped);
  }
}

TEST(TwentyParsesGuesses)
{
  struct Case
  {
    char const * said;
    char const * thing;
  };
  for (auto const & c : std::vector<Case>{
           {"Is it a deer?", "a deer"},
           {"It's the moon.", "the moon"},
           {"Is it a stop sign", "a stop sign"},
           {"is it a traffic light", "a traffic light"},
           {"I guess the water tower", "a water tower"},
           {"Is it you?", "me"},
           {"Is it me?", "you"},
           {"Is it my blinker?", "your turn signal"},
           {"Is it your turn signal?", "your turn signal"},
           {"Is it a big rig?", "a semi truck"},
           {"Is it a tree?", "a tree"},
           {"How about a cow?", "a cow"},
           {"Are you thinking of the steering wheel?", "the steering wheel"},
       })
  {
    auto const h = ParseTwentyHeard(c.said);
    CHECK_EQUAL(static_cast<int>(h.kind), static_cast<int>(Kind::Guess));
    CHECK_EQUAL(std::string(TwentyThings()[h.thing].name), std::string(c.thing));
  }
  auto const giraffe = ParseTwentyHeard("Is it a giraffe?");
  CHECK_EQUAL(static_cast<int>(giraffe.kind), static_cast<int>(Kind::UnknownGuess));
  CHECK_EQUAL(giraffe.guess, std::string("a giraffe"));
}

TEST(TwentyParsesTheRest)
{
  CHECK_EQUAL(static_cast<int>(ParseTwentyHeard("I give up").kind), static_cast<int>(Kind::GiveUp));
  CHECK_EQUAL(static_cast<int>(ParseTwentyHeard("Just tell me what it is").kind), static_cast<int>(Kind::GiveUp));
  CHECK_EQUAL(static_cast<int>(ParseTwentyHeard("Okay I'm done playing").kind), static_cast<int>(Kind::Quit));
  CHECK_EQUAL(static_cast<int>(ParseTwentyHeard("Can I get a hint?").kind), static_cast<int>(Kind::Hint));
  CHECK_EQUAL(static_cast<int>(ParseTwentyHeard("What color is it").kind), static_cast<int>(Kind::Unclear));
  CHECK(AsksForTwentyQuestions("Let's play 20 questions"));
  CHECK(AsksForTwentyQuestions("can we play twenty questions"));
  CHECK(!AsksForTwentyQuestions("I have a question"));
}

TEST(TwentyEveryThingIsGuessableByName)
{
  // Saying "is it <its name>" must find that very thing (or one with the same name).
  for (auto const & t : TwentyThings())
  {
    std::string const said = std::string(t.name) == "me" || std::string(t.name) == "you" ? t.aliases[0] : t.name;
    auto const h = ParseTwentyHeard("Is it " + said + "?");
    CHECK_EQUAL(static_cast<int>(h.kind), static_cast<int>(Kind::Guess));
    CHECK_EQUAL(std::string(TwentyThings()[h.thing].name), std::string(t.name));
  }
}

TEST(TwentyNoAliasLooksLikeAQuestion)
{
  // "Is it an animal", "is it a building": category words must stay questions, never guesses.
  for (char const * q : {"Is it an animal?", "Is it a building?", "Is it a plant?", "Is it a vehicle?",
                         "Is it a sign?", "Is it a person?", "Is it food?", "Is it on the road?", "Is it in the car?",
                         "Is it a light?", "Is it water?", "Is it in the sky?"})
    CHECK_EQUAL(static_cast<int>(ParseTwentyHeard(q).kind), static_cast<int>(Kind::Question));
}

TEST(TwentyAnswersTruthfully)
{
  auto deer = TqGameWith("a deer", {"forest"});
  CHECK(deer.OnDriverSaid("Is it alive?", 1000, 0)->key == "twenty.yes");
  CHECK(deer.OnDriverSaid("Is it man made?", 2000, 0)->key == "twenty.no");
  CHECK(deer.OnDriverSaid("Is it bigger than a breadbox?", 3000, 0)->key == "twenty.yes");
  CHECK(deer.OnDriverSaid("Is it in the car?", 4000, 0)->key == "twenty.no");
  CHECK(deer.OnDriverSaid("Is it on the road?", 5000, 0)->key == "twenty.sometimes");
  CHECK_EQUAL(deer.QuestionsLeft(), 15);

  auto blinker = TqGameWith("your turn signal");
  CHECK(blinker.OnDriverSaid("Is it in the car?", 1000, 0)->key == "twenty.yes");
  CHECK(blinker.OnDriverSaid("Is it man made?", 2000, 0)->key == "twenty.yes");
  CHECK(blinker.OnDriverSaid("Is it smaller than a car?", 3000, 0)->key == "twenty.yes");
  CHECK(blinker.OnDriverSaid("Is it alive?", 4000, 0)->key == "twenty.no");
}

TEST(TwentyMocksGuessesAndCountsRepeats)
{
  auto g = TqGameWith("a cow", {"farm"});
  CHECK(g.OnDriverSaid("is it a horse", 1000, 0)->key == "twenty.wrongClose");
  CHECK(g.OnDriverSaid("is it the radio", 2000, 0)->key == "twenty.wrongFar");
  auto const giraffe = g.OnDriverSaid("is it a giraffe", 3000, 0);
  CHECK(giraffe->key == "twenty.wrongUnknown");
  CHECK(giraffe->text.find("giraffe") != std::string::npos);
  CHECK(g.OnDriverSaid("is it alive", 4000, 0)->key == "twenty.yes");
  CHECK(g.OnDriverSaid("is it a living thing", 5000, 0)->key == "twenty.again");
  // "What?" doesn't use up a question.
  CHECK(g.OnDriverSaid("hmm what was the last thing you said", 6000, 0)->key == "twenty.unclear");
  CHECK_EQUAL(g.QuestionsLeft(), 15);
  auto const win = g.OnDriverSaid("Is it a cow?", 7000, 0);
  CHECK(win->over);
  CHECK(win->text.find(" cow") != std::string::npos);
  CHECK_EQUAL(g.Log().driverWins, 1);
  CHECK(!g.Active());
}

TEST(TwentyGloatsAfterTwenty)
{
  auto g = TqGameWith("the moon", {}, true);
  std::optional<TwentyQuestions::Reply> last;
  for (int i = 0; i < 20; ++i)
  {
    CHECK(g.Active());
    last = g.OnDriverSaid(i % 2 ? "is it a deer" : "is it blue", 1000.0 * i, 0);
  }
  CHECK(last->over);
  CHECK(last->key == "twenty.lose");
  CHECK(last->text.find("he moon") != std::string::npos);
  CHECK_EQUAL(g.Log().gpsWins, 1);
  CHECK(!g.Active());
}

TEST(TwentyCountsDownAndHints)
{
  auto g = TqGameWith("a stop sign");
  for (int i = 0; i < 9; ++i)
    g.OnDriverSaid(i % 2 ? "is it a deer" : "is it a cat", 1000.0 * i, 0);
  // The tenth question tells you half are gone.
  auto const tenth = g.OnDriverSaid("is it red", 10000, 0);
  CHECK_EQUAL(g.QuestionsLeft(), 10);
  CHECK(tenth->text.find("10") != std::string::npos);
  auto const hint = g.OnDriverSaid("give me a hint", 11000, 0);
  CHECK(hint->key == "twenty.hint");
  CHECK(TqNoPlaceholders(hint->text));
  CHECK_EQUAL(g.QuestionsLeft(), 9);
}

TEST(TwentyOfferYesNoAndSilence)
{
  TwentyQuestions yes(SeededRng(3));
  yes.Offer({}, false, 0, 0);
  CHECK(yes.Active());
  CHECK(!yes.OnDriverSaid("hmm", 1000, 0));
  CHECK(yes.OnDriverSaid("sure", 2000, 0)->key == "twenty.accepted");
  CHECK(yes.GetState() == TwentyQuestions::State::Playing);

  TwentyQuestions no(SeededRng(3));
  no.Offer({}, false, 0, 0);
  auto const declined = no.OnDriverSaid("no thanks", 1000, 0);
  CHECK(declined->grudge && declined->over);
  CHECK(!no.Active());

  // A question straight away is a yes.
  TwentyQuestions eager(SeededRng(3));
  eager.Offer({}, false, 0, 0);
  CHECK(eager.OnDriverSaid("is it alive", 1000, 0));
  CHECK_EQUAL(eager.QuestionsLeft(), 19);

  // Ignoring the offer is a no.
  TwentyQuestions ignored(SeededRng(3));
  ignored.Offer({}, false, 0, 0);
  CHECK(!ignored.Tick(30000, 0));
  CHECK(ignored.Tick(TwentyQuestions::kNudgeAfterMs, 0)->key == "twenty.ignored");
  CHECK(!ignored.Active());
}

TEST(TwentyNudgesThenCallsIt)
{
  TwentyQuestions g(SeededRng(5));
  g.Start({}, false, 0, 0);
  CHECK(!g.Tick(30000, 0));
  CHECK(g.Tick(TwentyQuestions::kNudgeAfterMs, 0)->key == "twenty.nudge");
  CHECK(!g.Tick(TwentyQuestions::kNudgeAfterMs + 1000, 0));
  auto const end = g.Tick(TwentyQuestions::kGiveUpAfterMs, 0);
  CHECK(end->key == "twenty.timeout" && end->over);
  CHECK(!g.Active());
}

TEST(TwentyPicksFromTheRouteAndNeverRepeatsSoon)
{
  // With a water tower on the route, it's picked most of the time.
  int towers = 0;
  for (uint32_t seed = 1; seed <= 100; ++seed)
  {
    TwentyQuestions g(SeededRng(seed));
    g.Start({"water_tower"}, false, 0, 0);
    if (std::string(g.Thing()->name) == "a water tower")
      ++towers;
    // Never something tagged that isn't near the route.
    CHECK(*g.Thing()->tag == '\0' || std::string(g.Thing()->tag) == "water_tower");
  }
  CHECK(towers > 50);

  TwentyQuestions g(SeededRng(11));
  std::vector<std::string> picked;
  for (int i = 0; i < 25; ++i)
  {
    g.NewTrip();
    g.Start({}, false, 0, 0);
    CHECK(std::find(picked.begin(), picked.end(), g.Thing()->name) == picked.end());
    picked.push_back(g.Thing()->name);
  }
}

TEST(TwentyKeepsTheSunOutOfTheNight)
{
  for (uint32_t seed = 1; seed <= 200; ++seed)
  {
    TwentyQuestions night(SeededRng(seed));
    night.Start({}, true, 0, 0);
    CHECK(!night.Thing()->dayOnly);
    TwentyQuestions day(SeededRng(seed));
    day.Start({}, false, 0, 0);
    CHECK(!day.Thing()->nightOnly);
  }
}

TEST(TwentyLogRoundTrips)
{
  TwentyLog log;
  log.games = 4;
  log.gpsWins = 3;
  log.driverWins = 1;
  log.grudges = 2;
  log.recent = {"a deer", "the moon", "your turn signal"};
  auto const back = TwentyLog::Parse(log.Serialize());
  CHECK_EQUAL(back.games, 4);
  CHECK_EQUAL(back.gpsWins, 3);
  CHECK_EQUAL(back.driverWins, 1);
  CHECK_EQUAL(back.grudges, 2);
  CHECK(back.recent == log.recent);
}

TEST(TwentyEveryLineFills)
{
  // Every pool exists at every tier, and every placeholder it uses gets a value.
  for (auto const * key : {"twenty.offer", "twenty.start", "twenty.accepted", "twenty.declined", "twenty.ignored",
                           "twenty.yes", "twenty.no", "twenty.sometimes", "twenty.again", "twenty.unclear",
                           "twenty.wrongClose", "twenty.wrongFar", "twenty.wrongUnknown", "twenty.hint",
                           "twenty.noHint", "twenty.left", "twenty.lastOne", "twenty.win", "twenty.winFast",
                           "twenty.lose", "twenty.giveUp", "twenty.quit", "twenty.nudge", "twenty.timeout",
                           "twenty.arrived", "twenty.resume", "twenty.nearby"})
    for (int tier = 0; tier < 3; ++tier)
    {
      std::string const k = key;
      auto const & lines = LinesFor(k, tier);
      for (auto const & line : lines)
      {
        Vars const vars = {{"thing", "a deer"}, {"where", "outside"}, {"left", "5"}, {"used", "15"},
                           {"wins", "3"},       {"losses", "1"},      {"guess", "a cow"}, {"answer", "yes"},
                           {"fact", "is alive"}};
        CHECK(TqNoPlaceholders(Fill(line, vars)));
      }
    }
}

TEST(TwentyDriverAsksByName)
{
  Navigator nav = TqNavigator();
  double t = kTqDayMs;
  double dist = 60000;
  TqDrive(nav, t, 30, 1, dist);
  nav.OnDriverSaid(t, "Let's play 20 questions");
  auto lines = TqGame(TqDrive(nav, t, 5, 1, dist));
  CHECK_EQUAL(lines.size(), size_t{1});
  // "Is it a police car" is a guess, not the police.
  nav.OnDriverSaid(t, "Is it a police car?");
  auto const all = TqDrive(nav, t, 5, 1, dist);
  CHECK_EQUAL(TqGame(all).size(), size_t{1});
  for (auto const & l : all)
    CHECK(l.category != Category::PullOver);
  CHECK_EQUAL(nav.GetTwentyLog().games, 1);
}

TEST(TwentyWaitsForTheTurnAndPicksBackUp)
{
  Navigator nav = TqNavigator();
  double t = kTqDayMs;
  double dist = 60000;
  TqDrive(nav, t, 30, 1, dist);
  nav.OnDriverSaid(t, "twenty questions");
  TqDrive(nav, t, 5, 1, dist);
  // Close to the turn: the answer waits until we're past it.
  dist = 150;
  TqDrive(nav, t, 1, 1, dist);
  nav.OnDriverSaid(t, "Is it alive?");
  auto near = TqGame(TqDrive(nav, t, 3, 1, dist));
  CHECK(near.empty());
  dist = 30000;
  auto past = TqDrive(nav, t, 3, 2, dist);
  CHECK_EQUAL(TqGame(past).size(), size_t{1});
  // A direction cuts in mid-game; then "anyway, where were we".
  dist = 3000;
  auto after = TqDrive(nav, t, 60, 3, dist);
  size_t direction = after.size(), resume = after.size();
  for (size_t i = 0; i < after.size(); ++i)
  {
    if (after[i].priority == Priority::Nav)
      direction = std::min(direction, i);
    if (after[i].category == Category::TwentyQuestions)
      resume = std::min(resume, i);
  }
  CHECK(direction < resume);
  CHECK(resume < after.size());
  CHECK(after[resume].text.find("19") != std::string::npos);
}

TEST(TwentyRevealsAtArrival)
{
  Navigator nav = TqNavigator();
  double t = kTqDayMs;
  double dist = 60000;
  TqDrive(nav, t, 30, 1, dist);
  nav.OnDriverSaid(t, "let's play twenty questions");
  TqDrive(nav, t, 5, 1, dist);
  auto r = TqRoute(1, 0);
  r.state = RouteState::Finished;
  nav.OnFix(TqFix(t), r);
  auto const lines = TqGame(nav.TakeLines());
  CHECK_EQUAL(lines.size(), size_t{1});
  CHECK(lines[0].text.find("It was") != std::string::npos);
  CHECK_EQUAL(nav.GetTwentyLog().gpsWins, 1);
}

TEST(TwentyIsAQuietStretchBit)
{
  // The other bits have had their first time, so 20 Questions gets its debut.
  Navigator nav = TqNavigator({"call.script", "ispy.day", "fact.frame"});
  std::set<std::string> asked;
  nav.SetNearbyThingsLookup([&](LatLon const &) {
    asked.insert("yes");
    return std::set<std::string>{"water_tower"};
  });
  double t = kTqDayMs;
  double dist = 60000;
  auto const lines = TqGame(TqDrive(nav, t, 300, 1, dist));
  // The offer, and after a minute and a bit of silence, the grudge for ignoring it.
  CHECK_EQUAL(lines.size(), size_t{2});
  CHECK(lines[0].text.find("20 questions") != std::string::npos);
  CHECK(!asked.empty());
  CHECK_EQUAL(nav.GetTwentyLog().grudges, 1);
}
