// "Arrive at Joe's Diner, on your right" (loop B119, BI-102). When the host marks the place itself,
// the arrival call says which side of the road it's on, from geometry alone; called from too far out,
// a short "It's on your right" follows once when close. No mark, a mark on the road, or one behind
// leaves the call exactly as it was.
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"

#include <cmath>
#include <optional>
#include <string>

using namespace backseat;

namespace
{
double constexpr kLat = 39.7;
double constexpr kLon = -105;
double constexpr kMPerDegLat = 111195;

/// A point `northM` ahead and `eastM` to the side of the car (which faces north).
LatLon Offset(double northM, double eastM)
{
  return {kLat + northM / kMPerDegLat, kLon + eastM / (kMPerDegLat * std::cos(kLat * M_PI / 180))};
}

RouteSnapshot Arrive(double dist, std::optional<LatLon> mark, double bearing = 0)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 7;
  r.turn = TurnKind::Destination;
  r.distToTurnM = dist;
  r.destination = "Joe's Diner";
  r.etaSeconds = 600;
  r.plannedTurns = {TurnKind::Destination};
  r.routeBearingDeg = bearing;
  r.destinationMark = mark;
  return r;
}

Fix At(double tMs, double mps)
{
  Fix f;
  f.tMs = tMs;
  f.lat = kLat;
  f.lon = kLon;
  f.speedMps = mps;
  return f;
}

bool IsSide(std::string const & text, char const * key)
{
  for (int tier = 0; tier < 3; ++tier)
    for (auto const & line : LinesFor(key, tier))
      if (text == line)
        return true;
  return false;
}

struct Heard
{
  std::string instruction;
  int right = 0;
  int left = 0;
};

/// Drives the arrival leg from `startM` down to 10 m at 10 m/s, 1 Hz.
Heard Drive(double startM, std::optional<LatLon> mark, std::string const & country = "")
{
  EngineConfig c;
  c.rng = SeededRng(5);
  Navigator nav(c);
  if (!country.empty())
    nav.SetCountry(country);
  Heard out;
  double t = 0;
  for (double d = startM; d >= 10; d -= 10, t += 1000)
  {
    nav.OnFix(At(t, 10), Arrive(d, mark));
    for (auto const & l : nav.TakeLines())
    {
      out.right += IsSide(l.text, "arrive.side.right");
      out.left += IsSide(l.text, "arrive.side.left");
    }
    if (out.instruction.empty())
      out.instruction = nav.LastInstructionText();
  }
  return out;
}
}  // namespace

TEST(DestinationSideIsGeometry)
{
  LatLon const here{kLat, kLon};
  CHECK_EQUAL(DestinationSide(here, 0, Offset(100, 25)), std::string("right"));
  CHECK_EQUAL(DestinationSide(here, 0, Offset(100, -25)), std::string("left"));
  // Heading south, the same east-side spot is now on the left.
  CHECK_EQUAL(DestinationSide(here, 180, Offset(-100, 25)), std::string("left"));
  CHECK_EQUAL(DestinationSide(here, 90, Offset(25, 100)), std::string("left"));
  CHECK_EQUAL(DestinationSide(here, 90, Offset(-25, 100)), std::string("right"));
  // On the road, behind, unknown bearing, or too far to judge: no side.
  CHECK_EQUAL(DestinationSide(here, 0, Offset(100, 3)), std::string());
  CHECK_EQUAL(DestinationSide(here, 0, Offset(-100, 25)), std::string());
  CHECK_EQUAL(DestinationSide(here, -1, Offset(100, 25)), std::string());
  CHECK_EQUAL(DestinationSide(here, NAN, Offset(100, 25)), std::string());
  CHECK_EQUAL(DestinationSide(here, 0, Offset(900, 25)), std::string());
}

TEST(ArrivalCallSaysTheSideWhenClose)
{
  Heard const right = Drive(300, Offset(100, 25));
  CHECK_EQUAL(right.instruction, std::string("Arrive at Joe's Diner, on your right"));
  CHECK_EQUAL(right.right, 0);  // already in the call; no follow-up
  CHECK_EQUAL(right.left, 0);
  Heard const left = Drive(300, Offset(100, -25));
  CHECK_EQUAL(left.instruction, std::string("Arrive at Joe's Diner, on your left"));
  CHECK_EQUAL(left.right + left.left, 0);
}

TEST(ArrivalCallUnchangedWithoutAUsefulMark)
{
  for (auto const & mark : {std::optional<LatLon>{}, std::optional<LatLon>{Offset(100, 3)},
                            std::optional<LatLon>{Offset(-100, 25)}})
  {
    Heard const h = Drive(300, mark);
    CHECK_EQUAL(h.instruction, std::string("Arrive at Joe's Diner"));
    CHECK_EQUAL(h.right + h.left, 0);
  }
}

TEST(LeftHandTrafficUsesTheSameGeometricSide)
{
  Heard const h = Drive(300, Offset(100, 25), "UK");
  SetLineDriveSide(false);  // the line filters are process-wide
  CHECK_EQUAL(h.instruction, std::string("Arrive at Joe's Diner, on your right"));
}

TEST(FarArrivalCallGetsOneSideLineWhenClose)
{
  Heard const h = Drive(2000, Offset(100, 25));
  CHECK_EQUAL(h.instruction, std::string("Arrive at Joe's Diner"));
  CHECK_EQUAL(h.right, 1);
  CHECK_EQUAL(h.left, 0);
  Heard const none = Drive(2000, Offset(100, 3));
  CHECK_EQUAL(none.right + none.left, 0);
}

TEST(SideLinesExistInEveryTier)
{
  for (char const * key : {"arrive.side.right", "arrive.side.left"})
    for (int tier = 0; tier < 3; ++tier)
      CHECK(!LinesFor(key, tier).empty());
}
