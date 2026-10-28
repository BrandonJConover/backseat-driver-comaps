#import "MWMRouter.h"

#import <CoreLocation/CoreLocation.h>

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
/// Like applySketch:inView: for a stroke already turned into coordinates (it may run off screen,
/// after panning). With `previous` (the line this stroke continues), the two are applied as one
/// line in place of the previous one; undo still takes them back one stroke at a time.
+ (MWMRouteSketchResult)applySketchCoordinates:(NSArray<CLLocation *> *)coordinates
                                      previous:(NSArray<CLLocation *> *)previous
                                        inView:(UIView *)mapView;
/// Draw mode's "Go": builds the route from everything drawn, in the order it was drawn. Each item
/// is either a stroke (two or more coordinates: roads to take) or a stop (one coordinate, from a
/// long press). The route starts here (or keeps its start) and ends at the route's destination if
/// there is one, otherwise at the last stop, otherwise where the last stroke ends. Replaces any
/// earlier drawing and stops in between.
+ (MWMRouteSketchResult)buildSketch:(NSArray<NSArray<CLLocation *> *> *)items inView:(UIView *)mapView;
/// Draw mode session: remembers the route as it was, so Cancel can put it back.
+ (void)beginSketchSession;
/// Puts the route back as it was when the session began (no route if there was none).
+ (void)cancelSketchSession;

/// Dragging the route line (MapQuest style). Begins at `point` if it's on the route line (a new via
/// point there) or on a via point (`handle`, from viaHandlesInView:). NO if neither.
+ (BOOL)beginRouteDragAt:(CGPoint)point inView:(UIView *)mapView;
/// Moves the via point under the finger; the route re-plans at most every 300 ms.
+ (void)moveRouteDragTo:(CGPoint)point inView:(UIView *)mapView;
/// Drops it there and re-plans.
+ (void)endRouteDrag;
/// Where the via points (drawn or dragged shaping points) are on screen, in route order.
+ (NSArray<NSValue *> *)viaHandlesInView:(UIView *)mapView;
/// Removes the via point nearest `point` (within a finger's reach) and re-plans. NO if none.
+ (BOOL)removeViaAt:(CGPoint)point inView:(UIView *)mapView;

/// Where a point of the map view is on the map, and back.
+ (CLLocationCoordinate2D)coordinateAtPoint:(CGPoint)point inView:(UIView *)mapView;
+ (CGPoint)pointForCoordinate:(CLLocationCoordinate2D)coordinate inView:(UIView *)mapView;
+ (BOOL)clearSketch;
+ (BOOL)hasSketch;
/// Puts the route back the way it was before the last stroke (or Erase) and rebuilds; with nothing
/// before it, the route goes away. NO if there's nothing to undo.
+ (BOOL)undoSketch;
+ (BOOL)canUndoSketch;
/// Forgets the undo history (the drawing screen closed).
+ (void)forgetSketchHistory;

@end

NS_ASSUME_NONNULL_END
