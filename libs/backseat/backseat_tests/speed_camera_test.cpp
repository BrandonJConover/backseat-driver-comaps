// Speed cameras: one warning on the way up to each camera (always when the car is over the
// camera's limit, an easy heads-up only now and then otherwise), a word as the car passes it
// ("camera.flash" still speeding, "camera.slowed" when the warning worked), and no regular
// speeding nag piled on top of the camera lines.
#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/units.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
EngineConfig CamConfig()
{
  EngineConfig c;
  c.rng = SeededRng(17);
  return c;
}

double Mph(double v) { return FromDisplay(v, Units::Mph); }

/// A straight route north with speed cameras at these distances from the start.
struct CamDrive
{
  Navigator nav{CamConfig()};
  std::vector<Commentary> said;
  std::vector<double> camerasAtM;
  std::optional<double> cameraLimitMps;
  double limitMps = Mph(50);
  double alongM = 0;
  int t = 0;

  RouteSnapshot Route() const
  {
    RouteSnapshot r;
    r.state = RouteState::OnRoute;
    r.turnId = 1;
    r.turn = TurnKind::Right;
    r.distToTurnM = 50000 - alongM;
    r.nextStreet = "Main Street";
    r.destination = "the city";
    r.etaSeconds = 3600;
    r.plannedTurns = {TurnKind::Right, TurnKind::Destination};
    r.destinationPoint = {40.5, -105};
    for (double at : camerasAtM)
      if (at > alongM && at - alongM < 2000)
      {
        r.cameraAheadM = at - alongM;
        r.cameraAt = LatLon{39.7 + at / 111000.0, -105};
        r.cameraLimitMps = cameraLimitMps;
        break;
      }
    return r;
  }

  void Run(int seconds, double mps)
  {
    for (int end = t + seconds; t < end; ++t)
    {
      Fix f;
      f.tMs = t * 1000.0;
      f.lat = 39.7 + alongM / 111000.0;
      f.lon = -105;
      f.speedMps = mps;
      f.speedLimitMps = limitMps;
      nav.OnFix(f, Route());
      alongM += mps;
      for (auto & l : nav.TakeLines())
        said.push_back(std::move(l));
    }
  }

  int Count(std::string const & key) const
  {
    return static_cast<int>(std::count_if(said.begin(), said.end(), [&](Commentary const & c) { return c.key == key; }));
  }
  int CountPrefix(std::string const & prefix, size_t from = 0) const
  {
    return static_cast<int>(std::count_if(said.begin() + from, said.end(),
                                          [&](Commentary const & c) { return c.key.rfind(prefix, 0) == 0; }));
  }
};
}  // namespace

TEST(SpeedCameraWarnsASpeederOnceAndHushesTheSpeedingNag)
{
  CamDrive d;
  // Settle in at the limit, then speed up 1.2 km before the camera.
  d.camerasAtM = {Mph(50) * 120 + 1200};
  d.Run(120, Mph(50));
  CHECK_EQUAL(d.CountPrefix("speeding."), 0);
  size_t const before = d.said.size();
  // 62 in a 50 the whole way up to the camera and past it.
  d.Run(70, Mph(62));
  CHECK_EQUAL(d.Count("hazard.speedCameraSpeeding"), 1);
  CHECK_EQUAL(d.Count("hazard.speedCamera"), 0);
  CHECK_EQUAL(d.Count("camera.flash"), 1);
  // The camera lines were the speeding lines: at most the one nag that may have come before the warning.
  CHECK(d.CountPrefix("speeding.", before) <= 1);
  // The warning names the numbers.
  for (auto const & c : d.said)
    if (c.key == "hazard.speedCameraSpeeding")
      CHECK(c.text.find("62") != std::string::npos || c.text.find("50") != std::string::npos ||
            c.text.find("BRAKE") != std::string::npos);
}

TEST(SpeedCameraSlowedDownGetsTheBackhandedCompliment)
{
  CamDrive d;
  d.camerasAtM = {Mph(50) * 120 + 15 * Mph(62) + 300};
  d.Run(120, Mph(50));
  // Speeding into the warning, then right back down to the limit before the camera.
  d.Run(15, Mph(62));
  d.Run(40, Mph(48));
  CHECK_EQUAL(d.Count("hazard.speedCameraSpeeding"), 1);
  CHECK_EQUAL(d.Count("camera.flash"), 0);
  CHECK_EQUAL(d.Count("camera.slowed"), 1);
}

TEST(SpeedCameraHeadsUpWithinTheLimitIsRationedInAuto)
{
  CamDrive d;
  // Three cameras two to four minutes apart, all driven at the limit.
  double const v = Mph(45);
  d.camerasAtM = {v * 120, v * 240, v * 480};
  d.Run(560, v);
  // The first gets a heads-up; the next ones come too soon after it.
  CHECK_EQUAL(d.Count("hazard.speedCamera"), 1);
  CHECK_EQUAL(d.Count("hazard.speedCameraSpeeding"), 0);
  CHECK_EQUAL(d.Count("camera.flash") + d.Count("camera.slowed"), 0);
}

TEST(SpeedCameraAlwaysModeCallsEveryCamera)
{
  CamDrive d;
  d.nav.SetCameraAlerts(CameraAlerts::Always);
  double const v = Mph(45);
  d.camerasAtM = {v * 120, v * 240, v * 480};
  d.Run(560, v);
  CHECK_EQUAL(d.Count("hazard.speedCamera"), 3);
}

TEST(SpeedCameraOffSaysNothing)
{
  CamDrive d;
  d.nav.SetCameraAlerts(CameraAlerts::Off);
  d.camerasAtM = {Mph(50) * 120 + 1200};
  d.Run(120, Mph(50));
  d.Run(70, Mph(62));
  CHECK_EQUAL(d.CountPrefix("hazard.speedCamera") + d.CountPrefix("camera."), 0);
  // The ordinary speeding lines still do their job.
  CHECK(d.CountPrefix("speeding.") >= 1);
}

TEST(SpeedCamerasInARowGetOneWarning)
{
  CamDrive d;
  // An enforcement zone: three cameras 400 m apart, driven through speeding.
  double const first = Mph(50) * 120 + 1200;
  d.camerasAtM = {first, first + 400, first + 800};
  d.Run(120, Mph(50));
  d.Run(90, Mph(62));
  CHECK_EQUAL(d.Count("hazard.speedCameraSpeeding"), 1);
  // One flash line for the first camera; the next ones within the minute stay quiet.
  CHECK_EQUAL(d.Count("camera.flash"), 1);
}

TEST(SpeedCameraUsesTheCamerasOwnLimit)
{
  CamDrive d;
  // Posted 50, but the camera enforces 40 (a school zone): 48 is fine for the road, not the camera.
  d.cameraLimitMps = Mph(40);
  d.camerasAtM = {Mph(48) * 150 + 300};
  d.Run(180, Mph(48));
  CHECK_EQUAL(d.Count("hazard.speedCameraSpeeding"), 1);
  CHECK_EQUAL(d.Count("camera.flash"), 1);
}

TEST(SpeedCameraJunkSnapshotIsIgnored)
{
  CamDrive d;
  d.Run(60, Mph(50));
  RouteSnapshot r = d.Route();
  r.cameraAheadM = -5;
  r.cameraAt = LatLon{39.8, -105};
  Fix f;
  f.tMs = 61000;
  f.lat = 39.75;
  f.lon = -105;
  f.speedMps = Mph(70);
  f.speedLimitMps = Mph(50);
  d.nav.OnFix(f, r);
  for (auto const & c : d.nav.TakeLines())
    CHECK(c.key.rfind("hazard.speedCamera", 0) != 0);
}
