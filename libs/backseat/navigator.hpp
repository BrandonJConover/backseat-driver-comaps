// Runs one Backseat Driver trip on top of a turn-by-turn router. The host feeds it GPS fixes
// and a snapshot of the route state; it turns changes in that state into engine events
// (instruction, progress, turn completed, missed turn, reroute, arrival) and queues up what
// the GPS wants to say. It knows nothing about CoMaps, so it is unit-tested on its own;
// map/backseat_adapter builds the snapshot from the routing session.
#pragma once

#include "backseat/engine.hpp"
#include "backseat/history.hpp"
#include "backseat/plate_game.hpp"
#include "backseat/report.hpp"
#include "backseat/rewriter.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace backseat
{
enum class RouteState
{
  /// No route is being followed (none built, still in preview, or cancelled).
  Inactive,
  /// Route built, the driver hasn't reached it yet.
  NotStarted,
  OnRoute,
  /// The driver left the route; a new one is on the way (or was declined).
  OffRoute,
  Finished
};

enum class TurnKind
{
  None,
  Straight,
  Left,
  SharpLeft,
  SlightLeft,
  Right,
  SharpRight,
  SlightRight,
  UTurn,
  Roundabout,
  ExitLeft,
  ExitRight,
  Destination
};

struct RouteSnapshot
{
  RouteState state = RouteState::Inactive;
  /// Identifies the next maneuver; changes when the driver moves on to the next one.
  std::optional<uint32_t> turnId;
  TurnKind turn = TurnKind::None;
  /// Street after the next maneuver, if known.
  std::string nextStreet;
  double distToTurnM = 0;
  /// What controls the next maneuver's junction, if the host can tell (stop sign, light).
  Control control = Control::None;
  /// What the router estimates for the whole trip, read when the trip starts.
  double etaSeconds = 0;
  std::string destination;
  /// Every maneuver on the route, for the opening rundown.
  std::vector<TurnKind> plannedTurns;
  LatLon origin;
  LatLon destinationPoint;
};

/// What kind of place the driver stopped at, if the host can tell. Empty when unknown.
using PlaceLookup = std::function<std::optional<Place>(LatLon const &)>;
/// Whether a point is in a parking lot (on a parking aisle or inside a parking area).
using ParkingLookup = std::function<bool(LatLon const &)>;
/// The state or province at a point, by name as the map has it ("Colorado"). Empty when unknown.
using RegionLookup = std::function<std::optional<std::string>(LatLon const &)>;

struct Fix
{
  double tMs = 0;
  double lat = 0;
  double lon = 0;
  /// Negative when unknown.
  double speedMps = -1;
  double bearingDeg = -1;
  /// Posted limit for the road, if known.
  std::optional<double> speedLimitMps;
};

/// "Turn left onto Elm Street" for a maneuver and the street after it.
std::string InstructionText(TurnKind turn, std::string const & street, std::string const & destination);

class Navigator
{
public:
  using TripRecorded = std::function<void(TripRecord const &, TripReport const &)>;

  explicit Navigator(EngineConfig config = {}, std::vector<TripRecord> history = {}, TripRecorded onTripRecorded = {},
                     RewriteGate gate = RewriteGate());

  /// A real GPS fix (not an extrapolated one) and the route state at that moment.
  void OnFix(Fix const & fix, RouteSnapshot const & route);
  /// Route state changes between fixes, e.g. right before the host speaks.
  void Poll(RouteSnapshot const & route);
  /// Battery level 0..1. Cheap to call often; only changes reach the engine.
  void OnBattery(double tMs, double level, bool charging);
  /// Lets the navigator name the place of an unplanned stop ("Why are we stopping for gas?").
  void SetPlaceLookup(PlaceLookup lookup) { m_placeLookup = std::move(lookup); }
  /// Lets the license plate game know which state the car is in ("a Colorado plate in Colorado?").
  void SetRegionLookup(RegionLookup lookup) { m_regionLookup = std::move(lookup); }
  /// Plates called out in a parking lot don't count ("Hey that's cheating").
  void SetParkingLookup(ParkingLookup lookup) { m_parkingLookup = std::move(lookup); }
  /// Plates spotted on earlier trips, and where to save the log when it changes.
  void SetPlateLog(PlateLog log, std::function<void(PlateLog const &)> onChange);
  PlateLog const & GetPlateLog() const { return m_plates.Log(); }
  /// Something the driver said out loud (speech recognition), e.g. "I see a Texas plate".
  /// Same thread as OnFix.
  void OnDriverSaid(double tMs, std::string const & text);
  /// On-device model that rewords roasts in real time. Without one, canned lines are spoken.
  void SetRewriter(std::shared_ptr<LineRewriter> rewriter) { m_gate.SetRewriter(std::move(rewriter)); }

  /// Everything queued to be spoken since the last call, oldest first.
  std::vector<Commentary> TakeLines();

  bool InTrip() const { return m_engine != nullptr; }
  CritiqueEngine const * Engine() const { return m_engine.get(); }

private:
  void Update(double tMs, RouteSnapshot const & route);
  void StartTrip(double tMs, RouteSnapshot const & route);
  void Say(std::vector<Commentary> const & lines, bool mayRewrite = true);
  void SayNav(double tMs, std::string const & text);
  void TrackStop(Fix const & fix, RouteSnapshot const & route);
  void TrackTraffic(Fix const & fix, RouteSnapshot const & route);
  void ResetTrip();
  void TrackPlateGame(double tMs, LatLon const & here, RouteSnapshot const & route);
  void SayPlate(double tMs, PlateGame::Reply const & reply);
  int Tier() const;
  bool Parked() const;

  EngineConfig m_config;
  std::vector<TripRecord> m_history;
  TripRecorded m_onTripRecorded;
  std::unique_ptr<CritiqueEngine> m_engine;
  RewriteGate m_gate;

  double m_lastT = 0;
  RouteState m_lastState = RouteState::Inactive;
  std::optional<uint32_t> m_turnId;
  double m_lastDistToTurnM = 0;
  bool m_rerouting = false;
  bool m_arrived = false;
  TripPlan m_plan;
  PlaceLookup m_placeLookup;

  // Stops: where and when the car stopped, and whether the phone wandered off meanwhile.
  std::optional<double> m_stopSince;
  LatLon m_stopAt;
  double m_stopWanderM = 0;
  bool m_stopAnnounced = false;

  // Traffic: recent (time, speed, limit) to judge a crawl, and whether we're in one.
  struct SpeedSample
  {
    double t;
    double mps;
    double limitMps;
  };
  std::vector<SpeedSample> m_speeds;
  bool m_inTraffic = false;

  // License plate game.
  PlateGame m_plates;
  RegionLookup m_regionLookup;
  ParkingLookup m_parkingLookup;
  /// The last real fix, for judging whether the driver is parked when they talk.
  std::optional<Fix> m_lastFix;
  std::function<void(PlateLog const &)> m_onPlateLog;
  int m_tripsOnRoute = 0;
  double m_tripStartT = 0;
  double m_lastRegionCheckT = -1e18;
  /// Game lines held back while a maneuver is close; directions come first.
  std::vector<Commentary> m_heldPlateLines;

  std::optional<int> m_batteryPercent;
  std::optional<bool> m_charging;
};
}  // namespace backseat
