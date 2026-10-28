#include "backseat/route_sketch.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace backseat
{
namespace
{
double constexpr kEarthRadiusM = 6371000.0;
double constexpr kDegToRad = 3.14159265358979323846 / 180.0;

struct Xy
{
  double x = 0;
  double y = 0;
};

// Local flat projection around `origin`. Strokes are drawn on one screen, so this is plenty.
Xy ToXy(LatLon const & p, LatLon const & origin)
{
  return {(p.lon - origin.lon) * kDegToRad * kEarthRadiusM * std::cos(origin.lat * kDegToRad),
          (p.lat - origin.lat) * kDegToRad * kEarthRadiusM};
}

double SegmentDistance(Xy const & p, Xy const & a, Xy const & b)
{
  double const dx = b.x - a.x;
  double const dy = b.y - a.y;
  double const len2 = dx * dx + dy * dy;
  double t = len2 == 0 ? 0 : ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
  t = std::clamp(t, 0.0, 1.0);
  return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

void Simplify(std::vector<Xy> const & xy, std::size_t first, std::size_t last, double tolerance,
              std::vector<bool> & keep)
{
  if (last <= first + 1)
    return;
  double worst = -1;
  std::size_t worstIdx = first;
  for (std::size_t i = first + 1; i < last; ++i)
  {
    double const d = SegmentDistance(xy[i], xy[first], xy[last]);
    if (d > worst)
    {
      worst = d;
      worstIdx = i;
    }
  }
  if (worst <= tolerance)
    return;
  keep[worstIdx] = true;
  Simplify(xy, first, worstIdx, tolerance, keep);
  Simplify(xy, worstIdx, last, tolerance, keep);
}

LatLon Lerp(LatLon const & a, LatLon const & b, double t)
{
  return {a.lat + (b.lat - a.lat) * t, a.lon + (b.lon - a.lon) * t};
}

std::vector<LatLon> PlacePoints(std::vector<LatLon> const & simplified, SketchOptions const & o)
{
  std::vector<LatLon> out;
  if (simplified.size() < 2)
    return out;
  LatLon const & first = simplified.front();
  LatLon const & last = simplified.back();
  for (std::size_t i = 0; i + 1 < simplified.size(); ++i)
  {
    LatLon const & a = simplified[i];
    LatLon const & b = simplified[i + 1];
    double const len = SketchDistanceM(a, b);
    auto const n = static_cast<std::size_t>(std::max(1.0, std::ceil(len / o.maxSpacingM)));
    for (std::size_t k = 0; k < n; ++k)
    {
      LatLon const p = Lerp(a, b, (k + 0.5) / n);
      if (SketchDistanceM(p, first) < o.endpointClearM || SketchDistanceM(p, last) < o.endpointClearM)
        continue;
      if (!out.empty() && SketchDistanceM(p, out.back()) < o.minGapM)
        continue;
      out.push_back(p);
    }
  }
  return out;
}
}  // namespace

double SketchDistanceM(LatLon const & a, LatLon const & b)
{
  double const dLat = (b.lat - a.lat) * kDegToRad;
  double const dLon = (b.lon - a.lon) * kDegToRad;
  double const h = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(a.lat * kDegToRad) * std::cos(b.lat * kDegToRad) * std::sin(dLon / 2) * std::sin(dLon / 2);
  return 2 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(h)));
}

std::vector<LatLon> SimplifyStroke(std::vector<LatLon> const & stroke, double toleranceM)
{
  if (stroke.size() < 3)
    return stroke;
  std::vector<Xy> xy;
  xy.reserve(stroke.size());
  for (auto const & p : stroke)
    xy.push_back(ToXy(p, stroke.front()));
  std::vector<bool> keep(stroke.size(), false);
  keep.front() = keep.back() = true;
  Simplify(xy, 0, stroke.size() - 1, toleranceM, keep);
  std::vector<LatLon> out;
  for (std::size_t i = 0; i < stroke.size(); ++i)
    if (keep[i])
      out.push_back(stroke[i]);
  return out;
}

std::vector<LatLon> ShapingPoints(std::vector<LatLon> const & stroke, SketchOptions const & options)
{
  SketchOptions o = options;
  std::vector<LatLon> points = PlacePoints(SimplifyStroke(stroke, o.toleranceM), o);
  // Over budget: loosen until it fits. Each round keeps fewer, more important bends.
  for (int round = 0; round < 20 && points.size() > o.maxPoints; ++round)
  {
    o.toleranceM *= 1.5;
    o.maxSpacingM *= 1.5;
    points = PlacePoints(SimplifyStroke(stroke, o.toleranceM), o);
  }
  if (points.size() > o.maxPoints)
    points.resize(o.maxPoints);
  return points;
}

SketchPlan PlanSketch(std::vector<LatLon> const & stroke, std::vector<SketchRoutePoint> const & route,
                      double snapRadiusM, std::size_t freeSlots, SketchOptions options)
{
  SketchPlan plan;
  if (stroke.size() < 2 || route.empty())
    return plan;

  auto nearestStop = [&](LatLon const & p, std::size_t minIdx, double & bestDist)
  {
    std::size_t best = route.size();
    bestDist = std::numeric_limits<double>::max();
    for (std::size_t i = minIdx; i < route.size(); ++i)
    {
      if (route[i].isShaping)
        continue;
      double const d = SketchDistanceM(p, route[i].at);
      if (d < bestDist)
      {
        bestDist = d;
        best = i;
      }
    }
    return best;
  };

  // The stroke always grabs the route somewhere: the closest stop to where the finger went down.
  double startDist = 0;
  plan.from = nearestStop(stroke.front(), 0, startDist);
  if (plan.from == route.size())
    plan.from = 0;

  double endDist = 0;
  std::size_t const to = nearestStop(stroke.back(), plan.from + 1, endDist);
  if (to < route.size() && endDist <= snapRadiusM)
  {
    plan.to = to;
  }
  else
  {
    plan.newStop = true;
    plan.newStopAt = stroke.back();
    // The new stop goes in before the next real stop, and the old shaping on that leg goes away.
    plan.to = plan.from + 1;
    while (plan.to < route.size() && route[plan.to].isShaping)
      ++plan.to;
  }

  // Shaping points between from and to get replaced, so they free up their slots.
  std::size_t replaced = 0;
  for (std::size_t i = plan.from + 1; i < plan.to; ++i)
    replaced += route[i].isShaping ? 1 : 0;
  std::size_t budget = freeSlots + replaced;
  if (plan.newStop)
    budget = budget > 0 ? budget - 1 : 0;

  bool const overBudget = budget < options.maxPoints;
  options.maxPoints = std::min(options.maxPoints, budget);
  plan.shaping = ShapingPoints(stroke, options);
  if (overBudget)
  {
    // Would the stroke have used more than it got?
    SketchOptions unlimited = options;
    unlimited.maxPoints = std::numeric_limits<std::size_t>::max();
    plan.trimmed = ShapingPoints(stroke, unlimited).size() > plan.shaping.size();
  }
  return plan;
}

SketchVerdict JudgeSketch(double fastestMinutes, double drawnMinutes)
{
  int const diff = static_cast<int>(std::lround(drawnMinutes - fastestMinutes));
  if (diff >= 1)
    return {"sketch.slower", diff};
  if (diff <= -1)
    return {"sketch.faster", -diff};
  return {"sketch.same", 0};
}
std::vector<LatLon> ResampleStroke(std::vector<LatLon> const & stroke, double everyM)
{
  if (stroke.size() < 2 || everyM <= 0)
    return stroke;
  std::vector<LatLon> out{stroke.front()};
  double carried = 0;
  for (std::size_t i = 0; i + 1 < stroke.size(); ++i)
  {
    LatLon const & a = stroke[i];
    LatLon const & b = stroke[i + 1];
    double const len = SketchDistanceM(a, b);
    double along = everyM - carried;
    while (along <= len)
    {
      out.push_back(Lerp(a, b, along / len));
      along += everyM;
    }
    carried = len - (along - everyM);
  }
  if (SketchDistanceM(out.back(), stroke.back()) > 1)
    out.push_back(stroke.back());
  return out;
}

namespace
{
struct Projection
{
  LatLon at;
  double distanceM = std::numeric_limits<double>::max();
};

Projection Project(LatLon const & p, RoadSegment const & segment)
{
  Xy const a = ToXy(segment.a, p);
  Xy const b = ToXy(segment.b, p);
  double const dx = b.x - a.x;
  double const dy = b.y - a.y;
  double const len2 = dx * dx + dy * dy;
  double t = len2 == 0 ? 0 : (-a.x * dx - a.y * dy) / len2;
  t = std::clamp(t, 0.0, 1.0);
  return {Lerp(segment.a, segment.b, t), std::hypot(a.x + t * dx, a.y + t * dy)};
}
}  // namespace

std::vector<SnappedPoint> SnapStroke(std::vector<LatLon> const & stroke, std::vector<RoadSegment> const & roads,
                                     SnapOptions const & options)
{
  std::vector<SnappedPoint> out;
  if (stroke.empty() || roads.empty())
    return out;
  std::optional<std::uint64_t> onRoad;
  for (auto const & sample : ResampleStroke(stroke, options.sampleM))
  {
    Projection best;
    std::uint64_t bestRoad = 0;
    Projection same;
    for (auto const & segment : roads)
    {
      auto const projection = Project(sample, segment);
      if (projection.distanceM < best.distanceM)
      {
        best = projection;
        bestRoad = segment.road;
      }
      if (onRoad && segment.road == *onRoad && projection.distanceM < same.distanceM)
        same = projection;
    }
    if (best.distanceM > options.maxSnapM)
      continue;
    // Through an intersection the cross street is briefly nearer; stay on the road being traced.
    if (onRoad && bestRoad != *onRoad && same.distanceM <= options.maxSnapM &&
        same.distanceM <= best.distanceM + options.stickyM)
    {
      best = same;
      bestRoad = *onRoad;
    }
    onRoad = bestRoad;
    if (!out.empty() && SketchDistanceM(out.back().at, best.at) < 1)
      continue;
    out.push_back({best.at, bestRoad});
  }
  return out;
}
}  // namespace backseat
