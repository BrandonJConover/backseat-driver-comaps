#import "MWMRouter+Sketch.h"

#include <CoreApi/Framework.h>

#include "geometry/mercator.hpp"

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
