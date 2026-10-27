#include "check.hpp"

#include "backseat/route_sketch.hpp"

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
