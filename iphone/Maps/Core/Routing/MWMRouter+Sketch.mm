#import "MWMRouter+Sketch.h"

#include <CoreApi/Framework.h>

#include "backseat/route_order.hpp"
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

namespace
{
backseat::LatLon LatLonOf(RouteMarkData const & p)
{
  auto const ll = mercator::ToLatLon(p.m_position);
  return {ll.m_lat, ll.m_lon};
}

/// The route's real points, in order, without via points.
std::vector<RouteMarkData> RealPoints()
{
  std::vector<RouteMarkData> real;
  for (auto const & p : GetFramework().GetRoutingManager().GetRoutePoints())
    if (!p.m_isShaping)
      real.push_back(p);
  return real;
}

/// A round trip ends within this of where it starts.
double constexpr kRoundTripM = 50;
}  // namespace

+ (NSArray<NSDictionary<NSString *, id> *> *)plannerStops
{
  auto & manager = GetFramework().GetRoutingManager();
  auto const all = manager.GetRoutePoints();
  // Time and distance from the start to each route point, from the route's subroutes (one per pair
  // of consecutive route points, via points included).
  std::vector<double> seconds(all.size(), -1), meters(all.size(), -1);
  auto const route = manager.RoutingSession().GetRouteForTests();
  if (route && route->IsValid() && route->GetSubrouteCount() + 1 == all.size())
  {
    auto const & segments = route->GetRouteSegments();
    seconds[0] = meters[0] = 0;
    for (size_t i = 0; i < route->GetSubrouteCount(); ++i)
    {
      size_t const end = std::min(route->GetSubrouteAttrs(i).GetEndSegmentIdx(), segments.size());
      if (end == 0)
        continue;
      seconds[i + 1] = segments[end - 1].GetTimeFromBeginningSec();
      meters[i + 1] = segments[end - 1].GetDistFromBeginningMeters();
    }
  }
  NSMutableArray * rows = [NSMutableArray array];
  std::optional<size_t> previous;
  int stopNumber = 0;
  for (size_t i = 0; i < all.size(); ++i)
  {
    auto const & p = all[i];
    if (p.m_isShaping)
      continue;
    NSString * kind = p.m_pointType == RouteMarkType::Start ? @"start" : p.m_pointType == RouteMarkType::Finish ? @"finish" : @"stop";
    NSString * title = @(p.m_title.c_str());
    if (title.length == 0)
      title = p.m_isMyPosition ? @"My position" : [kind isEqualToString:@"finish"] ? @"Destination" : [NSString stringWithFormat:@"Stop %d", stopNumber + 1];
    if ([kind isEqualToString:@"stop"])
      ++stopNumber;
    NSMutableDictionary * row = [@{@"title": title, @"kind": kind} mutableCopy];
    if (previous && seconds[i] >= 0 && seconds[*previous] >= 0)
    {
      row[@"legSeconds"] = @(seconds[i] - seconds[*previous]);
      row[@"legMeters"] = @(meters[i] - meters[*previous]);
    }
    previous = i;
    [rows addObject:row];
  }
  return rows;
}

+ (nullable NSDictionary<NSString *, NSNumber *> *)plannerSummary
{
  auto const route = GetFramework().GetRoutingManager().RoutingSession().GetRouteForTests();
  if (!route || !route->IsValid())
    return nil;
  return @{@"seconds": @(route->GetTotalTimeSec()), @"meters": @(route->GetTotalDistanceMeters())};
}

+ (void)movePlannerStopFrom:(NSInteger)from to:(NSInteger)to
{
  auto real = RealPoints();
  if (from <= 0 || to <= 0 || from >= NSInteger(real.size()) || to >= NSInteger(real.size()) || from == to)
    return;
  Remember();
  auto moved = std::move(real[from]);
  real.erase(real.begin() + from);
  real.insert(real.begin() + to, std::move(moved));
  SetPoints(std::move(real));
  [self rebuildWithBestRouter:NO];
}

+ (void)removePlannerStopAt:(NSInteger)index
{
  auto real = RealPoints();
  if (index <= 0 || index >= NSInteger(real.size()) || real.size() <= 2)
    return;
  Remember();
  real.erase(real.begin() + index);
  SetPoints(std::move(real));
  [self rebuildWithBestRouter:NO];
}

+ (void)optimizePlannerStops
{
  auto real = RealPoints();
  if (real.size() < 4)
    return;
  std::vector<backseat::LatLon> points;
  for (auto const & p : real)
    points.push_back(LatLonOf(p));
  auto const order = backseat::OptimizeStopOrder(points);
  Remember();
  std::vector<RouteMarkData> ordered;
  for (size_t i : order)
    ordered.push_back(real[i]);
  SetPoints(std::move(ordered));
  [self rebuildWithBestRouter:NO];
}

+ (BOOL)isRoundTrip
{
  auto const real = RealPoints();
  return real.size() >= 3 && backseat::SketchDistanceM(LatLonOf(real.front()), LatLonOf(real.back())) <= kRoundTripM;
}

+ (void)setRoundTrip:(BOOL)roundTrip
{
  if (roundTrip == [self isRoundTrip])
    return;
  auto all = GetFramework().GetRoutingManager().GetRoutePoints();
  if (all.size() < 2)
    return;
  Remember();
  if (roundTrip)
  {
    RouteMarkData back;
    back.m_position = all.front().m_position;
    back.m_title = "Back to start";
    all.push_back(std::move(back));
  }
  else
  {
    all.pop_back();
  }
  SetPoints(std::move(all));
  [self rebuildWithBestRouter:NO];
}

namespace
{
std::vector<backseat::LatLon> ToLatLons(NSArray<CLLocation *> * locations)
{
  std::vector<backseat::LatLon> out;
  out.reserve(locations.count);
  for (CLLocation * c in locations)
    out.push_back({c.coordinate.latitude, c.coordinate.longitude});
  return out;
}

NSArray<CLLocation *> * ToLocations(std::vector<backseat::LatLon> const & points)
{
  NSMutableArray<CLLocation *> * out = [NSMutableArray arrayWithCapacity:points.size()];
  for (auto const & p : points)
    [out addObject:[[CLLocation alloc] initWithLatitude:p.lat longitude:p.lon]];
  return out;
}

/// Where the kept route is pinned outside a cut.
double constexpr kAnchorM = 80;
}  // namespace

+ (NSArray<CLLocation *> *)routeLine
{
  auto const route = GetFramework().GetRoutingManager().RoutingSession().GetRouteForTests();
  if (!route || !route->IsValid())
    return @[];
  std::vector<backseat::LatLon> line;
  for (auto const & g : route->GetFollowedPolyline().GetPolyline().GetPoints())
  {
    auto const ll = mercator::ToLatLon(g);
    line.push_back({ll.m_lat, ll.m_lon});
  }
  return ToLocations(backseat::ResampleStroke(line, 10));
}

+ (double)metersPerPointInView:(UIView *)mapView
{
  auto & f = GetFramework();
  CGFloat const scale = mapView.contentScaleFactor;
  CGPoint const mid = CGPointMake(CGRectGetMidX(mapView.bounds), CGRectGetMidY(mapView.bounds));
  return mercator::DistanceOnEarth(f.PtoG(m2::PointD(mid.x * scale, mid.y * scale)),
                                   f.PtoG(m2::PointD((mid.x + 1) * scale, mid.y * scale)));
}

+ (nullable NSArray<NSNumber *> *)eraseSpanOn:(NSArray<CLLocation *> *)line
                                       eraser:(NSArray<CLLocation *> *)eraser
                                      radiusM:(double)radiusM
{
  auto const cut = backseat::EraseSpan(ToLatLons(line), ToLatLons(eraser), radiusM);
  if (!cut)
    return nil;
  return @[@(cut->from), @(cut->to)];
}

+ (nullable NSArray<CLLocation *> *)matchGapOn:(NSArray<CLLocation *> *)line
                                          from:(NSInteger)from
                                            to:(NSInteger)to
                                        stroke:(NSArray<CLLocation *> *)stroke
                                        reachM:(double)reachM
{
  auto const way = backseat::MatchGap(ToLatLons(line), {size_t(from), size_t(to)}, ToLatLons(stroke), reachM);
  return way ? ToLocations(*way) : nil;
}

+ (nullable NSArray<NSNumber *> *)strokeAcrossRoute:(NSArray<CLLocation *> *)line
                                             stroke:(NSArray<CLLocation *> *)stroke
                                             reachM:(double)reachM
{
  auto const cut = backseat::StrokeAcrossRoute(ToLatLons(line), ToLatLons(stroke), reachM);
  if (!cut)
    return nil;
  return @[@(cut->from), @(cut->to)];
}

+ (MWMRouteSketchResult)applyRouteFills:(NSArray<NSDictionary<NSString *, id> *> *)fills
                                   line:(NSArray<CLLocation *> *)lineLocations
                                 inView:(UIView *)mapView
{
  auto & manager = GetFramework().GetRoutingManager();
  auto const line = ToLatLons(lineLocations);
  auto points = manager.GetRoutePoints();
  if (fills.count == 0 || line.size() < 2 || points.size() < 2)
    return MWMRouteSketchResultNone;

  // Each route point's place along the line, so new via points can go in between in order.
  auto placeOf = [&](backseat::LatLon const & p)
  {
    size_t best = 0;
    double bestD = std::numeric_limits<double>::max();
    for (size_t i = 0; i < line.size(); ++i)
    {
      double const d = backseat::SketchDistanceM(p, line[i]);
      if (d < bestD)
      {
        bestD = d;
        best = i;
      }
    }
    return double(best);
  };

  std::vector<backseat::RouteCut> cuts;
  for (NSDictionary * fill in fills)
    cuts.push_back({[fill[@"from"] unsignedLongValue], [fill[@"to"] unsignedLongValue]});

  struct Placed
  {
    double at;
    RouteMarkData point;
  };
  std::vector<Placed> middle;
  for (size_t i = 1; i + 1 < points.size(); ++i)
  {
    double const at = placeOf(LatLonOf(points[i]));
    // Via points inside an erased stretch went with it.
    bool const erased = points[i].m_isShaping &&
        std::any_of(cuts.begin(), cuts.end(), [&](auto const & c) { return at >= c.from && at <= c.to; });
    if (!erased)
      middle.push_back({at, points[i]});
  }

  backseat::SketchOptions options;
  options.toleranceM = std::max(25.0, 10 * [self metersPerPointInView:mapView]);
  bool trimmed = false;
  auto shaping = [](backseat::LatLon const & ll)
  {
    RouteMarkData p;
    p.m_isShaping = true;
    p.m_position = mercator::FromLatLon(ll.lat, ll.lon);
    return p;
  };
  for (size_t f = 0; f < cuts.size(); ++f)
  {
    auto const & cut = cuts[f];
    auto const way = ToLatLons(fills[f][@"stroke"]);
    auto const [before, after] = backseat::CutAnchors(line, cut, kAnchorM);
    // The way across sits between the cut's ends, in drawing order.
    double const span = std::max(1.0, double(cut.to) - double(cut.from));
    if (before)
      middle.push_back({double(cut.from) - 0.5, shaping(*before)});
    auto const across = backseat::ShapingPoints(way, options);
    if (across.size() < backseat::ShapingPoints(way, backseat::SketchOptions{}).size())
      trimmed = true;
    for (size_t i = 0; i < across.size(); ++i)
      middle.push_back({double(cut.from) + span * (i + 1) / (across.size() + 1), shaping(across[i])});
    if (after)
      middle.push_back({double(cut.to) + 0.5, shaping(*after)});
  }
  std::stable_sort(middle.begin(), middle.end(), [](auto const & a, auto const & b) { return a.at < b.at; });

  std::vector<RouteMarkData> all;
  all.push_back(points.front());
  for (auto & m : middle)
    all.push_back(std::move(m.point));
  all.push_back(points.back());
  size_t const maxPoints = RoutePointsLayout::kMaxIntermediatePointsCount + 2;
  if (all.size() > maxPoints)
  {
    trimmed = true;
    auto const finish = all.back();
    all.resize(maxPoints - 1);
    all.push_back(finish);
  }

  Remember();
  SetPoints(std::move(all));
  manager.BackseatSketchApplied(false, trimmed);
  [self rebuildWithBestRouter:NO];
  return trimmed ? (MWMRouteSketchResultApplied | MWMRouteSketchResultTrimmed) : MWMRouteSketchResultApplied;
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
