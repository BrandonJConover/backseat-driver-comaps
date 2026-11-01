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
/// Draw mode is open: the map stays where the driver puts it. Location follow stops, route rebuilds
/// don't fit the map to the route, and navigation's snap-back to the car waits. Closing restores
/// following when a route is being driven.
+ (void)setDrawModeOpen:(BOOL)open;
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

/// The planner panel: the route's real points (start, stops, destination; not drawn or dragged via
/// points), each a dictionary with "title" (NSString), "kind" ("start", "stop", "finish") and,
/// once the route is built, "legSeconds" and "legMeters" (NSNumber) from the point before.
+ (NSArray<NSDictionary<NSString *, id> *> *)plannerStops;
/// "seconds" and "meters" for the whole built route; nil while it builds.
+ (nullable NSDictionary<NSString *, NSNumber *> *)plannerSummary;
/// Moves a planner row (not the start, row 0) to another position, and re-plans. Via points are
/// dropped, since they belonged to the old order.
+ (void)movePlannerStopFrom:(NSInteger)from to:(NSInteger)to;
+ (void)removePlannerStopAt:(NSInteger)index;
/// Keeps the start and the destination and visits the stops between in the shortest order.
+ (void)optimizePlannerStops;
/// The destination is back where the trip starts.
@property(class, nonatomic, getter=isRoundTrip) BOOL roundTrip;

// Erase and reconnect. Cuts are pairs of vertex indexes into a snapshot of the route line.

/// The built route's line, a point about every 10 m; empty while it builds.
+ (NSArray<CLLocation *> *)routeLine;
/// Meters per screen point at the middle of the map view now.
+ (double)metersPerPointInView:(UIView *)mapView;
/// The stretch of `line` the eraser went over: @[from, to], or nil.
+ (nullable NSArray<NSNumber *> *)eraseSpanOn:(NSArray<CLLocation *> *)line
                                       eraser:(NSArray<CLLocation *> *)eraser
                                      radiusM:(double)radiusM;
/// The stroke as the way across cut [from, to], oriented start to end; nil if it doesn't connect it.
+ (nullable NSArray<CLLocation *> *)matchGapOn:(NSArray<CLLocation *> *)line
                                          from:(NSInteger)from
                                            to:(NSInteger)to
                                        stroke:(NSArray<CLLocation *> *)stroke
                                        reachM:(double)reachM;
/// A stroke from the route back to the route: the stretch it replaces as @[from, to], or nil.
+ (nullable NSArray<NSNumber *> *)strokeAcrossRoute:(NSArray<CLLocation *> *)line
                                             stroke:(NSArray<CLLocation *> *)stroke
                                             reachM:(double)reachM;
/// Re-plans the route with each filled cut going the drawn way: via points pinned just outside each
/// cut keep the rest of the route as it was. `fills` are @{"from", "to", "stroke"} on `line`.
+ (MWMRouteSketchResult)applyRouteFills:(NSArray<NSDictionary<NSString *, id> *> *)fills
                                   line:(NSArray<CLLocation *> *)line
                                 inView:(UIView *)mapView;

/// A finger stroke moved onto the car roads it traced, or the stroke itself when there's no road
/// data to snap to.
+ (NSArray<CLLocation *> *)snapStrokeToRoads:(NSArray<CLLocation *> *)stroke;
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
