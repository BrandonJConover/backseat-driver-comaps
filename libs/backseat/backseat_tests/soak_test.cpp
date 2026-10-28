// Whole simulated trips (sim_drive.hpp), checked for what single-moment tests can't see.
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/navigator.hpp"

#include <map>

using namespace backseat;
using namespace backseat_tests;

namespace
{
SimResult Drive(TripShape shape, uint32_t seed)
{
  EngineConfig c;
  c.rng = SeededRng(seed);
  Navigator nav(c);
  return SimulateTrip(nav, {shape, seed});
}
}  // namespace

// The plain "Now, ..." call has to be over before the car gets to the turn. On the first drive
// logs and in this sim before the fix, one town turn in eight got it late: a "did I mention", a
// speeding nag or "Nice turn" for the last one was still playing when it was due.
TEST(SoakFinalTurnCallsAreOverBeforeTheTurn)
{
  int turns = 0;
  int late = 0;
  for (uint32_t seed = 1; seed <= 40; ++seed)
  {
    auto const r = Drive(TripShape::Town, seed);
    CHECK(r.arrived);
    for (auto const & m : r.maneuvers)
    {
      if (m.reachedMs < 0 || m.missed || m.turn == TurnKind::Destination)
        continue;
      ++turns;
      CHECK(m.directed);
      if (m.finalEndMs && *m.finalEndMs > m.reachedMs)
        ++late;
    }
  }
  CHECK(turns > 300);
  // Left: back-to-back turns where the second one's direction and its final call meet.
  CHECK(late * 100 <= turns * 3);
}

// Nothing but directions plays from when the final call is due until the car makes the turn.
TEST(SoakRoastsStayOutOfTheTurn)
{
  int turns = 0;
  int crowded = 0;
  for (uint32_t seed = 1; seed <= 40; ++seed)
  {
    auto const r = Drive(TripShape::Town, seed);
    for (auto const & m : r.maneuvers)
    {
      if (m.reachedMs < 0 || m.missed || !m.finalEndMs)
        continue;
      ++turns;
      for (auto const & s : r.spoken)
        if (s.line.priority != Priority::Nav && s.startMs < m.reachedMs && s.endMs > *m.finalEndMs - 2000)
        {
          ++crowded;
          break;
        }
    }
  }
  CHECK(turns > 300);
  CHECK(crowded * 100 <= turns * 3);
}

// An hour on the interstate at 4 over used to be 90 "still speeding" nags, every line in the pool
// twice. Now they back off to one every 5 minutes.
TEST(SoakRoadTripSpeedingNagsBackOff)
{
  double minutes = 0;
  int speeding = 0;
  int repeats = 0;
  for (uint32_t seed = 1; seed <= 6; ++seed)
  {
    auto const r = Drive(TripShape::RoadTrip, seed);
    CHECK(r.arrived);
    minutes += (r.endMs - r.spoken.front().queuedMs) / 60000;
    std::map<std::string, int> said;
    for (auto const & s : r.spoken)
    {
      if (s.line.category == Category::Speeding)
        ++speeding;
      if (s.line.priority != Priority::Nav && s.line.category != Category::PhoneCall && ++said[s.line.text] == 2)
        ++repeats;
    }
  }
  CHECK(minutes > 180);
  CHECK(speeding / (minutes / 60) < 25);
  CHECK(repeats <= 6);
}

// Every turn on a road trip still gets its direction, quiet bits and all.
TEST(SoakRoadTripTurnsAreAllDirected)
{
  for (uint32_t seed = 1; seed <= 6; ++seed)
  {
    auto const r = Drive(TripShape::RoadTrip, seed);
    for (auto const & m : r.maneuvers)
      if (m.reachedMs >= 0 && !m.missed)
        CHECK(m.directed);
  }
}

// After the first trip ever, the opening lines (greeting, rundown) never push the first turn's
// final call past the turn: a first turn that's right there is said before the greeting.
TEST(SoakTripStartsDontBuryTheFirstTurn)
{
  int late = 0;
  int trips = 0;
  for (uint32_t seed = 1; seed <= 60; ++seed)
  {
    EngineConfig c;
    c.rng = SeededRng(seed);
    Navigator nav(c);
    SimulateTrip(nav, {TripShape::Town, seed + 1000});
    auto const r = SimulateTrip(nav, {TripShape::Town, seed}, 1.7e12 + 86400000.0);
    auto const & first = r.maneuvers.front();
    if (first.reachedMs < 0 || first.missed || first.turn == TurnKind::Destination)
      continue;
    ++trips;
    CHECK(first.directed);
    if (first.finalEndMs && *first.finalEndMs > first.reachedMs)
      ++late;
  }
  CHECK(trips > 40);
  CHECK(late * 100 <= trips * 8);
}
