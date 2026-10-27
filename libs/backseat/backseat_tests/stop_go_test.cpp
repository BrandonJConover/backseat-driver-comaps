#include "check.hpp"

#include "backseat/stop_go.hpp"

#include <vector>

using namespace backseat;

namespace
{
/// Feeds one speed per second for `seconds`, starting at `t`; returns the quips said.
std::vector<StopGo::Quip> StopGoDrive(StopGo & s, double & t, double mps, int seconds)
{
  std::vector<StopGo::Quip> out;
  for (int i = 0; i < seconds; ++i, t += 1000)
    if (auto const q = s.Update(t, mps); q != StopGo::Quip::None)
      out.push_back(q);
  return out;
}
}  // namespace

TEST(StopGoLetsTheFirstStopSlide)
{
  StopGo s;
  double t = 0;
  StopGoDrive(s, t, 5, 10);
  CHECK(StopGoDrive(s, t, 0, 10).empty());
}

TEST(StopGoCallsOutTheSecondStop)
{
  StopGo s;
  double t = 0;
  StopGoDrive(s, t, 5, 5);
  StopGoDrive(s, t, 0, 5);
  StopGoDrive(s, t, 5, 5);
  auto const q = StopGoDrive(s, t, 0, 5);
  CHECK_EQUAL(q.size(), size_t(1));
  CHECK(q[0] == StopGo::Quip::StopGo);
}

TEST(StopGoCreepingIsNotGoing)
{
  StopGo s;
  double t = 0;
  StopGoDrive(s, t, 5, 5);
  StopGoDrive(s, t, 0, 5);
  // Rolling forward a car length at walking pace, then stopping, isn't a go.
  for (int i = 0; i < 4; ++i)
  {
    StopGoDrive(s, t, 1.5, 3);
    CHECK(StopGoDrive(s, t, 0, 5).empty());
  }
}

TEST(StopGoWaitsBetweenRemarks)
{
  StopGo s;
  double t = 0;
  int said = 0;
  // Stop and go every 10 seconds for 3 minutes.
  for (int i = 0; i < 18; ++i)
  {
    said += StopGoDrive(s, t, 5, 5).size();
    said += StopGoDrive(s, t, 0, 5).size();
  }
  // At most one per 75 s: at 20 s, 100 s, 180 s at the latest.
  CHECK(said >= 2 && said <= 3);
}

TEST(StopGoStandstillOncePerStop)
{
  StopGo s;
  double t = 0;
  StopGoDrive(s, t, 5, 5);
  auto q = StopGoDrive(s, t, 0, 120);
  CHECK_EQUAL(q.size(), size_t(1));
  CHECK(q[0] == StopGo::Quip::Standstill);
  // Moving again re-arms it, once the gap has passed.
  StopGoDrive(s, t, 5, 5);
  q = StopGoDrive(s, t, 0, 120);
  CHECK(!q.empty());
}

TEST(StopGoResetForgetsTheJam)
{
  StopGo s;
  double t = 0;
  StopGoDrive(s, t, 5, 5);
  StopGoDrive(s, t, 0, 5);
  StopGoDrive(s, t, 5, 5);
  s.Reset();
  CHECK(StopGoDrive(s, t, 0, 5).empty());
}
