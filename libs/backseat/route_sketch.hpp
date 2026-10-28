// Draw-your-own route: the driver drags a finger along the roads they want, and we turn that
// stroke into hidden "shaping" points the router has to pass through. No map data is needed here;
// the router does the snapping. RoutingManager applies the plan with ApplyRouteSketch().
#pragma once

#include "backseat/types.hpp"

#include <cstddef>
#include <string>
#include <cstdint>
#include <vector>

namespace backseat
{
struct SketchOptions
{
  /// Wiggles smaller than this are hand shake, not intent. Use about 10 screen pixels in meters.
  double toleranceM = 40;
  /// A traced straight stretch longer than this gets extra points, so the router can't swap it
  /// for a parallel road.
  double maxSpacingM = 2000;
  /// No shaping point this close to a stop: snapping there tends to cause a U-turn at the stop.
  double endpointClearM = 150;
  /// Shaping points closer than this to each other are merged.
  double minGapM = 120;
  /// Budget for this stroke. CoMaps allows 100 intermediate points in total.
  std::size_t maxPoints = 30;
};

/// Distance on the Earth's surface, in meters.
double SketchDistanceM(LatLon const & a, LatLon const & b);

/// Douglas-Peucker on the stroke. Keeps the first and last points.
std::vector<LatLon> SimplifyStroke(std::vector<LatLon> const & stroke, double toleranceM);

/// The points to route through so the route follows the stroke. They sit in the middle of each
/// traced stretch, not on the corners: a corner is an intersection, and a point on an
/// intersection can snap to the cross street.
std::vector<LatLon> ShapingPoints(std::vector<LatLon> const & stroke, SketchOptions const & options);

struct SketchRoutePoint
{
  LatLon at;
  /// A hidden point from an earlier stroke, not a stop the driver picked.
  bool isShaping = false;
};

struct SketchPlan
{
  /// Index (in the current route points) of the stop the stroke starts from.
  std::size_t from = 0;
  /// Index of the stop the stroke ends on. Every shaping point strictly between from and to is
  /// removed. With newStop, this is the next real stop after `from` (route.size() if none).
  std::size_t to = 0;
  /// The stroke ended away from every stop, so its end becomes a new stop right after `from`.
  /// When `from` is the destination, the new stop becomes the destination and the old one a stop.
  bool newStop = false;
  LatLon newStopAt;
  /// In driving order, to insert after `from`.
  std::vector<LatLon> shaping;
  /// The stroke was trimmed to fit the 100-point limit.
  bool trimmed = false;
};

/// Works out which part of the route a stroke reshapes. `route` is the current start, stops and
/// destination in order (it may hold only the start). A stroke end within `snapRadiusM` of a stop
/// lands on that stop. `freeSlots` is how many intermediate points can still be added.
SketchPlan PlanSketch(std::vector<LatLon> const & stroke, std::vector<SketchRoutePoint> const & route,
                      double snapRadiusM, std::size_t freeSlots, SketchOptions options = {});

struct SketchVerdict
{
  /// "sketch.slower", "sketch.same" or "sketch.faster".
  std::string key;
  /// Whole minutes lost (slower) or saved (faster). 0 for same.
  int minutes = 0;
};

/// What the GPS thinks of the drawn route, against the fastest one between the same stops.
/// Within a minute counts as the same route.
SketchVerdict JudgeSketch(double fastestMinutes, double drawnMinutes);

/// One straight piece of a road the car can drive. `road` tells pieces of the same road apart from
/// a cross street (the host uses the feature id).
struct RoadSegment
{
  LatLon a;
  LatLon b;
  std::uint64_t road = 0;
};

struct SnapOptions
{
  /// The stroke is looked at every this many meters.
  double sampleM = 15;
  /// A sample farther than this from every road is the finger wandering off: dropped.
  double maxSnapM = 35;
  /// At an intersection, the road the stroke is already on wins unless another is this much closer.
  double stickyM = 12;
};

struct SnappedPoint
{
  LatLon at;
  std::uint64_t road = 0;
};

/// The stroke as points every `everyM` along it (first and last kept).
std::vector<LatLon> ResampleStroke(std::vector<LatLon> const & stroke, double everyM);

/// Moves a finger stroke onto the roads it traced: each sample goes to the nearest point on the
/// nearest road segment, staying on the same road through intersections. Samples with no road
/// nearby are dropped.
std::vector<SnappedPoint> SnapStroke(std::vector<LatLon> const & stroke, std::vector<RoadSegment> const & roads,
                                     SnapOptions const & options = {});
}  // namespace backseat
