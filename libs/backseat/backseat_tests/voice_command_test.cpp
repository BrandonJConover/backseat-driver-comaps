#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/voice_command.hpp"

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
