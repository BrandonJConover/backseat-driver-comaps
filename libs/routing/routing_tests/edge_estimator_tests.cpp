#include "testing/testing.hpp"

#include "routing/edge_estimator.hpp"

#include "geometry/point_with_altitude.hpp"

#include <cmath>

namespace
{
using namespace routing;

double const kTan = 0.1;
geometry::Altitude const kAlt = 100.0;
auto const kPurposes = {EdgeEstimator::Purpose::Weight, EdgeEstimator::Purpose::ETA};
constexpr double kAccuracyEps = 1e-5;

// Climb penalty on plain surface (tangent = 0) must be 1.0 for ETA and Weight estimations.
UNIT_TEST(ClimbPenalty_ZeroTangent)
{
  double const zeroTangent = 0.0;

  for (auto const & purpose : kPurposes)
  {
    TEST_EQUAL(GetBicycleClimbPenalty(purpose, zeroTangent, kAlt), 1.0, ());
    TEST_EQUAL(GetPedestrianClimbPenalty(purpose, zeroTangent, kAlt), 1.0, ());
  }
}

// Descent penalty for pedestrians and bicycles must be less then the ascent penalty.
UNIT_TEST(ClimbPenalty_DescentLessThenAscent)
{
  for (auto const & purpose : kPurposes)
  {
    double const ascPenaltyPedestrian = GetPedestrianClimbPenalty(purpose, kTan, kAlt);
    double const descPenaltyPedestrian = GetPedestrianClimbPenalty(purpose, -kTan, kAlt);
    TEST_LESS(descPenaltyPedestrian, ascPenaltyPedestrian, ());

    double const ascPenaltyBicycle = GetBicycleClimbPenalty(purpose, kTan, kAlt);
    double const descPenaltyBicycle = GetBicycleClimbPenalty(purpose, -kTan, kAlt);
    TEST_LESS(descPenaltyBicycle, ascPenaltyBicycle, ());
  }
}

// Climb penalty high above the sea level (higher then 2.5 km) should be very significant.
UNIT_TEST(ClimbPenalty_HighAboveSeaLevel)
{
  for (auto const & purpose : kPurposes)
  {
    double const penalty2500 = GetPedestrianClimbPenalty(purpose, kTan, 2500);
    double const penalty4000 = GetPedestrianClimbPenalty(purpose, kTan, 4000);
    double const penalty5500 = GetPedestrianClimbPenalty(purpose, kTan, 5500);
    double const penalty7000 = GetPedestrianClimbPenalty(purpose, kTan, 7000);

    TEST_GREATER_OR_EQUAL(penalty2500, 2.0, ());
    TEST_GREATER_OR_EQUAL(penalty4000, penalty2500 + 1.0, ());
    TEST_GREATER_OR_EQUAL(penalty5500, penalty4000 + 1.0, ());
    TEST_GREATER_OR_EQUAL(penalty7000, penalty5500 + 1.0, ());

    double const penalty2500Bicyclce = GetBicycleClimbPenalty(purpose, kTan, 2500);

    TEST_GREATER_OR_EQUAL(penalty2500Bicyclce, 6.0, ());
  }
}
}  // namespace

// Backseat: the driver's avoided roads and fewer lefts change which route is picked, not its ETA.
namespace backseat_prefs
{
using namespace routing;

std::shared_ptr<EdgeEstimator> Car()
{
  return EdgeEstimator::Create(VehicleType::Car, 90.0 /* maxWeighSpeedKMpH */, SpeedKMpH(10.0, 10.0),
                               nullptr /* trafficStash */, nullptr /* dataSource */, nullptr /* numMwmIds */);
}

UNIT_TEST(DrivePreferences_AvoidedRoadCostsMoreOnlyWhenPicking)
{
  auto car = Car();
  RoadGeometry const road(false /* oneWay */, 50.0, 50.0, {{0.0, 0.0}, {0.0, 0.01}});
  Segment const avoided(1 /* mwm */, 7 /* feature */, 0 /* segment */, true /* forward */);
  Segment const other(1, 8, 0, true);
  double const plainWeight = car->CalcSegmentWeight(avoided, road, EdgeEstimator::Purpose::Weight);
  double const plainEta = car->CalcSegmentWeight(avoided, road, EdgeEstimator::Purpose::ETA);

  DrivePreferences::Snapshot prefs;
  prefs.m_avoided.insert({1, 7});
  car->SetDrivePreferences(std::make_shared<DrivePreferences::Snapshot const>(prefs));
  TEST_ALMOST_EQUAL_ABS(car->CalcSegmentWeight(avoided, road, EdgeEstimator::Purpose::Weight),
                        plainWeight * DrivePreferences::kAvoidedRoadFactor, 1e-6, ());
  TEST_ALMOST_EQUAL_ABS(car->CalcSegmentWeight(avoided, road, EdgeEstimator::Purpose::ETA), plainEta, 1e-9, ());
  TEST_ALMOST_EQUAL_ABS(car->CalcSegmentWeight(other, road, EdgeEstimator::Purpose::Weight), plainWeight, 1e-9, ());
}

UNIT_TEST(DrivePreferences_FewerLeftsCostsLeftsAcrossTraffic)
{
  auto car = Car();
  RoadGeometry const road;
  auto const weight = [&](double angle, bool leftHand)
  { return car->GetTurnPenalty(EdgeEstimator::Purpose::Weight, angle, road, road, leftHand); };
  double const left = weight(90.0, false);
  double const right = weight(-90.0, false);
  double const slightLeft = weight(20.0, false);

  DrivePreferences::Snapshot prefs;
  prefs.m_fewerLefts = true;
  car->SetDrivePreferences(std::make_shared<DrivePreferences::Snapshot const>(prefs));
  TEST_ALMOST_EQUAL_ABS(weight(90.0, false), left + DrivePreferences::kLeftTurnPenaltyS, 1e-9, ());
  TEST_ALMOST_EQUAL_ABS(weight(-90.0, false), right, 1e-9, ("a right turn costs the same"));
  TEST_ALMOST_EQUAL_ABS(weight(20.0, false), slightLeft, 1e-9, ("a slight bend isn't a turn"));
  // Where they drive on the left, the turn across traffic is the right.
  TEST_ALMOST_EQUAL_ABS(weight(-90.0, true), car->GetTurnPenalty(EdgeEstimator::Purpose::Weight, -90.0, road, road, true), 1e-9, ());
  TEST_GREATER(weight(-90.0, true), weight(90.0, true), ());
  // The ETA is real driving time.
  TEST_ALMOST_EQUAL_ABS(car->GetTurnPenalty(EdgeEstimator::Purpose::ETA, 90.0, road, road, false),
                        Car()->GetTurnPenalty(EdgeEstimator::Purpose::ETA, 90.0, road, road, false), 1e-9, ());
}

UNIT_TEST(DrivePreferences_Snapshots)
{
  auto & prefs = DrivePreferences::Instance();
  prefs.ClearAvoided();
  prefs.SetFewerLefts(false);
  auto const before = prefs.Get();
  prefs.Avoid({{2, 11}, {2, 12}});
  prefs.SetFewerLefts(true);
  TEST(before->Empty(), ("a route already being worked out keeps what it started with"));
  TEST_EQUAL(prefs.Get()->m_avoided.size(), 2, ());
  TEST(prefs.Get()->Avoids(Segment(2, 11, 3, false)), ());
  TEST(!prefs.Get()->Avoids(Segment(3, 11, 3, false)), ());
  TEST(prefs.FewerLefts(), ());
  prefs.ClearAvoided();
  TEST(prefs.Get()->m_avoided.empty(), ());
  TEST(prefs.FewerLefts(), ("fewer lefts outlasts the trip"));
  prefs.SetFewerLefts(false);
}
}  // namespace backseat_prefs
