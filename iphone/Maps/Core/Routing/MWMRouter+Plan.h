#import "MWMRouter.h"

#import <CoreLocation/CoreLocation.h>

NS_ASSUME_NONNULL_BEGIN

@interface MWMRouter (Plan)

/// Point-by-point planning: replaces the route with my position, then `points` in order, so each
/// leg is the best route from one point to the next. The last point is the destination. The
/// indexes in `stops` are real stops; every other point is a hidden drive-through (shaping) point.
/// Rebuilds the route. NO if my position isn't known yet or there are no points.
+ (BOOL)planRouteThrough:(NSArray<CLLocation *> *)points stops:(NSIndexSet *)stops
    NS_SWIFT_NAME(planRoute(through:stops:));

@end

NS_ASSUME_NONNULL_END
