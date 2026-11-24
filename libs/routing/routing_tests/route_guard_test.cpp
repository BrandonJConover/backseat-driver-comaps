#include "testing/testing.hpp"

#include "routing/routing_tests/tools.hpp"

#include "routing/checkpoints.hpp"
#include "routing/route.hpp"
#include "routing/route_guard.hpp"
#include "routing/routing_helpers.hpp"
#include "routing/routing_session.hpp"

#include "platform/location.hpp"

#include "geometry/mercator.hpp"
#include "geometry/point_with_altitude.hpp"

#include <memory>
#include <vector>

// Backseat Driver: one bad GPS fix can't reroute the car; parking near a stop arrives there.
namespace route_guard_test
{
using namespace routing;
using namespace std;

// About 111 m per 0.001 degrees of latitude at the equator; the route runs due north.
double constexpr kMetersToLon = 1.0 / 111320.0;
double constexpr kStopLat = 0.009;  // ~1 km north of the start
double constexpr kMidLat = 0.0045;  // an intermediate stop halfway

location::GpsInfo Fix(double lat, double eastM, double speedMps, double timeS)
{
  location::GpsInfo info;
  info.m_source = location::EAppleNative;
  info.m_latitude = lat;
  info.m_longitude = eastM * kMetersToLon;
  info.m_horizontalAccuracy = 5.0;
  info.m_speed = speedMps;
  info.m_bearing = 0.0;
  info.m_timestamp = timeS;
  return info;
}

/// A session following a straight route north from (0, 0) to (kStopLat, 0), with a stop halfway
/// when |withMidStop|.
struct Trip
{
  explicit Trip(bool withMidStop, bool arriveNearStops = true)
  {
    vector<m2::PointD> const path = {mercator::FromLatLon(0, 0), mercator::FromLatLon(kMidLat, 0),
                                     mercator::FromLatLon(kStopLat, 0)};
    vector<Segment> const segments = {{0, 0, 0, true}, {0, 0, 1, true}};
    vector<turns::TurnItem> const turns = {turns::TurnItem(1, turns::CarDirection::None),
                                           turns::TurnItem(2, turns::CarDirection::ReachedYourDestination)};
    auto route = make_shared<Route>("dummy", path.begin(), path.end(), 1 /* route id */);
    vector<RouteSegment> segmentInfo;
    RouteSegmentsFrom(segments, path, turns, {}, segmentInfo);
    // FillSegmentInfo() counts each segment from its own start, so the first one has no length.
    double fromStartM = 0.0;
    double fromStartMerc = 0.0;
    for (size_t i = 0; i < segmentInfo.size(); ++i)
    {
      fromStartM += mercator::DistanceOnEarth(path[i], path[i + 1]);
      fromStartMerc += path[i].Length(path[i + 1]);
      segmentInfo[i].SetDistancesAndTime(fromStartM, fromStartMerc, 30.0 * (i + 1));
    }
    route->SetRouteSegments(std::move(segmentInfo));
    geometry::PointWithAltitude const a(path[0], geometry::kDefaultAltitudeMeters);
    geometry::PointWithAltitude const b(path[1], geometry::kDefaultAltitudeMeters);
    geometry::PointWithAltitude const c(path[2], geometry::kDefaultAltitudeMeters);
    if (withMidStop)
    {
      route->SetSubroteAttrs(vector<Route::SubrouteAttrs>({Route::SubrouteAttrs(a, b, 0, 1), Route::SubrouteAttrs(b, c, 1, 2)}));
      m_session.SetCheckpointsForTesting(Checkpoints({path[0], path[1], path[2]}));
    }
    else
    {
      route->SetSubroteAttrs(vector<Route::SubrouteAttrs>({Route::SubrouteAttrs(a, c, 0, 2)}));
      m_session.SetCheckpointsForTesting(Checkpoints(path[0], path[2]));
    }
    m_session.Init(nullptr /* pointCheckCallback */);
    m_session.SetCheckpointCallback([this](size_t passed) { m_passed.push_back(passed); });
    m_session.SetArriveNearStops(arriveNearStops);
    m_session.AssignRouteForTesting(route, RouterResultCode::NoError);
  }

  SessionState Real(location::GpsInfo const & info)
  {
    m_session.OnRealFix();
    return m_session.OnLocationPositionChanged(info);
  }
  /// What the Extrapolator sends between real fixes.
  SessionState Guess(location::GpsInfo const & info) { return m_session.OnLocationPositionChanged(info); }

  RoutingSession m_session;
  vector<size_t> m_passed;
};

UNIT_TEST(RouteGuard_OneBadFixDoesNotReroute)
{
  Trip trip(false /* withMidStop */);
  double t = 100.0;
  TEST_EQUAL(trip.Real(Fix(0.001, 0, 15, t)), SessionState::OnRoute, ());
  // A multipath fix 60 m off the road, then two seconds of the Extrapolator running with it.
  TEST_EQUAL(trip.Real(Fix(0.0011, 60, 15, t += 1)), SessionState::OnRoute, ());
  for (int i = 1; i <= 10; ++i)
    TEST_EQUAL(trip.Guess(Fix(0.0011 + i * 0.00002, 60 + i * 12, 15, t + i * 0.2)), SessionState::OnRoute, (i));
  // The next real fix is back on the road.
  TEST_EQUAL(trip.Real(Fix(0.0013, 0, 15, t += 1)), SessionState::OnRoute, ());
  TEST(trip.m_session.IsOnRoute(), ());
}

UNIT_TEST(RouteGuard_TwoOffRouteFixesReroute)
{
  Trip trip(false /* withMidStop */);
  double t = 100.0;
  TEST_EQUAL(trip.Real(Fix(0.001, 0, 15, t)), SessionState::OnRoute, ());
  TEST_EQUAL(trip.Real(Fix(0.0011, 60, 15, t += 1)), SessionState::OnRoute, ());
  for (int i = 1; i <= 10; ++i)
    trip.Guess(Fix(0.0011, 60 + i * 3, 15, t + i * 0.2));
  // A second real fix still off the road: that's a missed turn.
  TEST_EQUAL(trip.Real(Fix(0.0011, 95, 15, t += 1)), SessionState::RouteNeedsRebuild, ());
}

UNIT_TEST(RouteGuard_HostWithoutRealFixesKeepsOldTiming)
{
  Trip trip(false /* withMidStop */);
  double t = 100.0;
  TEST_EQUAL(trip.Guess(Fix(0.001, 0, 15, t)), SessionState::OnRoute, ());
  SessionState state = SessionState::OnRoute;
  for (int i = 1; i <= 6; ++i)
    state = trip.Guess(Fix(0.0011, 60 + i * 5, 15, t + i * 0.2));
  TEST_EQUAL(state, SessionState::RouteNeedsRebuild, ());
}

UNIT_TEST(RouteGuard_ParkedInTheLotArrives)
{
  Trip trip(false /* withMidStop */);
  double t = 100.0;
  for (double lat = 0.001; lat <= 0.0080001; lat += 0.001)
    TEST_EQUAL(trip.Real(Fix(lat, 0, 12, t += 9)), SessionState::OnRoute, (lat));
  // Into the lot, 80 m east of the road, crawling around for a spot: no rebuild.
  for (int i = 0; i < 15; ++i)
  {
    auto const state = trip.Real(Fix(0.0083 + i * 0.00002, 80 + (i % 3) * 4, 2.5, t += 1));
    TEST_NOT_EQUAL(state, SessionState::RouteNeedsRebuild, (i));
    TEST_NOT_EQUAL(state, SessionState::RouteFinished, (i));
  }
  // Parked: GPS jitter, no speed.
  SessionState state = SessionState::OnRoute;
  int seconds = 0;
  while (state != SessionState::RouteFinished && seconds < 60)
  {
    state = trip.Real(Fix(0.0086, 88 + (seconds % 2) * 2, 0.0, t += 1));
    ++seconds;
  }
  TEST_EQUAL(state, SessionState::RouteFinished, ());
  TEST_GREATER_OR_EQUAL(seconds, 20, ());
  TEST_LESS_OR_EQUAL(seconds, 22, ());
  TEST_EQUAL(trip.m_passed, vector<size_t>({1}), ());
}

UNIT_TEST(RouteGuard_RedLightShortOfTheStopDoesNotArrive)
{
  Trip trip(false /* withMidStop */);
  double t = 100.0;
  for (double lat = 0.001; lat <= 0.0080001; lat += 0.001)
    trip.Real(Fix(lat, 0, 12, t += 9));
  // Stopped on the route ~110 m short of the stop for a long light.
  for (int i = 0; i < 90; ++i)
    TEST_EQUAL(trip.Real(Fix(0.0080, 0, 0.0, t += 1)), SessionState::OnRoute, (i));
  TEST(trip.m_passed.empty(), ());
}

UNIT_TEST(RouteGuard_PassingRightByTheStopArrives)
{
  Trip trip(false /* withMidStop */);
  double t = 100.0;
  for (double lat = 0.001; lat <= 0.0080001; lat += 0.001)
    trip.Real(Fix(lat, 0, 12, t += 9));
  // The pin is on a building; the driver comes up the street beside it, 35 m over.
  SessionState state = SessionState::OnRoute;
  for (double lat = 0.0082; lat <= 0.0090001 && state != SessionState::RouteFinished; lat += 0.0002)
    state = trip.Real(Fix(lat, 35, 8, t += 2));
  TEST_EQUAL(state, SessionState::RouteFinished, ());
}

UNIT_TEST(RouteGuard_ParkedAtAMidStopPassesIt)
{
  Trip trip(true /* withMidStop */);
  double t = 100.0;
  for (double lat = 0.001; lat <= 0.0030001; lat += 0.001)
    TEST_EQUAL(trip.Real(Fix(lat, 0, 12, t += 9)), SessionState::OnRoute, (lat));
  // Gas station 60 m east of the stop's pin: pull in and stay a while.
  trip.Real(Fix(0.0042, 60, 3, t += 3));
  for (int i = 0; i < 25; ++i)
    trip.Real(Fix(0.0045, 60, 0.0, t += 1));
  TEST_EQUAL(trip.m_passed, vector<size_t>({1}), ());
  TEST(!trip.m_session.IsFinished(), ());
  // Leaving by the back exit, away from the route: a fresh route to the destination.
  SessionState state = SessionState::OnRoute;
  for (int i = 1; i <= 8 && state != SessionState::RouteNeedsRebuild; ++i)
    state = trip.Real(Fix(0.0045, 60 + i * 15, 10, t += 1));
  TEST_EQUAL(state, SessionState::RouteNeedsRebuild, ());
}

UNIT_TEST(RouteGuard_OffForOtherTravelModes)
{
  Trip trip(false /* withMidStop */, false /* arriveNearStops */);
  double t = 100.0;
  for (double lat = 0.001; lat <= 0.0080001; lat += 0.001)
    trip.Real(Fix(lat, 0, 12, t += 9));
  for (int i = 0; i < 40; ++i)
    TEST_NOT_EQUAL(trip.Real(Fix(0.0086, 88 + (i % 2) * 2, 0.0, t += 1)), SessionState::RouteFinished, (i));
}

UNIT_TEST(RouteGuard_FailedRebuildsBackOff)
{
  Trip trip(false /* withMidStop */);
  double t = 100.0;
  TEST_EQUAL(trip.Guess(Fix(0.001, 0, 15, t)), SessionState::OnRoute, ());
  auto const offRouteChecksToRebuild = [&]()
  {
    for (int i = 1; i <= 100; ++i)
      if (trip.Guess(Fix(0.0011, 60 + i * 5, 15, t += 0.2)) == SessionState::RouteNeedsRebuild)
        return i;
    return 0;
  };
  TEST_EQUAL(offRouteChecksToRebuild(), 6, ());
  // The rebuild fails (no map out here): the old route stays, and the next ask waits longer.
  trip.m_session.AssignRouteForTesting(make_shared<Route>("dummy", 2 /* route id */), RouterResultCode::RouteNotFound);
  TEST(trip.m_session.IsOnRoute(), ());
  TEST_EQUAL(offRouteChecksToRebuild(), 11, ());
  trip.m_session.AssignRouteForTesting(make_shared<Route>("dummy", 3 /* route id */), RouterResultCode::RouteNotFound);
  TEST_EQUAL(offRouteChecksToRebuild(), 21, ());
  for (int i = 0; i < 10; ++i)
    trip.m_session.AssignRouteForTesting(make_shared<Route>("dummy", 4 /* route id */), RouterResultCode::RouteNotFound);
  TEST_EQUAL(offRouteChecksToRebuild(), 81, ("capped"));
}

UNIT_TEST(RouteGuard_NearStopArrivalTimer)
{
  NearStopArrival arrival;
  NearStopArrival::Position p;
  p.m_toStopM = 100;
  p.m_routeLeftM = 120;
  p.m_onRoute = false;
  p.m_speedMps = 0.0;
  p.m_timeS = 0;
  TEST(!arrival.Arrived(p), ());
  // Unknown speed neither starts nor stops the clock.
  p.m_speedMps = -1;
  p.m_timeS = 10;
  TEST(!arrival.Arrived(p), ());
  p.m_timeS = 20;
  TEST(arrival.Arrived(p), ());
  // Moving again starts over.
  p.m_speedMps = 5;
  p.m_timeS = 21;
  TEST(!arrival.Arrived(p), ());
  p.m_speedMps = 0;
  p.m_timeS = 30;
  TEST(!arrival.Arrived(p), ());
  p.m_timeS = 50;
  TEST(arrival.Arrived(p), ());
  // Far from the stop is never arriving, parked or not.
  arrival.Reset();
  p.m_toStopM = 400;
  p.m_timeS = 100;
  TEST(!arrival.Arrived(p), ());
  p.m_timeS = 200;
  TEST(!arrival.Arrived(p), ());
}

UNIT_TEST(RouteGuard_OffRouteFixGate)
{
  OffRouteFixGate gate;
  TEST(gate.ConfirmsOffRoute(), ("no real fixes reported: old behavior"));
  gate.OnRoute();
  gate.OnRealFix();
  TEST(!gate.ConfirmsOffRoute(), ());
  TEST(!gate.ConfirmsOffRoute(), ("guesses from the same fix"));
  gate.OnRealFix();
  TEST(gate.ConfirmsOffRoute(), ());
  gate.OnRoute();
  gate.OnRealFix();
  TEST(!gate.ConfirmsOffRoute(), ("back on the route starts over"));
}
}  // namespace route_guard_test
