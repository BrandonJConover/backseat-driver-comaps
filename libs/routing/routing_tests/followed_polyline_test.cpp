#include "testing/testing.hpp"

#include "routing/base/followed_polyline.hpp"

#include "geometry/mercator.hpp"
#include "geometry/polyline2d.hpp"

namespace routing_test
{
using namespace routing;

namespace
{
static m2::PolylineD const kTestDirectedPolyline1(std::vector<m2::PointD>{{0.0, 0.0}, {3.0, 0.0}, {5.0, 0.0}});
static m2::PolylineD const kTestDirectedPolyline2(std::vector<m2::PointD>{{6.0, 0.0}, {7.0, 0.0}});
}  // namespace

UNIT_TEST(FollowedPolylineAppend)
{
  FollowedPolyline followedPolyline1(kTestDirectedPolyline1.Begin(), kTestDirectedPolyline1.End());
  FollowedPolyline const followedPolyline2(kTestDirectedPolyline2.Begin(), kTestDirectedPolyline2.End());

  TEST_EQUAL(followedPolyline1.GetPolyline(), kTestDirectedPolyline1, ());
  followedPolyline1.Append(followedPolyline2);
  TEST_EQUAL(followedPolyline1.GetPolyline().GetSize(), 5, ());

  m2::PolylineD polyline1 = kTestDirectedPolyline1;
  polyline1.Append(kTestDirectedPolyline2);
  TEST_EQUAL(followedPolyline1.GetPolyline(), polyline1, ());
}

UNIT_TEST(FollowedPolylinePop)
{
  FollowedPolyline followedPolyline(kTestDirectedPolyline1.Begin(), kTestDirectedPolyline1.End());

  TEST_EQUAL(followedPolyline.GetPolyline(), kTestDirectedPolyline1, ());
  TEST_EQUAL(followedPolyline.GetPolyline().GetSize(), 3, ());
  followedPolyline.PopBack();
  TEST_EQUAL(followedPolyline.GetPolyline().GetSize(), 2, ());
}

UNIT_TEST(FollowedPolylineInitializationFogTest)
{
  FollowedPolyline polyline(kTestDirectedPolyline1.Begin(), kTestDirectedPolyline1.End());
  TEST(polyline.IsValid(), ());
  TEST_EQUAL(polyline.GetCurrentIter().m_ind, 0, ());
  TEST_EQUAL(polyline.GetPolyline().GetSize(), 3, ());
}

UNIT_TEST(FollowedPolylineFollowingTestByProjection)
{
  FollowedPolyline polyline(kTestDirectedPolyline1.Begin(), kTestDirectedPolyline1.End());
  TEST_EQUAL(polyline.GetCurrentIter().m_ind, 0, ());
  polyline.UpdateProjection(mercator::RectByCenterXYAndSizeInMeters({0, 0}, 2));
  TEST_EQUAL(polyline.GetCurrentIter().m_ind, 0, ());
  TEST_EQUAL(polyline.GetCurrentIter().m_pt, m2::PointD(0, 0), ());
  polyline.UpdateProjection(mercator::RectByCenterXYAndSizeInMeters({1, 0}, 2));
  TEST_EQUAL(polyline.GetCurrentIter().m_ind, 0, ());
  TEST_EQUAL(polyline.GetCurrentIter().m_pt, m2::PointD(1, 0), ());
  polyline.UpdateProjection(mercator::RectByCenterXYAndSizeInMeters({4, 0}, 2));
  TEST_EQUAL(polyline.GetCurrentIter().m_ind, 1, ());
  TEST_EQUAL(polyline.GetCurrentIter().m_pt, m2::PointD(4, 0), ());
  auto iter = polyline.UpdateProjection(mercator::RectByCenterXYAndSizeInMeters({5.0001, 0}, 1));
  TEST(!iter.IsValid(), ());
  TEST_EQUAL(polyline.GetCurrentIter().m_ind, 1, ());
  TEST_EQUAL(polyline.GetCurrentIter().m_pt, m2::PointD(4, 0), ());
  polyline.UpdateProjection(mercator::RectByCenterXYAndSizeInMeters({5.0001, 0}, 2000));
  TEST_EQUAL(polyline.GetCurrentIter().m_ind, 1, ());
  TEST_EQUAL(polyline.GetCurrentIter().m_pt, m2::PointD(5, 0), ());
}

UNIT_TEST(FollowedPolylineDistanceCalculationTest)
{
  // Test full length case.
  FollowedPolyline polyline(kTestDirectedPolyline1.Begin(), kTestDirectedPolyline1.End());
  double distance = polyline.GetDistanceM(polyline.Begin(), polyline.End());
  double masterDistance = mercator::DistanceOnEarth(kTestDirectedPolyline1.Front(), kTestDirectedPolyline1.Back());
  TEST_ALMOST_EQUAL_ULPS(distance, masterDistance, ());
  distance = polyline.GetTotalDistanceMeters();
  TEST_ALMOST_EQUAL_ULPS(distance, masterDistance, ());

  // Test partial length case.
  polyline.UpdateProjection(mercator::RectByCenterXYAndSizeInMeters({3, 0}, 2));
  distance = polyline.GetDistanceM(polyline.GetCurrentIter(), polyline.End());
  masterDistance = mercator::DistanceOnEarth(kTestDirectedPolyline1.GetPoint(1), kTestDirectedPolyline1.Back());
  TEST_ALMOST_EQUAL_ULPS(distance, masterDistance, ());
  distance = polyline.GetDistanceToEndMeters();
  TEST_ALMOST_EQUAL_ULPS(distance, masterDistance, ());

  // Test point in the middle case.
  polyline.UpdateProjection(mercator::RectByCenterXYAndSizeInMeters({4, 0}, 2));
  distance = polyline.GetDistanceM(polyline.GetCurrentIter(), polyline.End());
  masterDistance = mercator::DistanceOnEarth(m2::PointD(4, 0), kTestDirectedPolyline1.Back());
  TEST_ALMOST_EQUAL_ULPS(distance, masterDistance, ());
  distance = polyline.GetDistanceToEndMeters();
  TEST_ALMOST_EQUAL_ULPS(distance, masterDistance, ());
}

UNIT_TEST(FollowedPolylineDirectionTest)
{
  m2::PolylineD testPolyline(std::vector<m2::PointD>{{0, 0}, {1.00003, 0}, {1.00003, 1}});
  FollowedPolyline polyline(testPolyline.Begin(), testPolyline.End());
  TEST_EQUAL(polyline.GetCurrentIter().m_ind, 0, ());
  m2::PointD directionPoint;
  polyline.GetCurrentDirectionPoint(directionPoint, 20);
  TEST_EQUAL(directionPoint, testPolyline.GetPoint(1), ());
  polyline.UpdateProjection(mercator::RectByCenterXYAndSizeInMeters({1.0, 0}, 2));
  polyline.GetCurrentDirectionPoint(directionPoint, 0.0001);
  TEST_EQUAL(directionPoint, testPolyline.GetPoint(1), ());
  polyline.GetCurrentDirectionPoint(directionPoint, 20);
  TEST_EQUAL(directionPoint, testPolyline.GetPoint(2), ());
}

UNIT_TEST(FollowedPolylineGetDistanceFromBeginM)
{
  m2::PolylineD testPolyline(std::vector<m2::PointD>{{0, 0}, {1, 0}, {2, 0}, {3, 0}, {5, 0}, {6, 0}});
  FollowedPolyline polyline(testPolyline.Begin(), testPolyline.End());
  m2::PointD point(4, 0);
  polyline.UpdateProjection(mercator::RectByCenterXYAndSizeInMeters(point, 2));

  double const distance = polyline.GetDistanceFromStartMeters();
  double const masterDistance = mercator::DistanceOnEarth(kTestDirectedPolyline1.Front(), point);
  TEST_ALMOST_EQUAL_ULPS(distance, masterDistance, ());
}

// Backseat: a fix 40 m off the street (multipath; it claimed 10 m) is outside the matching box of the
// street the car is on, but 8 m from where the route comes back the other way 48 m over. The far search
// jumped there and every turn in between was skipped. With a filter for the way the car is heading,
// that stretch isn't taken and the position stays put until a good fix.
UNIT_TEST(FollowedPolylineFarMatchKeepsToTheCarsHeading)
{
  auto const at = [](double eastM, double northM) { return mercator::FromLatLon(36.39 + northM / 111320.0, -94.29 + eastM / 89600.0); };
  // Real route geometry is dense: a point every 10 m, so the stretch coming back is far down the list.
  std::vector<m2::PointD> pts;
  for (int x = 0; x < 500; x += 10)
    pts.push_back(at(x, 0));  // segments 0..49 run east
  for (int y = 0; y < 40; y += 10)
    pts.push_back(at(500, y * 1.2));
  for (int x = 500; x > 0; x -= 10)
    pts.push_back(at(x, 48));  // then back west
  pts.push_back(at(0, 48));
  pts.push_back(at(0, 300));
  m2::RectD const offStreet = mercator::RectByCenterXYAndSizeInMeters(at(300, 40), 30.0);
  m2::RectD const onStreet = mercator::RectByCenterXYAndSizeInMeters(at(320, 2), 30.0);
  {
    // Unfiltered (upstream): jumps to the stretch coming back.
    FollowedPolyline poly(pts.begin(), pts.end());
    TEST(poly.UpdateMatchingProjection(mercator::RectByCenterXYAndSizeInMeters(at(100, 0), 30.0)), ());
    TEST(poly.UpdateMatchingProjection(offStreet), ());
    TEST_GREATER(poly.GetCurrentIter().m_ind, 53, ());
  }
  {
    FollowedPolyline poly(pts.begin(), pts.end());
    TEST(poly.UpdateMatchingProjection(mercator::RectByCenterXYAndSizeInMeters(at(100, 0), 30.0)), ());
    size_t const before = poly.GetCurrentIter().m_ind;
    // Heading east: only the eastbound first street would do, and it's too far from this fix.
    auto const eastbound = [&](size_t i) { return i < 50; };
    TEST(!poly.UpdateMatchingProjection(offStreet, eastbound), ());
    TEST_EQUAL(poly.GetCurrentIter().m_ind, before, ());
    // A good fix further along the street the car is really on is matched as usual.
    TEST(poly.UpdateMatchingProjection(onStreet, eastbound), ());
    TEST_GREATER(poly.GetCurrentIter().m_ind, 29, ());
    TEST_LESS(poly.GetCurrentIter().m_ind, 50, ());
  }
}
}  // namespace routing_test
