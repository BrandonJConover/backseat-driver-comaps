#import "MWMRouter+Plan.h"
#import "MWMLocationManager.h"

#include <CoreApi/Framework.h>

#include "geometry/mercator.hpp"

@implementation MWMRouter (Plan)

+ (BOOL)planRouteThrough:(NSArray<CLLocation *> *)points stops:(NSIndexSet *)stops
{
  CLLocation * here = [MWMLocationManager lastLocation];
  if (!here || points.count == 0)
    return NO;

  auto & routing = GetFramework().GetRoutingManager();
  routing.RemoveRoutePoints();

  RouteMarkData start;
  start.m_pointType = RouteMarkType::Start;
  start.m_isMyPosition = true;
  start.m_position = mercator::FromLatLon(here.coordinate.latitude, here.coordinate.longitude);
  routing.AddRoutePoint(std::move(start), false /* reorderIntermediatePoints */);

  // CoMaps takes up to 100 points between start and finish.
  NSUInteger const count = MIN(points.count, (NSUInteger)RoutePointsLayout::kMaxIntermediatePointsCount + 1);
  size_t stopNumber = 0;
  for (NSUInteger i = 0; i < count; ++i)
  {
    bool const isFinish = i + 1 == count;
    bool const isStop = isFinish || [stops containsIndex:i];
    RouteMarkData point;
    point.m_pointType = isFinish ? RouteMarkType::Finish : RouteMarkType::Intermediate;
    point.m_intermediateIndex = isFinish ? 0 : i;
    point.m_isShaping = !isStop;
    if (isStop && !isFinish)
      point.m_title = "Stop " + std::to_string(++stopNumber);
    else if (isFinish)
      point.m_title = "End";
    CLLocationCoordinate2D const c = points[i].coordinate;
    point.m_position = mercator::FromLatLon(c.latitude, c.longitude);
    routing.AddRoutePoint(std::move(point), false /* reorderIntermediatePoints */);
  }
  [self rebuildWithBestRouter:NO];
  return YES;
}

@end
