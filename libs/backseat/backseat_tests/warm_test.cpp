// backseat::Warm pays the one-time setup (line table, its metric / left-hand copy, every lazily built
// ForSpeech pattern) before the drive, so the first spoken line doesn't. Loop B66, cloud box,
// fresh process (BACKSEAT_TEST_ONLY=Warm): first ForSpeech ~1.9 ms then ~0.08 ms, first default
// LinesFor ~2.5 ms, first metric + left-hand LinesFor ~6 ms more; worst first line over the 515
// golden inputs 1.9 ms cold vs 0.1 ms after Warm. Warm itself ~11 ms, meant for a background thread.
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/pronunciation.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// One line per kind of pass, the ones a first drive is likely to start with.
std::vector<std::string> const kFirstLines = {
    "Turn left onto I-35 at 5pm.",
    "Arriving at 1600 Pennsylvania Ave NW",
    "It's -5°F out and gas is $3.49/gal.",
    "Open Mon-Fri 9am-5pm, about 3–5 min away.",
    "Built in 1868, closed 11/6/2026.",
    "King George VI Ave to St. John's Svc Rd.",
    "Check www.example.com later.",
    "Merge onto US-101 N in 1/2 mi.",
};
}  // namespace

TEST(WarmLeavesWhatTheDriverHearsAlone)
{
  SetLineMetric(false);
  SetLineDriveSide(false);
  std::vector<std::string> before;
  for (auto const & line : kFirstLines)
    before.push_back(pronunciation::ForSpeech(line));
  auto const lines = LinesFor("turnCompleted.onTime", 0);

  Warm(true, true);
  Warm(false, false);

  // Same words out, and the driver's settings untouched: still imperial, right-hand lines.
  for (size_t i = 0; i < kFirstLines.size(); ++i)
    CHECK_EQUAL(pronunciation::ForSpeech(kFirstLines[i]), before[i]);
  CHECK(LinesFor("turnCompleted.onTime", 0) == lines);

  // The variant it warmed is the one the driver gets once they say so.
  SetLineMetric(true);
  SetLineDriveSide(true);
  CHECK(!LinesFor("turnCompleted.onTime", 0).empty());
  SetLineMetric(false);
  SetLineDriveSide(false);
}

TEST(WarmPaysTheColdFirstLine)
{
  // Run alone (BACKSEAT_TEST_ONLY=WarmPays) to see the real cold numbers; in the full suite earlier
  // tests already warmed everything and these just stay fast.
  auto start = Clock::now();
  Warm(true, true);
  double const warmMs = MsSince(start);

  double worstMs = 0;
  double totalMs = 0;
  for (auto const & line : kFirstLines)
  {
    start = Clock::now();
    CHECK(!pronunciation::ForSpeech(line).empty());
    double const ms = MsSince(start);
    totalMs += ms;
    worstMs = std::max(worstMs, ms);
  }
  SetLineMetric(true);
  SetLineDriveSide(true);
  start = Clock::now();
  CHECK(!LinesFor("turnCompleted.onTime", 0).empty());
  double const linesMs = MsSince(start);
  SetLineMetric(false);
  SetLineDriveSide(false);

  std::cout << "  warm " << warmMs << " ms; then " << kFirstLines.size() << " first lines " << totalMs
            << " ms (worst " << worstMs << " ms), metric+left LinesFor " << linesMs << " ms\n";
  // Generous so debug and sanitizer builds pass; release is ~0.1 ms a line and ~0 for LinesFor.
  CHECK(worstMs < 50);
  CHECK(linesMs < 50);
}
