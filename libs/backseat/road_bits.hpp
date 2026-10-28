// Road-trip remarks: what's around the car (a highway, farmland, gravel, a small town, the
// wilderness), what's coming up on the route (a rest area, no gas for a long way, far-off coffee, a
// scenic overlook), the dark, the weather, losing phone signal, drive-thrus and rest stops, and two
// traps that ask the driver something and use the answer against them later.
// The host answers the map, weather and signal questions; when to say what lives here, so it can
// be tested without a map. The navigator feeds it every fix and says what it returns.
#pragma once

#include "backseat/lines.hpp"
#include "backseat/types.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace backseat
{
/// What the map shows around the car.
struct Surroundings
{
  /// On a motorway, or a trunk road built like one.
  bool motorway = false;
  /// The road is gravel, dirt or otherwise unpaved.
  bool unpaved = false;
  /// Farmland, a meadow or a farmyard right beside the road, on that side.
  bool farmLeft = false;
  bool farmRight = false;
  /// Woods, scrub, heath or bare rock around the car, and no buildings close by.
  bool wild = false;
  /// Inside a village or hamlet (not a town or a city).
  bool village = false;
};
/// What's around a point, given which way the car is heading.
using SurroundingsLookup = std::function<Surroundings(LatLon const &, double bearingDeg)>;

enum class Stopover
{
  RestArea,
  Fuel,
  Coffee,
  /// A scenic viewpoint (tourism=viewpoint) right by the road.
  Viewpoint
};

struct StopoverAhead
{
  Stopover kind = Stopover::RestArea;
  /// Along the route from the car.
  double distM = 0;
};

struct RouteAhead
{
  /// Nearest first.
  std::vector<StopoverAhead> stops;
  /// How far ahead was searched: the limit asked for, or less where the route ends sooner.
  double searchedM = 0;
};
/// Rest areas, gas stations, coffee and viewpoints along the route ahead of the car, within `maxM`.
using RouteAheadLookup = std::function<RouteAhead(double maxM)>;

/// The road-trip kind of place the car is stopped at, if any.
enum class StopSpot
{
  None,
  RestArea,
  /// A fast food place with a drive-through.
  DriveThru,
  Fuel
};
using StopSpotLookup = std::function<StopSpot(LatLon const &)>;

struct Weather
{
  bool rain = false;
  bool snow = false;
  bool fog = false;
  bool windy = false;
  /// Clear or sunny, nothing falling, no fog.
  bool clear = false;
};

/// Reads a National Weather Service observation (api.weather.gov, stations/{id}/observations/latest):
/// its textDescription ("Light Rain and Fog/Mist", "Sunny") and wind speed and gusts in m/s
/// (negative when missing).
Weather WeatherFromNws(std::string const & description, double windMps, double gustMps);

/// A line key to say and what to fill into it.
struct RoadRemark
{
  std::string key;
  Vars vars;
};

/// What the navigator knows at each fix.
struct RoadTick
{
  double tMs = 0;
  LatLon here;
  /// Negative when unknown.
  double speedMps = -1;
  double bearingDeg = -1;
  /// On the route, not rerouting, with a maneuver ahead.
  bool steady = false;
  /// Seconds to the next maneuver at the current speed (0 when not steady).
  double toTurnS = 0;
  /// Since anything was handed to the speaker.
  double sinceSpokenMs = 0;
  /// Since the trip started.
  double tripMs = 0;
  /// Something else is still going (a phone call, a punchline waiting): don't start anything.
  bool busy = false;
  /// The sun is down (and the date is known).
  bool night = false;
  /// Local hour 0..23, or -1 when unknown.
  int localHour = -1;
};

class RoadBits
{
public:
  explicit RoadBits(Units units = Units::Mph) : m_units(units) {}

  void SetSurroundingsLookup(SurroundingsLookup lookup) { m_surroundings = std::move(lookup); }
  void SetRouteAheadLookup(RouteAheadLookup lookup) { m_routeAhead = std::move(lookup); }
  void SetStopSpotLookup(StopSpotLookup lookup) { m_stopSpot = std::move(lookup); }
  void SetUnits(Units units) { m_units = units; }

  /// The phone gained or lost its data connection.
  void OnConnectivity(double tMs, bool online);
  /// The latest weather where the car is. Cheap to call often; the host refreshes it every 15 minutes.
  void OnWeather(double tMs, Weather const & weather);

  /// Called on every fix during a trip. `said` remembers across trips; `r` is a random number in [0, 1).
  std::vector<RoadRemark> Tick(RoadTick const & k, SaidLog & said, double r);

  /// A quiet stretch picked a trap: the question to ask now, if there's one to ask this trip.
  std::optional<RoadRemark> StartTrap(double tMs, SaidLog & said);
  /// Waiting for the driver to answer a question.
  bool AwaitingAnswer(double tMs) const { return m_await != Await::None && tMs <= m_awaitUntil; }
  /// The driver's answer (lowercase words with spaces around them, " like this "): what to say back.
  std::optional<RoadRemark> OnAnswer(double tMs, std::string const & words, SaidLog & said);

  /// The kind of place of the current (or just-ended) stop, once known. The navigator's own stop
  /// lines leave rest areas and drive-thrus to this.
  StopSpot CurrentStopSpot() const { return m_spot; }

  void Reset();

private:
  struct Pending
  {
    RoadRemark remark;
    double until = 0;
  };
  enum class Await
  {
    None,
    Bathroom,
    Rate,
    Survey
  };

  void TrackSurroundings(RoadTick const & k);
  void TrackRouteAhead(RoadTick const & k);
  void TrackStops(RoadTick const & k, SaidLog & said);
  void Queue(double tMs, RoadRemark remark, double validMs = 90000);
  std::optional<RoadRemark> Ambient(RoadTick const & k, SaidLog & said, double r);
  /// Estimated distance from the car to a stopover found at the last route-ahead poll.
  double AheadM(StopoverAhead const & s) const { return s.distM - (m_odoM - m_aheadOdoM); }
  std::optional<StopoverAhead> Nearest(Stopover kind) const;
  std::string Distance(double meters) const;

  Units m_units;
  SurroundingsLookup m_surroundings;
  RouteAheadLookup m_routeAhead;
  StopSpotLookup m_stopSpot;

  // Where the car is.
  double m_odoM = 0;
  std::optional<LatLon> m_lastHere;
  double m_lastSurroundT = -1e18;
  double m_motorwaySince = -1;
  double m_unpavedSince = -1;
  double m_wildSince = -1;
  double m_villageEnteredT = -1e18;
  bool m_inVillage = false;
  double m_farmT = -1e18;
  bool m_farmOnLeft = false;

  // The route ahead.
  double m_lastAheadT = -1e18;
  RouteAhead m_ahead;
  double m_aheadOdoM = 0;
  /// The rest area being watched, as an odometer reading; whether it was announced.
  std::optional<double> m_restAtOdoM;
  bool m_restAnnounced = false;
  std::optional<double> m_restPassedOdoM;
  bool m_surveyArmed = false;
  std::optional<double> m_viewpointAtOdoM;

  // Stops.
  std::optional<double> m_stopSince;
  LatLon m_stopAt;
  StopSpot m_spot = StopSpot::None;
  bool m_spotChecked = false;
  double m_drivingSinceT = -1;
  bool m_caughtThisStop = false;
  /// Where on the odometer the car last stopped at a rest area.
  std::optional<double> m_restStopOdoM;
  /// The drive-thru the car is in: where, since when, and what was said.
  std::optional<LatLon> m_driveThruAt;
  double m_driveThruSince = 0;
  /// Times the car moved up and stopped again there. A drive-thru line moves; a parked car doesn't,
  /// so nothing is said about the drive-thru until it has.
  int m_driveThruCreeps = 0;
  bool m_driveThruLongSaid = false;

  // Signal and weather.
  std::optional<double> m_offlineSince;
  bool m_signalLostSaid = false;
  bool m_signalBackSaid = false;
  std::optional<Weather> m_weather;
  std::vector<std::string> m_weatherSaid;

  // What was said this trip.
  std::vector<Pending> m_pending;
  double m_lastAmbientT = -1e18;
  int m_ambientSaid = 0;
  std::vector<std::string> m_saidThisTrip;
  int m_highwaySaid = 0;
  double m_lastHighwayT = -1e18;
  double m_lastRestAheadT = -1e18;
  bool m_sawDaylight = false;
  bool m_nightSaid = false;
  /// Chance rolls already made this trip, by key, so a "no" sticks.
  std::vector<std::string> m_rolled;
  bool m_trapAsked = false;

  Await m_await = Await::None;
  double m_awaitUntil = 0;
};

/// Never more than this many ambient remarks a trip, and at least this far apart.
int constexpr kRoadAmbientPerTrip = 8;
double constexpr kRoadAmbientEveryMs = 5 * 60000;
}  // namespace backseat
