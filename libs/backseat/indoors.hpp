// Starting navigation from inside a building: "You drove into a building and then decided it was
// time for directions." The map lookup (which building footprints cover a point) lives in the host;
// the geometry and the decision are here so they can be tested.
#pragma once

#include "backseat/types.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace backseat
{
/// One triangle of a building's footprint, as the map stores areas.
struct FootprintTriangle
{
  LatLon a, b, c;
};

/// How far inside the footprint `p` is: meters to the nearest outside wall. Empty when outside.
/// Walls are the triangle edges that only one triangle has; edges shared by two are inside.
std::optional<double> DepthInsideM(std::vector<FootprintTriangle> const & footprint, LatLon const & p);

/// The building around a point, if any. The host leaves out parking garages, carports, roofs
/// (gas station canopies) and anything inside a parking area: starting there is normal.
struct Building
{
  /// DepthInsideM() of the point.
  double depthM = 0;
  /// Tagged as a garage (building=garage or garages).
  bool garage = false;
};
using BuildingLookup = std::function<std::optional<Building>(LatLon const &)>;

enum class IndoorStart
{
  No,
  /// "You drove into a building..."
  Building,
  /// At home or in a garage: "Make sure to open the garage door before you leave."
  Garage
};

/// Whether a trip that starts here starts indoors. `accuracyM` and `speedMps` are negative when
/// unknown. Near a wall it's GPS noise from the driveway, and moving it's a road through a building.
IndoorStart JudgeIndoorStart(std::optional<Building> const & building, double accuracyM, double speedMps, bool atHome);

/// Deeper inside than this, past the usual GPS wobble near a wall.
double constexpr kIndoorMinDepthM = 2;
/// A fix vaguer than this can't place the phone in a building.
double constexpr kIndoorMaxAccuracyM = 65;
/// Faster than this is driving, not sitting in a building.
double constexpr kIndoorMaxMps = 3;
}  // namespace backseat
