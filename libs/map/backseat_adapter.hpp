#pragma once

#include "backseat/navigator.hpp"

#include "platform/location.hpp"

#include <string>
#include <vector>

namespace routing
{
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

private:
  backseat::RouteSnapshot Snapshot(routing::RoutingSession const & session, std::string const & destinationName) const;

  backseat::Navigator m_navigator;
};
