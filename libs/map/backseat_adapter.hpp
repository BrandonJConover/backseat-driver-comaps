#pragma once

#include "backseat/navigator.hpp"

#include "platform/location.hpp"

#include "geometry/point2d.hpp"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class DataSource;

namespace routing
{
class Route;
class RoutingSession;
}

/// Connects Backseat Driver's navigator to CoMaps routing: builds route snapshots from the
/// routing session, feeds it real GPS fixes, and hands back what to say in place of the
/// stock turn notifications. Trip history is kept in the settings directory.
class BackseatAdapter
{
public:
  BackseatAdapter();

  /// A real (not extrapolated) GPS fix.
  void OnLocationUpdate(location::GpsInfo const & info, routing::RoutingSession const & session,
                        std::string const & destinationName);

  /// Called when the platform is about to speak: catches route changes since the last fix
  /// and returns the lines to say.
  std::vector<std::string> TakeNotifications(routing::RoutingSession const & session,
                                             std::string const & destinationName);

  /// Map data, for naming the place of an unplanned stop ("Why are we stopping for gas?").
  void SetDataSource(std::function<DataSource &()> getter);
  /// Traffic lights on the route just built (mercator points). A left turn off a side street
  /// with no light there is treated as a stop sign, since stop signs aren't in the map data.
  void SetTrafficLights(std::vector<m2::PointD> points);

private:
  backseat::RouteSnapshot Snapshot(routing::RoutingSession const & session, std::string const & destinationName) const;
  backseat::Control GuessControl(routing::Route const & route, uint32_t turnIndex) const;
  std::optional<backseat::Place> PlaceAt(backseat::LatLon const & at) const;
  void CheckBattery(double tMs);

  backseat::Navigator m_navigator;
  std::function<DataSource &()> m_dataSource;
  mutable std::mutex m_lightsMutex;
  std::vector<m2::PointD> m_trafficLights;
  double m_lastBatteryCheckMs = 0;
};
