#import "MWMRouter.h"

NS_ASSUME_NONNULL_BEGIN

typedef NS_OPTIONS(NSUInteger, MWMRouteSketchResult) {
  MWMRouteSketchResultNone = 0,
  MWMRouteSketchResultApplied = 1 << 0,
  /// The stroke ended on empty map, so a stop was added there.
  MWMRouteSketchResultAddedStop = 1 << 1,
  /// The 100-point limit cut the stroke short.
  MWMRouteSketchResultTrimmed = 1 << 2,
};

@interface MWMRouter (Sketch)

/// `points` are in the map view's coordinates (points, not pixels), in drawing order.
/// Reshapes the route along the stroke and rebuilds it.
+ (MWMRouteSketchResult)applySketch:(NSArray<NSValue *> *)points inView:(UIView *)mapView;
/// Removes the drawing (keeps the stops) and rebuilds. NO if there was nothing drawn.
+ (BOOL)clearSketch;
+ (BOOL)hasSketch;

@end

NS_ASSUME_NONNULL_END
