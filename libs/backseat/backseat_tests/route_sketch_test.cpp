#include "check.hpp"

#include "backseat/route_sketch.hpp"

#include <cmath>
#include <vector>

using namespace backseat;

namespace
{
// About 111 m per 0.001 degree of latitude.
std::vector<LatLon> Line(LatLon a, LatLon b, int steps)
{
  std::vector<LatLon> out;
  for (int i = 0; i <= steps; ++i)
    out.push_back({a.lat + (b.lat - a.lat) * i / steps, a.lon + (b.lon - a.lon) * i / steps});
  return out;
}

std::vector<LatLon> LShape()
{
  // North 2.2 km, then east about 1.7 km (at 40 degrees north), with a little hand shake.
  auto stroke = Line({40.0, -105.0}, {40.02, -105.0}, 40);
  auto east = Line({40.02, -105.0}, {40.02, -104.98}, 40);
  stroke.insert(stroke.end(), east.begin() + 1, east.end());
  for (size_t i = 0; i < stroke.size(); i += 3)
    stroke[i].lon += 0.00005;  // ~4 m of wobble
  return stroke;
}
}  // namespace

TEST(SketchSimplifiesHandShake)
{
  auto const simple = SimplifyStroke(LShape(), 40);
  CHECK_EQUAL(simple.size(), 3u);
  CHECK(SketchDistanceM(simple[1], {40.02, -105.0}) < 20);
}

TEST(SketchPointsSitMidStretchNotOnCorners)
{
  auto const pts = ShapingPoints(LShape(), SketchOptions{});
  CHECK_EQUAL(pts.size(), 3u);  // north leg is long enough for two, east leg one
  for (auto const & p : pts)
    CHECK(SketchDistanceM(p, {40.02, -105.0}) > 300);  // nothing on the intersection
}

TEST(SketchStaysUnderBudget)
{
  // A zigzag with 60 corners.
  std::vector<LatLon> stroke;
  for (int i = 0; i <= 60; ++i)
    stroke.push_back({40.0 + i * 0.005, -105.0 + (i % 2) * 0.01});
  SketchOptions o;
  o.maxPoints = 10;
  CHECK(ShapingPoints(stroke, o).size() <= 10);
}

TEST(SketchFromStartToDestinationReplacesOldShaping)
{
  std::vector<SketchRoutePoint> route = {{{40.0, -105.0}, false},
                                         {{40.01, -105.01}, true},
                                         {{40.02, -104.98}, false}};
  auto const plan = PlanSketch(LShape(), route, 200, 99);
  CHECK_EQUAL(plan.from, 0u);
  CHECK_EQUAL(plan.to, 2u);
  CHECK(!plan.newStop);
  CHECK(!plan.trimmed);
  CHECK_EQUAL(plan.shaping.size(), 3u);
}

TEST(SketchEndingOnEmptyMapAddsAStop)
{
  // Only a start: drawing out from it sets the destination.
  std::vector<SketchRoutePoint> route = {{{40.0, -105.0}, false}};
  auto const plan = PlanSketch(LShape(), route, 200, 100);
  CHECK(plan.newStop);
  CHECK_EQUAL(plan.from, 0u);
  CHECK_EQUAL(plan.to, 1u);
  CHECK(SketchDistanceM(plan.newStopAt, {40.02, -104.98}) < 1);
}

TEST(SketchDetourInsertsBeforeNextRealStop)
{
  std::vector<SketchRoutePoint> route = {{{40.0, -105.0}, false},
                                         {{40.005, -104.99}, true},
                                         {{39.99, -104.95}, false}};
  auto const plan = PlanSketch(LShape(), route, 200, 99);
  CHECK(plan.newStop);
  CHECK_EQUAL(plan.to, 2u);  // the old shaping on that leg is dropped
}

TEST(SketchTrimsWhenOutOfSlots)
{
  std::vector<SketchRoutePoint> route = {{{40.0, -105.0}, false}, {{40.02, -104.98}, false}};
  auto const plan = PlanSketch(LShape(), route, 200, 1);
  CHECK_EQUAL(plan.shaping.size(), 1u);
  CHECK(plan.trimmed);
}

TEST(SketchVerdicts)
{
  CHECK_EQUAL(JudgeSketch(22, 34).key, std::string("sketch.slower"));
  CHECK_EQUAL(JudgeSketch(22, 34).minutes, 12);
  CHECK_EQUAL(JudgeSketch(22, 22.4).key, std::string("sketch.same"));
  CHECK_EQUAL(JudgeSketch(22, 19).key, std::string("sketch.faster"));
  CHECK_EQUAL(JudgeSketch(22, 19).minutes, 3);
}

namespace
{
// An east-west road along 40.0 N and a north-south one crossing it at -105.0.
std::vector<RoadSegment> Crossroads()
{
  return {{{40.0, -105.01}, {40.0, -105.0}, 1},
          {{40.0, -105.0}, {40.0, -104.99}, 1},
          {{39.99, -105.0}, {40.0, -105.0}, 2},
          {{40.0, -105.0}, {40.01, -105.0}, 2}};
}
}  // namespace

TEST(SketchResamplesEvenly)
{
  auto const pts = ResampleStroke(Line({40.0, -105.0}, {40.0, -104.99}, 3), 100);
  // About 850 m: a point every 100 m plus the end.
  CHECK(pts.size() >= 9 && pts.size() <= 10);
  for (size_t i = 1; i + 1 < pts.size(); ++i)
    CHECK(std::fabs(SketchDistanceM(pts[i - 1], pts[i]) - 100) < 1);
}

TEST(SketchSnapsOntoTheRoadTraced)
{
  // Drawn ~11 m north of the east-west road.
  auto const snapped = SnapStroke(Line({40.0001, -105.009}, {40.0001, -105.005}, 10), Crossroads());
  CHECK(snapped.size() > 5);
  for (auto const & p : snapped)
  {
    CHECK(std::fabs(p.at.lat - 40.0) < 1e-6);
    CHECK_EQUAL(p.road, 1u);
  }
}

TEST(SketchDropsWhereThereIsNoRoad)
{
  // ~110 m north of anything: nothing to snap to.
  CHECK(SnapStroke(Line({40.001, -105.009}, {40.001, -105.005}, 5), Crossroads()).empty());
}

TEST(SketchStaysOnTheRoadThroughAnIntersection)
{
  // Along the east-west road, wobbling a few meters north as it crosses the north-south one.
  std::vector<LatLon> stroke = Line({40.00003, -105.003}, {40.00003, -104.997}, 20);
  for (auto const & p : SnapStroke(stroke, Crossroads()))
    CHECK_EQUAL(p.road, 1u);
}

namespace
{
// A route 1 km due north, a vertex every 10 m.
std::vector<LatLon> NorthKm() { return Line({40.0, -105.0}, {40.009, -105.0}, 100); }
}  // namespace

TEST(EraseSpanCoversWhatWasRubbed)
{
  auto const line = NorthKm();
  // Rubbed back and forth across the line from ~400 m to ~600 m north.
  auto const cut = EraseSpan(line, Line({40.0036, -105.0001}, {40.0054, -104.9999}, 20), 15);
  CHECK(cut.has_value());
  CHECK(cut->from >= 35 && cut->from <= 41);
  CHECK(cut->to >= 59 && cut->to <= 65);
  // Nowhere near the line: nothing erased.
  CHECK(!EraseSpan(line, Line({40.004, -104.99}, {40.005, -104.99}, 5), 15).has_value());
}

TEST(EraseCutsMerge)
{
  auto const merged = MergeCuts({{50, 60}, {10, 20}, {18, 30}});
  CHECK_EQUAL(merged.size(), 2u);
  CHECK_EQUAL(merged[0].from, 10u);
  CHECK_EQUAL(merged[0].to, 30u);
  CHECK_EQUAL(merged[1].from, 50u);
}

TEST(CutAnchorsPinTheKeptRoute)
{
  auto const line = NorthKm();
  auto const [before, after] = CutAnchors(line, {40, 60}, 100);
  CHECK(before.has_value() && after.has_value());
  CHECK(std::fabs(SketchDistanceM(*before, line[40]) - 100) < 2);
  CHECK(std::fabs(SketchDistanceM(*after, line[60]) - 100) < 2);
  // A cut from the very start has nothing before it.
  CHECK(!CutAnchors(line, {0, 20}, 100).first.has_value());
}

TEST(GapMatchesEitherWay)
{
  auto const line = NorthKm();
  RouteCut const cut{40, 60};
  // A detour east of the gap, drawn from its south end to its north end...
  std::vector<LatLon> detour = {line[40], {40.0040, -104.999}, {40.0056, -104.999}, line[60]};
  auto const forward = MatchGap(line, cut, detour, 30);
  CHECK(forward.has_value());
  CHECK(SketchDistanceM(forward->front(), line[40]) < 1);
  // ...or drawn the other way: it comes back turned around.
  std::vector<LatLon> backwards(detour.rbegin(), detour.rend());
  auto const turned = MatchGap(line, cut, backwards, 30);
  CHECK(turned.has_value());
  CHECK(SketchDistanceM(turned->front(), line[40]) < 1);
  // Ends nowhere near the gap.
  CHECK(!MatchGap(line, cut, Line({40.002, -104.99}, {40.003, -104.99}, 3), 30).has_value());
}

TEST(StrokeAcrossRouteReplacesTheStretch)
{
  auto const line = NorthKm();
  // From the route at ~300 m, out east, back onto the route at ~700 m.
  std::vector<LatLon> stroke = {{40.0027, -105.00005}, {40.0030, -104.998}, {40.0060, -104.998}, {40.0063, -105.00005}};
  auto const cut = StrokeAcrossRoute(line, stroke, 20);
  CHECK(cut.has_value());
  CHECK(cut->from >= 28 && cut->from <= 32);
  CHECK(cut->to >= 68 && cut->to <= 72);
  // A stroke that ends off the route doesn't count.
  stroke.back() = {40.0063, -104.995};
  CHECK(!StrokeAcrossRoute(line, stroke, 20).has_value());
}
