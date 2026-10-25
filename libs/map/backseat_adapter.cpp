#include "map/backseat_adapter.hpp"

#include "routing/following_info.hpp"
#include "routing/route.hpp"
#include "routing/routing_session.hpp"
#include "routing/turns.hpp"

#include "indexer/classificator.hpp"
#include "indexer/data_source.hpp"
#include "indexer/feature.hpp"
#include "indexer/feature_algo.hpp"
#include "indexer/feature_data.hpp"
#include "indexer/ftypes_matcher.hpp"
#include "indexer/scales.hpp"

#include "platform/platform.hpp"

#include "geometry/mercator.hpp"

#include "base/logging.hpp"

#include <fstream>
#include <sstream>

namespace
{
std::string const kTripsFile = "backseat_trips.tsv";
size_t constexpr kMaxTrips = 200;
/// A light this close to the turn point controls the junction.
double constexpr kTrafficLightRadiusM = 30;
/// How far around a stop to look for the gas station or drive-through.
double constexpr kPlaceSearchSizeM = 80;
double constexpr kBatteryCheckEveryMs = 30000;

/// Bigger roads rank lower. Side streets, driveways and service roads share the top rank.
int RoadRank(ftypes::HighwayClass c)
{
  using ftypes::HighwayClass;
  switch (c)
  {
  case HighwayClass::Motorway: return 0;
  case HighwayClass::Trunk: return 1;
  case HighwayClass::Primary: return 2;
  case HighwayClass::Secondary: return 3;
  case HighwayClass::Tertiary: return 4;
  case HighwayClass::LivingStreet:
  case HighwayClass::Service:
  case HighwayClass::ServiceMinor: return 5;
  default: return -1;
  }
}

backseat::TurnKind ToTurnKind(routing::turns::CarDirection d)
{
  using routing::turns::CarDirection;
  using backseat::TurnKind;
  switch (d)
  {
  case CarDirection::GoStraight: return TurnKind::Straight;
  case CarDirection::TurnRight: return TurnKind::Right;
  case CarDirection::TurnSharpRight: return TurnKind::SharpRight;
  case CarDirection::TurnSlightRight: return TurnKind::SlightRight;
  case CarDirection::TurnLeft: return TurnKind::Left;
  case CarDirection::TurnSharpLeft: return TurnKind::SharpLeft;
  case CarDirection::TurnSlightLeft: return TurnKind::SlightLeft;
  case CarDirection::UTurnLeft:
  case CarDirection::UTurnRight: return TurnKind::UTurn;
  case CarDirection::EnterRoundAbout:
  case CarDirection::LeaveRoundAbout:
  case CarDirection::StayOnRoundAbout: return TurnKind::Roundabout;
  case CarDirection::ReachedYourDestination: return TurnKind::Destination;
  case CarDirection::ExitHighwayToLeft: return TurnKind::ExitLeft;
  case CarDirection::ExitHighwayToRight: return TurnKind::ExitRight;
  default: return TurnKind::None;
  }
}

backseat::LatLon ToBackseatLatLon(m2::PointD const & p)
{
  auto const ll = mercator::ToLatLon(p);
  return {ll.m_lat, ll.m_lon};
}

std::vector<backseat::TripRecord> LoadTrips()
{
  std::vector<backseat::TripRecord> trips;
  std::ifstream in(GetPlatform().SettingsPathForFile(kTripsFile));
  std::string line;
  while (std::getline(in, line))
  {
    std::istringstream fields(line);
    backseat::TripRecord r;
    std::string expected, actual, share, missed, brakes, ended;
    if (std::getline(fields, r.routeKey, '\t') && std::getline(fields, r.destination, '\t') &&
        std::getline(fields, ended, '\t') && std::getline(fields, expected, '\t') &&
        std::getline(fields, actual, '\t') && std::getline(fields, share, '\t') && std::getline(fields, missed, '\t') &&
        std::getline(fields, brakes, '\t') && std::getline(fields, r.grade, '\t'))
    {
      try
      {
        r.endedAt = std::stod(ended);
        r.expectedMinutes = std::stod(expected);
        r.actualMinutes = std::stod(actual);
        r.speedingShare = std::stod(share);
        r.missedTurns = std::stoi(missed);
        r.hardBrakes = std::stoi(brakes);
        trips.push_back(r);
      }
      catch (std::exception const &)
      {
        // A damaged line just gets forgotten.
      }
    }
  }
  if (trips.size() > kMaxTrips)
    trips.erase(trips.begin(), trips.end() - kMaxTrips);
  return trips;
}

void AppendTrip(backseat::TripRecord const & r)
{
  std::ofstream out(GetPlatform().SettingsPathForFile(kTripsFile), std::ios::app);
  out << r.routeKey << '\t' << r.destination << '\t' << r.endedAt << '\t' << r.expectedMinutes << '\t'
      << r.actualMinutes << '\t' << r.speedingShare << '\t' << r.missedTurns << '\t' << r.hardBrakes << '\t' << r.grade
      << '\n';
}
}  // namespace

BackseatAdapter::BackseatAdapter()
  : m_navigator({}, LoadTrips(), [](backseat::TripRecord const & r, backseat::TripReport const & report)
{
  LOG(LINFO, ("Backseat Driver trip to", r.destination, "graded", report.overall.letter));
  AppendTrip(r);
})
{}

void BackseatAdapter::OnLocationUpdate(location::GpsInfo const & info, routing::RoutingSession const & session,
                                       std::string const & destinationName)
{
  auto const route = Snapshot(session, destinationName);
  if (route.state == backseat::RouteState::Inactive && !m_navigator.InTrip())
    return;
  backseat::Fix fix;
  fix.tMs = info.m_timestamp * 1000;
  fix.lat = info.m_latitude;
  fix.lon = info.m_longitude;
  fix.speedMps = info.m_speed;
  fix.bearingDeg = info.m_bearing;
  if (route.state != backseat::RouteState::Inactive)
  {
    double const limit = session.GetCurrentSpeedLimit();
    if (limit > 0)
      fix.speedLimitMps = limit;
  }
  m_navigator.OnFix(fix, route);
  CheckBattery(fix.tMs);
}

void BackseatAdapter::SetDataSource(std::function<DataSource &()> getter)
{
  m_dataSource = std::move(getter);
  m_navigator.SetPlaceLookup([this](backseat::LatLon const & at) { return PlaceAt(at); });
}

void BackseatAdapter::SetTrafficLights(std::vector<m2::PointD> points)
{
  std::lock_guard lock(m_lightsMutex);
  m_trafficLights = std::move(points);
}

void BackseatAdapter::CheckBattery(double tMs)
{
  if (tMs - m_lastBatteryCheckMs < kBatteryCheckEveryMs)
    return;
  m_lastBatteryCheckMs = tMs;
  auto const status = Platform::GetChargingStatus();
  if (status == Platform::ChargingStatus::Unknown)
    return;
  m_navigator.OnBattery(tMs, Platform::GetBatteryLevel() / 100.0, status == Platform::ChargingStatus::Plugged);
}

backseat::Control BackseatAdapter::GuessControl(routing::Route const & route, uint32_t turnIndex) const
{
  // The turn sits at the end of segment turnIndex - 1; the road after it is segment turnIndex.
  auto const & segments = route.GetRouteSegments();
  if (turnIndex == 0 || turnIndex >= segments.size())
    return backseat::Control::None;
  auto const & in = segments[turnIndex - 1];
  auto const & out = segments[turnIndex];
  m2::PointD const junction = in.GetJunction().GetPoint();
  {
    std::lock_guard lock(m_lightsMutex);
    for (auto const & light : m_trafficLights)
      if (mercator::DistanceOnEarth(junction, light) < kTrafficLightRadiusM)
        return backseat::Control::TrafficLight;
  }
  int const from = RoadRank(in.GetRoadNameInfo().m_highwayClass);
  int const to = RoadRank(out.GetRoadNameInfo().m_highwayClass);
  // Off a side street onto a bigger road, with no light: almost always a stop sign.
  if (from >= RoadRank(ftypes::HighwayClass::Tertiary) && to >= 0 && to < from)
    return backseat::Control::StopSign;
  return backseat::Control::None;
}

std::optional<backseat::Place> BackseatAdapter::PlaceAt(backseat::LatLon const & at) const
{
  if (!m_dataSource)
    return {};
  auto const & c = classif();
  static uint32_t const kFuel = c.GetTypeByPath({"amenity", "fuel"});
  static uint32_t const kCafe = c.GetTypeByPath({"amenity", "cafe"});
  static uint32_t const kFastFood = c.GetTypeByPath({"amenity", "fast_food"});
  static uint32_t const kRestaurant = c.GetTypeByPath({"amenity", "restaurant"});
  static uint32_t const kShop = c.GetTypeByPath({"shop"});

  // Lower is a better explanation for the stop.
  auto const rankOf = [](backseat::PlaceKind k) { return static_cast<int>(k); };
  m2::PointD const center = mercator::FromLatLon(at.lat, at.lon);
  std::optional<backseat::Place> best;
  double bestDistance = 0;
  m_dataSource().ForEachInRect([&](FeatureType & ft)
  {
    std::optional<backseat::PlaceKind> kind;
    for (uint32_t type : feature::TypesHolder(ft))
    {
      std::optional<backseat::PlaceKind> k;
      uint32_t top = type;
      ftype::TruncValue(top, 1);
      if (type == kFuel)
        k = backseat::PlaceKind::Gas;
      else if (type == kCafe)
        k = backseat::PlaceKind::Coffee;
      else if (type == kFastFood || type == kRestaurant)
        k = backseat::PlaceKind::Food;
      else if (top == kShop)
        k = backseat::PlaceKind::Store;
      if (k && (!kind || rankOf(*k) < rankOf(*kind)))
        kind = k;
    }
    if (!kind)
      return;
    double const distance = mercator::DistanceOnEarth(center, feature::GetCenter(ft));
    bool const better = !best || rankOf(*kind) < rankOf(best->kind) ||
                        (rankOf(*kind) == rankOf(best->kind) && distance < bestDistance);
    if (!better)
      return;
    best = backseat::Place{*kind, ft.GetTranslatedName().m_primary};
    bestDistance = distance;
  }, mercator::RectByCenterXYAndSizeInMeters(center, kPlaceSearchSizeM), scales::GetUpperScale());
  return best;
}

std::vector<std::string> BackseatAdapter::TakeNotifications(routing::RoutingSession const & session,
                                                            std::string const & destinationName)
{
  m_navigator.Poll(Snapshot(session, destinationName));
  std::vector<std::string> out;
  for (auto const & c : m_navigator.TakeLines())
    out.push_back(c.text);
  return out;
}

backseat::RouteSnapshot BackseatAdapter::Snapshot(routing::RoutingSession const & session,
                                                  std::string const & destinationName) const
{
  using routing::SessionState;
  backseat::RouteSnapshot snap;
  if (!session.IsActive() || !session.IsFollowing())
    return snap;
  auto const route = session.GetRouteForTests();
  if (!route || !route->IsValid())
    return snap;

  routing::FollowingInfo info;
  session.GetRouteFollowingInfo(info);
  switch (info.m_routingSessionState)
  {
  case SessionState::OnRoute: snap.state = backseat::RouteState::OnRoute; break;
  case SessionState::RouteNotStarted:
  case SessionState::RouteBuilding: snap.state = backseat::RouteState::NotStarted; break;
  case SessionState::RouteNeedsRebuild:
  case SessionState::RouteRebuilding:
  case SessionState::OffRoute: snap.state = backseat::RouteState::OffRoute; break;
  case SessionState::RouteFinished: snap.state = backseat::RouteState::Finished; break;
  case SessionState::NoValidRoute:
  case SessionState::RouteNoFollowing: return snap;
  }

  snap.destination = destinationName;
  snap.etaSeconds = route->GetTotalTimeSec();
  for (auto const & segment : route->GetRouteSegments())
  {
    auto const & turn = segment.GetTurn();
    if (!turn.IsTurnNone())
      snap.plannedTurns.push_back(ToTurnKind(turn.m_turn));
  }
  auto const & poly = route->GetPoly();
  if (poly.GetSize() > 0)
  {
    snap.origin = ToBackseatLatLon(poly.Front());
    snap.destinationPoint = ToBackseatLatLon(poly.Back());
  }

  if (snap.state == backseat::RouteState::OnRoute && session.IsNavigable())
  {
    double distanceToTurnM = 0;
    routing::turns::TurnItem turn;
    route->GetNearestTurn(distanceToTurnM, turn);
    if (!turn.IsTurnNone())
    {
      snap.turnId = turn.m_index;
      snap.turn = ToTurnKind(turn.m_turn);
      snap.distToTurnM = distanceToTurnM;
      snap.nextStreet = info.m_nextStreetName;
      snap.control = GuessControl(*route, turn.m_index);
    }
  }
  return snap;
}
