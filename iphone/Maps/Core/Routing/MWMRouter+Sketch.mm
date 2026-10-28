#import "MWMRouter+Sketch.h"

#include <CoreApi/Framework.h>

#include "backseat/route_sketch.hpp"
#include "geometry/mercator.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <cmath>
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

/// The route when Draw mode opened.
std::optional<std::vector<RouteMarkData>> & SessionStart()
{
  static std::optional<std::vector<RouteMarkData>> start;
  return start;
}

/// Puts `points` back as the route, in this order, with types and indexes made consistent.
void SetPoints(std::vector<RouteMarkData> points)
{
  auto & manager = GetFramework().GetRoutingManager();
  manager.RemoveRoutePoints();
  for (size_t i = 0; i < points.size(); ++i)
  {
    auto & p = points[i];
    p.m_pointType = i == 0 ? RouteMarkType::Start : i + 1 == points.size() ? RouteMarkType::Finish : RouteMarkType::Intermediate;
    p.m_intermediateIndex = p.m_pointType == RouteMarkType::Intermediate ? i - 1 : 0;
    manager.AddRoutePoint(std::move(p), false /* reorderIntermediatePoints */);
  }
}

/// Screen distance (points) within which a finger is on the line or on a handle.
CGFloat constexpr kReach = 28;

m2::PointD ToMap(CGPoint p, UIView * view)
{
  CGFloat const s = view.contentScaleFactor;
  return GetFramework().P3dtoG(m2::PointD(p.x * s, p.y * s));
}

CGPoint ToScreen(m2::PointD const & g, UIView * view)
{
  CGFloat const s = view.contentScaleFactor;
  auto const p = GetFramework().GtoP3d(g);
  return CGPointMake(p.x / s, p.y / s);
}

CGFloat Distance(CGPoint a, CGPoint b) { return hypot(a.x - b.x, a.y - b.y); }

/// Distance from `p` to segment ab, on screen.
CGFloat SegmentDistance(CGPoint p, CGPoint a, CGPoint b)
{
  CGFloat const dx = b.x - a.x, dy = b.y - a.y;
  CGFloat const len2 = dx * dx + dy * dy;
  CGFloat t = len2 == 0 ? 0 : ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
  t = std::clamp<CGFloat>(t, 0, 1);
  return Distance(p, CGPointMake(a.x + t * dx, a.y + t * dy));
}

/// The dragged via point's index in the route points, while a drag is on.
std::optional<size_t> gDragIndex;
CFAbsoluteTime gLastPlan = 0;
BOOL gPlanPending = NO;
double constexpr kPlanEvery = 0.3;
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

  // The last stop or stroke end becomes the destination when the route has none. Points already in
  // the route (stops, dragged vias) stay, ahead of what was drawn.
  std::vector<RouteMarkData> middle;
  if (!passedAny && existing.size() > 2 && existing.front().m_pointType == RouteMarkType::Start)
    middle.assign(existing.begin() + 1, existing.end() - 1);
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

+ (void)setDrawModeOpen:(BOOL)open
{
  static NSTimer * keepPut;
  auto & f = GetFramework();
  f.GetRoutingManager().SetBackseatKeepViewport(open);
  [keepPut invalidate];
  keepPut = nil;
  if (open)
  {
    f.StopLocationFollow();
    f.GetRoutingManager().DisableFollowMode();
    // While a route is followed the map returns to the car after 20 s untouched; any redraw resets
    // that clock, so nudge one while drawing.
    keepPut = [NSTimer scheduledTimerWithTimeInterval:5 repeats:YES block:^(NSTimer *) {
      GetFramework().InvalidateRendering();
    }];
  }
  else if ([self isOnRoute])
  {
    f.GetRoutingManager().FollowRoute();
  }
}

+ (void)beginSketchSession
{
  History().clear();
  SessionStart() = GetFramework().GetRoutingManager().GetRoutePoints();
}

+ (void)cancelSketchSession
{
  auto start = std::exchange(SessionStart(), std::nullopt);
  bool const changed = !History().empty();
  History().clear();
  if (!start || !changed)
    return;
  if (start->size() < 2)
  {
    [self stopRouting];
    GetFramework().GetRoutingManager().RemoveRoutePoints();
    return;
  }
  for (auto & p : *start)
    p.m_isPassed = false;
  SetPoints(std::move(*start));
  [self rebuildWithBestRouter:NO];
}

+ (BOOL)beginRouteDragAt:(CGPoint)point inView:(UIView *)mapView
{
  auto & manager = GetFramework().GetRoutingManager();
  if (!manager.IsRoutingActive())
    return NO;
  auto points = manager.GetRoutePoints();
  if (points.size() < 2)
    return NO;

  // On a via point already: drag that one.
  for (size_t i = 1; i + 1 < points.size(); ++i)
  {
    if (points[i].m_isShaping && Distance(ToScreen(points[i].m_position, mapView), point) <= kReach)
    {
      Remember();
      gDragIndex = i;
      return YES;
    }
  }

  // On the line: where along it, and so between which two route points the new via goes. (Only
  // once a route is built: there's no line before that.)
  auto const route = manager.RoutingSession().GetRouteForTests();
  if (!route || !route->IsValid())
    return NO;
  auto const & line = route->GetFollowedPolyline().GetPolyline().GetPoints();
  if (line.size() < 2)
    return NO;
  std::vector<CGPoint> screen;
  screen.reserve(line.size());
  for (auto const & g : line)
    screen.push_back(ToScreen(g, mapView));
  size_t nearest = 0;
  CGFloat best = CGFLOAT_MAX;
  for (size_t i = 0; i + 1 < screen.size(); ++i)
  {
    CGFloat const d = SegmentDistance(point, screen[i], screen[i + 1]);
    if (d < best)
    {
      best = d;
      nearest = i;
    }
  }
  if (best > kReach)
    return NO;
  auto lineIndexOf = [&](m2::PointD const & g)
  {
    size_t idx = 0;
    double bestD = std::numeric_limits<double>::max();
    for (size_t i = 0; i < line.size(); ++i)
    {
      double const d = line[i].SquaredLength(g);
      if (d < bestD)
      {
        bestD = d;
        idx = i;
      }
    }
    return idx;
  };
  size_t insertAt = points.size() - 1;
  for (size_t i = 1; i + 1 < points.size(); ++i)
  {
    if (lineIndexOf(points[i].m_position) > nearest)
    {
      insertAt = i;
      break;
    }
  }
  Remember();
  RouteMarkData via;
  via.m_isShaping = true;
  via.m_position = ToMap(point, mapView);
  points.insert(points.begin() + insertAt, std::move(via));
  SetPoints(std::move(points));
  gDragIndex = insertAt;
  return YES;
}

+ (void)moveRouteDragTo:(CGPoint)point inView:(UIView *)mapView
{
  if (!gDragIndex)
    return;
  auto points = GetFramework().GetRoutingManager().GetRoutePoints();
  if (*gDragIndex >= points.size())
    return;
  points[*gDragIndex].m_position = ToMap(point, mapView);
  SetPoints(std::move(points));
  // Re-plan as the finger moves, but not more often than the router can keep up with.
  CFAbsoluteTime const now = CFAbsoluteTimeGetCurrent();
  if (now - gLastPlan >= kPlanEvery)
  {
    gLastPlan = now;
    [self rebuildWithBestRouter:NO];
  }
  else if (!gPlanPending)
  {
    gPlanPending = YES;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, int64_t(kPlanEvery * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
      gPlanPending = NO;
      if (gDragIndex)
      {
        gLastPlan = CFAbsoluteTimeGetCurrent();
        [self rebuildWithBestRouter:NO];
      }
    });
  }
}

+ (void)endRouteDrag
{
  if (!gDragIndex)
    return;
  gDragIndex.reset();
  gLastPlan = CFAbsoluteTimeGetCurrent();
  [self rebuildWithBestRouter:NO];
}

+ (NSArray<NSValue *> *)viaHandlesInView:(UIView *)mapView
{
  NSMutableArray<NSValue *> * handles = [NSMutableArray array];
  for (auto const & p : GetFramework().GetRoutingManager().GetRoutePoints())
    if (p.m_isShaping)
      [handles addObject:[NSValue valueWithCGPoint:ToScreen(p.m_position, mapView)]];
  return handles;
}

+ (BOOL)removeViaAt:(CGPoint)point inView:(UIView *)mapView
{
  auto points = GetFramework().GetRoutingManager().GetRoutePoints();
  for (size_t i = 1; i + 1 < points.size(); ++i)
  {
    if (points[i].m_isShaping && Distance(ToScreen(points[i].m_position, mapView), point) <= kReach)
    {
      Remember();
      points.erase(points.begin() + i);
      SetPoints(std::move(points));
      [self rebuildWithBestRouter:NO];
      return YES;
    }
  }
  return NO;
}

+ (NSArray<CLLocation *> *)snapStrokeToRoads:(NSArray<CLLocation *> *)stroke
{
  std::vector<backseat::LatLon> in;
  for (CLLocation * c in stroke)
    in.push_back({c.coordinate.latitude, c.coordinate.longitude});
  auto const snapped = GetFramework().GetRoutingManager().BackseatSnapToRoads(in);
  if (snapped.size() < 2)
    return stroke;
  NSMutableArray<CLLocation *> * out = [NSMutableArray arrayWithCapacity:snapped.size()];
  for (auto const & p : snapped)
    [out addObject:[[CLLocation alloc] initWithLatitude:p.lat longitude:p.lon]];
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
