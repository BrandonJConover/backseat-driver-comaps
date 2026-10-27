#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/voice_command.hpp"

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
  CHECK(ParseAppCommands("shut up")[0].action == AppAction::Mute);
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
