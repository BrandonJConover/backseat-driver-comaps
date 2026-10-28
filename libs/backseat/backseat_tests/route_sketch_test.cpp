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
