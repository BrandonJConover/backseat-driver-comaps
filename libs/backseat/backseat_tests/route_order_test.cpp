#include "check.hpp"

#include "backseat/route_order.hpp"
#include "backseat/route_sketch.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace backseat;

namespace
{
// Points along a line of longitude, about 1.1 km apart per 0.01 degree of latitude.
LatLon At(double km) { return {40.0 + km / 111.0, -105.0}; }
}  // namespace

TEST(StopOrderKeepsStartAndEnd)
{
  auto const order = OptimizeStopOrder({At(0), At(3), At(1), At(2), At(4)});
  CHECK_EQUAL(order.front(), 0u);
  CHECK_EQUAL(order.back(), 4u);
}

TEST(StopOrderVisitsStopsOnTheWay)
{
  // Start at 0 km, end at 10 km, stops given out of order.
  std::vector<LatLon> const points = {At(0), At(7), At(2), At(9), At(4), At(10)};
  auto const order = OptimizeStopOrder(points);
  std::vector<size_t> const expected = {0, 2, 4, 1, 3, 5};
  CHECK(order == expected);
}

TEST(StopOrderNeverWorseThanAsGiven)
{
  // Ten stops scattered: past the exhaustive limit, so nearest-next plus 2-opt.
  std::vector<LatLon> points = {At(0)};
  for (double km : {8.0, 1.0, 6.0, 3.0, 9.0, 2.0, 7.0, 4.0, 5.0, 0.5})
    points.push_back({40.0 + km / 111.0, -105.0 + (static_cast<int>(km * 10) % 3) * 0.002});
  points.push_back(At(10));
  std::vector<size_t> asGiven(points.size());
  for (size_t i = 0; i < asGiven.size(); ++i)
    asGiven[i] = i;
  auto const order = OptimizeStopOrder(points);
  CHECK_EQUAL(order.size(), points.size());
  CHECK(OrderCost(points, order) <= OrderCost(points, asGiven));
  CHECK_EQUAL(order.front(), 0u);
  CHECK_EQUAL(order.back(), points.size() - 1);
}

TEST(StopOrderUsesTheGivenCost)
{
  // A cost that makes going to stop 2 first cheaper, against straight-line distance.
  std::vector<LatLon> const points = {At(0), At(1), At(2), At(3)};
  auto const order = OptimizeStopOrder(points, [&](LatLon const & a, LatLon const & b)
  {
    if (a.lat == points[0].lat && b.lat == points[2].lat)
      return 0.0;
    return 1000.0;
  });
  std::vector<size_t> const expected = {0, 2, 1, 3};
  CHECK(order == expected);
}

namespace
{
bool VisitsEveryPointOnce(std::vector<size_t> order, size_t n)
{
  if (order.size() != n || order.front() != 0 || order.back() != n - 1)
    return false;
  std::sort(order.begin(), order.end());
  for (size_t i = 0; i < n; ++i)
    if (order[i] != i)
      return false;
  return true;
}

std::vector<LatLon> TenStops()
{
  std::vector<LatLon> points = {At(0)};
  for (double km : {8.0, 1.0, 6.0, 3.0, 9.0, 2.0, 7.0, 4.0, 5.0, 0.5})
    points.push_back(At(km));
  points.push_back(At(10));
  return points;
}
}  // namespace

TEST(StopOrderKeepsEveryStopWhenTheRouterFindsNoRoad)
{
  // A host cost that says "no road" (infinity) or fails (NaN) for some legs must not drop stops:
  // before, nearest-next found no finite leg from stop 3 and visited the start again instead.
  for (double bad : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), -5.0})
  {
    for (auto const & points : {TenStops(), std::vector<LatLon>{At(0), At(3), At(1), At(2), At(4)}})
    {
      auto const order = OptimizeStopOrder(points, [&](LatLon const & a, LatLon const & b)
      {
        if (a.lat == points[3].lat || b.lat == points[2].lat)
          return bad;
        return SketchDistanceM(a, b);
      });
      CHECK(VisitsEveryPointOnce(order, points.size()));
    }
  }
}

TEST(StopOrderKeepsEveryStopWhenNoLegCanBePriced)
{
  auto const points = TenStops();
  auto const order = OptimizeStopOrder(points, [](LatLon const &, LatLon const &)
  { return std::numeric_limits<double>::quiet_NaN(); });
  CHECK(VisitsEveryPointOnce(order, points.size()));
  CHECK(std::isfinite(OrderCost(points, order, [](LatLon const &, LatLon const &) { return -1.0; })));
}

TEST(StopOrderAvoidsTheUnreachableLegWhenItCan)
{
  // Stop 1 -> stop 2 has no road (one-way the other way); the optimizer goes 0, 2, 1, 3 instead of the shorter-looking 0, 1, 2, 3.
  std::vector<LatLon> const points = {At(0), At(1), At(2), At(3)};
  auto const order = OptimizeStopOrder(points, [&](LatLon const & a, LatLon const & b)
  {
    if (a.lat == points[1].lat && b.lat == points[2].lat)
      return std::numeric_limits<double>::infinity();
    return SketchDistanceM(a, b);
  });
  std::vector<size_t> const expected = {0, 2, 1, 3};
  CHECK(order == expected);
}

TEST(StopOrderHandlesPointsWithNoCoordinates)
{
  auto points = TenStops();
  points[4] = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
  CHECK(VisitsEveryPointOnce(OptimizeStopOrder(points), points.size()));
}

TEST(StopOrderSmallTripsComeBackAsGiven)
{
  CHECK(OptimizeStopOrder({}).empty());
  CHECK(OptimizeStopOrder({At(0)}) == std::vector<size_t>{0});
  CHECK((OptimizeStopOrder({At(0), At(5)}) == std::vector<size_t>{0, 1}));
  CHECK((OptimizeStopOrder({At(0), At(9), At(5)}) == std::vector<size_t>{0, 1, 2}));
  CHECK_EQUAL(OrderCost({At(0)}, {0}), 0.0);
  CHECK_EQUAL(OrderCost({}, {}), 0.0);
}

TEST(OrderCostRefusesIndexesPastTheEnd)
{
  std::vector<LatLon> const points = {At(0), At(1)};
  CHECK(std::isinf(OrderCost(points, {0, 1, 7})));
  CHECK(std::isinf(OrderCost(points, {9, 0})));
}
