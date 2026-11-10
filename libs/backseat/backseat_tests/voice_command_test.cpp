#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"
#include "backseat/voice_command.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

using namespace backseat;

TEST(DestinationCommandsAreHeard)
{
  auto work = ParseDestinationCommand("Take me to work");
  CHECK(work && work->target == "work" && !work->polite);
  CHECK(KnownPlaceOf(work->target) == KnownPlace::Work);

  auto home = ParseDestinationCommand("good morning, could you take me home please");
  CHECK(home && home->target == "home" && home->polite);
  CHECK(ParseDestinationCommand("take me home")->target == "home");
  home = ParseDestinationCommand("good morning! navigate to my house please");
  CHECK(home && home->target == "house" && home->polite);
  CHECK(KnownPlaceOf(home->target) == KnownPlace::Home);

  auto cafe = ParseDestinationCommand("directions to the Starbucks on Main");
  CHECK(cafe && cafe->target == "starbucks on main" && !cafe->polite);
  CHECK(!KnownPlaceOf(cafe->target));

  auto office = ParseDestinationCommand("hey, go to the office");
  CHECK(office && office->polite && KnownPlaceOf(office->target) == KnownPlace::Work);

  CHECK(!ParseDestinationCommand("I see a Texas plate"));
  CHECK(!ParseDestinationCommand("take me to"));
}

TEST(HomeAndWorkComeFromPastTrips)
{
  std::vector<TripRecord> trips;
  auto const trip = [&](LatLon from, LatLon to)
  {
    TripRecord r;
    r.routeKey = RouteKey(from, to);
    r.to = to;
    trips.push_back(r);
  };
  LatLon const home{39.7401, -104.9903};
  LatLon const work{39.7502, -104.9401};
  LatLon const gym{39.7000, -104.9000};
  CHECK(!GuessPlace(trips, KnownPlace::Work));
  for (int i = 0; i < 3; ++i)
  {
    trip(home, work);
    trip(work, home);
  }
  trip(home, gym);
  auto const w = GuessPlace(trips, KnownPlace::Work);
  auto const h = GuessPlace(trips, KnownPlace::Home);
  CHECK(w && std::abs(w->lat - work.lat) < 1e-6 && std::abs(w->lon - work.lon) < 1e-6);
  CHECK(h && std::abs(h->lat - home.lat) < 1e-6);
}

TEST(NavigatorOpensWithBossyWhenQueued)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Left;
  r.distToTurnM = 2000;
  r.destination = "Work";
  Fix f;
  f.tMs = 100000;
  f.lat = 39.74;
  f.lon = -104.99;
  f.speedMps = 0;

  EngineConfig c;
  c.rng = SeededRng(9);
  Navigator nav(c);
  nav.QueueOpener(60000, "bossy", {{"place", "your work"}});
  nav.OnFix(f, r);
  auto lines = nav.TakeLines();
  CHECK(!lines.empty());
  CHECK_EQUAL(lines[0].text, std::string("Alright, bossy. I don't even know where your work is. Ok, maybe I do. "
                                         "Woke up on the wrong side of the bed much?"));

  // Queued long before the trip: forgotten.
  Navigator later(c);
  later.QueueOpener(0, "bossy", {{"place", "your work"}});
  f.tMs = Navigator::kOpenerValidMs + 1000;
  later.OnFix(f, r);
  for (auto const & l : later.TakeLines())
    CHECK(l.category != Category::Bossy);
}

TEST(AppToolsCoverEveryAction)
{
  auto const & tools = AppTools();
  CHECK_EQUAL(tools.size(), static_cast<size_t>(AppAction::Count));
  for (size_t i = 0; i < tools.size(); ++i)
  {
    CHECK(tools[i].action == static_cast<AppAction>(i));
    CHECK(AppActionNamed(tools[i].name) == tools[i].action);
  }
  CHECK(!AppActionNamed("launch_missiles"));
}

TEST(AppCommandsAreHeardInOrder)
{
  auto const both = ParseAppCommands("Avoid tolls and take me home");
  CHECK_EQUAL(both.size(), size_t(2));
  CHECK(both[0].action == AppAction::AvoidTolls);
  CHECK(both[1].action == AppAction::NavigateTo);
  CHECK_EQUAL(both[1].argument, std::string("home"));

  CHECK(ParseAppCommands("ok cancel the route")[0].action == AppAction::CancelRoute);
  CHECK(ParseAppCommands("please unmute")[0].action == AppAction::Unmute);
  CHECK_EQUAL(ParseAppCommands("please unmute").size(), size_t(1));
  CHECK(ParseAppCommands("shut up")[0].action == AppAction::DirectionsOnly);
  CHECK(ParseAppCommands("How much longer?")[0].action == AppAction::TimeLeft);
  CHECK(ParseAppCommands("what did you say")[0].action == AppAction::RepeatInstruction);
  CHECK(ParseAppCommands("go to settings")[0].action == AppAction::OpenSettings);
  CHECK_EQUAL(ParseAppCommands("go to settings").size(), size_t(1));
  CHECK(ParseAppCommands("stop listening")[0].action == AppAction::StopListening);
  CHECK(ParseAppCommands("so what can you do?")[0].action == AppAction::ListCommands);

  auto const voice = ParseAppCommands("switch to the echo voice");
  CHECK_EQUAL(voice.size(), size_t(1));
  CHECK(voice[0].action == AppAction::ChangeVoice);
  CHECK_EQUAL(voice[0].argument, std::string("echo"));
  CHECK_EQUAL(ParseAppCommands("change your voice to heart")[0].argument, std::string("heart"));

  auto const list = ParseAppCommands("What voice options do you have?");
  CHECK_EQUAL(list.size(), size_t(1));
  CHECK(list[0].action == AppAction::ListVoices);

  // Chatter from an always-on microphone.
  CHECK(ParseAppCommands("I love this song").empty());
  CHECK(ParseAppCommands("your voice is weird").empty());
  // Police and plates stay with the navigator.
  CHECK(ParseAppCommands("there's a cop behind us").empty());
}

TEST(VoicesHaveCodePhrases)
{
  std::vector<VoiceOption> const voices = {{"af_heart", "Heart"}, {"am_echo", "Echo"}, {"am_adam", "Adam"}, {"bm_george", "George"}};
  auto const phrases = VoicePhrases(voices);
  CHECK_EQUAL(phrases[1], std::string("purple monkey"));
  CHECK_EQUAL(phrases[2], std::string("elephant shoe"));
  CHECK(phrases[0] != phrases[3]);
  CHECK(VoiceByPhrase(voices, "Purple Monkey!") == size_t(1));
  CHECK(VoiceByPhrase(voices, "elephant shoe") == size_t(2));
  CHECK(!VoiceByPhrase(voices, "purple"));
  CHECK(VoiceNamed(voices, "echo") == size_t(1));
  CHECK(VoiceNamed(voices, "bm_george") == size_t(3));
  CHECK(!VoiceNamed(voices, "gandalf"));
}

TEST(NavigatorRepliesWithoutRepeating)
{
  EngineConfig c;
  c.rng = [] { return 0.0; };
  Navigator nav(c);
  // Between trips, too.
  std::set<std::string> said;
  for (int i = 0; i < 3; ++i)
    said.insert(nav.Reply("cmd.cancel"));
  CHECK_EQUAL(said.size(), size_t(3));
  CHECK(nav.Reply("cmd.navigate", {{"place", "Work"}}).find("Work") != std::string::npos);
}

TEST(RepliesKnowTheDriver)
{
  EngineConfig c;
  c.rng = [] { return 0.0; };
  Navigator nav(c);
  nav.SetDriverPronouns(Pronouns::She);
  CHECK_EQUAL(CountVoiceName("Bella", "Bella"), 1);
  CHECK_EQUAL(CountVoiceName("uh BELLA please bella", "Bella"), 2);
  CHECK_EQUAL(CountVoiceName("Bela", "Bella"), 1);
  CHECK_EQUAL(CountVoiceName("Jess", "Jessica"), 1);
  CHECK_EQUAL(CountVoiceName("Isabella", "Bella"), 0);
  CHECK_EQUAL(CountVoiceName("heart", "Bella"), 0);
  CHECK_EQUAL(CountVoiceName("okay banana phone", "banana phone"), 1);
  CHECK_EQUAL(CountVoiceName("banana", "banana phone"), 0);
  for (auto const * key : {"voice.preview.complaint", "voice.preview.notMissing"})
    CHECK(!nav.Reply(key).empty());
  CHECK_EQUAL(nav.Reply("cmd.voice.fellForIt", {{"voice", "Echo"}}),
              std::string("Ha ha ha! I can't believe you fell for that. She's all yours, Echo."));
  nav.SetDriverName("Brandon");
  CHECK_EQUAL(nav.Reply("setup.whereTo", {{"wrong", "Doug"}}), std::string("Okay Doug, where are we headed?"));
  CHECK(nav.Reply("name.wrong") != nav.Reply("name.wrong"));
}

TEST(SetupAnswersAreUnderstood)
{
  CHECK_EQUAL(ParseNameAnswer("My name is Brandon"), std::string("Brandon"));
  CHECK_EQUAL(ParseNameAnswer("um, call me B please"), std::string("B"));
  CHECK_EQUAL(ParseNameAnswer("Brandon Conover"), std::string("Brandon Conover"));
  CHECK_EQUAL(ParseNameAnswer("uh"), std::string());
  CHECK(ParsePronounAnswer("he him") == std::string("he"));
  CHECK(ParsePronounAnswer("I'm a girl") == std::string("she"));
  CHECK(ParsePronounAnswer("they please") == std::string("they"));
  CHECK(!ParsePronounAnswer("pass"));
  CHECK_EQUAL(ParsePassengerAnswer("usually my wife")->word, std::string("wife"));
  CHECK_EQUAL(ParsePassengerAnswer("usually my wife")->pronouns, std::string("she"));
  CHECK_EQUAL(ParsePassengerAnswer("nope, just me")->word, std::string());
  CHECK(!ParsePassengerAnswer("hmm"));
  CHECK_EQUAL(MentionedPartner("my husband hates this song")->word, std::string("husband"));
  CHECK(!MentionedPartner("no wife jokes please"));
}

namespace
{
std::tm Utc(double ms)
{
  std::time_t const s = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  gmtime_r(&s, &tm);
  return tm;
}
}  // namespace

TEST(RoutinesAreLearnedAndBroken)
{
  LatLon const home{39.70, -104.90};
  LatLon const work{39.75, -104.99};
  LatLon const gym{39.72, -104.95};
  // Two weeks of commutes: Monday 2026-10-12 to Friday, leaving at 8:00 (UTC here), 25 minutes.
  double const monday = 1791763200000.0;  // 2026-10-12 00:00 UTC, a Monday
  double constexpr kDay = 86400000.0;
  std::vector<TripRecord> history;
  for (int week = 0; week < 2; ++week)
    for (int d = 0; d < 5; ++d)
    {
      TripRecord r;
      r.routeKey = RouteKey(home, work);
      r.destination = "Acme Corp";
      r.to = work;
      r.actualMinutes = 25;
      r.endedAt = monday + (week * 7 + d) * kDay + (8 * 60 + 25) * 60000.0;
      history.push_back(r);
    }
  auto const routines = LearnRoutines(history, Utc);
  CHECK_EQUAL(routines.size(), size_t(1));
  CHECK_EQUAL(routines[0].startMinute, 8 * 60);
  CHECK(routines[0].usualOn[1] && routines[0].usualOn[5] && !routines[0].usualOn[6]);
  CHECK_EQUAL(routines[0].destination, std::string("Acme Corp"));
  CHECK_EQUAL(routines[0].usualMinutes, 25);

  auto at = [&](int dayOffset, int hour, int minute) { return Utc(monday + dayOffset * kDay + (hour * 60 + minute) * 60000.0); };
  CHECK(!CheckRoutine(routines, RouteKey(home, work), at(15, 8, 5)));  // a Tuesday, on time
  auto const saturday = CheckRoutine(routines, RouteKey(home, work), at(19, 8, 10));
  CHECK(saturday && saturday->kind == RoutineBreak::Kind::OffDay);
  auto const late = CheckRoutine(routines, RouteKey(home, work), at(15, 9, 30));
  CHECK(late && late->kind == RoutineBreak::Kind::Late && late->minutesLate == 90);
  auto const elsewhere = CheckRoutine(routines, RouteKey(home, gym), at(15, 8, 5));
  CHECK(elsewhere && elsewhere->kind == RoutineBreak::Kind::Elsewhere);
  CHECK(!CheckRoutine(routines, RouteKey(home, gym), at(15, 19, 0)));
}

TEST(UsualTripIsGuessed)
{
  LatLon const home{39.70, -104.90};
  LatLon const work{39.75, -104.99};
  Routine r;
  r.routeKey = RouteKey(home, work);
  r.startMinute = 8 * 60;
  r.usualOn = {false, true, true, true, true, true, false};
  r.trips = 10;
  std::tm tue{};
  tue.tm_wday = 2;
  tue.tm_hour = 8;
  tue.tm_min = 20;
  CHECK(UsualTrip({r}, home, tue).has_value());
  CHECK(!UsualTrip({r}, work, tue));
  tue.tm_hour = 14;
  CHECK(!UsualTrip({r}, home, tue));
  CHECK(SaidYes("yeah"));
  CHECK(SaidYes("yes please"));
  CHECK(!SaidYes("no, not today"));
  CHECK(!SaidYes("the gym"));
}

TEST(NamesAreMisheardSlightly)
{
  CHECK_EQUAL(MisheardName("Brandon", 0), std::string("Brendon"));
  CHECK_EQUAL(MisheardName("Brandon", 1), std::string("Brandom"));
  CHECK_EQUAL(MisheardName("Brandon", 2), std::string("Brandy"));
  for (unsigned v = 0; v < 6; ++v)
    for (std::string const name : {"Sarah", "Al", "Bo", "Ian", "Jess", "Kate", "Mike", "X"})
      CHECK(MisheardName(name, v) != name);
  CHECK_EQUAL(MisheardName("Brandon Conover", 0), std::string("Brendon"));
}

TEST(PronounsAreGuessedAndCorrected)
{
  CHECK(PronounsForName("Brandon") == std::string("he"));
  CHECK(PronounsForName("sarah connor") == std::string("she"));
  CHECK(!PronounsForName("Zorblax"));
  CHECK(PronounCorrection("excuse me, I'm a woman") == std::string("she"));
  CHECK(PronounCorrection("I am a guy") == std::string("he"));
  CHECK(PronounCorrection("my pronouns are they them") == std::string("they"));
  CHECK(!PronounCorrection("I'm a terrible driver"));
  CHECK(!PronounCorrection("I'm hungry"));
}

TEST(SearchChoicesAreHeard)
{
  std::vector<std::string> const names = {"Starbucks", "Starbucks Colfax Avenue", "Starbucks Union Station"};
  CHECK(ParseChoice("the first one", names) == size_t(0));
  CHECK(ParseChoice("number two please", names) == size_t(1));
  CHECK(ParseChoice("the closest", names) == size_t(0));
  CHECK(ParseChoice("last one", names) == size_t(2));
  CHECK(ParseChoice("the one on Colfax", names) == size_t(1));
  CHECK(ParseChoice("union station", names) == size_t(2));
  CHECK(!ParseChoice("hmm", names));
  CHECK(!ParseChoice("first", {}));
}

TEST(HandEditedProfileIsCalledOut)
{
  DriverProfile const before{"Brandon", "he", "34"};
  auto name = ProfileChanged(before, {"Brenda", "she", "34"});
  CHECK(name && name->key == "profile.name");
  CHECK(name->vars.at("old") == "Brandon" && name->vars.at("new") == "Brenda");
  auto title = ProfileChanged(before, {"Brandon", "she", "34"});
  CHECK(title && title->key == "profile.title" && title->vars.at("old") == "sir" && title->vars.at("new") == "ma'am");
  CHECK(ProfileChanged(before, {"Brandon", "he", "40"})->key == "profile.age");
  CHECK(ProfileChanged(before, {"Brandon", "he", "29"})->key == "profile.younger");
  CHECK(!ProfileChanged(before, before));
  CHECK(!ProfileChanged(before, {"brandon", "he", "34"}));
  // Filling in something new is setup, not changing it up.
  CHECK(!ProfileChanged({"Brandon", "he", ""}, {"Brandon", "he", "34"}));
  CHECK(!ProfileChanged({"", "", ""}, {"Brandon", "he", ""}));
}

TEST(PastPlacesAreRankedByHowOftenTheyreDriven)
{
  LatLon const home{39.70, -104.90};
  LatLon const work{39.75, -104.99};
  LatLon const gym{39.72, -104.95};
  std::vector<TripRecord> history;
  auto trip = [&](LatLon from, LatLon to, std::string name, double endedAt, double expected)
  {
    TripRecord r;
    r.routeKey = RouteKey(from, to);
    r.destination = std::move(name);
    r.to = to;
    r.endedAt = endedAt;
    r.expectedMinutes = expected;
    r.actualMinutes = expected + 5;
    history.push_back(r);
  };
  trip(home, work, "Acme Corp", 1000, 20);
  trip(home, work, "Acme Corp", 2000, 24);
  trip(gym, work, "Acme", 3000, 10);  // from somewhere else, same place
  trip(home, gym, "Gym", 9000, 8);
  trip(work, home, "Home", 4000, 0);
  trip(work, home, "Home", 5000, 0);
  trip(home, {39.80, -105.0}, "", 0, 9);  // never finished: ignored

  auto const places = PastPlaces(history, 10);
  CHECK_EQUAL(places.size(), size_t(3));
  CHECK_EQUAL(places[0].destination, std::string("Acme"));  // the latest name
  CHECK_EQUAL(places[0].trips, 3);
  CHECK_EQUAL(places[0].usualMinutes, 18);
  CHECK_EQUAL(places[1].destination, std::string("Home"));
  CHECK_EQUAL(places[1].usualMinutes, 5);  // no estimate, so how long it took
  CHECK_EQUAL(places[2].destination, std::string("Gym"));
  CHECK_EQUAL(PastPlaces(history, 1).size(), size_t(1));
}

TEST(DriverMemoryRemembersAndForgets)
{
  DriverMemory memory;
  CHECK(memory.Remember("name", "Brandon", 1000));
  CHECK(!memory.Remember("name", "Brandon", 2000));
  CHECK(memory.Remember("name", "Brendon\twith a tab", 3000));
  CHECK_EQUAL(*memory.Recall("name"), std::string("Brendon with a tab"));
  CHECK(memory.Remember("note", "hates country music", 4000));
  CHECK(memory.Remember("note", "has a dog named Biscuit", 5000));
  CHECK(!memory.Remember("note", "hates country music", 6000));
  CHECK(!memory.Remember("snack", "  ", 7000));
  CHECK_EQUAL(memory.All().size(), size_t(3));

  auto const copy = DriverMemory::Parse(memory.Serialize());
  CHECK_EQUAL(copy.All().size(), size_t(3));
  CHECK_EQUAL(copy.All()[2].fact, std::string("has a dog named Biscuit"));
  CHECK_EQUAL(copy.All()[2].learnedAt, 5000.0);

  // The model sees topics first, then the newest notes.
  CHECK_EQUAL(memory.ForModel(1000), std::string("name: Brendon with a tab\nnote: has a dog named Biscuit\nnote: hates country music\n"));
  CHECK_EQUAL(memory.ForModel(60), std::string("name: Brendon with a tab\nnote: has a dog named Biscuit\n"));
  CHECK(memory.Forget("note"));
  CHECK(!memory.Forget("note"));
  CHECK_EQUAL(memory.All().size(), size_t(1));
}

TEST(FileAnswersAreCleanedUp)
{
  CHECK_EQUAL(ParseFavorite("Um, I'd say vanilla ice cream", {"ice cream"}), std::string("vanilla"));
  CHECK_EQUAL(ParseFavorite("My favorite is blue", {"color"}), std::string("blue"));
  CHECK_EQUAL(ParseFavorite("probably the gummy worms"), std::string("gummy worms"));
  CHECK_EQUAL(ParseFavorite("hmm"), std::string());
  CHECK_EQUAL(ParseBedSide("the left side I guess"), std::string("left"));
  CHECK_EQUAL(ParseBedSide("whatever side"), std::string());
  CHECK(DeclinedToAnswer("none of your business"));
  CHECK(DeclinedToAnswer("I don't know"));
  CHECK(DeclinedToAnswer(""));
  CHECK(!DeclinedToAnswer("chocolate"));
}

TEST(RatingsAreHeard)
{
  CHECK_EQUAL(ParseRating("I'd give it a seven"), 7);
  CHECK_EQUAL(ParseRating("10"), 10);
  CHECK_EQUAL(ParseRating("solid ten out of ten"), 10);
  CHECK_EQUAL(ParseRating("one"), 1);
  CHECK_EQUAL(ParseRating("it was fine"), 0);
  CHECK_EQUAL(ParseRating("someone cut me off"), 0);
}

TEST(BusinessOwnerAnswers)
{
  CHECK(SaidNo("nope"));
  CHECK(SaidNo("No, that's not my house"));
  CHECK(!SaidNo("yeah that's home"));
  CHECK(ParseBusinessOwner("my wife does") == std::optional<std::string>("wife"));
  CHECK(ParseBusinessOwner("My wife’s") == std::optional<std::string>("wife"));
  CHECK(ParseBusinessOwner("it's mine") == std::optional<std::string>(""));
  CHECK(ParseBusinessOwner("Sarah") == std::optional<std::string>("Sarah"));
  CHECK(ParseBusinessOwner("it's Sarah's") == std::optional<std::string>("Sarah"));
  CHECK(!ParseBusinessOwner("um").has_value());
  CHECK(BusinessOwnerPossessive("") == "your");
  CHECK(BusinessOwnerPossessive("wife") == "your wife's");
  CHECK(BusinessOwnerPossessive("Sarah") == "Sarah's");
}

TEST(TheGpsDoesntObeyItself)
{
  std::string const line = "Please stop making a U-turn. If you keep going, we'll be headed the wrong way again.";
  CHECK(IsEchoOf("Wrong way again", line));
  CHECK(IsEchoOf("Right away", "peace Lutheran. Yes, boss. Right away, boss."));
  CHECK(IsEchoOf("Three results", "I found three results. First, Peace Lutheran Church."));
  CHECK(IsEchoOf("headed the wrong way", line));
  // The driver's own words, even with a line's small words in them.
  CHECK(!IsEchoOf("Take me to work", "Take me to the moon, you said? Fine."));
  CHECK(!IsEchoOf("go to the store", "You should go to the left lane."));
  CHECK(!IsEchoOf("Yes", "Yes, boss."));
  CHECK(!IsEchoOf("work", "Back to work."));
  CHECK(!IsEchoOf("", line));

  SpokenLines lines;
  lines.Add(1000, line);
  CHECK(lines.EchoOf(12000, "Wrong way again") == line);
  CHECK(!lines.EchoOf(12000, "Take me to Walmart"));
  CHECK(!lines.EchoOf(21001, "Wrong way again"));
}

TEST(ChatterIsntACommand)
{
  CHECK(IsFiller("Yeah"));
  CHECK(IsFiller("uh, okay"));
  CHECK(IsFiller(""));
  CHECK(!IsFiller("Yeah take me home"));
  CHECK(!IsFiller("Walmart"));

  CHECK(!WordsSupportTool(AppAction::NavigateTo, "Melissa Drive", "Yeah"));
  CHECK(!WordsSupportTool(AppAction::NavigateTo, "Peace Lutheran", "Wrong way again"));
  CHECK(WordsSupportTool(AppAction::NavigateTo, "Sundance Lane", "Take me home"));
  CHECK(WordsSupportTool(AppAction::NavigateTo, "Peace Lutheran", "Peace Lutheran"));
  CHECK(WordsSupportTool(AppAction::NavigateTo, "the airport", "I need the airport"));
  CHECK(!WordsSupportTool(AppAction::ListVoices, "", "Are you metal"));
  CHECK(WordsSupportTool(AppAction::ListVoices, "", "what voices do you have"));
  CHECK(WordsSupportTool(AppAction::ChangeVoice, "Heart", "Switch to heart"));
  CHECK(WordsSupportTool(AppAction::ChangeVoice, "Pink", "Pink a pink"));
  CHECK(!WordsSupportTool(AppAction::ChangeVoice, "Heart", "I love this road"));
  CHECK(!WordsSupportTool(AppAction::CancelRoute, "", "I hate this song"));
  CHECK(WordsSupportTool(AppAction::CancelRoute, "", "stop the route"));
}

TEST(AWeakMatchIsntAMatch)
{
  CHECK(!PlaceMatches("peace Lutheran", "Latheron Place"));
  CHECK(PlaceMatches("peace Lutheran and Rogers", "Peace Lutheran Church"));
  CHECK(PlaceMatches("walmart", "Wal-Mart Supercenter"));
  CHECK(PlaceMatches("the airport", "XNA Airport"));
  CHECK(!PlaceMatches("coffee", "Starbucks"));
  // Everything the search asks to confirm has lines to say it with.
  for (char const * key : {"search.confirm", "search.confirm.no"})
    for (int tier = 0; tier < 3; ++tier)
      CHECK(LinesFor(key, tier).size() >= 3);
}

namespace
{
bool Heard(std::string const & spoken, AppAction action)
{
  auto const commands = ParseAppCommands(spoken);
  return std::any_of(commands.begin(), commands.end(), [action](AppCommand const & c) { return c.action == action; });
}
}  // namespace

TEST(HowFarIsTheTurnIsNextTurn)
{
  auto const onlyTurn = [](std::string const & spoken)
  {
    auto const commands = ParseAppCommands(spoken);
    return commands.size() == 1 && commands[0].action == AppAction::NextTurn;
  };
  CHECK(onlyTurn("how far is the next turn"));
  CHECK(onlyTurn("how far to my exit"));
  CHECK(onlyTurn("how long until I turn"));
  CHECK(onlyTurn("how long till my next turn"));
  CHECK(onlyTurn("how much further to the exit"));
  // The trip and the stop questions are unchanged.
  CHECK(Heard("how far", AppAction::TimeLeft));
  CHECK(!Heard("how far", AppAction::NextTurn));
  CHECK(Heard("how long until we get there", AppAction::TimeLeft));
  CHECK(!Heard("how long until we get there", AppAction::NextTurn));
  CHECK(Heard("how far is the next stop", AppAction::NextStop));
  CHECK(!Heard("how far is the next stop", AppAction::NextTurn));
  CHECK(Heard("what's the next turn", AppAction::NextTurn));
  // Not asked for.
  CHECK(ParseAppCommands("don't tell me how far the turn is").empty());
}

TEST(DrivingCommandsAreHeard)
{
  auto const gas = ParseAppCommands("find gas");
  CHECK(gas.size() == 1 && gas[0].action == AppAction::FindAlongRoute && gas[0].argument == "gas station");
  CHECK(ParseAppCommands("I need a bathroom")[0].argument == "restroom");
  CHECK(ParseAppCommands("add a stop at Walmart please")[0].argument == "walmart");
  CHECK(Heard("skip the next stop", AppAction::SkipStop));
  CHECK(Heard("how far is the next stop", AppAction::NextStop));
  CHECK(!Heard("how far is the next stop", AppAction::TimeLeft));
  CHECK(Heard("what's the speed limit", AppAction::SpeedLimit));
  CHECK(Heard("how fast am I going", AppAction::CurrentSpeed));
  CHECK(Heard("what road is this", AppAction::CurrentRoad));
  // B96: "where am I" says the road first (eyes on the road), then recenters the map.
  for (char const * said : {"where am I", "Where am I?", "show me where I am", "hey where am i right now"})
  {
    auto const c = ParseAppCommands(said);
    CHECK_EQUAL(c.size(), size_t(2));
    CHECK(c.size() == 2 && c[0].action == AppAction::CurrentRoad && c[1].action == AppAction::Recenter);
  }
  CHECK_EQUAL(ParseAppCommands("recenter").size(), size_t(1));
  CHECK(ParseAppCommands("recenter")[0].action == AppAction::Recenter);
  for (char const * said : {"where am I going", "where am I headed", "where am I supposed to turn"})
    CHECK(!Heard(said, AppAction::CurrentRoad));
  CHECK(ParseAppCommands("don't tell me where I am").empty());
  {
    auto const both = ParseAppCommands("where am I, what road is this");
    CHECK(std::count_if(both.begin(), both.end(), [](auto const & c) { return c.action == AppAction::CurrentRoad; }) == 1);
  }
  CHECK(Heard("what's the next turn", AppAction::NextTurn));
  CHECK(!Heard("what's the next turn", AppAction::RepeatInstruction));
  CHECK(Heard("what time will we get there", AppAction::ArrivalTime));
  CHECK(Heard("find a different route", AppAction::DifferentRoute));
  CHECK(Heard("take me back", AppAction::TakeMeBack));
  CHECK(!Heard("take me back to work", AppAction::TakeMeBack));
  CHECK(ParseAppCommands("take me back to work")[0].action == AppAction::NavigateTo);
}

TEST(ComedyCommandsAreHeard)
{
  CHECK(Heard("could you be nicer", AppAction::BeNicer));
  CHECK(Heard("be meaner", AppAction::BeMeaner));
  CHECK(Heard("how am I doing", AppAction::GradeSoFar));
  CHECK(Heard("roast me", AppAction::RoastMe));
  CHECK(Heard("tell me a joke", AppAction::TellJoke));
  CHECK(Heard("that was your fault", AppAction::BlameGps));
  CHECK(Heard("you're wrong", AppAction::BlameGps));
  // Plain talk stays plain talk.
  for (char const * chatter : {"what a joke", "that movie was funny", "they're mean people", "he has a nice car"})
    CHECK(ParseAppCommands(chatter).empty());
  for (char const * key : {"cmd.meaner", "cmd.grade", "cmd.grade.none", "cmd.roastMe", "cmd.joke", "cmd.blame"})
    CHECK(LinesFor(key, 0).size() >= 6);
}

TEST(YourFileCanBeReadAndCleaned)
{
  CHECK(Heard("what do you know about me", AppAction::WhatDoYouKnow));
  auto const named = ParseAppCommands("forget my favorite color");
  CHECK(named.size() == 1 && named[0].action == AppAction::ForgetThat && named[0].argument == "favorite color");
  CHECK(ParseAppCommands("forget that")[0].argument.empty());
  CHECK(ParseAppCommands("I forget that one").empty());
  CHECK(ParseAppCommands("I always forget my keys").empty());
  CHECK(ParseAppCommands("don't forget to call mom").empty());

  DriverMemory memory;
  memory.Remember("favorite color", "blue", 1);
  memory.Remember("favorite ice cream", "mint", 2);
  CHECK(MemoryTopicNamed(memory, "favorite color") == std::string("favorite color"));
  CHECK(MemoryTopicNamed(memory, "the mint") == std::nullopt);
  CHECK(MemoryTopicNamed(memory, "mint") == std::string("favorite ice cream"));
  CHECK(!MemoryTopicNamed(memory, "dog"));

  CHECK(IsJunkAnswer("favorite color", "so let's see"));
  CHECK(IsJunkAnswer("favorite color", "um"));
  CHECK(IsJunkAnswer("favorite color", "my favorite color"));
  CHECK(!IsJunkAnswer("favorite color", "blue"));
  CHECK(!IsJunkAnswer("pet", "has a dog named Biscuit"));
  CHECK(LinesFor("cmd.known", 0).size() >= 6);
  CHECK(LinesFor("cmd.forgot", 0).size() >= 6);
}

TEST(TravelModeCanBeSaid)
{
  auto const walk = ParseAppCommands("give me walking directions");
  CHECK(walk.size() == 1 && walk[0].action == AppAction::SetTravelMode && walk[0].argument == "walk");
  CHECK(ParseAppCommands("I'm biking")[0].argument == "bike");
  CHECK(ParseAppCommands("switch to driving")[0].argument == "drive");
  CHECK(ParseAppCommands("I was walking the dog").empty());
  CHECK(ParseAppCommands("we're going off roading")[0].argument == "offroad");
  CHECK(ParseAppCommands("we're back on the road")[0].argument == "drive");
}

TEST(DrivingChatterIsntACommand)
{
  for (char const * chatter : {"I'm skipping lunch", "next time we should stop", "what a joke", "the speed limit here is silly",
                               "I can't find my gas card", "we need to talk", "there's no other way to say it"})
  {
    auto const commands = ParseAppCommands(chatter);
    CHECK(commands.empty());
  }
}

TEST(CommandsDontRunTwice)
{
  CommandRepeats repeats;
  CHECK(!repeats.IsRepeat({AppAction::NavigateTo, "peace Lutheran and Rogers"}, 0));
  CHECK(repeats.IsRepeat({AppAction::NavigateTo, "Peace Lutheran"}, 4000));
  // Still being built after the 15 seconds that catch any other repeat.
  CHECK(repeats.IsRepeat({AppAction::NavigateTo, "peace lutheran"}, 40000));
  // Somewhere else is a new request.
  CHECK(!repeats.IsRepeat({AppAction::NavigateTo, "Walmart"}, 41000));
  repeats.RouteStarted();
  // Started, but the same words a moment later are still a repeat.
  CHECK(repeats.IsRepeat({AppAction::NavigateTo, "walmart"}, 42000));
  CHECK(!repeats.IsRepeat({AppAction::NavigateTo, "walmart"}, 57000));
  // A route that never came can be asked for again.
  CHECK(!repeats.IsRepeat({AppAction::NavigateTo, "walmart"}, 118000));

  CHECK(!repeats.IsRepeat({AppAction::Mute, ""}, 200000));
  CHECK(repeats.IsRepeat({AppAction::Mute, ""}, 210000));
  CHECK(!repeats.IsRepeat({AppAction::Unmute, ""}, 211000));
  CHECK(!repeats.IsRepeat({AppAction::Mute, ""}, 216000));
  CHECK(!repeats.IsRepeat({AppAction::RepeatInstruction, ""}, 217000));
  CHECK(!repeats.IsRepeat({AppAction::RepeatInstruction, ""}, 218000));
}

namespace
{
/// "what's" with each curly apostrophe dictation and keyboards send: ’ (U+2019), ‘ (U+2018), ʼ (U+02BC).
std::vector<std::string> CurlyVersions(std::string const & straight)
{
  std::vector<std::string> out;
  for (char const * curly : {"\xE2\x80\x99", "\xE2\x80\x98", "\xCA\xBC"})
  {
    std::string s;
    for (char const c : straight)
      s += c == '\'' ? std::string(curly) : std::string(1, c);
    out.push_back(s);
  }
  return out;
}

std::string CommandsSummary(std::vector<AppCommand> const & commands)
{
  std::string out;
  for (auto const & c : commands)
    out += std::to_string(static_cast<int>(c.action)) + ":" + c.argument + ";";
  return out;
}
}  // namespace

TEST(StraightApostrophesOnlyTouchesApostrophes)
{
  CHECK_EQUAL(StraightApostrophes("What\xE2\x80\x99s next"), std::string("What's next"));
  CHECK_EQUAL(StraightApostrophes("\xE2\x80\x98I\xCA\xBCm\xE2\x80\x99"), std::string("'I'm'"));
  // Dashes, quotes and other UTF-8 stay as they were.
  for (char const * keep : {"left\xE2\x80\x94right", "\xE2\x80\x9CHi\xE2\x80\x9D", "caf\xC3\xA9", "\xE2\x80", "\xCA"})
    CHECK_EQUAL(StraightApostrophes(keep), std::string(keep));
}

TEST(CurlyApostrophesParseLikeStraightOnes)
{
  // Every ParseAppCommands cue with an apostrophe, in a sentence.
  for (char const * said :
       {"What's the next turn", "what's my next turn", "When's my next turn?", "What's the speed limit",
        "what's my speed", "What's the ETA", "what's our arrival time", "What's the next stop", "where's the next stop",
        "what's my grade", "What's in my file", "I'm walking", "I'm biking now", "I'm hiking", "I'm driving",
        "we're off road", "you're wrong", "that's on you", "let's go to the airport", "Let's avoid tolls"})
  {
    auto const want = CommandsSummary(ParseAppCommands(said));
    CHECK(!want.empty());
    for (auto const & curly : CurlyVersions(said))
      CHECK_EQUAL(CommandsSummary(ParseAppCommands(curly)), want);
  }
  for (auto const & curly : CurlyVersions("my wife's"))
    CHECK(ParseBusinessOwner(curly) == ParseBusinessOwner("my wife's"));
  for (auto const & curly : CurlyVersions("excuse me, I'm a woman"))
    CHECK(PronounCorrection(curly) == std::string("she"));
  for (auto const & curly : CurlyVersions("I'm hungry"))
    CHECK(!PronounCorrection(curly));
}

TEST(NegatedAndOverheardCommandsDontRun)
{
  // Negated in the same clause: heard, not asked for.
  for (char const * said : {"don't cancel the route", "please don't stop navigating", "don't mute", "do not take me home",
                            "I don't want to go to work", "never cancel the route", "don’t mute"})
    CHECK(ParseAppCommands(said).empty());
  // Overheard: a bare "go to" that nobody ordered, and a hush aimed at someone in the car.
  for (char const * said : {"should we go to Costco", "my wife wants to go to Target", "kids be quiet", "shut up Kevin",
                            "Kevin, be quiet", "be quiet guys", "I want to go home"})
    CHECK(ParseAppCommands(said).empty());

  for (char const * said : {"go to work", "Go to work.", "okay go to work", "take me to work instead", "hey, go to work",
                            "could you go to work", "thanks. Go to work"})
  {
    auto const c = ParseAppCommands(said);
    CHECK(c.size() == 1 && c[0].action == AppAction::NavigateTo && c[0].argument == "work");
  }
  auto const airport = ParseAppCommands("let's go to the airport");
  CHECK(airport.size() == 1 && airport[0].argument == "airport");
  for (char const * tail : {"later", "right now", "real quick", "for me"})
  {
    auto const c = ParseAppCommands(std::string("take me to the gym ") + tail);
    CHECK(c.size() == 1 && c[0].argument == "gym");
  }
  CHECK(ParseAppCommands("go home")[0].argument == "home");

  auto const argue = ParseAppCommands("cancel the route, don't argue");
  CHECK(argue.size() == 1 && argue[0].action == AppAction::CancelRoute);
  CHECK(ParseAppCommands("never mind, cancel the route")[0].action == AppAction::CancelRoute);
  CHECK(ParseAppCommands("never mind cancel the route")[0].action == AppAction::CancelRoute);
  CHECK(ParseAppCommands("why don't you tell me a joke")[0].action == AppAction::TellJoke);
  // Said to the GPS itself it still hushes it (directions stay on; "mute" is the full mute).
  for (char const * said : {"shut up", "be quiet", "shut up, Backseat", "be quiet please", "oh shut up"})
  {
    auto const c = ParseAppCommands(said);
    CHECK(!c.empty() && c[0].action == AppAction::DirectionsOnly);
  }
  auto const kevin = ParseAppCommands("Kevin, mute");
  CHECK(!kevin.empty() && kevin[0].action == AppAction::Mute);
}

TEST(NeedsSaidAsNeedsFindAStop)
{
  auto const stopFor = [](char const * said) -> std::string
  {
    auto const commands = ParseAppCommands(said);
    for (auto const & c : commands)
      CHECK(c.action != AppAction::NavigateTo);
    for (auto const & c : commands)
      if (c.action == AppAction::FindAlongRoute)
        return c.argument;
    return "<none>";
  };
  // Needs said as needs, and "nearest" places: a stop, never the end of the trip.
  CHECK_EQUAL(stopFor("I need to go to the bathroom"), std::string("restroom"));
  CHECK_EQUAL(stopFor("my son needs to go to the bathroom"), std::string("restroom"));
  CHECK_EQUAL(stopFor("I have to pee"), std::string("restroom"));
  CHECK_EQUAL(stopFor("I gotta pee so bad"), std::string("restroom"));
  CHECK_EQUAL(stopFor("we need a bathroom break"), std::string("restroom"));
  CHECK_EQUAL(stopFor("we're low on gas"), std::string("gas station"));
  CHECK_EQUAL(stopFor("we’re running low on fuel"), std::string("gas station"));
  CHECK_EQUAL(stopFor("the gas light came on"), std::string("gas station"));
  CHECK_EQUAL(stopFor("I'm starving"), std::string("restaurant"));
  CHECK_EQUAL(stopFor("we have to stop for lunch"), std::string("restaurant"));
  CHECK_EQUAL(stopFor("I need some coffee"), std::string("coffee"));
  CHECK_EQUAL(stopFor("take me to the nearest gas station"), std::string("gas station"));
  CHECK_EQUAL(stopFor("take me to a gas station please"), std::string("gas station"));
  CHECK_EQUAL(stopFor("navigate to the closest Starbucks"), std::string("starbucks"));
  CHECK_EQUAL(stopFor("find a Starbucks"), std::string("starbucks"));
  CHECK_EQUAL(stopFor("find me a McDonald's"), std::string("mcdonald's"));
  CHECK_EQUAL(stopFor("can you find the nearest Walgreens"), std::string("walgreens"));
  CHECK_EQUAL(stopFor("find a place to eat"), std::string("restaurant"));
  CHECK_EQUAL(stopFor("stop at Target on the way"), std::string("target"));
  CHECK_EQUAL(stopFor("let's stop at a gas station"), std::string("gas station"));
  CHECK_EQUAL(stopFor("okay, stop by the Costco first"), std::string("costco"));
  // What already worked still does.
  CHECK_EQUAL(stopFor("find gas"), std::string("gas station"));
  CHECK_EQUAL(stopFor("I need a bathroom"), std::string("restroom"));
  CHECK_EQUAL(stopFor("add a stop at Walmart please"), std::string("walmart"));

  // Not a stop: places as destinations, other errands, chatter, negations.
  CHECK(ParseAppCommands("take me to Starbucks")[0].action == AppAction::NavigateTo);
  CHECK(ParseAppCommands("take me to the airport")[0].argument == "airport");
  CHECK(ParseAppCommands("take me to work")[0].action == AppAction::NavigateTo);
  CHECK(Heard("find a different route", AppAction::DifferentRoute));
  CHECK(!Heard("find a different route", AppAction::FindAlongRoute));
  for (char const * said : {"I need to go to work", "I can't find a pen", "stop at the light", "stop at the stop sign",
                            "should we stop at Target", "we stopped at Target on the way", "I don't have to pee",
                            "I'm not hungry", "skip the stop at Walmart", "she has to go to the dentist",
                            "I need to get home", "find a way out of this", "stop at nothing"})
  {
    auto const commands = ParseAppCommands(said);
    CHECK(std::none_of(commands.begin(), commands.end(), [](AppCommand const & c)
    { return c.action == AppAction::FindAlongRoute || c.action == AppAction::NavigateTo; }));
  }
  // The model's pick holds up when the words were a need.
  CHECK(WordsSupportTool(AppAction::FindAlongRoute, "restroom", "I have to pee"));
  CHECK(WordsSupportTool(AppAction::FindAlongRoute, "restaurant", "I'm starving"));
}

TEST(TalkLessKeepsTheTurns)
{
  auto const only = [](char const * said, AppAction action)
  {
    auto const c = ParseAppCommands(said);
    bool const ok = c.size() == 1 && c[0].action == action;
    if (!ok)
      std::printf("  heard \"%s\": %zu commands, first %s\n", said, c.size(), c.empty() ? "-" : ToolFor(c[0].action).name);
    return ok;
  };
  // A hush keeps the directions: only "mute" goes fully silent.
  for (char const * said : {"shut up", "Shut up!", "be quiet", "stop talking", "hush", "just directions please",
                            "only directions", "directions only", "just give me the directions", "just navigate",
                            "oh my god just shut up", "can you stop talking", "could you be quiet, please"})
    CHECK(only(said, AppAction::DirectionsOnly));
  for (char const * said : {"mute", "mute everything", "mute yourself", "turn the sound off"})
    CHECK(only(said, AppAction::Mute));
  for (char const * said : {"talk less", "less talking please", "fewer jokes", "tone it down", "you talk too much",
                            "stop being so chatty", "less chatty please"})
    CHECK(!ParseAppCommands(said).empty() && ParseAppCommands(said)[0].action == AppAction::TalkLess);
  for (char const * said : {"talk more", "more jokes", "you can talk more now", "say whatever you want"})
    CHECK(only(said, AppAction::TalkMore));
  // Unmuting brings everything back by itself.
  for (char const * said : {"you can talk again", "talk to me again", "unmute and talk to me again"})
    CHECK(only(said, AppAction::Unmute));
  // Curly apostrophes and negations (B18, B19) work here too.
  CHECK(only("don\u2019t be so chatty, just directions", AppAction::DirectionsOnly));
  for (char const * said : {"don't shut up", "please don't stop talking", "never be quiet", "don't talk less",
                            "kids be quiet", "shut up Kevin", "be quiet guys", "Kevin, stop talking"})
    CHECK(ParseAppCommands(said).empty());
  // The tools are there for the on-device model too, by name.
  CHECK(AppActionNamed("directions_only") == AppAction::DirectionsOnly);
  CHECK(AppActionNamed("talk_less") == AppAction::TalkLess);
  CHECK(AppActionNamed("talk_more") == AppAction::TalkMore);
}

TEST(NegatedRoadPreferencesAndReroutes)
{
  // B124: "don't take the highway" means avoid it, not nothing.
  auto const only = [](char const * said, AppAction action)
  {
    auto const c = ParseAppCommands(said);
    bool const ok = c.size() == 1 && c[0].action == action;
    if (!ok)
      std::printf("  heard \"%s\": %zu commands, first %s\n", said, c.size(), c.empty() ? "-" : ToolFor(c[0].action).name);
    return ok;
  };
  for (char const * said : {"don't take the highway", "skip the highway", "stay off the highway", "stay off highways",
                            "please don't use the freeway", "skip the highways today", "don’t take the freeway",
                            "don't take the highway, avoid highways"})
  {
    CHECK(only(said, AppAction::AvoidHighways));
    CHECK(WordsSupportTool(AppAction::AvoidHighways, "", said));
  }
  for (char const * said : {"don't use toll roads", "stay off toll roads", "stay off the toll roads", "skip toll roads",
                            "please don't take toll roads"})
  {
    CHECK(only(said, AppAction::AvoidTolls));
    CHECK(WordsSupportTool(AppAction::AvoidTolls, "", said));
  }
  // The other way round: not avoiding is allowing.
  for (char const * said : {"don't avoid tolls", "don't avoid toll roads"})
  {
    CHECK(!Heard(said, AppAction::AvoidTolls));
    CHECK(only(said, AppAction::AllowTolls));
  }
  CHECK(only("don't avoid highways", AppAction::AllowHighways));
  // A question isn't a negation: "why don't we take the highway" still allows it.
  CHECK(only("why don't we take the highway", AppAction::AllowHighways));
  for (char const * said : {"reroute", "reroute me", "re-route me please", "find a faster route", "is there a faster way",
                            "is there a quicker route", "find me a shorter route"})
  {
    CHECK(only(said, AppAction::DifferentRoute));
    CHECK(WordsSupportTool(AppAction::DifferentRoute, "", said));
  }
  CHECK(!Heard("find a faster route", AppAction::FindAlongRoute));
  CHECK(ParseAppCommands("don't reroute me").empty());
  CHECK(!WordsSupportTool(AppAction::DifferentRoute, "", "the route is fine"));
  // What already worked still does.
  CHECK(only("avoid tolls", AppAction::AvoidTolls));
  CHECK(only("take the highway", AppAction::AllowHighways));
  CHECK(only("find a different route", AppAction::DifferentRoute));
}
