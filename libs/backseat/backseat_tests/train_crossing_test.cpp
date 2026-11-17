// Loop B130 (BI-111): stopped at a railway crossing for 20 s gets one "wait.train" line per stop,
// never while moving, nothing without a crossing lookup, and the crossing explains the stop (no
// pull-over line). No line may suggest beating or racing the train.
#include "check.hpp"

#include "backseat/lines_data.hpp"
#include "backseat/navigator.hpp"
#include "backseat/units.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
EngineConfig Config()
{
  EngineConfig c;
  c.rng = SeededRng(7);
  return c;
}

RouteSnapshot Far()
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Left;
  r.distToTurnM = 2000;
  r.nextStreet = "Elm Street";
  r.destination = "Grandma's";
  r.etaSeconds = 600;
  r.plannedTurns = {TurnKind::Left, TurnKind::Right, TurnKind::Destination};
  r.destinationPoint = {39.8, -105};
  return r;
}

Fix At(double tMs, double mps, double lat)
{
  Fix f;
  f.tMs = tMs;
  f.lat = lat;
  f.lon = -105;
  f.speedMps = mps;
  f.speedLimitMps = FromDisplay(40, Units::Mph);
  return f;
}

int Count(std::vector<Commentary> const & lines, std::string const & key)
{
  return static_cast<int>(std::count_if(lines.begin(), lines.end(), [&](Commentary const & c) { return c.key == key; }));
}

/// Drives 30 s, then stops for `stopS` seconds, then (optionally) drives off for 30 s.
std::vector<Commentary> DriveStop(Navigator & nav, int stopS, bool driveOff = false, int startS = 0)
{
  std::vector<Commentary> said;
  auto take = [&] { for (auto & l : nav.TakeLines()) said.push_back(std::move(l)); };
  int t = startS;
  double lat = 39.7 + startS * 0.0001;
  for (; t <= startS + 30; ++t, lat += 0.0001)
    nav.OnFix(At(t * 1000.0, 11, lat), Far());
  take();
  int const stopEnd = t + stopS;
  for (; t < stopEnd; ++t)
    nav.OnFix(At(t * 1000.0, 0, lat), Far());
  if (driveOff)
    for (int end = t + 30; t < end; ++t, lat += 0.0001)
      nav.OnFix(At(t * 1000.0, 11, lat), Far());
  take();
  return said;
}
}  // namespace

TEST(TrainCrossingWaitGetsOneLine)
{
  Navigator nav(Config());
  nav.SetLevelCrossingLookup([](LatLon const &) { return true; });
  auto const said = DriveStop(nav, 25);
  CHECK_EQUAL(Count(said, "wait.train"), 1);
}

TEST(TrainCrossingWaitsTwentySeconds)
{
  Navigator nav(Config());
  nav.SetLevelCrossingLookup([](LatLon const &) { return true; });
  CHECK_EQUAL(Count(DriveStop(nav, 15, true), "wait.train"), 0);
}

TEST(TrainCrossingOncePerStopEvenWhenLong)
{
  Navigator nav(Config());
  nav.SetLevelCrossingLookup([](LatLon const &) { return true; });
  auto const said = DriveStop(nav, 240);
  CHECK_EQUAL(Count(said, "wait.train"), 1);
  // The crossing explains the stop: no "did we break down?".
  CHECK_EQUAL(Count(said, "pullOver"), 0);
  CHECK_EQUAL(Count(said, "pullOver.limit"), 0);
}

TEST(TrainCrossingNotAtARedLight)
{
  Navigator nav(Config());
  nav.SetTrafficLightLookup([](LatLon const &) { return true; });
  nav.SetLevelCrossingLookup([](LatLon const &) { return false; });
  CHECK_EQUAL(Count(DriveStop(nav, 60), "wait.train"), 0);
}

TEST(TrainCrossingNothingWithoutLookup)
{
  Navigator nav(Config());
  CHECK_EQUAL(Count(DriveStop(nav, 60), "wait.train"), 0);
}

TEST(TrainCrossingNothingWhileMoving)
{
  Navigator nav(Config());
  int asked = 0;
  nav.SetLevelCrossingLookup([&](LatLon const &) { ++asked; return true; });
  std::vector<Commentary> said;
  double lat = 39.7;
  for (int t = 0; t <= 300; ++t, lat += 0.0001)
  {
    nav.OnFix(At(t * 1000.0, 11, lat), Far());
    for (auto & l : nav.TakeLines())
      said.push_back(std::move(l));
  }
  CHECK_EQUAL(Count(said, "wait.train"), 0);
  CHECK_EQUAL(asked, 0);
}

TEST(TrainCrossingNotOnFootOrDirectionsOnly)
{
  Navigator walk(Config());
  walk.SetTravelMode(TravelMode::Walk);
  walk.SetLevelCrossingLookup([](LatLon const &) { return true; });
  CHECK_EQUAL(Count(DriveStop(walk, 60), "wait.train"), 0);

  Navigator quiet(Config());
  quiet.SetTalkLevel(TalkLevel::DirectionsOnly);
  quiet.SetLevelCrossingLookup([](LatLon const &) { return true; });
  CHECK_EQUAL(Count(DriveStop(quiet, 60), "wait.train"), 0);
}

TEST(TrainCrossingNextStopGetsItsOwnLine)
{
  Navigator nav(Config());
  nav.SetLevelCrossingLookup([](LatLon const &) { return true; });
  auto said = DriveStop(nav, 25, true);
  auto more = DriveStop(nav, 25, false, 86);  // right after the first one
  said.insert(said.end(), more.begin(), more.end());
  CHECK_EQUAL(Count(said, "wait.train"), 2);
}

TEST(TrainLinesNeverTemptTheDriver)
{
  auto const it = LineData().find("wait.train");
  CHECK(it != LineData().end());
  int n = 0;
  for (auto const & tier : it->second)
  {
    CHECK(!tier.empty());
    for (auto line : tier)
    {
      ++n;
      std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) { return std::tolower(c); });
      for (char const * bad : {"beat", "race", "racing", "before the train", "gate", "go around", "hurry", "floor it",
                               "make it across", "screen", "look at"})
        CHECK(line.find(bad) == std::string::npos);
    }
  }
  CHECK(n >= 8);
}
