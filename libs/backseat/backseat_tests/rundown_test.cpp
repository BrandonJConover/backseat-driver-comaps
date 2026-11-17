// Direct tests for rundown.cpp: the trip greeting and the condescending route preview
// (fallback:tests, loop B32). The parity trace covers seeded happy paths; these pin each style,
// the turn-reading, the caps, grammar and bad random values.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/history.hpp"
#include "backseat/rundown.hpp"

#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
using Kind = RouteStep::Kind;

Rng Fixed(double r)
{
  return [r] { return r; };
}

bool Has(std::string const & text, std::string const & part) { return text.find(part) != std::string::npos; }

std::vector<RouteStep> Turns(std::vector<Kind> const & kinds)
{
  std::vector<RouteStep> steps;
  for (auto k : kinds)
    steps.push_back({"Turn", k});
  return steps;
}

std::vector<TripRecord> Trips(size_t n, int missedTurns = 0, std::string const & grade = "B")
{
  std::vector<TripRecord> trips(n);
  for (auto & t : trips)
  {
    t.missedTurns = missedTurns;
    t.grade = grade;
  }
  return trips;
}

// Styles rotate by trips started before this one: 1 Quiz, 2 FirstOnly, 3 Count, 4 Skip, 5 Tally, 6 Classic.
size_t constexpr kQuiz = 1, kFirstOnly = 2, kCount = 3, kSkip = 4, kTally = 5, kClassic = 6;
}  // namespace

TEST(RundownFirstTripRattlesOffEveryTurn)
{
  auto const said = RouteRundown(Turns({Kind::Left, Kind::Other, Kind::Right, Kind::UTurn}), {}, Fixed(0.5));
  CHECK(Has(said, "Okay, real quick. Left, right, U-turn."));
  // Brandon's music line closes the first trip.
  CHECK(Has(said, "why don't we put on some music."));
  CHECK(Has(RouteRundown({}, {}, Fixed(0.5)), "It's a straight line. No turns."));
}

TEST(RundownFirstTripCapsAtTwelve)
{
  std::vector<Kind> kinds(15, Kind::Right);
  auto const said = RouteRundown(Turns(kinds), {}, Fixed(0.5));
  CHECK(Has(said, "And 3 more after that."));
  size_t rights = 0;
  for (size_t p = said.find("ight"); p != std::string::npos; p = said.find("ight", p + 1))
    ++rights;
  CHECK_EQUAL(rights, size_t{12});
}

TEST(RundownReadsTurnsFromTextWhenRoutingDoesNot)
{
  std::vector<RouteStep> steps = {{"Make a U-turn at Main St", std::nullopt},
                                  {"Turn left onto Elm Street", std::nullopt},
                                  {"Continue on I-35", std::nullopt},
                                  {"Turn right onto Oak Ave", std::nullopt}};
  CHECK(Has(RouteRundown(steps, {}, Fixed(0.5)), "U-turn, left, right."));
  // Routing's own maneuver beats the text.
  std::vector<RouteStep> overridden = {{"Turn left onto Elm Street", Kind::Right}};
  CHECK(Has(RouteRundown(overridden, {}, Fixed(0.5)), "Okay, real quick. Right."));
}

TEST(RundownReturningDriverStylesRotate)
{
  auto const steps = Turns({Kind::Left, Kind::Right, Kind::Left});
  auto const h = Trips(1);
  CHECK(Has(RouteRundown(steps, h, Fixed(0.5), kQuiz), "It's a left. You were going to say right."));
  CHECK(Has(RouteRundown(steps, h, Fixed(0.5), kFirstOnly), "First turn's a left."));
  CHECK(Has(RouteRundown(steps, h, Fixed(0.5), kCount), "3 turns today."));
  CHECK(Has(RouteRundown(steps, h, Fixed(0.0), kSkip), "No rundown today."));
  CHECK(Has(RouteRundown(steps, h, Fixed(0.5), kTally), "2 lefts and 1 right today."));
  CHECK(Has(RouteRundown(steps, h, Fixed(0.5), kClassic), "First you're going to turn left, then right, and left."));
  // The cycle repeats.
  CHECK_EQUAL(RouteRundown(steps, h, Fixed(0.5), kQuiz + 6), RouteRundown(steps, h, Fixed(0.5), kQuiz));
  // tripsBefore defaults to the finished trips in history.
  CHECK_EQUAL(RouteRundown(steps, Trips(kCount), Fixed(0.5)), RouteRundown(steps, h, Fixed(0.5), kCount));
}

TEST(RundownStyleEdges)
{
  auto const h = Trips(1);
  CHECK(Has(RouteRundown(Turns({Kind::UTurn}), h, Fixed(0.5), kQuiz), "It's a U-turn."));
  CHECK(Has(RouteRundown(Turns({Kind::Left}), h, Fixed(0.5), kCount), "One turn today. One."));
  // Tally with only one direction falls back to the count.
  CHECK(Has(RouteRundown(Turns({Kind::Left, Kind::Left}), h, Fixed(0.5), kTally), "2 turns today."));
  CHECK(Has(RouteRundown({}, h, Fixed(0.5), kClassic), "it's a straight line"));
}

TEST(RundownClassicGrammar)
{
  auto const h = Trips(1);
  // A single turn.
  CHECK(Has(RouteRundown(Turns({Kind::Right}), h, Fixed(0.5), kClassic),
            "You're going to turn right. One turn. That's the whole trip."));
  // Streaks.
  CHECK(Has(RouteRundown(Turns({Kind::Left, Kind::Left, Kind::Left, Kind::Right}), h, Fixed(0.5), kClassic),
            "turn left, then left again, then yes, left again, and right."));
  // A leading U-turn reads "going to make a U-turn", not "going to a U-turn".
  auto const u = RouteRundown(Turns({Kind::UTurn, Kind::Left}), h, Fixed(0.5), kClassic);
  CHECK(Has(u, "First you're going to make a U-turn, don't ask, and left."));
  CHECK(Has(RouteRundown(Turns({Kind::UTurn}), h, Fixed(0.5), kClassic), "You're going to make a U-turn, don't ask."));
  CHECK(Has(RouteRundown(Turns({Kind::Left, Kind::UTurn, Kind::UTurn}), h, Fixed(0.5), kClassic),
            "then a U-turn, don't ask, and another U-turn, don't ask."));
}

TEST(RundownClassicGivesUpPastSeven)
{
  std::vector<Kind> kinds;
  for (int i = 0; i < 8; ++i)
    kinds.push_back(i % 2 ? Kind::Right : Kind::Left);
  // Eight turns: one left over, said in the singular.
  auto const eight = RouteRundown(Turns(kinds), Trips(1), Fixed(0.5), kClassic + 6);
  CHECK(Has(eight, "and then 1 more turn that I'll explain"));
  CHECK(!Has(eight, "1 more turns"));
  kinds.push_back(Kind::Left);
  auto const nine = RouteRundown(Turns(kinds), Trips(3), Fixed(0.5), kClassic);
  CHECK(Has(nine, "and then 2 more turns that I'll explain as we go, because I've met you."));
  // The full list only comes round from the sixth trip on, so it always has "met you" by then.
  CHECK(Has(RouteRundown(Turns(kinds), Trips(1), Fixed(0.5), kClassic), "because I've met you."));
}

TEST(RundownLeftReminderOnlyAfterRecentMissedTurns)
{
  auto const steps = Turns({Kind::Left, Kind::Right});
  std::string const reminder = "Reminder: left is the side";
  CHECK(!Has(RouteRundown(steps, Trips(6), Fixed(0.5), kClassic), reminder));
  auto missed = Trips(6);
  missed.back().missedTurns = 1;
  CHECK(Has(RouteRundown(steps, missed, Fixed(0.5), kClassic), reminder));
  // Only the last five trips count.
  auto old = Trips(6);
  old.front().missedTurns = 2;
  CHECK(!Has(RouteRundown(steps, old, Fixed(0.5), kClassic), reminder));
  // No lefts on this route: nothing to remind.
  CHECK(!Has(RouteRundown(Turns({Kind::Right, Kind::Right}), missed, Fixed(0.5), kClassic), reminder));
}

TEST(RundownSurvivesBadRandomValues)
{
  auto const steps = Turns({Kind::Left, Kind::Right});
  auto const h = Trips(1);
  double const nan = std::numeric_limits<double>::quiet_NaN();
  for (double r : {0.0, 0.999999, 1.0, 1.5, -0.5, nan, std::numeric_limits<double>::infinity()})
  {
    for (size_t before : {kSkip, kClassic})
    {
      auto const said = RouteRundown(steps, h, Fixed(r), before);
      CHECK(!said.empty());
      CHECK(!Has(said, "{"));
    }
  }
  // 1.0 or more is the last line, NaN or negative the first.
  CHECK_EQUAL(RouteRundown(steps, h, Fixed(1.0), kSkip), RouteRundown(steps, h, Fixed(0.99), kSkip));
  CHECK_EQUAL(RouteRundown(steps, h, Fixed(nan), kSkip), RouteRundown(steps, h, Fixed(0.0), kSkip));
}

TEST(GreetingFirstTripAndRotation)
{
  CHECK(Has(TripGreeting({}, Fixed(0.5)), "I didn't ask to be in this car."));
  auto const h = Trips(2, 0, "C");
  std::set<std::string> seen;
  for (size_t before = 1; before <= 8; ++before)
  {
    auto const line = TripGreeting(h, Fixed(0.5), before);
    CHECK(!Has(line, "{"));
    seen.insert(line);
  }
  CHECK_EQUAL(seen.size(), size_t{8});  // eight greetings, taken in turn
  CHECK_EQUAL(TripGreeting(h, Fixed(0.5), 9), TripGreeting(h, Fixed(0.5), 1));
  CHECK(Has(TripGreeting(h, Fixed(0.5), 3), "Trip number 4 together."));
  CHECK(Has(TripGreeting(h, Fixed(0.5), 4), "Grade: " + SpokenGrade("C") + "."));
}

TEST(GreetingSkipsGradeLinesWithNoFinishedTrip)
{
  // Trips started but none finished: no grade to quote, never a leftover "{grade}".
  for (size_t before = 1; before <= 16; ++before)
  {
    auto const line = TripGreeting({}, Fixed(0.5), before);
    CHECK(!Has(line, "{"));
    CHECK(!Has(line, "Grade"));
    CHECK(!Has(line, "earned you"));
  }
  CHECK_EQUAL(TripGreeting({}, Fixed(0.5), 4), TripGreeting({}, Fixed(0.5), 5));
}
