// Brandon (2026-11-14): "It doesn't always register that I'm moving. It is a bit delayed at giving
// instructions at times because it hasn't registered that I've moved from my initial coordinates."
//
// - With one-tap Go the trip started on a fix the route hadn't matched yet (no first turn), so the
//   greeting and the rundown went out first and the first direction queued behind them a fix later.
//   The trip now waits a few seconds for the route to find the car.
// - Fixes with no GPS speed (Wi-Fi or a weak signal at the start of a trip) read as parked: "Still
//   parked" played while pulling away, and the first "Now, turn" was timed for a standing car.
#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/lines.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
double constexpr kDay = 86400000;
double constexpr kDegPerM = 1.0 / 111320;

EngineConfig Seeded()
{
  EngineConfig c;
  c.rng = SeededRng(7);
  return c;
}

RouteSnapshot Route(RouteState state, std::optional<uint32_t> turnId, TurnKind turn, double dist,
                    std::string street = "Oak Avenue")
{
  RouteSnapshot r;
  r.state = state;
  r.turnId = turnId;
  r.turn = turn;
  r.distToTurnM = dist;
  r.nextStreet = std::move(street);
  r.destination = "Grandma's";
  r.etaSeconds = 900;
  r.plannedTurns = {TurnKind::Right, TurnKind::Left, TurnKind::Left, TurnKind::Destination};
  return r;
}

RouteSnapshot NotMatched()
{
  RouteSnapshot r = Route(RouteState::NotStarted, std::nullopt, TurnKind::None, 0);
  r.nextStreet.clear();
  return r;
}

Fix FixAt(double tMs, double lat, double mps, double accuracyM = 10)
{
  Fix f;
  f.tMs = tMs;
  f.lat = lat;
  f.lon = -94.3;
  f.speedMps = mps;
  f.accuracyM = accuracyM;
  return f;
}

/// One whole trip first, so the next one isn't the first ever (that one always gets the speed-run rundown).
void FirstTripDone(Navigator & nav)
{
  nav.OnFix(FixAt(0, 36.39, 10), Route(RouteState::OnRoute, 1, TurnKind::Left, 900));
  RouteSnapshot done = Route(RouteState::OnRoute, 1, TurnKind::Left, 900);
  done.state = RouteState::Finished;
  nav.OnFix(FixAt(60000, 36.39, 0), done);
  nav.OnFix(FixAt(70000, 36.39, 0), RouteSnapshot{});
  nav.TakeLines();
}

size_t IndexOf(std::vector<Commentary> const & lines, std::string const & text)
{
  for (size_t i = 0; i < lines.size(); ++i)
    if (lines[i].text.find(text) != std::string::npos)
      return i;
  return lines.size();
}

bool HasKey(std::vector<Commentary> const & lines, std::string const & prefix)
{
  return std::any_of(lines.begin(), lines.end(), [&](Commentary const & l) { return l.key.rfind(prefix, 0) == 0; });
}
}  // namespace

TEST(TheFirstDirectionDoesntWaitBehindTheGreetingWhileTheRouteFindsTheCar)
{
  Navigator nav(Seeded());
  FirstTripDone(nav);
  // One-tap Go: the first fix reaches us before the route has matched it, so there's no turn yet.
  nav.OnFix(FixAt(kDay, 36.39, 0), NotMatched());
  CHECK(!nav.InTrip());
  CHECK(nav.TakeLines().empty());
  // A second later it has: the turn is 120 m away and goes first, ahead of the greeting, with no rundown.
  nav.OnFix(FixAt(kDay + 1000, 36.39, 2), Route(RouteState::OnRoute, 5, TurnKind::Right, 120));
  CHECK(nav.InTrip());
  auto const lines = nav.TakeLines();
  CHECK(!lines.empty());
  CHECK_EQUAL(IndexOf(lines, "turn right onto Oak Avenue"), size_t{0});
  CHECK(std::count_if(lines.begin(), lines.end(), [](Commentary const & l) { return l.priority == Priority::Nav; }) <= 2);
}

TEST(ARouteThatNeverMatchesStillStartsTheTrip)
{
  Navigator nav(Seeded());
  FirstTripDone(nav);
  nav.OnFix(FixAt(kDay, 36.39, 0), NotMatched());
  nav.OnFix(FixAt(kDay + 2000, 36.39, 0), NotMatched());
  CHECK(!nav.InTrip());
  nav.OnFix(FixAt(kDay + 5000, 36.39, 0), NotMatched());
  CHECK(nav.InTrip());
  CHECK(!nav.TakeLines().empty());
}

TEST(AStartedRouteNeedsNoWait)
{
  Navigator nav(Seeded());
  nav.OnFix(FixAt(0, 36.39, 0), Route(RouteState::OnRoute, 3, TurnKind::Left, 600));
  CHECK(nav.InTrip());
  auto const lines = nav.TakeLines();
  CHECK(IndexOf(lines, "turn left onto Oak Avenue") < lines.size());
}

TEST(DrivingAwayOnFixesWithNoSpeedIsntStillParked)
{
  Navigator nav(Seeded());
  FirstTripDone(nav);
  double lat = 36.39;
  double toTurnM = 5000;
  std::vector<Commentary> said;
  nav.OnFix(FixAt(kDay, lat, 0), Route(RouteState::OnRoute, 5, TurnKind::Right, toTurnM));
  // Rolling at 12 m/s, but the fixes come with no speed (Wi-Fi, weak GPS) and no matched progress.
  for (double s = 1; s <= 90; ++s)
  {
    lat += 12 * kDegPerM;
    toTurnM -= 12;
    RouteSnapshot r = Route(RouteState::OnRoute, 5, TurnKind::Right, toTurnM);
    r.turnId = static_cast<uint32_t>(5 + static_cast<int>(s));  // no same-turn progress to read
    nav.OnFix(FixAt(kDay + s * 1000, lat, -1, 15), r);
    auto const lines = nav.TakeLines();
    said.insert(said.end(), lines.begin(), lines.end());
  }
  CHECK(!HasKey(said, "start.slow"));
}

TEST(AParkedCarWithNoSpeedIsStillParked)
{
  Navigator nav(Seeded());
  FirstTripDone(nav);
  std::vector<Commentary> said;
  nav.OnFix(FixAt(kDay, 36.39, 0), Route(RouteState::OnRoute, 5, TurnKind::Right, 900));
  for (double s = 1; s <= 60; ++s)
  {
    // Jitter well inside the accuracy: parked.
    nav.OnFix(FixAt(kDay + s * 1000, 36.39 + (static_cast<int>(s) % 2) * 3 * kDegPerM, -1, 15),
              Route(RouteState::OnRoute, 5, TurnKind::Right, 900));
    auto const lines = nav.TakeLines();
    said.insert(said.end(), lines.begin(), lines.end());
  }
  CHECK(HasKey(said, "start.slow"));
}

TEST(TurnTimingUsesProgressWhenTheLastSpeedWasParked)
{
  Navigator nav(Seeded());
  nav.OnFix(FixAt(0, 36.39, 0), Route(RouteState::OnRoute, 3, TurnKind::Left, 900));
  CHECK_EQUAL(nav.TimingMps(-1), 0.0);
  // Pulling away on fixes with no speed: 12 m between accurate fixes a second apart.
  nav.OnFix(FixAt(1000, 36.39 + 12 * kDegPerM, -1, 5), Route(RouteState::OnRoute, 3, TurnKind::Left, 888));
  CHECK(std::abs(nav.TimingMps(-1) - 12) < 0.5);
  // A real speed always wins.
  CHECK_EQUAL(nav.TimingMps(9), 9.0);
}
