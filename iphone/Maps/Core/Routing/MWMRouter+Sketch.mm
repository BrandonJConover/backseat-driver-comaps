#import "MWMRouter+Sketch.h"

#include <CoreApi/Framework.h>

#include "backseat/route_sketch.hpp"
#include "geometry/mercator.hpp"

#include <algorithm>
#include <iterator>
#include <optional>
#include <vector>

namespace
{
/// The route points (stops and drawn shaping points) before each change, newest last.
std::vector<std::vector<RouteMarkData>> & History()
{
  static std::vector<std::vector<RouteMarkData>> history;
  return history;
}

void Remember()
{
  History().push_back(GetFramework().GetRoutingManager().GetRoutePoints());
}
}  // namespace

@implementation MWMRouter (Sketch)

+ (MWMRouteSketchResult)applySketch:(NSArray<NSValue *> *)points inView:(UIView *)mapView
{
  auto & f = GetFramework();
  CGFloat const scale = mapView.contentScaleFactor;
  std::vector<m2::PointD> stroke;
  stroke.reserve(points.count);
  for (NSValue * value in points)
  {
    CGPoint const p = value.CGPointValue;
    // P3dtoG, so it still lines up when the map is tilted while driving.
    stroke.push_back(f.P3dtoG(m2::PointD(p.x * scale, p.y * scale)));
  }
  if (stroke.size() < 2)
    return MWMRouteSketchResultNone;

  CGPoint const mid = CGPointMake(CGRectGetMidX(mapView.bounds), CGRectGetMidY(mapView.bounds));
  m2::PointD const a = f.PtoG(m2::PointD(mid.x * scale, mid.y * scale));
  m2::PointD const b = f.PtoG(m2::PointD((mid.x + 1) * scale, mid.y * scale));
  double const metersPerPoint = mercator::DistanceOnEarth(a, b);

  Remember();
  auto const result = f.GetRoutingManager().ApplyRouteSketch(stroke, metersPerPoint);
  if (!result.applied)
  {
    History().pop_back();
    return MWMRouteSketchResultNone;
  }
  // The GPS judges the drawn route once it's built (TakeSketchVerdict).
  f.GetRoutingManager().BackseatSketchApplied(result.addedStop, result.trimmed);
  [self rebuildWithBestRouter:NO];

  MWMRouteSketchResult out = MWMRouteSketchResultApplied;
  if (result.addedStop)
    out |= MWMRouteSketchResultAddedStop;
  if (result.trimmed)
    out |= MWMRouteSketchResultTrimmed;
  return out;
}

+ (MWMRouteSketchResult)buildSketch:(NSArray<NSArray<CLLocation *> *> *)items inView:(UIView *)mapView
{
  auto & f = GetFramework();
  auto & manager = f.GetRoutingManager();
  if (items.count == 0)
    return MWMRouteSketchResultNone;

  CGFloat const scale = mapView.contentScaleFactor;
  CGPoint const mid = CGPointMake(CGRectGetMidX(mapView.bounds), CGRectGetMidY(mapView.bounds));
  double const metersPerPoint = mercator::DistanceOnEarth(f.PtoG(m2::PointD(mid.x * scale, mid.y * scale)),
                                                          f.PtoG(m2::PointD((mid.x + 1) * scale, mid.y * scale)));

  // Start: the route's (unless it's behind us mid-drive), or here.
  auto const existing = manager.GetRoutePoints();
  std::optional<RouteMarkData> start;
  std::optional<RouteMarkData> finish;
  bool const passedAny = std::any_of(existing.begin(), existing.end(), [](auto const & p) { return p.m_isPassed; });
  if (!existing.empty() && !passedAny && existing.front().m_pointType == RouteMarkType::Start)
    start = existing.front();
  if (!existing.empty() && existing.back().m_pointType == RouteMarkType::Finish)
    finish = existing.back();
  if (!start)
  {
    auto const & myPosition = f.GetBookmarkManager().MyPositionMark();
    if (!myPosition.HasPosition())
      return MWMRouteSketchResultNone;
    start = RouteMarkData();
    start->m_isMyPosition = true;
    start->m_position = myPosition.GetPivot();
  }

  auto toLatLon = [](CLLocation * c) { return backseat::LatLon{c.coordinate.latitude, c.coordinate.longitude}; };
  auto toMercator = [](CLLocation * c) { return mercator::FromLatLon(c.coordinate.latitude, c.coordinate.longitude); };

  // The last stop or stroke end becomes the destination when the route has none.
  std::vector<RouteMarkData> middle;
  size_t const maxMiddle = RoutePointsLayout::kMaxIntermediatePointsCount;
  bool trimmed = false;
  size_t stops = 0;
  for (NSArray<CLLocation *> * item in items)
  {
    if (item.count == 1)
    {
      RouteMarkData stop;
      stop.m_position = toMercator(item.firstObject);
      middle.push_back(std::move(stop));
      ++stops;
      continue;
    }
    std::vector<backseat::LatLon> stroke;
    for (CLLocation * c in item)
      stroke.push_back(toLatLon(c));
    backseat::SketchOptions options;
    options.toleranceM = std::max(25.0, 10 * metersPerPoint);
    size_t const left = maxMiddle > middle.size() + 1 ? maxMiddle - middle.size() - 1 : 0;
    options.maxPoints = std::min(options.maxPoints, left);
    auto const shaping = backseat::ShapingPoints(stroke, options);
    if (shaping.size() < backseat::ShapingPoints(stroke, backseat::SketchOptions{}).size())
      trimmed = true;
    for (auto const & ll : shaping)
    {
      RouteMarkData point;
      point.m_isShaping = true;
      point.m_position = mercator::FromLatLon(ll.lat, ll.lon);
      middle.push_back(std::move(point));
    }
  }
  if (!finish)
  {
    // No destination yet: the last stop, or where the drawing ends.
    auto const lastStop = std::find_if(middle.rbegin(), middle.rend(), [](auto const & p) { return !p.m_isShaping; });
    if (lastStop != middle.rend())
    {
      finish = *lastStop;
      middle.erase(std::next(lastStop).base(), middle.end());
    }
    else
    {
      NSArray<CLLocation *> * lastStroke = items.lastObject;
      finish = RouteMarkData();
      finish->m_position = toMercator(lastStroke.lastObject);
      ++stops;
    }
  }
  if (middle.empty() && !finish)
    return MWMRouteSketchResultNone;

  std::vector<RouteMarkData> all;
  all.push_back(*start);
  all.insert(all.end(), std::make_move_iterator(middle.begin()), std::make_move_iterator(middle.end()));
  all.push_back(*finish);

  Remember();
  manager.RemoveRoutePoints();
  for (size_t i = 0; i < all.size(); ++i)
  {
    auto & p = all[i];
    p.m_isPassed = false;
    p.m_pointType = i == 0 ? RouteMarkType::Start : i + 1 == all.size() ? RouteMarkType::Finish : RouteMarkType::Intermediate;
    p.m_intermediateIndex = p.m_pointType == RouteMarkType::Intermediate ? i - 1 : 0;
    manager.AddRoutePoint(std::move(p), false /* reorderIntermediatePoints */);
  }
  manager.BackseatSketchApplied(stops > 0, trimmed);
  [self rebuildWithBestRouter:NO];

  MWMRouteSketchResult out = MWMRouteSketchResultApplied;
  if (stops > 0)
    out |= MWMRouteSketchResultAddedStop;
  if (trimmed)
    out |= MWMRouteSketchResultTrimmed;
  return out;
}

+ (CLLocationCoordinate2D)coordinateAtPoint:(CGPoint)point inView:(UIView *)mapView
{
  CGFloat const scale = mapView.contentScaleFactor;
  auto const ll = mercator::ToLatLon(GetFramework().P3dtoG(m2::PointD(point.x * scale, point.y * scale)));
  return CLLocationCoordinate2DMake(ll.m_lat, ll.m_lon);
}

+ (CGPoint)pointForCoordinate:(CLLocationCoordinate2D)coordinate inView:(UIView *)mapView
{
  CGFloat const scale = mapView.contentScaleFactor;
  auto const p = GetFramework().GtoP3d(mercator::FromLatLon(coordinate.latitude, coordinate.longitude));
  return CGPointMake(p.x / scale, p.y / scale);
}

+ (MWMRouteSketchResult)applySketchCoordinates:(NSArray<CLLocation *> *)coordinates
                                      previous:(NSArray<CLLocation *> *)previous
                                        inView:(UIView *)mapView
{
  auto & f = GetFramework();
  auto & manager = f.GetRoutingManager();
  std::vector<m2::PointD> stroke;
  for (CLLocation * c in [previous arrayByAddingObjectsFromArray:coordinates])
    stroke.push_back(mercator::FromLatLon(c.coordinate.latitude, c.coordinate.longitude));
  if (stroke.size() < 2)
    return MWMRouteSketchResultNone;

  CGFloat const scale = mapView.contentScaleFactor;
  CGPoint const mid = CGPointMake(CGRectGetMidX(mapView.bounds), CGRectGetMidY(mapView.bounds));
  m2::PointD const a = f.PtoG(m2::PointD(mid.x * scale, mid.y * scale));
  m2::PointD const b = f.PtoG(m2::PointD((mid.x + 1) * scale, mid.y * scale));
  double const metersPerPoint = mercator::DistanceOnEarth(a, b);

  // Continuing a line: take the previous stroke back, then draw both as one. The route after the
  // previous stroke goes back on the undo stack, so undo still goes one stroke at a time.
  std::optional<std::vector<RouteMarkData>> afterPrevious;
  if (previous.count > 0 && !History().empty())
  {
    afterPrevious = manager.GetRoutePoints();
    auto before = std::move(History().back());
    History().pop_back();
    manager.RemoveRoutePoints();
    for (auto & point : before)
      manager.AddRoutePoint(std::move(point), false /* reorderIntermediatePoints */);
  }

  Remember();
  auto const result = manager.ApplyRouteSketch(stroke, metersPerPoint);
  if (!result.applied)
  {
    History().pop_back();
    if (afterPrevious)
    {
      // Put the previous stroke's route back as it was.
      History().push_back(manager.GetRoutePoints());
      manager.RemoveRoutePoints();
      for (auto & point : *afterPrevious)
        manager.AddRoutePoint(std::move(point), false /* reorderIntermediatePoints */);
    }
    return MWMRouteSketchResultNone;
  }
  if (afterPrevious)
    History().push_back(std::move(*afterPrevious));
  manager.BackseatSketchApplied(result.addedStop, result.trimmed);
  [self rebuildWithBestRouter:NO];

  MWMRouteSketchResult out = MWMRouteSketchResultApplied;
  if (result.addedStop)
    out |= MWMRouteSketchResultAddedStop;
  if (result.trimmed)
    out |= MWMRouteSketchResultTrimmed;
  return out;
}

+ (BOOL)clearSketch
{
  Remember();
  if (!GetFramework().GetRoutingManager().ClearRouteSketch())
  {
    History().pop_back();
    return NO;
  }
  [self rebuildWithBestRouter:NO];
  return YES;
}

+ (BOOL)hasSketch
{
  return GetFramework().GetRoutingManager().HasRouteSketch();
}

+ (BOOL)canUndoSketch
{
  return !History().empty();
}

+ (BOOL)undoSketch
{
  if (History().empty())
    return NO;
  auto points = std::move(History().back());
  History().pop_back();
  auto & manager = GetFramework().GetRoutingManager();
  if (points.size() < 2)
  {
    // Before the first stroke there was no route: drawing started it from here.
    [self stopRouting];
    manager.RemoveRoutePoints();
    return YES;
  }
  manager.RemoveRoutePoints();
  // Saved in driving order with their types and indexes: put them back exactly.
  for (auto & point : points)
    manager.AddRoutePoint(std::move(point), false /* reorderIntermediatePoints */);
  [self rebuildWithBestRouter:NO];
  return YES;
}

+ (void)forgetSketchHistory
{
  History().clear();
}

@end
