// Whole simulated trips (sim_drive.hpp), checked for what single-moment tests can't see.
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
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

// Every line that reaches the voice is filled in: no "{street}" read out as "curly brace street",
// nothing empty. Town and road trips, by car (miles and km), on foot and by bike.
TEST(SoakNoUnfilledPlaceholdersInSpokenLines)
{
  // Navigators set the process-wide line filters; put them back for the next test (loop B83).
  struct FiltersBack { ~FiltersBack() { SetLineFilters({}); } } filtersBack;
  struct Setup
  {
    TripShape shape;
    TravelMode mode;
    Units units;
  };
  Setup const setups[] = {
      {TripShape::Town, TravelMode::Drive, Units::Mph},     {TripShape::RoadTrip, TravelMode::Drive, Units::Mph},
      {TripShape::Town, TravelMode::Drive, Units::Kmh},     {TripShape::RoadTrip, TravelMode::Drive, Units::Kmh},
      {TripShape::Town, TravelMode::Walk, Units::Mph},      {TripShape::Town, TravelMode::Bike, Units::Mph},
      {TripShape::Town, TravelMode::Walk, Units::Kmh},      {TripShape::Town, TravelMode::Bike, Units::Kmh},
  };
  // The check below relies on Fill() leaving a placeholder it has no value for in the text.
  CHECK(Fill("Turn onto {street}.", {}).find("{street}") != std::string::npos);
  int lines = 0;
  int bad = 0;
  for (auto const & su : setups)
  {
    int const before = lines;
    uint32_t const seeds = su.shape == TripShape::RoadTrip ? 6 : 30;
    for (uint32_t seed = 1; seed <= seeds; ++seed)
    {
      EngineConfig c;
      c.rng = SeededRng(seed);
      Navigator nav(c);
      nav.SetUnits(su.units);
      nav.SetTravelMode(su.mode);
      auto const r = SimulateTrip(nav, {su.shape, seed});
      for (auto const & s : r.spoken)
      {
        ++lines;
        auto const & text = s.line.text;
        if (text.find_first_not_of(" \t\n") == std::string::npos || text.find_first_of("{}") != std::string::npos)
        {
          if (++bad <= 10)
            std::fprintf(stderr, "  unfilled [%s]: %s\n", s.line.key.c_str(), text.c_str());
        }
      }
    }
    CHECK(lines - before > 100);
  }
  CHECK(lines > 5000);
  CHECK(bad == 0);
}

namespace
{
bool HasNonFiniteWord(std::string text)
{
  for (auto & c : text)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (char const * w : {"nan", "inf"})
    for (size_t at = text.find(w); at != std::string::npos; at = text.find(w, at + 1))
    {
      size_t const end = at + std::strlen(w);
      bool const startOk = at == 0 || !std::isalpha(static_cast<unsigned char>(text[at - 1]));
      bool const endOk = end == text.size() || !std::isalpha(static_cast<unsigned char>(text[end]));
      if (startOk && endOk)
        return true;
    }
  return false;
}

// The driving judgements a broken fix could fake: a hard stop or launch, leaving the road, going the
// wrong way, a U-turn.
bool IsJudgement(std::string const & key)
{
  for (char const * k : {"hardBraking", "hardAccel", "offroad.enter", "wrongWay", "uturn"})
    if (key.rfind(k, 0) == 0)
      return true;
  return false;
}
}  // namespace

// GPS stacks hand over junk now and then: NaN or infinite fields, bearings past 360, stale or repeated
// timestamps, absurd accuracy. None of it may crash the trip, reach the voice as "nan"/"inf", or fake a
// judgement the same trip without the junk doesn't make.
TEST(SoakHostileFixesAreHarmless)
{
  // Navigators set the process-wide line filters; put them back for the next test (loop B83).
  struct FiltersBack { ~FiltersBack() { SetLineFilters({}); } } filtersBack;
  int lines = 0;
  int bad = 0;
  for (TravelMode const mode : {TravelMode::Drive, TravelMode::Walk})
    for (TripShape const shape : {TripShape::Town, TripShape::RoadTrip})
    {
      if (mode == TravelMode::Walk && shape == TripShape::RoadTrip)
        continue;
      uint32_t const seeds = shape == TripShape::RoadTrip ? 4 : 20;
      for (uint32_t seed = 1; seed <= seeds; ++seed)
      {
        auto run = [&](bool hostile, TripReport & report)
        {
          EngineConfig c;
          c.rng = SeededRng(seed);
          Navigator nav(c);
          nav.SetTravelMode(mode);
          SimOptions o{shape, seed};
          o.missTurns = false;
          o.hostileFixes = hostile;
          auto r = SimulateTrip(nav, o);
          if (nav.Engine())
            report = nav.Engine()->Report();
          return r;
        };
        TripReport cleanReport, hostileReport;
        auto const clean = run(false, cleanReport);
        auto const hostile = run(true, hostileReport);
        CHECK(hostile.arrived);
        int cleanJudged = 0, hostileJudged = 0;
        for (auto const & s : clean.spoken)
          cleanJudged += IsJudgement(s.line.key);
        for (auto const & s : hostile.spoken)
        {
          ++lines;
          hostileJudged += IsJudgement(s.line.key);
          if (HasNonFiniteWord(s.line.text) || s.line.text.find_first_not_of(" \t\n") == std::string::npos)
          {
            if (++bad <= 10)
              std::fprintf(stderr, "  junk [%s]: %s\n", s.line.key.c_str(), s.line.text.c_str());
          }
          else if (IsJudgement(s.line.key) && hostileJudged > cleanJudged)
          {
            if (++bad <= 10)
              std::fprintf(stderr, "  faked [%s] seed %u: %s\n", s.line.key.c_str(), seed, s.line.text.c_str());
          }
        }
        CHECK(hostileReport.hardBrakes == cleanReport.hardBrakes);
        CHECK(hostileReport.hardAccels == cleanReport.hardAccels);
        CHECK(hostileReport.wrongWay == cleanReport.wrongWay);
        if (hostileReport.hardBrakes != cleanReport.hardBrakes || hostileReport.hardAccels != cleanReport.hardAccels)
          std::fprintf(stderr, "  seed %u brakes %d/%d accels %d/%d\n", seed, cleanReport.hardBrakes,
                       hostileReport.hardBrakes, cleanReport.hardAccels, hostileReport.hardAccels);
      }
    }
  CHECK(lines > 1000);
  CHECK(bad == 0);
}

namespace
{
SimResult DriveTalking(TripShape shape, uint32_t seed, TalkLevel level)
{
  EngineConfig c;
  c.rng = SeededRng(seed);
  Navigator nav(c);
  nav.SetPopQuizOn(true);
  nav.SetTalkLevel(level);
  auto r = SimulateTrip(nav, {shape, seed});
  CHECK(nav.GetTalkLevel() == level);
  return r;
}

/// Remarks the driver could have done without: not directions, and not a beat of a game or bit
/// (those go whole or not at all).
int Remarks(SimResult const & r)
{
  return static_cast<int>(std::count_if(r.spoken.begin(), r.spoken.end(), [](Spoken const & s)
  {
    switch (s.line.category)
    {
    case Category::PlateGame:
    case Category::PhoneCall:
    case Category::Quiet:
    case Category::TwentyQuestions:
    case Category::PopQuiz:
    case Category::Bossy:
    case Category::Thanked:
    case Category::Title: return false;
    default: return s.line.priority != Priority::Nav;
    }
  }));
}
}  // namespace

// "Shut up" keeps the turns (scout 2026-11-06: it used to mute the directions too). In
// directions-only every maneuver still gets its call and nothing else is said: no bits, games,
// idle remarks or badges.
TEST(SoakDirectionsOnlySaysEveryTurnAndNothingElse)
{
  int turns = 0;
  for (uint32_t seed = 1; seed <= 12; ++seed)
    for (auto const shape : {TripShape::Town, TripShape::RoadTrip})
    {
      auto const r = DriveTalking(shape, seed, TalkLevel::DirectionsOnly);
      CHECK(r.arrived);
      for (auto const & m : r.maneuvers)
      {
        if (m.reachedMs < 0 || m.missed || m.turn == TurnKind::Destination)
          continue;
        ++turns;
        CHECK(m.directed);
      }
      for (auto const & s : r.spoken)
      {
        bool const plain = s.line.priority == Priority::Nav && s.line.category != Category::Achievement;
        if (!plain)
          std::printf("  seed %u said %s: %s\n", seed, ToString(s.line.category), s.line.text.c_str());
        CHECK(plain);
      }
      CHECK(std::any_of(r.spoken.begin(), r.spoken.end(),
                        [](Spoken const & s) { return s.line.category == Category::Arrived; }));
    }
  CHECK(turns > 100);
}

// "Talk less": about half the remarks, every turn still called.
TEST(SoakTalkLessHalvesTheRemarks)
{
  int full = 0, less = 0;
  for (uint32_t seed = 1; seed <= 12; ++seed)
    for (auto const shape : {TripShape::Town, TripShape::RoadTrip})
    {
      full += Remarks(DriveTalking(shape, seed, TalkLevel::Full));
      auto const r = DriveTalking(shape, seed, TalkLevel::Less);
      CHECK(r.arrived);
      less += Remarks(r);
      for (auto const & m : r.maneuvers)
        if (m.reachedMs >= 0 && !m.missed && m.turn != TurnKind::Destination)
          CHECK(m.directed);
    }
  std::printf("  remarks: full %d, less %d\n", full, less);
  CHECK(full > 100);
  CHECK(less > 0);
  CHECK(less * 100 <= full * 70);
}

// Back to Full, and a hush mid-quiz ends the quiz rather than leaving it hanging in silence.
TEST(TalkLevelSwitchesBackAndEndsTheQuiz)
{
  EngineConfig c;
  c.rng = SeededRng(3);
  Navigator nav(c);
  CHECK(nav.GetTalkLevel() == TalkLevel::Full);
  nav.SetTalkLevel(TalkLevel::DirectionsOnly);
  CHECK(!nav.PopQuizCard());
  nav.SetTalkLevel(TalkLevel::Full);
  CHECK(nav.GetTalkLevel() == TalkLevel::Full);
}
