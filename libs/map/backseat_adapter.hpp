#pragma once

#include "backseat/navigator.hpp"

#include "platform/location.hpp"

#include "geometry/point2d.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class DataSource;

namespace storage
{
class CountryInfoGetter;
}

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
  /// Map regions, so the license plate game knows which state the car is in.
  void SetCountryInfoGetter(std::function<storage::CountryInfoGetter const &()> getter);
  /// What the driver said out loud (speech recognition), e.g. "I see a Texas plate".
  /// Call on the same thread as OnLocationUpdate.
  void OnDriverSaid(std::string const & text) { m_navigator.OnDriverSaid(0, text); }
  /// Traffic lights on the route just built (mercator points). A left turn off a side street
  /// with no light there is treated as a stop sign, since stop signs aren't in the map data.
  void SetTrafficLights(std::vector<m2::PointD> points);
  /// The platform's on-device language model, for rewording roasts in real time.
  void SetRewriter(std::shared_ptr<backseat::LineRewriter> rewriter) { m_navigator.SetRewriter(std::move(rewriter)); }

  /// How to voice one line of the last TakeNotifications() batch.
  struct LineVoice
  {
    /// A navigation line: the platform must say it right away, so it never waits on a slow voice.
    bool nav = true;
    /// Suggested speech rate, 1 = normal.
    double speed = 1;
  };

  /// One entry per line of the last TakeNotifications() batch, in the same order.
  std::vector<LineVoice> const & LastLineVoices() const { return m_lastVoices; }

private:
  backseat::RouteSnapshot Snapshot(routing::RoutingSession const & session, std::string const & destinationName) const;
  backseat::Control GuessControl(routing::Route const & route, uint32_t turnIndex) const;
  std::optional<backseat::Place> PlaceAt(backseat::LatLon const & at) const;
  void CheckBattery(double tMs);

  backseat::Navigator m_navigator;
  std::function<DataSource &()> m_dataSource;
  std::function<storage::CountryInfoGetter const &()> m_countryInfo;
  mutable std::mutex m_lightsMutex;
  std::vector<m2::PointD> m_trafficLights;
  double m_lastBatteryCheckMs = 0;
  std::vector<LineVoice> m_lastVoices;
};
