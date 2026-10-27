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
  CHECK(nav.Reply("setup.done").find("Brandon") != std::string::npos);
}
