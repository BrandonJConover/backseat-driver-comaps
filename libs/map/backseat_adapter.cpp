#include "map/backseat_adapter.hpp"

#include "backseat/voice_command.hpp"

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

#include "storage/country_info_getter.hpp"

#include "geometry/mercator.hpp"
#include "geometry/parametrized_segment.hpp"

#include "base/logging.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
std::string const kTripsFile = "backseat_trips.tsv";
std::string const kPlatesFile = "backseat_plates.txt";
std::string const kSaidFile = "backseat_said.tsv";
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

/// The router formats street names for the screen: "[2] North Colorado Boulevard" or, at
/// exits, "[I 25] > Denver". Shields are drawn as signs there, but read aloud they come out
/// as "bracket two". Keep the name, or the bare route number when there is no name.
std::string SpeakableStreetName(std::string const & formatted)
{
  std::string name, refs;
  bool inShield = false;
  for (char const c : formatted)
  {
    if (c == '[')
      inShield = true;
    else if (c == ']')
    {
      inShield = false;
      refs += ' ';
    }
    else
      (inShield ? refs : name) += c;
  }
  auto const tidy = [](std::string s)
  {
    size_t const gt = s.find('>');
    if (gt != std::string::npos)
      s = "toward " + s.substr(gt + 1);
    std::string out;
    for (char const c : s)
    {
      if (c == ':' || (c == ' ' && (out.empty() || out.back() == ' ')))
        continue;
      out += c;
    }
    while (!out.empty() && out.back() == ' ')
      out.pop_back();
    return out;
  };
  name = tidy(name);
  if (name.empty() || name.rfind("toward ", 0) == 0)
  {
    std::string const r = tidy(refs);
    if (!r.empty())
      name = name.empty() ? r : r + " " + name;
  }
  return name;
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
        // Newer lines end with where the trip was headed: "lat,lon".
        std::string to;
        if (std::getline(fields, to, '\t'))
        {
          auto const comma = to.find(',');
          if (comma != std::string::npos)
            r.to = backseat::LatLon{std::stod(to.substr(0, comma)), std::stod(to.substr(comma + 1))};
        }
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
      << r.actualMinutes << '\t' << r.speedingShare << '\t' << r.missedTurns << '\t' << r.hardBrakes << '\t' << r.grade;
  if (r.to)
    out << '\t' << std::setprecision(9) << r.to->lat << ',' << r.to->lon;
  out << '\n';
}
backseat::PlateLog LoadPlates()
{
  std::ifstream in(GetPlatform().SettingsPathForFile(kPlatesFile));
  std::stringstream text;
  text << in.rdbuf();
  return backseat::PlateLog::Parse(text.str());
}

backseat::SaidLog LoadSaid()
{
  std::ifstream in(GetPlatform().SettingsPathForFile(kSaidFile));
  std::stringstream text;
  text << in.rdbuf();
  return backseat::SaidLog::Parse(text.str());
}

void SaveSaid(backseat::SaidLog const & log)
{
  std::ofstream(GetPlatform().SettingsPathForFile(kSaidFile)) << log.Serialize();
}

void SavePlates(backseat::PlateLog const & log)
{
  std::ofstream(GetPlatform().SettingsPathForFile(kPlatesFile)) << log.Serialize();
}

/// "Colorado" for the map region "US_Colorado_Denver", "British Columbia" for
/// "Canada_British Columbia_Vancouver", "Mexico" for any part of Mexico.
std::optional<std::string> PlateRegionOfMap(std::string const & countryId)
{
  if (countryId.rfind("Mexico", 0) == 0)
    return std::string("Mexico");
  for (std::string const prefix : {"US_", "Canada_"})
  {
    if (countryId.rfind(prefix, 0) != 0)
      continue;
    std::string region = countryId.substr(prefix.size());
    return region.substr(0, region.find('_'));
  }
  return {};
}
}  // namespace

BackseatAdapter::BackseatAdapter()
  : m_navigator({}, LoadTrips(), [](backseat::TripRecord const & r, backseat::TripReport const & report)
{
  LOG(LINFO, ("Backseat Driver trip to", r.destination, "graded", report.overall.letter));
  AppendTrip(r);
})
{
  m_navigator.SetPlateLog(LoadPlates(), &SavePlates);
  m_navigator.SetSaidLog(LoadSaid(), &SaveSaid);
}

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
  m_navigator.SetParkingLookup([this](backseat::LatLon const & at) { return InParkingLot(at); });
  m_navigator.SetSightLookup([this](backseat::LatLon const & at, double bearing) { return SightAt(at, bearing); });
  m_navigator.SetVenueLookup([this](backseat::LatLon const & at) { return VenueAt(at); });
  m_navigator.SetBridgeLookup([this](backseat::LatLon const & at, double bearing) { return BridgeAt(at, bearing); });
  // A stop within a light's queue is a red light, not pulling over.
  m_navigator.SetTrafficLightLookup([this](backseat::LatLon const & at)
  {
    double constexpr kRedLightQueueM = 60;
    m2::PointD const p = mercator::FromLatLon(at.lat, at.lon);
    std::lock_guard lock(m_lightsMutex);
    return std::any_of(m_trafficLights.begin(), m_trafficLights.end(), [&](m2::PointD const & light)
    { return mercator::DistanceOnEarth(p, light) < kRedLightQueueM; });
  });
}

void BackseatAdapter::SetCountryInfoGetter(std::function<storage::CountryInfoGetter const &()> getter)
{
  m_countryInfo = std::move(getter);
  m_navigator.SetRegionLookup([this](backseat::LatLon const & at) -> std::optional<std::string>
  {
    if (!m_countryInfo)
      return {};
    return PlateRegionOfMap(m_countryInfo().GetRegionCountryId(mercator::FromLatLon(at.lat, at.lon)));
  });
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

backseat::Bridge BackseatAdapter::BridgeAt(backseat::LatLon const & at, double bearingDeg) const
{
  if (!m_dataSource)
    return backseat::Bridge::None;
  auto const & c = classif();
  static uint32_t const kHighway = c.GetTypeByPath({"highway"});
  static std::vector<std::string> const kCarRoads = {
      "motorway",      "trunk",          "primary",      "secondary",      "tertiary",
      "unclassified",  "residential",    "living_street", "service",       "motorway_link",
      "trunk_link",    "primary_link",   "secondary_link", "tertiary_link", "road"};
  static std::vector<uint32_t> const kWater = {
      c.GetTypeByPath({"natural", "water"}), c.GetTypeByPath({"waterway", "river"}),
      c.GetTypeByPath({"waterway", "canal"}), c.GetTypeByPath({"natural", "bay"})};

  // Are we driving along a road bridge? Close to it and heading the same way (or the opposite
  // way on a two-way road), so passing under an overpass doesn't count.
  double constexpr kOnBridgeM = 12;
  double constexpr kAlongDeg = 35;
  m2::PointD const center = mercator::FromLatLon(at.lat, at.lon);
  bool onBridge = false;
  m_dataSource().ForEachInRect([&](FeatureType & ft)
  {
    if (onBridge || ft.GetGeomType() != feature::GeomType::Line)
      return;
    bool isBridge = false;
    for (uint32_t const type : feature::TypesHolder(ft))
    {
      if (ftype::GetLevel(type) != 3)
        continue;
      uint32_t top = type;
      ftype::TruncValue(top, 1);
      if (top != kHighway)
        continue;
      ClassifObject const * road = c.GetRoot()->GetObject(ftype::GetValue(type, 0))->GetObject(ftype::GetValue(type, 1));
      ClassifObject const * kind = road->GetObject(ftype::GetValue(type, 2));
      if (kind->GetName() == "bridge" &&
          std::find(kCarRoads.begin(), kCarRoads.end(), road->GetName()) != kCarRoads.end())
        isBridge = true;
    }
    if (!isBridge)
      return;
    ft.ParseGeometry(scales::GetUpperScale());
    for (size_t i = 1; i < ft.GetPointsCount() && !onBridge; ++i)
    {
      m2::PointD const a = ft.GetPoint(i - 1);
      m2::PointD const b = ft.GetPoint(i);
      m2::ParametrizedSegment<m2::PointD> const segment(a, b);
      if (mercator::DistanceOnEarth(center, segment.ClosestPointTo(center)) > kOnBridgeM)
        continue;
      // Mercator y points north, so this is a compass bearing.
      double const segBearing = std::atan2(b.x - a.x, b.y - a.y) * 180 / M_PI;
      double diff = std::fmod(std::fabs(segBearing - bearingDeg), 180.0);
      diff = std::min(diff, 180 - diff);
      onBridge = diff <= kAlongDeg;
    }
  }, mercator::RectByCenterXYAndSizeInMeters(center, 2 * kOnBridgeM), scales::GetUpperScale());
  if (!onBridge)
    return backseat::Bridge::None;

  // Any river, lake or bay right around the bridge makes it a water crossing.
  double constexpr kWaterSearchM = 150;
  bool water = false;
  m_dataSource().ForEachInRect([&](FeatureType & ft)
  {
    if (water)
      return;
    for (uint32_t type : feature::TypesHolder(ft))
    {
      ftype::TruncValue(type, 2);
      if (std::find(kWater.begin(), kWater.end(), type) != kWater.end())
        water = true;
    }
  }, mercator::RectByCenterXYAndSizeInMeters(center, kWaterSearchM), scales::GetUpperScale());
  return water ? backseat::Bridge::Water : backseat::Bridge::Land;
}

std::optional<backseat::Venue> BackseatAdapter::VenueAt(backseat::LatLon const & at) const
{
  if (!m_dataSource)
    return {};
  using backseat::Venue;
  auto const & c = classif();
  static std::vector<std::pair<uint32_t, Venue>> const kVenues = {
      {c.GetTypeByPath({"amenity", "cafe"}), Venue::Coffee},
      {c.GetTypeByPath({"shop", "coffee"}), Venue::Coffee},
      {c.GetTypeByPath({"tourism", "theme_park"}), Venue::ThemePark},
      {c.GetTypeByPath({"leisure", "water_park"}), Venue::ThemePark},
      {c.GetTypeByPath({"leisure", "fitness_centre"}), Venue::Gym},
      {c.GetTypeByPath({"aeroway", "aerodrome"}), Venue::Airport},
      {c.GetTypeByPath({"aeroway", "terminal"}), Venue::Airport},
      {c.GetTypeByPath({"shop", "mall"}), Venue::Mall},
      {c.GetTypeByPath({"tourism", "zoo"}), Venue::Zoo},
      {c.GetTypeByPath({"natural", "beach"}), Venue::Beach},
      {c.GetTypeByPath({"amenity", "cinema"}), Venue::Cinema},
      {c.GetTypeByPath({"shop", "electronics"}), Venue::Electronics},
      {c.GetTypeByPath({"shop", "mobile_phone"}), Venue::Electronics}};

  // The place itself, or one whose grounds contain the point (a theme park, an airport). Far
  // enough to cover parking out front, close enough not to pick up the cafe next door's
  // neighbor. Points win over areas: a cafe inside a mall is coffee.
  double constexpr kNearM = 60;
  m2::PointD const center = mercator::FromLatLon(at.lat, at.lon);
  std::optional<Venue> best;
  double bestDistance = 0;
  m_dataSource().ForEachInRect([&](FeatureType & ft)
  {
    std::optional<Venue> venue;
    for (uint32_t type : feature::TypesHolder(ft))
    {
      ftype::TruncValue(type, 2);
      for (auto const & [t, v] : kVenues)
        if (t == type)
          venue = v;
    }
    if (!venue)
      return;
    double distance = mercator::DistanceOnEarth(center, feature::GetCenter(ft));
    if (ft.GetGeomType() == feature::GeomType::Area)
    {
      if (!ft.GetLimitRect(scales::GetUpperScale()).IsPointInside(center))
        return;
      distance += 1000;  // a containing area loses to any point nearby
    }
    else if (distance > kNearM)
      return;
    if (!best || distance < bestDistance)
    {
      best = venue;
      bestDistance = distance;
    }
  }, mercator::RectByCenterXYAndSizeInMeters(center, 2 * kNearM), scales::GetUpperScale());
  return best;
}

BackseatAdapter::HeardCommand BackseatAdapter::OnDriverSaid(std::string const & text)
{
  HeardCommand heard;
  auto const command = backseat::ParseDestinationCommand(text);
  if (!command)
  {
    m_navigator.OnDriverSaid(0, text);
    return heard;
  }
  auto const known = backseat::KnownPlaceOf(command->target);
  std::string const place = !known                                ? command->target
                            : *known == backseat::KnownPlace::Home ? "your home"
                                                                   : "your work";
  if (!command->polite)
  {
    double const nowMs = std::chrono::duration<double, std::milli>(
                             std::chrono::system_clock::now().time_since_epoch()).count();
    m_navigator.QueueOpener(nowMs, "bossy", {{"place", place}});
  }
  if (known)
  {
    if (auto const at = backseat::GuessPlace(m_navigator.History(), *known))
    {
      heard.kind = HeardCommand::Kind::RouteTo;
      heard.lat = at->lat;
      heard.lon = at->lon;
      heard.name = *known == backseat::KnownPlace::Home ? "Home" : "Work";
      LOG(LINFO, ("Backseat: heard a command for", heard.name));
      return heard;
    }
  }
  // Somewhere new (or home/work before there's enough history): let the driver pick.
  heard.kind = HeardCommand::Kind::Search;
  heard.query = command->target;
  LOG(LINFO, ("Backseat: searching for", heard.query));
  return heard;
}

std::optional<backseat::Sight> BackseatAdapter::SightAt(backseat::LatLon const & at, double bearingDeg) const
{
  if (!m_dataSource)
    return {};
  auto const & c = classif();
  // Things a tour guide would point at. Shops and restaurants count; it's that kind of tour.
  static std::vector<uint32_t> const kSights = {
      c.GetTypeByPath({"amenity", "cafe"}),       c.GetTypeByPath({"amenity", "restaurant"}),
      c.GetTypeByPath({"amenity", "fast_food"}),  c.GetTypeByPath({"amenity", "cinema"}),
      c.GetTypeByPath({"amenity", "theatre"}),    c.GetTypeByPath({"amenity", "library"}),
      c.GetTypeByPath({"amenity", "place_of_worship"}), c.GetTypeByPath({"tourism", "attraction"}),
      c.GetTypeByPath({"tourism", "museum"}),     c.GetTypeByPath({"tourism", "hotel"}),
      c.GetTypeByPath({"leisure", "park"}),       c.GetTypeByPath({"leisure", "stadium"}),
      c.GetTypeByPath({"shop"}),                  c.GetTypeByPath({"historic"})};

  double constexpr kBesideM = 80;
  m2::PointD const center = mercator::FromLatLon(at.lat, at.lon);
  std::optional<backseat::Sight> best;
  double bestDistance = 0;
  m_dataSource().ForEachInRect([&](FeatureType & ft)
  {
    if (ft.GetGeomType() == feature::GeomType::Line)
      return;
    bool sight = false;
    for (uint32_t type : feature::TypesHolder(ft))
    {
      uint32_t two = type;
      ftype::TruncValue(two, 2);
      uint32_t one = type;
      ftype::TruncValue(one, 1);
      if (std::find(kSights.begin(), kSights.end(), two) != kSights.end() ||
          std::find(kSights.begin(), kSights.end(), one) != kSights.end())
        sight = true;
    }
    if (!sight)
      return;
    auto const primary = ft.GetTranslatedName().m_primary;
    if (!primary || primary->empty())
      return;
    std::string const & name = *primary;
    m2::PointD const where = feature::GetCenter(ft);
    double const distance = mercator::DistanceOnEarth(center, where);
    if (distance > kBesideM)
      return;
    // Beside the road, not ahead or behind: 30 to 150 degrees off the heading.
    double const toIt = std::atan2(where.x - center.x, where.y - center.y) * 180 / M_PI;
    double off = std::fmod(toIt - bearingDeg + 540.0, 360.0) - 180;  // -180..180, negative is left
    if (std::fabs(off) < 30 || std::fabs(off) > 150)
      return;
    if (best && distance >= bestDistance)
      return;
    // Chains are "a Starbucks"; one-of-a-kind places are just their name.
    bool const chain = !ft.GetMetadata(feature::Metadata::FMD_BRAND).empty();
    best = backseat::Sight{chain ? "a " + name : name, off < 0};
    bestDistance = distance;
  }, mercator::RectByCenterXYAndSizeInMeters(center, 2 * kBesideM), scales::GetUpperScale());
  return best;
}

bool BackseatAdapter::InParkingLot(backseat::LatLon const & at) const
{
  if (!m_dataSource)
    return false;
  auto const & c = classif();
  static uint32_t const kParking = c.GetTypeByPath({"amenity", "parking"});
  static uint32_t const kParkingAisle = c.GetTypeByPath({"highway", "service", "parking_aisle"});
  // Parking aisles are lines through the lot; within a car's length or so of one counts.
  double constexpr kAisleSearchM = 20;
  m2::PointD const center = mercator::FromLatLon(at.lat, at.lon);
  bool found = false;
  m_dataSource().ForEachInRect([&](FeatureType & ft)
  {
    if (found)
      return;
    for (uint32_t const type : feature::TypesHolder(ft))
    {
      if (type == kParkingAisle)
        found = true;
      else if (type == kParking && ft.GetGeomType() == feature::GeomType::Area &&
               ft.GetLimitRect(scales::GetUpperScale()).IsPointInside(center))
        found = true;
    }
  }, mercator::RectByCenterXYAndSizeInMeters(center, kAisleSearchM), scales::GetUpperScale());
  return found;
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
  m_lastVoices.clear();
  for (auto const & c : m_navigator.TakeLines())
  {
    LOG(LINFO, ("Backseat says:", c.text));
    out.push_back(c.text);
    m_lastVoices.push_back({c.priority == backseat::Priority::Nav, c.voiceSpeed});
  }
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
  // Which way the route runs from the car's matched spot, for "you're facing the wrong way".
  if (route->GetFollowedPolyline().IsValid())
  {
    m2::PointD const here = route->GetFollowedPolyline().GetCurrentIter().m_pt;
    m2::PointD ahead;
    route->GetCurrentDirectionPoint(ahead);
    if (mercator::DistanceOnEarth(here, ahead) > 1)
    {
      // Mercator keeps angles, so the bearing is the plain screen angle turned to compass degrees.
      double const screenDeg = std::atan2(ahead.y - here.y, ahead.x - here.x) * 180.0 / M_PI;
      snap.routeBearingDeg = std::fmod(450.0 - screenDeg, 360.0);
    }
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
      snap.nextStreet = SpeakableStreetName(info.m_nextStreetName);
      snap.control = GuessControl(*route, turn.m_index);
    }
  }
  return snap;
}
