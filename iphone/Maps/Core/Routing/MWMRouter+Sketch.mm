#import "MWMRouter+Sketch.h"

#include <CoreApi/Framework.h>

#include "geometry/mercator.hpp"

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

  auto const result = f.GetRoutingManager().ApplyRouteSketch(stroke, metersPerPoint);
  if (!result.applied)
    return MWMRouteSketchResultNone;
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

+ (BOOL)clearSketch
{
  if (!GetFramework().GetRoutingManager().ClearRouteSketch())
    return NO;
  [self rebuildWithBestRouter:NO];
  return YES;
}

+ (BOOL)hasSketch
{
  return GetFramework().GetRoutingManager().HasRouteSketch();
}

@end
