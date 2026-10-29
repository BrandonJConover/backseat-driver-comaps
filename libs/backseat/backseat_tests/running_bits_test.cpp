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
