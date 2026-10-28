// Runs one Backseat Driver trip on top of a turn-by-turn router. The host feeds it GPS fixes
// and a snapshot of the route state; it turns changes in that state into engine events
// (instruction, progress, turn completed, missed turn, reroute, arrival) and queues up what
// the GPS wants to say. It knows nothing about CoMaps, so it is unit-tested on its own;
// map/backseat_adapter builds the snapshot from the routing session.
#pragma once

#include "backseat/achievements.hpp"
#include "backseat/engine.hpp"
#include "backseat/history.hpp"
#include "backseat/plate_game.hpp"
#include "backseat/report.hpp"
#include "backseat/rewriter.hpp"
#include "backseat/stop_go.hpp"

#include <ctime>
#include <deque>
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

/// Which side of the road the lanes for the next maneuver are on, when the driver has to move over.
enum class LaneSide
{
  None,
  Left,
  Right
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
  /// The next maneuver needs the left or right lanes (and not the ones in the middle).
  LaneSide laneSide = LaneSide::None;
  /// The maneuver after the next one, and how far past it, if the host knows (-1 if not).
  TurnKind thenTurn = TurnKind::None;
  std::string thenStreet;
  double thenGapM = -1;
  /// What the router estimates for the whole trip, read when the trip starts.
  double etaSeconds = 0;
  std::string destination;
  /// Every maneuver on the route, for the opening rundown.
  std::vector<TurnKind> plannedTurns;
  LatLon origin;
  LatLon destinationPoint;
  /// Which way the route goes from the car's spot on it, degrees clockwise from north. Negative
  /// when unknown.
  double routeBearingDeg = -1;
  /// The driver drew this route on the map (route_sketch.hpp), read when the trip starts.
  bool sketched = false;
  /// For a drawn route: the fastest route between the same stops, in seconds; 0 if unknown.
  double fastestSeconds = 0;
  /// How far ahead the route bends sharply where there's no maneuver (CurveAheadM()); negative if
  /// not within reach.
  double curveAheadM = -1;
};

/// What kind of place the driver stopped at, if the host can tell. Empty when unknown.
using PlaceLookup = std::function<std::optional<Place>(LatLon const &)>;
/// Whether there's a traffic light (or similar reason to wait) right by a point.
using TrafficLightLookup = std::function<bool(LatLon const &)>;
/// The road the router would take instead, from a point on the route, avoiding the jam there
/// (e.g. "I-25"). Empty when there's no real alternative.
using AlternateLookup = std::function<std::optional<std::string>(LatLon const &)>;

enum class Bridge
{
  None,
  Land,
  /// Over a river, lake or the sea.
  Water
};
/// Whether the car is on a bridge, given where it is and which way it's heading.
using BridgeLookup = std::function<Bridge(LatLon const &, double bearingDeg)>;

/// What kind of notable place is at a point (a coffee shop, a theme park). Empty when nothing is.
using VenueLookup = std::function<std::optional<Venue>(LatLon const &)>;

/// Something worth pointing out, barely: a named place beside the road.
struct Sight
{
  /// As it should be said: "a Starbucks", "Coors Field".
  std::string name;
  /// Used when `at` is unknown; with `at`, the side is worked out from the car's heading.
  bool onLeft = false;
  /// Where it is. With it, the GPS waits until the car is about to pass it before saying anything,
  /// so the driver can actually see it. Without it, it's said right away.
  std::optional<LatLon> at;
  /// What it is, for a line about that kind of place: coffee, food, gas, park, worship, school,
  /// hospital, museum, store or landmark (stadiums, monuments, attractions). Empty or anything
  /// else gets the general lines.
  std::string kind;
};
/// A named place right beside the road ahead of the car (ideally 50-400 m ahead and within ~40 m
/// of the road), given which way the car is heading.
using SightLookup = std::function<std::optional<Sight>(LatLon const &, double bearingDeg)>;

/// Whether other audio (music, a podcast) is playing on the phone.
using MusicLookup = std::function<bool()>;

/// How the GPS refers to the driver when it throws them under the bus ("It was them, officer").
enum class Pronouns
{
  They,
  He,
  She
};

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

/// Distance along `ahead` (the route from the car onward) to the first sharp bend: where the heading
/// has turned at least `minDeg` within 80 m. Negative if none starts within `maxM`.
double CurveAheadM(std::vector<LatLon> const & ahead, double maxM, double minDeg = 50);

/// "Turn left onto Elm Street" for a maneuver and the street after it.
std::string InstructionText(TurnKind turn, std::string const & street, std::string const & destination);

/// The next maneuver's text, with the one after it when that comes right after: "Turn left onto
/// Winesap Drive, then right onto Cameo Lane".
std::string InstructionText(RouteSnapshot const & route);

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
  /// Stops next to a traffic light are red lights, not pulling over.
  void SetTrafficLightLookup(TrafficLightLookup lookup) { m_lightLookup = std::move(lookup); }
  /// Names the road to blame a jam on ("I told you to take I-25").
  void SetAlternateLookup(AlternateLookup lookup) { m_alternateLookup = std::move(lookup); }
  void SetDriverPronouns(Pronouns p) { m_pronouns = p; }
  /// What to call the driver, from first-run setup. Empty when they never said.
  void SetDriverName(std::string name) { m_driverName = std::move(name); }
  /// The driver's partner ("wife", "husband", "partner") and their pronouns, from settings.
  /// Empty turns off the "I wish your wife was driving" line.
  void SetPartner(std::string word, Pronouns pronouns)
  {
    m_partner = std::move(word);
    m_partnerPronouns = pronouns;
  }
  /// The driver's car can drive itself; getting lost is then even less forgivable.
  void SetCarHasAutopilot(bool has) { m_carHasAutopilot = has; }
  /// Lets the GPS panic on bridges.
  void SetBridgeLookup(BridgeLookup lookup) { m_bridgeLookup = std::move(lookup); }
  /// Lets the GPS ask for music on a quiet drive, and keep it to itself when music is on.
  void SetMusicLookup(MusicLookup lookup) { m_musicLookup = std::move(lookup); }
  /// Local calendar time for a timestamp (ms since the epoch), for holidays. Defaults to the
  /// device's time zone; tests swap it.
  void SetLocalTime(std::function<std::tm(double)> localTime) { m_localTime = std::move(localTime); }
  /// Lets the GPS play tour guide, without enthusiasm ("On your right, there's a Starbucks. That's it.").
  void SetSightLookup(SightLookup lookup) { m_sightLookup = std::move(lookup); }
  /// Lets the GPS comment on where we're going ("Oh, we are getting coffee?").
  void SetVenueLookup(VenueLookup lookup) { m_venueLookup = std::move(lookup); }
  /// Plates spotted on earlier trips, and where to save the log when it changes.
  void SetPlateLog(PlateLog log, std::function<void(PlateLog const &)> onChange);
  /// What was said on earlier trips, and where to save it when it changes (from TakeLines()).
  void SetSaidLog(SaidLog log, std::function<void(SaidLog const &)> onChange);
  /// Badges unlocked on earlier trips, and where to save them when one is unlocked.
  void SetAchievements(Achievements achievements, std::function<void(Achievements const &)> onChange);
  Achievements const & GetAchievements() const { return m_achievements; }
  PlateLog const & GetPlateLog() const { return m_plates.Log(); }
  /// Something the driver said out loud (speech recognition), e.g. "I see a Texas plate".
  /// Same thread as OnFix.
  void OnDriverSaid(double tMs, std::string const & text);
  /// A line to say right now, outside the queue (e.g. the snarky reply to a voice command), picked
  /// like any other so it doesn't repeat. Works between trips too. The driver's {name} and
  /// pronouns ({theycap} {theyre} {them} {their}) are filled in unless `vars` has them.
  std::string Reply(std::string const & key, Vars const & vars = {});
  /// On-device model that rewords roasts in real time. Without one, canned lines are spoken.
  void SetRewriter(std::shared_ptr<LineRewriter> rewriter) { m_gate.SetRewriter(std::move(rewriter)); }

  /// Everything queued to be spoken since the last call, oldest first.
  std::vector<Commentary> TakeLines();

  /// A line to open the next trip with, if it starts within kOpenerValidMs of `tMs`
  /// (e.g. "Alright, bossy" when the driver barked the destination).
  void QueueOpener(double tMs, std::string key, Vars vars = {});
  static double constexpr kOpenerValidMs = 600000;
  std::vector<TripRecord> const & History() const { return m_history; }
  /// Routines learned from past trips (the commute), in local time.
  std::vector<Routine> Routines() const;
  /// "work" or "home" when `at` is there, otherwise `name` (or "the usual place").
  std::string PlaceName(std::optional<LatLon> const & at, std::string const & name) const;

  bool InTrip() const { return m_engine != nullptr; }
  CritiqueEngine const * Engine() const { return m_engine.get(); }

private:
  void Update(double tMs, RouteSnapshot const & route);
  void StartTrip(double tMs, RouteSnapshot const & route);
  void Say(std::vector<Commentary> const & lines, bool mayRewrite = true);
  /// A navigation-priority line said at `t` (later than now).
  void SayNavLater(double t, std::string const & text);
  void SayNav(double tMs, std::string const & text, double voiceSpeed = 1.0);
  void TrackStop(Fix const & fix, RouteSnapshot const & route);
  void TrackTraffic(Fix const & fix, RouteSnapshot const & route);
  void ResetTrip();
  /// A maneuver was done: counts lefts in a row and unlocks NASCAR Driver on the third.
  void CountLefts(double t);
  /// "In half a mile, turn left onto Elm Street." for the maneuver in route.
  void SayInstruction(double t, RouteSnapshot const & route);
  void TrackPlateGame(double tMs, LatLon const & here, RouteSnapshot const & route);
  void SayPlate(double tMs, PlateGame::Reply const & reply);
  void SayLine(double tMs, Category category, std::string const & key, Vars vars = {},
               Priority priority = Priority::Roast);
  void TrackFear(Fix const & fix, RouteSnapshot const & route);
  void SayPolice(double tMs);
  void SayLemon(double tMs);
  void SayVenue(double tMs, RouteSnapshot const & route);
  void SayRoutine(double tMs, RouteSnapshot const & route);
  void SayHoliday(double tMs);
  std::tm LocalTime(double ms) const;
  bool SayRoutineBreak(double t, RouteSnapshot const & route);
  bool KnowsTheDate(double ms) const;
  void TrackLongDrive(double tMs, RouteSnapshot const & route);
  void TrackSights(Fix const & fix, RouteSnapshot const & route);
  void TrackIdle(Fix const & fix, RouteSnapshot const & route);
  void SaySight(double tMs, Sight const & sight);
  double NavWaitMs(Commentary const & line) const;
  void TrackWrongWay(Fix const & fix, RouteSnapshot const & route);
  /// "Strange, it feels as if I've been here before." after the car turns around.
  void TrackUTurn(Fix const & fix, RouteSnapshot const & route);
  /// "Starting the journey is always the hardest step..." when the car sits after the trip starts.
  void TrackSlowStart(Fix const & fix);
  /// "Hey did you know there is a curve ahead? I did."
  void TrackCurveAhead(Fix const & fix, RouteSnapshot const & route);
  void TrackMusic(double tMs, RouteSnapshot const & route);
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
  /// Pulled over at the roadside: said once per stop. The police line once per trip.
  bool m_pullOverSaid = false;
  bool m_policeSaid = false;
  std::optional<double> m_lastLimitMps;
  std::optional<double> m_lastBadSpeedingT;
  TrafficLightLookup m_lightLookup;
  Pronouns m_pronouns = Pronouns::They;
  std::string m_driverName;

  VenueLookup m_venueLookup;
  SightLookup m_sightLookup;
  MusicLookup m_musicLookup;
  std::function<std::tm(double)> m_localTime;
  bool m_christmasSaid = false;
  bool m_roundaboutSaid = false;
  /// The maneuver the last "get into the left lane" was for; once each.
  std::optional<uint32_t> m_laneSaidFor;
  double m_turnSaidAt = -1e18;
  /// Just past a turn, the next direction waits a moment (unless it's close) so it doesn't land
  /// mid-turn: when it goes out.
  std::optional<double> m_instructionDueAt;
  /// Punchlines waiting out their awkward pause, by when to say them.
  std::vector<Commentary> m_punchlines;
  double m_lastMusicCheckT = -1e18;
  bool m_musicAsked = false;
  bool m_musicJudged = false;
  bool m_musicPlaying = false;
  double m_musicStartedT = 0;
  double m_lastSightCheckT = -1e18;
  double m_lastSightT = -1e18;
  std::vector<std::string> m_sightsSaid;
  /// A sight spotted up ahead, waiting until the car is about to pass it.
  struct UpcomingSight
  {
    Sight sight;
    double spottedT;
  };
  std::optional<UpcomingSight> m_upcomingSight;
  /// When the last line went out (TakeLines), for filling a long silence.
  double m_lastSpokeT = -1e18;
  double m_idleGapMs = 0;
  /// When the car started heading away from the route, or negative while it isn't.
  double m_wrongWaySince = -1;
  double m_lastWrongWayT = -1e18;
  int m_wrongWaySaid = 0;
  /// Recent headings while moving (time, degrees), for spotting a U-turn.
  std::deque<std::pair<double, double>> m_headings;
  double m_lastUTurnT = -1e18;
  int m_uTurnsSaid = 0;
  /// The car has got going this trip, or the slow-start line was already said.
  bool m_startedRolling = false;
  /// Reroutes this trip; the third one gets the autopilot line.
  int m_reroutes = 0;
  /// This trip follows a route the driver drew, and how many minutes slower it is than the fastest
  /// one (0 if unknown or not slower).
  bool m_sketched = false;
  int m_sketchMinutesLost = 0;
  bool m_carHasAutopilot = false;
  std::string m_partner;
  Pronouns m_partnerPronouns = Pronouns::They;
  /// Fed up this trip: the driver must say "wise, omnipotent overseer GPS" from now on.
  bool m_titleDemanded = false;
  /// The driver asked for a game this trip and got turned down; the GPS pitches it at this time.
  bool m_gameRefused = false;
  std::optional<double> m_gamePitchAt;
  double m_lastTitleScoldT = -1e18;
  struct Opener
  {
    double t;
    std::string key;
    Vars vars;
  };
  std::optional<Opener> m_opener;
  /// The driver said the car is driving itself this trip ("it's on autopilot").
  bool m_autopilotClaimed = false;
  bool m_lemonSaid = false;
  /// Long-drive remarks said so far this trip.
  size_t m_longDriveSaid = 0;

  // Fear: bridges and sharp curves.
  BridgeLookup m_bridgeLookup;
  double m_lastBridgeCheckT = -1e18;
  bool m_onBridge = false;
  double m_lastBridgeYelpT = -1e18;
  double m_lastCurveYelpT = -1e18;
  int m_hardCurveSamples = 0;
  double m_lastCurveAheadT = -1e18;
  /// Speeding up through a bend: "Ahh, you are supposed to slow down in curves not accelerate."
  int m_curveGasSamples = 0;
  double m_lastCurveGasT = -1e18;

  // Traffic: recent (time, speed, limit) to judge a crawl, and whether we're in one.
  struct SpeedSample
  {
    double t;
    double mps;
    double limitMps;
  };
  std::vector<SpeedSample> m_speeds;
  bool m_inTraffic = false;
  StopGo m_stopGo;
  AlternateLookup m_alternateLookup;

  // License plate game.
  PlateGame m_plates;
  RegionLookup m_regionLookup;
  ParkingLookup m_parkingLookup;
  /// The last real fix, for judging whether the driver is parked when they talk.
  std::optional<Fix> m_lastFix;
  std::function<void(PlateLog const &)> m_onPlateLog;
  SaidLog m_said;
  std::function<void(SaidLog const &)> m_onSaid;
  Achievements m_achievements;
  std::function<void(Achievements const &)> m_onAchievements;
  /// The maneuver we were heading for, and how many lefts in a row were done (NASCAR Driver).
  TurnKind m_turnKind = TurnKind::None;
  int m_leftsInARow = 0;
  int m_tripsOnRoute = 0;
  double m_tripStartT = 0;
  double m_lastRegionCheckT = -1e18;
  /// Game lines held back while a maneuver is close; directions come first.
  std::vector<Commentary> m_heldPlateLines;

  std::optional<int> m_batteryPercent;
  std::optional<bool> m_charging;
};
}  // namespace backseat
