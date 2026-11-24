#pragma once

// Backseat Driver: two guards on how the car's position moves a route along.
//
// OffRouteFixGate: the car only leaves its route on two real GPS fixes. Between fixes the
// Extrapolator feeds the route five guesses a second, each projected from the last two fixes. One
// bad fix (a multipath jump off a building, a tunnel mouth) plus its guesses passed the off-route
// count on its own within about a second, so the app rebuilt the route and told the driver they'd
// missed a turn they hadn't. The next real fix, back on the road, came too late.
//
// NearStopArrival: arriving where the driver actually parks. A route only finished once the car's
// spot on the route came within 20 m of its end, and the end is the pin itself, often the middle of
// a building. Park at the far side of the lot, or on the street across from the door, and the trip
// never ended: the route rebuilt from the lot, "recalculating", and the arrival (and its grade) never
// came. The same held for stops along the way: the route kept pointing back at a gas station the
// car had already left by another exit.

#include <cstdint>
#include <optional>

namespace routing
{
class OffRouteFixGate
{
public:
  /// A real fix from the phone (not one the Extrapolator made up).
  void OnRealFix() { ++m_fixes; }
  /// The car was found on the route again.
  void OnRoute() { m_firstOffFix = 0; }
  /// One off-route check. True once two different real fixes were off the route, or always when the
  /// host never reports real fixes (tests, and hosts that don't call OnRealFix).
  bool ConfirmsOffRoute()
  {
    if (m_fixes == 0)
      return true;
    if (m_firstOffFix == 0)
      m_firstOffFix = m_fixes;
    return m_fixes > m_firstOffFix;
  }

private:
  uint64_t m_fixes = 0;
  /// The real fix the car was first off the route on; 0 while on it.
  uint64_t m_firstOffFix = 0;
};

class NearStopArrival
{
public:
  /// Passing right by the stop: within this of it, with little of the route left to it.
  static double constexpr kPassByM = 40.0;
  static double constexpr kPassByRouteLeftM = 150.0;
  /// Parked near the stop: within this of it, nearly there along the route, stopped this long.
  static double constexpr kNearM = 150.0;
  static double constexpr kNearRouteLeftM = 400.0;
  static double constexpr kParkedSpeedMps = 1.5;
  static double constexpr kParkedForS = 20.0;
  /// Parked while still on the route, the route has to be nearly done too (not a red light a
  /// block short of the stop).
  static double constexpr kParkedOnRouteLeftM = 75.0;
  /// Slower than this near the stop (a parking lot crawl), leaving the route doesn't rebuild it.
  static double constexpr kHoldRebuildSpeedMps = 4.0;

  struct Position
  {
    /// Straight line from the car to the stop.
    double m_toStopM = 0.0;
    /// Along the route from the car's last spot on it to the stop.
    double m_routeLeftM = 0.0;
    /// The car is on the route right now.
    bool m_onRoute = false;
    /// Negative when the phone doesn't know.
    double m_speedMps = -1.0;
    /// Seconds, any epoch.
    double m_timeS = 0.0;
  };

  /// A new route, or the next stop: forget how long the car has been parked.
  void Reset() { m_stillSince.reset(); }

  /// The car has reached the stop (passed right by it, or parked near it).
  bool Arrived(Position const & p)
  {
    TrackStill(p);
    if (p.m_toStopM <= kPassByM && p.m_routeLeftM <= kPassByRouteLeftM)
      return true;
    if (!Near(p) || !m_stillSince || p.m_timeS - *m_stillSince < kParkedForS)
      return false;
    return !p.m_onRoute || p.m_routeLeftM <= kParkedOnRouteLeftM;
  }

  /// Off the route near the stop at parking-lot speed: don't rebuild the route, the car is here.
  bool HoldsRebuild(Position const & p) const
  {
    return Near(p) && (p.m_speedMps < 0.0 || p.m_speedMps < kHoldRebuildSpeedMps);
  }

private:
  static bool Near(Position const & p) { return p.m_toStopM <= kNearM && p.m_routeLeftM <= kNearRouteLeftM; }

  void TrackStill(Position const & p)
  {
    // An unknown speed (Wi-Fi fix, weak GPS) says nothing either way.
    if (p.m_speedMps < 0.0)
      return;
    if (p.m_speedMps >= kParkedSpeedMps || !Near(p))
    {
      m_stillSince.reset();
      return;
    }
    if (!m_stillSince || p.m_timeS < *m_stillSince)
      m_stillSince = p.m_timeS;
  }

  std::optional<double> m_stillSince;
};
}  // namespace routing
