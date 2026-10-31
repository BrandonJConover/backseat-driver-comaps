#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/navigator.hpp"
#include "backseat/running_bits.hpp"

#include <map>
#include <set>

using namespace backseat;
using namespace backseat_tests;

namespace
{
Incident Bad(double t, IncidentKind kind, std::string detail)
{
  Incident i;
  i.t = t;
  i.kind = kind;
  i.detail = std::move(detail);
  return i;
}

/// One more on-time turn in `st`.
void Turned(TripStats & st) { ++st.turnsCompleted; }
}  // namespace

TEST(RunningBitsStreetAndGradeHelpers)
{
  CHECK_EQUAL(StreetOf("Turn right onto 8th Street"), std::string("8th Street"));
  CHECK_EQUAL(StreetOf("Turn right onto Oak Avenue, then left onto Elm Street."), std::string("Oak Avenue"));
  CHECK_EQUAL(StreetOf("Take the roundabout"), std::string());
  CHECK_EQUAL(BetterGrade("B"), std::string("B+"));
  CHECK_EQUAL(BetterGrade("C+"), std::string("B-"));
  CHECK_EQUAL(BetterGrade("D"), std::string("C-"));
  CHECK_EQUAL(BetterGrade("A"), std::string("A"));
}

TEST(RunningBitsEveryKeyHasLines)
{
  for (char const * key : {"turnCompleted.credit", "turnCompleted.redemption", "turnCompleted.streak3",
                           "turnCompleted.streak6", "turnCompleted.streak10", "clean.1", "clean.2", "clean.3",
                           "arrived.blame", "arrived.clean"})
    for (int tier = 0; tier < 3; ++tier)
      CHECK(!LinesFor(key, tier).empty());
  for (auto const & id : RunningBits::StoryIds())
  {
    CHECK_EQUAL(LinesFor("speeding.story." + id, 0).size(), size_t{5});
    CHECK(!LinesFor("speeding.end." + id, 0).empty());
  }
}

// After a blown turn, the next good one calls back to it by name.
TEST(RunningBitsRedemptionNamesTheBlownTurn)
{
  RunningBits b;
  b.NewTrip(false);
  TripStats st;
  std::vector<Incident> inc;
  st.missedTurns = 1;
  inc.push_back(Bad(1000, IncidentKind::MissedTurn, "Turn right onto 8th Street"));
  Turned(st);
  auto const v = b.OnTimeTurn(st, inc, 0.9);
  CHECK(v.has_value());
  CHECK_EQUAL(v->key, std::string("turnCompleted.redemption"));
  CHECK_EQUAL(v->vars.at("street"), std::string("8th Street"));
  // Only once.
  Turned(st);
  auto const next = b.OnTimeTurn(st, inc, 0.9);
  CHECK(!next || next->key != "turnCompleted.redemption");
}

// Streaks get their lines once each; most other turns go by quietly.
TEST(RunningBitsTurnPraiseIsRareAndBuilds)
{
  RunningBits b;
  b.NewTrip(false);
  TripStats st;
  std::vector<Incident> inc;
  std::map<std::string, int> said;
  int quiet = 0;
  for (int i = 0; i < 15; ++i)
  {
    Turned(st);
    if (auto v = b.OnTimeTurn(st, inc, 0.7))
      ++said[v->key];
    else
      ++quiet;
  }
  CHECK_EQUAL(said["turnCompleted.streak3"], 1);
  CHECK_EQUAL(said["turnCompleted.streak6"], 1);
  CHECK_EQUAL(said["turnCompleted.streak10"], 1);
  CHECK(said["turnCompleted.onTime"] + said["turnCompleted.credit"] <= 4);
  CHECK(quiet >= 8);
  // A mistake starts the streak over.
  ++st.lateTurns;
  ++st.turnsCompleted;
  inc.push_back(Bad(5000, IncidentKind::LateTurn, "Turn left onto Elm Street"));
  b.OnTimeTurn(st, inc, 0.7);
  CHECK_EQUAL(b.Streak(), 0);
}

// A streak that skipped past a milestone (verdicts held back) gets just the latest line.
TEST(RunningBitsSkippedStreakSaysTheLatest)
{
  RunningBits b;
  b.NewTrip(false);
  TripStats st;
  st.turnsCompleted = 7;
  auto const v = b.OnTimeTurn(st, {}, 0.7);
  CHECK(v.has_value());
  CHECK_EQUAL(v->key, std::string("turnCompleted.streak6"));
}

// "Still speeding" tells one story, a beat at a time, then stops.
TEST(RunningBitsSpeedingStoryRunsOutAndCallsBackAtArrival)
{
  RunningBits b;
  b.NewTrip(false);
  CHECK(!b.StillSpeeding(0));
  b.SetStory("ticket");
  for (size_t i = 0; i < 5; ++i)
  {
    double const t = i * 150000.0;
    auto const beat = b.StillSpeeding(t);
    CHECK(beat.has_value());
    CHECK_EQUAL(*beat->beat, i);
    // Not again right away.
    CHECK(!b.StillSpeeding(t + 30000));
  }
  CHECK(!b.StillSpeeding(1e9));
  auto const end = b.Arrival("C", {}, 0.9);
  CHECK(end.has_value());
  CHECK_EQUAL(end->key, std::string("speeding.end.ticket"));
}

TEST(RunningBitsArrivalBlamesTheTurnOrDoubtsACleanTrip)
{
  RunningBits b;
  b.NewTrip(false);
  std::vector<Incident> inc = {Bad(1000, IncidentKind::MissedTurn, "Turn left onto Elm Tree Road")};
  auto const blame = b.Arrival("B", inc, 0.9);
  CHECK(blame.has_value());
  CHECK_EQUAL(blame->key, std::string("arrived.blame"));
  CHECK_EQUAL(blame->vars.at("street"), std::string("Elm Tree Road"));
  CHECK_EQUAL(blame->vars.at("better"), std::string("B plus"));
  auto const clean = b.Arrival("A", {}, 0.1);
  CHECK(clean.has_value());
  CHECK_EQUAL(clean->key, std::string("arrived.clean"));
  CHECK(!b.Arrival("A", {}, 0.9));
}

// Good driving: suspicion at 5 minutes, worse at 10; a mistake starts the clock over.
TEST(RunningBitsCleanDrivingGetsSuspicious)
{
  RunningBits b;
  b.NewTrip(true);
  std::vector<Incident> inc;
  CHECK(!b.CleanBeatDue(4 * 60000, 0, inc));
  auto const one = b.CleanBeatDue(5 * 60000, 0, inc);
  CHECK(one.has_value());
  CHECK_EQUAL(one->key, std::string("clean.1"));
  CHECK(!b.CleanBeatDue(6 * 60000, 0, inc));
  inc.push_back(Bad(7 * 60000, IncidentKind::HardBraking, ""));
  CHECK(!b.CleanBeatDue(12 * 60000, 0, inc));
  auto const two = b.CleanBeatDue(17 * 60000, 0, inc);
  CHECK(two.has_value());
  CHECK_EQUAL(two->key, std::string("clean.2"));

  RunningBits off;
  off.NewTrip(false);
  CHECK(!off.CleanBeatDue(30 * 60000, 0, {}));
}

// Whole commutes (the same town route ten days running, a driver a bit over the limit): the
// jokes that used to be most of the trip are now a few, and a story beat is never repeated.
TEST(RunningBitsSoakCommuteJokesDontPileUp)
{
  SaidLog said;
  std::vector<TripRecord> history;
  double t0 = 1.7e12;
  int verdicts = 0;
  int speeding = 0;
  int trips = 0;
  std::set<std::string> stories;
  for (int i = 0; i < 10; ++i)
  {
    EngineConfig c;
    c.rng = SeededRng(100 + i);
    // The simulated driver stays within the usual allowance; this is about the stories.
    c.speedingTolerance = 1;
    c.speedingBarelyEveryMs = 1;
    Navigator nav(c, history, [&](TripRecord const & r, TripReport const &) { history.push_back(r); });
    nav.SetSaidLog(said, [&](SaidLog const & s) { said = s; });
    auto const r = SimulateTrip(nav, {TripShape::Town, 7}, t0);
    CHECK(r.arrived);
    std::string story;
    for (auto const & s : r.spoken)
    {
      verdicts += s.line.category == Category::TurnCompleted;
      speeding += s.line.category == Category::Speeding;
      if (s.line.key.rfind("speeding.story.", 0) == 0)
      {
        CHECK(story.empty() || story == s.line.key);
        story = s.line.key;
      }
    }
    if (!story.empty())
      stories.insert(story);
    ++trips;
    t0 = r.endMs + 5 * 3.6e6;
  }
  // It was 7 verdicts and 6 speeding lines a trip.
  CHECK(verdicts <= trips * 3);
  CHECK(speeding <= trips * 4);
  // A different story most days.
  CHECK(stories.size() >= 5);
}

namespace
{
/// Drives `legs` straight legs of `legM` each at 15 m/s, heading from `heading(t)`; every line said.
std::vector<Commentary> DriveLegs(int legs, double legM, std::function<double(double)> const & heading)
{
  EngineConfig c;
  c.rng = SeededRng(5);
  Navigator nav(c);
  std::vector<Commentary> said;
  double t = 0;
  for (int leg = 1; leg <= legs; ++leg)
  {
    for (double left = legM; left > 0; left -= 15)
    {
      RouteSnapshot r;
      r.state = RouteState::OnRoute;
      r.turnId = static_cast<uint32_t>(leg);
      r.turn = TurnKind::Left;
      r.distToTurnM = left;
      r.nextStreet = "Elm Street";
      r.destination = "Grandma's";
      r.etaSeconds = 600;
      r.plannedTurns = {TurnKind::Left, TurnKind::Left, TurnKind::Left, TurnKind::Destination};
      Fix f;
      f.tMs = t;
      f.lat = 39.7;
      f.lon = -105;
      f.speedMps = 15;
      f.bearingDeg = heading(t);
      nav.OnFix(f, r);
      for (auto & l : nav.TakeLines())
        said.push_back(std::move(l));
      t += 1000;
    }
  }
  return said;
}

int CountKey(std::vector<Commentary> const & lines, std::string const & prefix)
{
  return static_cast<int>(std::count_if(lines.begin(), lines.end(),
                                        [&](Commentary const & l) { return l.key.rfind(prefix, 0) == 0; }));
}
}  // namespace

// "Please continue straight." was on nearly every town leg; now Brandon's chain is once a trip.
TEST(StraightNagsAreOnceATripInTown)
{
  auto const lines = DriveLegs(3, 2500, [](double) { return 0.0; });
  int const nags = CountKey(lines, "straight.");
  CHECK(nags >= 1);
  CHECK(nags <= 3);
  // Only the first leg got the chain; it starts with Brandon's line.
  CHECK(std::all_of(lines.begin(), lines.end(), [](Commentary const & l) { return l.key.rfind("straight.", 0) != 0 || l.t < 150000; }));
  auto const first = std::find_if(lines.begin(), lines.end(), [](Commentary const & l) { return l.key == "straight.please"; });
  CHECK(first != lines.end());
  CHECK_EQUAL(first->text, std::string("Please continue straight."));
}

// Through a curve it says to follow the road, not to go straight (Brandon, 2026-10-29).
TEST(StraightNagsFollowTheRoadInACurve)
{
  auto const lines = DriveLegs(1, 2500, [](double t) { return std::fmod(t / 1000 * 3, 60.0); });
  CHECK(CountKey(lines, "straight.curve") >= 1);
  CHECK_EQUAL(CountKey(lines, "straight.please") + CountKey(lines, "straight.still") + CountKey(lines, "straight.serious"), 0);
}

// A winding road gets curve jokes, Brandon's first; a straight one doesn't.
TEST(CurvesGetJokes)
{
  auto const winding = DriveLegs(1, 6000, [](double t) { return 40 * std::sin(t / 8000); });
  int const jokes = CountKey(winding, "curve.bend");
  CHECK(jokes >= 2);
  auto const first = std::find_if(winding.begin(), winding.end(), [](Commentary const & l) { return l.key == "curve.bend"; });
  CHECK(first != winding.end());
  CHECK_EQUAL(first->text, std::string("Hold on tight."));
  CHECK_EQUAL(CountKey(DriveLegs(1, 6000, [](double) { return 0.0; }), "curve.bend"), 0);
}

// A cold morning: Brandon's freezing line early in the trip, the air vent one a few minutes later.
TEST(ColdMorningsWantThePocketThenTheVent)
{
  EngineConfig c;
  c.rng = SeededRng(5);
  Navigator nav(c);
  Weather w;
  w.tempC = -3;
  nav.OnWeather(0, w);
  std::vector<Commentary> said;
  for (double t = 0, left = 9000; left > 0; t += 1000, left -= 15)
  {
    RouteSnapshot r;
    r.state = RouteState::OnRoute;
    r.turnId = 1;
    r.turn = TurnKind::Left;
    r.distToTurnM = left;
    r.destination = "Work";
    r.etaSeconds = 600;
    r.plannedTurns = {TurnKind::Left, TurnKind::Destination};
    Fix f;
    f.tMs = t;
    f.lat = 39.7;
    f.lon = -105;
    f.speedMps = 15;
    f.bearingDeg = 0;
    nav.OnFix(f, r);
    for (auto & l : nav.TakeLines())
      said.push_back(std::move(l));
  }
  auto const at = [&](char const * key) {
    return std::find_if(said.begin(), said.end(), [&](Commentary const & l) { return l.key == key; });
  };
  CHECK(at("cold.start") != said.end());
  CHECK(at("cold.vent") != said.end());
  CHECK_EQUAL(at("cold.start")->text, std::string("It's freezing out here. I need to go in your pocket asap."));
  CHECK(at("cold.vent")->t - at("cold.start")->t >= 180000);
  CHECK(WeatherFromNws("Cloudy", 1, -1, -2.5).tempC == std::optional<double>(-2.5));
  CHECK(!WeatherFromNws("Cloudy", 1, -1).tempC);
}

namespace
{
/// Drives one 9 km straight leg in `nav` from `t0`; every line said.
std::vector<Commentary> DriveFrom(Navigator & nav, double t0, double accelAt = -1, double parkedMs = 0)
{
  std::vector<Commentary> said;
  double speed = 0;
  for (double t = t0, left = 9000; left > 0; t += 1000, left -= speed)
  {
    // Starts from a stop; at `accelAt` floors it from a crawl.
    if (t - t0 < parkedMs)
      speed = 0;
    else if (accelAt >= 0 && t - t0 >= accelAt && t - t0 < accelAt + 4000)
      speed = std::min(20.0, (t - t0 - accelAt) / 1000 * 6);
    else if (accelAt >= 0 && t - t0 < accelAt)
      speed = 2;
    else
      speed = 15;
    RouteSnapshot r;
    r.state = RouteState::OnRoute;
    r.turnId = 1;
    r.turn = TurnKind::Left;
    r.distToTurnM = left;
    r.destination = "Home";
    r.etaSeconds = 600;
    r.plannedTurns = {TurnKind::Left, TurnKind::Destination};
    r.origin = {39.7, -105};
    r.destinationPoint = {39.78, -105};
    Fix f;
    f.tMs = t;
    f.lat = 39.7;
    f.lon = -105;
    f.speedMps = speed;
    f.bearingDeg = 0;
    nav.OnFix(f, r);
    for (auto & l : nav.TakeLines())
      said.push_back(std::move(l));
  }
  return said;
}

Commentary const * FindKey(std::vector<Commentary> const & lines, std::string const & key)
{
  auto const it = std::find_if(lines.begin(), lines.end(), [&](Commentary const & l) { return l.key == key; });
  return it == lines.end() ? nullptr : &*it;
}

EngineConfig Seeded()
{
  EngineConfig c;
  c.rng = SeededRng(5);
  return c;
}
}  // namespace

// Hot out, leaving the grocery store: Brandon's eggs line.
TEST(HotCarCooksTheGroceries)
{
  Navigator nav(Seeded());
  nav.SetPlaceLookup([](LatLon const &) { return std::optional<Place>(Place{PlaceKind::Store, "Walmart", true}); });
  Weather w;
  w.tempC = 35;
  nav.OnWeather(0, w);
  auto const lines = DriveFrom(nav, 1.7e12);
  auto const eggs = FindKey(lines, "hot.groceries");
  CHECK(eggs != nullptr);
  CHECK_EQUAL(eggs->text, std::string("I think the eggs you just bought are hard boiled by now."));

  Navigator plain(Seeded());
  plain.OnWeather(0, w);
  auto const plainLines = DriveFrom(plain, 1.7e12);
  auto const hot = FindKey(plainLines, "hot.start");
  CHECK(hot != nullptr);
  CHECK_EQUAL(hot->text, std::string("It's like a sauna in here."));
}

// The evening after the clocks fall back (US, 2026-11-01), in the dark: Brandon's line, once a day.
TEST(ClocksFallingBackGetNoticed)
{
  double const fallBackMs = 1793520000000.0;  // 2026-11-01 08:00 UTC
  auto const localTime = [=](double ms)
  {
    std::time_t const s = static_cast<std::time_t>(ms / 1000) + (ms < fallBackMs ? -6 : -7) * 3600;
    std::tm tm{};
    gmtime_r(&s, &tm);
    return tm;
  };
  Navigator nav(Seeded());
  nav.SetLocalTime(localTime);
  double const evening = 1793755800000.0;  // 2026-11-04 01:30 UTC = Nov 3, 6:30 pm MST
  auto const evenings = DriveFrom(nav, evening);
  auto const first = FindKey(evenings, "clock.fallBack");
  CHECK(first != nullptr);
  CHECK_EQUAL(first->text, std::string("Wasn't it brighter out at this time yesterday? Maybe it's just me."));
  // Not again the same evening, and not a couple of weeks later.
  CHECK(CountKey(DriveFrom(nav, evening + 3.6e6), "clock.fallBack") == 0);
  CHECK(CountKey(DriveFrom(nav, evening + 14 * 86400000.0), "clock.fallBack") == 0);
}

// Flooring it: Brandon's lines debut first.
TEST(HardAccelerationGetsBrandonsLinesFirst)
{
  Navigator nav(Seeded());
  auto const lines = DriveFrom(nav, 1.7e12, 40000);
  auto const accel = FindKey(lines, "hardAccel");
  CHECK(accel != nullptr);
  CHECK_EQUAL(accel->text, std::string("The brake is the other one."));
}

// Leaving a bar late at night: Brandon's designated driver line. Not at lunch, not from home.
TEST(LeavingTheBarLateGetsTheDesignatedDriver)
{
  auto const bar = [](LatLon const &) { return std::optional<Place>(Place{PlaceKind::Other, "Joe's Pub", false, true}); };
  auto const atHour = [](int hour) { return [=](double) { std::tm tm{}; tm.tm_year = 126; tm.tm_mon = 9; tm.tm_mday = 30; tm.tm_hour = hour; return tm; }; };

  Navigator late(Seeded());
  late.SetPlaceLookup(bar);
  late.SetLocalTime(atHour(23));
  auto const lines = DriveFrom(late, 1.7e12);
  auto const dd = FindKey(lines, "nightOut.dd");
  CHECK(dd != nullptr);
  CHECK_EQUAL(dd->text, std::string("I just wanted to say. I'm really proud of you taking one for the team and being a "
                                    "designated driver. You will get that ankle monitor off. I believe in you."));
  CHECK(CountKey(lines, "nightOut.dd") == 1);
  CHECK(CountKey(lines, "nightOut.tube") == 0);

  // Still sitting in the lot: the breathalyzer bit instead, setup then punchline.
  Navigator parked(Seeded());
  parked.SetPlaceLookup(bar);
  parked.SetLocalTime(atHour(1));
  auto const sitting = DriveFrom(parked, 1.7e12, -1, 60000);
  auto const tube = FindKey(sitting, "nightOut.tube");
  CHECK(tube != nullptr);
  CHECK_EQUAL(tube->text, std::string("Remember, you must blow into the tube to start the car."));
  CHECK(CountKey(sitting, "nightOut.tube") == 2);
  auto const trunk = std::find_if(sitting.begin(), sitting.end(), [&](Commentary const & l) { return l.key == "nightOut.tube" && &l != tube; });
  CHECK_EQUAL(trunk->text, std::string("You might as well climb in the trunk. You are not going anywhere."));
  CHECK(CountKey(sitting, "nightOut.dd") == 0);

  Navigator lunch(Seeded());
  lunch.SetPlaceLookup(bar);
  lunch.SetLocalTime(atHour(13));
  CHECK(CountKey(DriveFrom(lunch, 1.7e12), "nightOut.dd") == 0);

  Navigator home(Seeded());
  home.SetLocalTime(atHour(23));
  CHECK(CountKey(DriveFrom(home, 1.7e12), "nightOut.dd") == 0);
}
