#pragma once

// Backseat Driver: the driver's own say on the route, beyond tolls and highways.
//
// "Avoid Main Street" / "get me off this road": the router keeps off those roads unless there's no
// other way. "Fewer left turns": a left across traffic costs the route an extra minute, so it takes
// rights and lights instead when that's close in time. Both only change which route is picked; the
// arrival time still comes from the real driving time.

#include "routing_common/num_mwm_id.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <utility>

namespace routing
{
class Segment;

class DrivePreferences
{
public:
  /// One road piece (feature) in one map, as the router numbers maps.
  using Road = std::pair<NumMwmId, uint32_t>;

  struct Snapshot
  {
    std::set<Road> m_avoided;
    bool m_fewerLefts = false;

    bool Avoids(Segment const & segment) const;
    bool Empty() const { return m_avoided.empty() && !m_fewerLefts; }
  };

  /// What an avoided road's time is multiplied by when picking the route.
  static double constexpr kAvoidedRoadFactor = 20.0;
  /// What a left across traffic costs (seconds) when picking the route, with fewer lefts on.
  static double constexpr kLeftTurnPenaltyS = 60.0;
  /// Sharper than this (degrees) is a turn; a slight bend left isn't one.
  static double constexpr kLeftTurnMinAngleDeg = 40.0;

  static DrivePreferences & Instance();

  /// What a route being worked out now should follow. Never null.
  std::shared_ptr<Snapshot const> Get() const;

  void Avoid(std::set<Road> const & roads);
  /// A new trip, or another router (its maps are numbered afresh).
  void ClearAvoided();
  void SetFewerLefts(bool fewer);
  bool FewerLefts() const;

private:
  mutable std::mutex m_mutex;
  std::shared_ptr<Snapshot const> m_snapshot = std::make_shared<Snapshot const>();
};
}  // namespace routing
