// Runs one Backseat Driver trip on top of a turn-by-turn router. The host feeds it GPS fixes
// and a snapshot of the route state; it turns changes in that state into engine events
// (instruction, progress, turn completed, missed turn, reroute, arrival) and queues up what
// the GPS wants to say. It knows nothing about CoMaps, so it is unit-tested on its own;
// map/backseat_adapter builds the snapshot from the routing session.
#pragma once

#include "backseat/achievements.hpp"
#include "backseat/engine.hpp"
#include "backseat/lines.hpp"
#include "backseat/history.hpp"
#include "backseat/indoors.hpp"
#include "backseat/plate_game.hpp"
#include "backseat/pop_quiz.hpp"
#include "backseat/quiet_bits.hpp"
#include "backseat/report.hpp"
#include "backseat/rewriter.hpp"
#include "backseat/road_bits.hpp"
#include "backseat/running_bits.hpp"
#include "backseat/stop_go.hpp"
#include "backseat/twenty_questions.hpp"

#include <array>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
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

/// Speed camera lines, from the app's speed camera setting.
enum class CameraAlerts
{
  /// Never: nothing said about cameras.
  Off,
  /// Auto (the default): a warning whenever the car is over the camera's limit; otherwise an easy
  /// heads-up now and then (the first camera of the trip, then at most one every 10 minutes).
  Speeding,
  /// Always: every camera gets a line.
  Always
};

/// How much the GPS says besides directions ("shut up", "talk less", "talk more").
enum class TalkLevel
{
  Full,
  /// About half the remarks; bits, games and directions as always.
  Less,
  /// Directions, lane calls and arrival only: no remarks, bits, games or badges.
  DirectionsOnly
};

/// Which side of the road the lanes for the next maneuver are on, when the driver has to move over.
enum class LaneSide
{
  None,
  Left,
  Right
};

/// What to say about the lanes for a maneuver, from the map's lanes (left to right, true for the
/// ones the route wants). Empty `phrase` and None side: nothing worth saying (all lanes work, none
/// do, or the ones that do are scattered).
struct LaneAdvice
{
  /// Which edge of the road the wanted lanes are on; None when they are in the middle.
  LaneSide side = LaneSide::None;
  /// How many lanes the route wants, and how many there are.
  int use = 0;
  int of = 0;
  /// The lanes, in words: "left lane", "right two lanes", "middle lane", "second lane from the left".
  std::string phrase;
};
LaneAdvice LaneAdviceFor(std::vector<bool> const & lanes);

/// Which part of a roundabout the next maneuver is (the router's Enter/StayOn/LeaveRoundAbout).
enum class RoundaboutLeg
{
  Enter,
  Stay,
  Leave
};

struct RouteSnapshot
{
  RouteState state = RouteState::Inactive;
  /// Identifies the next maneuver; changes when the driver moves on to the next one.
  std::optional<uint32_t> turnId;
  /// Which route `turnId` is on. A rebuilt route numbers its maneuvers afresh, so turn 3 of the new
  /// route is not turn 3 of the old one. 0 when the host doesn't say.
  uint64_t routeId = 0;
  TurnKind turn = TurnKind::None;
  /// Street after the next maneuver, if known. At a highway exit, the road and town on the sign:
  /// "I 35 North toward Dallas" (SpeakRoad()).
  std::string nextStreet;
  /// The next maneuver's highway exit number from the map (OSM junction:ref, "42B"), empty when
  /// unknown: "Take exit 42B on the right".
  std::string nextExitRef;
  double distToTurnM = 0;
  /// What controls the next maneuver's junction, if the host can tell (stop sign, light).
  Control control = Control::None;
  /// With a light at the next maneuver: which light it is counting from the car (1 the next light,
  /// 2 the one after), 0 when unknown or too many to count (LightNumberAt()).
  int lightNumber = 0;
  /// Something to see on the corner of the next turn, as said: "the Shell station", "the Walgreens",
  /// "First Baptist Church" (LandmarkPhrase()). Empty when there's nothing worth naming.
  std::string turnLandmark;
  /// The next maneuver needs the left or right lanes (and not the ones in the middle).
  LaneSide laneSide = LaneSide::None;
  /// The next maneuver's lanes from the map, left to right, true for the ones to use (OSM turn:lanes).
  /// Empty if the map has none; then `laneSide` alone decides the lane call.
  std::vector<bool> lanes;
  /// The maneuver after the next one, and how far past it, if the host knows (-1 if not).
  TurnKind thenTurn = TurnKind::None;
  std::string thenStreet;
  double thenGapM = -1;
  /// For a roundabout: which exit to take (1 is the first), 0 if the host doesn't know, and which
  /// part of the roundabout the maneuver is. `thenRoundaboutExit` is the same for the maneuver after.
  int roundaboutExit = 0;
  RoundaboutLeg roundaboutLeg = RoundaboutLeg::Enter;
  int thenRoundaboutExit = 0;
  /// What the router estimates for the whole trip, read when the trip starts.
  double etaSeconds = 0;
  /// The whole route's length in meters, read when the trip starts; 0 if unknown.
  double routeLengthM = 0;
  std::string destination;
  /// Every maneuver on the route, for the opening rundown.
  std::vector<TurnKind> plannedTurns;
  LatLon origin;
  LatLon destinationPoint;
  /// The place itself (the shop's door, not the end of the route on the road), when the host knows
  /// it: the arrival call says which side it's on (loop B119).
  std::optional<LatLon> destinationMark;
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
  /// Set on the one update right after the car drives past a highway exit: how far along the route
  /// the next exit is. Negative if the trip ends before another exit.
  std::optional<double> passedExitNextM;
  /// When the next maneuver is a highway exit: how many other exits the route passes before it
  /// (0 none, -1 unknown), and how far before it the nearest of them is (-1 unknown). Feeds the
  /// "skip this first exit, yours is the next one" call (loop B100).
  int exitsBeforeTurn = -1;
  double exitBeforeGapM = -1;
  /// The route's current stretch is a dirt track or unpaved.
  bool onDirt = false;
  /// How much of the route (by distance, 0..1) is dirt tracks and unpaved, read when the trip starts.
  double dirtShare = 0;
  /// Rise over run of the route just ahead (0.1 is a 10% climb, negative downhill), when the map
  /// has altitudes for it (walking and cycling routes do).
  std::optional<double> gradeAhead;
  /// How far along the route the next ferry leg starts, when the route takes one (BI-112). Unset
  /// when there's no ferry ahead.
  std::optional<double> ferryAheadM;
  /// The car is on the route's ferry leg right now.
  bool onFerry = false;
  /// How far along the route the next toll stretch starts (BI-113); 0 while driving on one, unset
  /// when the rest of the route is toll-free.
  std::optional<double> tollAheadM;
  /// The host read the route's toll stretches into tollAheadM. While false, "are there tolls?" gets
  /// "I can't tell", never a made-up "no tolls" (B165).
  bool tollsKnown = false;
  /// The next speed camera on the route, when one is within reach: how far ahead along the route,
  /// where it stands (the same camera keeps its spot across reroutes), and its own limit when the
  /// map has one. Unset when there's none.
  std::optional<double> cameraAheadM;
  std::optional<LatLon> cameraAt;
  std::optional<double> cameraLimitMps;
};

/// What kind of place the driver stopped at, if the host can tell. Empty when unknown.
using PlaceLookup = std::function<std::optional<Place>(LatLon const &)>;
/// Whether there's a traffic light (or similar reason to wait) right by a point.
using TrafficLightLookup = std::function<bool(LatLon const &)>;
/// Whether there's a railway level crossing right by a point (within ~60 m). Hosts answer from the
/// map's railway=level_crossing points; no lookup, no train lines.
using LevelCrossingLookup = std::function<bool(LatLon const &)>;
/// The road the router would take instead, from a point on the route, avoiding the jam there
/// (e.g. "I-25"). Empty when there's no real alternative.
using AlternateLookup = std::function<std::optional<std::string>(LatLon const &)>;

/// A jam the car is sitting in, measured from its own GPS (there's no free live traffic feed). The
/// host marks this stretch of the route as slow on the router's traffic map and rebuilds the route,
/// so the ETA counts the jam and a faster way around it is taken if there is one.
struct JamReport
{
  /// The jammed stretch along the route, relative to the car: from `behindM` back to `aheadM` ahead.
  /// What's behind is measured; what's ahead is a guess that grows the longer the jam lasts.
  double behindM = 0;
  double aheadM = 0;
  /// The car's average speed in the jam as a share of the posted limit (0..1).
  double speedShare = 0;
};
/// Gets a JamReport while the car is stuck (at most every few minutes), and nothing when the jam is
/// over: the host then drops the jam from its traffic map, without rerouting.
using JamReporter = std::function<void(std::optional<JamReport> const &)>;
/// What the router made of a JamReport, once the rebuilt route is in.
struct TrafficReroute
{
  /// The new route leaves the jammed road sooner than the old one did.
  bool newRoute = false;
  /// Seconds the new route saves over staying in the jam (0 when it saves nothing).
  double savedS = 0;
  /// Seconds the jam adds to the trip on the route the car is now on.
  double delayS = 0;
  /// The road the new route takes around the jam ("I-25"), if it has a name.
  std::string via;
};

enum class Bridge
{
  None,
  /// A bridge, and the map doesn't say over what. Its lines don't claim anything about what's below.
  Unknown,
  /// Over a river, lake or the sea.
  Water,
  Rail,
  Road,
  Valley
};
/// What a bridge crosses, from what the map shows under it. Water beats rail beats road beats valley:
/// a bridge over a river and the road beside it is a bridge over water.
Bridge ClassifyBridge(bool water, bool rail, bool road, bool valley);
/// The line pool for a bridge of this kind.
char const * BridgeLinesKey(Bridge bridge);
/// Whether the car is on a bridge, given where it is and which way it's heading.
using BridgeLookup = std::function<Bridge(LatLon const &, double bearingDeg)>;
/// Distance in meters from a spot to the nearest mapped car road; negative if the map can't tell.
using RoadDistanceLookup = std::function<double(LatLon const &)>;
/// Whether there's a trail (a path, track or bridleway, not a sidewalk) right under a point.
using TrailLookup = std::function<bool(LatLon const &)>;

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
  /// Horizontal accuracy in meters. Negative when unknown.
  double accuracyM = -1;
};

/// The fix with junk measurements (NaN/inf speed, a bearing that isn't finite or is past 360, NaN
/// accuracy, a bad limit) read as unknown, or nothing when it has no usable time or place, or replays a moment
/// before `newestTMs` (within a minute of it).
std::optional<Fix> CleanFix(Fix const & raw, double newestTMs);
/// The router's snapshot with junk numbers read as their documented "unknown" (loop B120): a distance,
/// ETA, gap, bearing, grade or exit number that is NaN, infinite, negative where it can't be, or longer
/// than the planet, and a point off the globe. A junk distance to the turn keeps `lastDistToTurnM`.
RouteSnapshot CleanRoute(RouteSnapshot route, double lastDistToTurnM);

/// Distance along `ahead` (the route from the car onward) to the first sharp bend: where the heading
/// has turned at least `minDeg` within 80 m. Negative if none starts within `maxM`.
double CurveAheadM(std::vector<LatLon> const & ahead, double maxM, double minDeg = 50);

/// What the map knows about the road after a maneuver, from the router's road-name info.
/// Multiple values are ';'-separated, as in OSM ("Dallas;Fort Worth").
struct RoadSign
{
  std::string name;            // "North Colorado Boulevard"
  std::string ref;             // "US 287"
  std::string junctionRef;     // Exit number, "42B"
  std::string destinationRef;  // Road the exit leads to, "I 35 North"
  std::string destination;     // Towns on the exit sign, "Dallas;Fort Worth"
  bool isLink = false;         // The road is a ramp.
};

/// The road as said out loud and the exit number apart. A plain street is its name, or its route
/// number when it has none ("Elm Street", "US 287"). At an exit or ramp with a sign it's the road and
/// the towns: "I 35 North toward Dallas and Fort Worth", or "toward Dallas" with no road number. At
/// most two of each are read. ForSpeech() turns "I 35" and "42B" into words later.
struct SpokenRoad
{
  std::string street;
  std::string exitRef;
};
SpokenRoad SpeakRoad(RoadSign const & sign);

/// "Turn left onto Elm Street" for a maneuver and the street after it. With a highway exit number,
/// an exit (or a keep-left/right onto one) says it: "Take exit 42B on the right for I 35 North toward
/// Dallas". A street that is only "toward Dallas" is said without "onto".
std::string InstructionText(TurnKind turn, std::string const & street, std::string const & destination,
                            std::string const & exitRef = {});


/// The next maneuver's text, with the one after it when that comes right after: "Turn left onto
/// Winesap Drive, then right onto Cameo Lane". "Right after" is 150 m, or 10 s at `speedMps` up to 500 m
/// (loop B86); exits and roundabouts chain too, going straight never does. A roundabout with a known
/// exit says which one: "At the roundabout, take the second exit onto Oak Street" (loop B98).
std::string InstructionText(RouteSnapshot const & route, double speedMps = 0);

/// Which side of the road the destination is on (loop B119): "right" or "left" when `mark` is ahead
/// of `here` along `routeBearingDeg`, at least 10 m off to the side and within 500 m; empty when the
/// bearing is unknown, or the mark is behind, on the road itself or too far to judge. Geometry only, so
/// it's the same in left-hand traffic.
std::string DestinationSide(LatLon const & here, double routeBearingDeg, LatLon const & mark);

/// Which light along the way the turn's light is, from how far along the route each mapped light is
/// (meters from the car, any order) and how far the turn is. Lights within `clusterM` of each other
/// are one junction (OSM often maps a signal per approach), and the turn's own junction is anything
/// within `atTurnM` of it. 1 when the turn's light is the next one, 2 or 3 for the second or third;
/// 0 when the turn has no light, there are more than three, or a light is under the car right now.
int LightNumberAt(std::vector<double> lightsAlongM, double turnAlongM, double clusterM = 50, double atTurnM = 30);

/// A corner landmark as said in a direction ("Turn left at the Shell station"), from the map's name
/// for it, what kind of place it is (gas, food, coffee, store, worship, school...) and whether it's a
/// chain. Chains get "the": "the Walgreens", gas chains "the Shell station". Names that would confuse
/// the direction ("Right Way Auto", "Exit 9 Diner", "Left Bank Cafe") or are too long to say quickly
/// come back empty.
std::string LandmarkPhrase(std::string const & name, std::string const & kind, bool chain);

/// Where the car was left, like Google's and Apple's parked-car pin. Saved when a drive ends (arrival,
/// or the mileage log seeing the car park), or when the driver says "remember where I parked".
struct ParkedCar
{
  LatLon at;
  /// When it was parked, ms since the epoch.
  double tMs = 0;
  /// The driver asked for this spot to be saved (it outranks a guess).
  bool saved = false;
};
/// "lat,lon,tMs,saved" for the settings file, and back. Nothing for a damaged or off-globe line.
std::string ParkedCarLine(ParkedCar const & car);
std::optional<ParkedCar> ParkedCarFromLine(std::string const & line);
/// How long it's been parked, to follow "It's been there": "for a few minutes", "for about 20 minutes",
/// "for about 3 hours", "since yesterday", "for 4 days". Empty for a time well in the future.
std::string ParkedAgoWords(double agoMs);
/// What to say to "where did I park?", and where to walk.
struct CarAnswer
{
  /// Line key: car.find (with {distance}, {direction}, {ago}), car.far (too far to walk), car.here
  /// (it's right there), car.unknown (nothing saved), car.driving (the driver is in it).
  std::string key;
  Vars vars;
  /// Set for car.find: walking directions go here.
  std::optional<LatLon> walkTo;
};
CarAnswer FindCarAnswer(std::optional<ParkedCar> const & car, std::optional<LatLon> const & here, double nowMs,
                        Units units, bool driving);

/// "the second exit" for 2, "exit 12" past the eighth; empty for 0 or less (unknown).
std::string RoundaboutExitWords(int exit);

/// How close the maneuver after next must be to chain onto the next one's call, at this speed.
double ChainWithinM(double speedMps);
/// How close an earlier exit has to sit before the route's exit for "skip this first exit, yours is
/// the next one" (loop B100): about 27 s of driving, 300 m to 1 km.
double ExitSkipWithinM(double speedMps);

/// Which way `to` is from `from`, as said out loud: "northwest".
std::string CompassDirection(LatLon const & from, LatLon const & to);

/// `spoken` answers quiz `q` rather than asking about the drive (see Navigator::QuizTakes).
bool QuizAnswerTakes(QuizQuestion const & q, std::string const & spoken);
/// The drive outranks a game: a question about the next turn or exit, or a command such as "cancel the
/// route", "mute" or "take me to Target". A game can't swallow these, even mid-question.
/// `aboutIt`: a 20 Questions question about "it", where "go" ("do you go there to get it?") isn't the route.
bool DriveComesFirst(std::string const & spoken, bool aboutIt = false);

/// Personal asks wait for a calm moment (loop B174, BI-087; TU Berlin: informal, personal-topic chatter
/// is the most distracting kind). Line keys that ask the driver something personal or invite an answer:
/// file questions, getting-to-know-you and mileage asks, "rate my directions", game and music offers.
std::vector<std::string> const & AnswerInvitingKeys();
bool InvitesAnswer(std::string const & key);
/// At highway speed (~50 mph and up) with a turn or exit under a minute away, or in rain, snow, ice or
/// fog, such an ask waits. City speed, or the next maneuver far off in clear weather: no wait.
bool AskMustWait(double speedMps, double toTurnS, bool badWeather);

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
  /// Miles or kilometers, from the app's Units setting (CoMaps sets it from the phone's region on
  /// first launch). Cheap to call on every fix; only a change does anything. Speeds, distances, the
  /// speeding thresholds and the few lines with a distance written into them all follow.
  void SetUnits(Units units);
  Units GetUnits() const { return m_config.units; }
  /// The country the car is in, as the map files name it ("US", "Canada", "UK"). Decides which
  /// holidays get a line: Canada gets Canada Day and its own Thanksgiving, not the Fourth of July.
  /// Empty (unknown) is treated as the US. A country that drives on the left also gets the few
  /// drive-side jokes mirrored ("The right lane is for passing").
  void SetCountry(std::string country)
  {
    SetLineDriveSide(DrivesOnLeft(country));
    m_country = std::move(country);
  }
  /// Lets the navigator name the place of an unplanned stop ("Why are we stopping for gas?").
  void SetPlaceLookup(PlaceLookup lookup) { m_placeLookup = std::move(lookup); }
  /// Lets the license plate game know which state the car is in ("a Colorado plate in Colorado?").
  void SetRegionLookup(RegionLookup lookup) { m_regionLookup = std::move(lookup); }
  /// Plates called out in a parking lot don't count ("Hey that's cheating").
  void SetParkingLookup(ParkingLookup lookup) { m_parkingLookup = std::move(lookup); }
  /// Starting a trip inside a building gets "You drove into a building and then decided it was time
  /// for directions." (indoors.hpp).
  void SetBuildingLookup(BuildingLookup lookup) { m_buildingLookup = std::move(lookup); }
  /// Stops next to a traffic light are red lights, not pulling over.
  void SetTrafficLightLookup(TrafficLightLookup lookup) { m_lightLookup = std::move(lookup); }
  /// Stopped at a railway crossing for a while gets one "wait.train" line per stop, never while
  /// moving (BI-111); the crossing also explains the stop, so no pull-over line.
  void SetLevelCrossingLookup(LevelCrossingLookup lookup) { m_crossingLookup = std::move(lookup); }
  /// Names the road to blame a jam on ("I told you to take I-25").
  void SetAlternateLookup(AlternateLookup lookup) { m_alternateLookup = std::move(lookup); }
  /// Hands jams to the host for traffic-aware routing (JamReport). Nothing set: jams only get jokes.
  void SetJamReporter(JamReporter reporter) { m_jamReporter = std::move(reporter); }
  /// The host rebuilt the route around a reported jam. Call before the next OnFix with the new route,
  /// so its new maneuver isn't taken for a turn the driver made. Says how it went: a way around, or
  /// no way around and how late that makes us.
  void OnTrafficReroute(double tMs, TrafficReroute const & result);
  void SetDriverPronouns(Pronouns p) { m_pronouns = p; }
  /// What to call the driver, from first-run setup. Empty when they never said.
  void SetDriverName(std::string name) { m_driverName = std::move(name); }
  /// The GPS's own name, i.e. the voice the driver picked ("Echo"), for "This is Echo".
  /// Picks its personality too: each voice comes with one (PersonaForVoice).
  void SetGpsName(std::string name)
  {
    SetPersona(PersonaForVoice(name));
    m_gpsName = std::move(name);
  }
  /// How the GPS talks: the Critic, Melancholy, Proud Parent or Dirty Santa. LinesFor() follows it.
  void SetPersona(Persona persona)
  {
    m_persona = persona;
    SetLinePersona(persona);
  }
  Persona GetPersona() const { return m_persona; }
  /// The driver's partner ("wife", "husband", "partner") and their pronouns, from settings.
  /// Empty turns off the "I wish your wife was driving" line.
  void SetPartner(std::string word, Pronouns pronouns)
  {
    m_partner = std::move(word);
    m_partnerPronouns = pronouns;
    if (!m_partner.empty())
      SetRidesAlone(false);
  }
  /// The driver said they ride alone ("just me" to setup.askPassenger, i.e. ParsePassengerAnswer gave
  /// an empty word): LinesFor() leaves out lines that talk to a passenger (loop B166, BI-129). Naming a
  /// partner with SetPartner turns it back off.
  void SetRidesAlone(bool alone)
  {
    m_ridesAlone = alone;
    SetLineRidesAlone(alone);
  }
  bool RidesAlone() const { return m_ridesAlone; }
  /// The driver's car can drive itself; getting lost is then even less forgivable.
  void SetCarHasAutopilot(bool has) { m_carHasAutopilot = has; }
  /// Where the driver lives, from the getting-to-know-you questions. On the streets, there are no
  /// home jokes and no garage door; LinesFor() leaves out lines that don't fit.
  void SetHousing(Housing housing)
  {
    m_housing = housing;
    SetLineHousing(housing);
  }
  Housing GetHousing() const { return m_housing; }
  /// Lets the GPS panic on bridges.
  void SetBridgeLookup(BridgeLookup lookup) { m_bridgeLookup = std::move(lookup); }
  void SetRoadDistanceLookup(RoadDistanceLookup lookup) { m_roadDistanceLookup = std::move(lookup); }
  /// Lets a walk turn into a hike once it's on trails.
  void SetTrailLookup(TrailLookup lookup) { m_trailLookup = std::move(lookup); }
  /// Lets the GPS ask for music on a quiet drive, and keep it to itself when music is on.
  void SetMusicLookup(MusicLookup lookup) { m_musicLookup = std::move(lookup); }
  /// Local calendar time for a timestamp (ms since the epoch), for holidays. Defaults to the
  /// device's time zone; tests swap it.
  void SetLocalTime(std::function<std::tm(double)> localTime) { m_localTime = std::move(localTime); }
  /// Lets the GPS play tour guide, without enthusiasm ("On your right, there's a Starbucks. That's it.").
  void SetSightLookup(SightLookup lookup) { m_sightLookup = std::move(lookup); }
  /// Lets the GPS comment on where we're going ("Oh, we are getting coffee?").
  void SetVenueLookup(VenueLookup lookup) { m_venueLookup = std::move(lookup); }
  /// Road-trip remarks (road_bits.hpp): what's around the car, what's coming up on the route, and
  /// what kind of place the car stopped at.
  void SetSurroundingsLookup(SurroundingsLookup lookup) { m_road.SetSurroundingsLookup(std::move(lookup)); }
  void SetRouteAheadLookup(RouteAheadLookup lookup) { m_road.SetRouteAheadLookup(std::move(lookup)); }
  void SetStopSpotLookup(StopSpotLookup lookup) { m_road.SetStopSpotLookup(std::move(lookup)); }
  /// The phone gained or lost its data connection ("No signal out here").
  void OnConnectivity(double tMs, bool online) { m_road.OnConnectivity(tMs, online); }
  /// The weather where the car is (WeatherFromNws); the host refreshes it every 15 minutes or so.
  void OnWeather(double tMs, Weather const & weather)
  {
    m_road.OnWeather(tMs, weather);
    if (weather.tempC)
      m_tempC = weather.tempC;
  }
  /// Plates spotted on earlier trips, and where to save the log when it changes.
  void SetPlateLog(PlateLog log, std::function<void(PlateLog const &)> onChange);
  /// What was said on earlier trips, and where to save it when it changes (from TakeLines()).
  void SetSaidLog(SaidLog log, std::function<void(SaidLog const &)> onChange);
  /// Badges unlocked on earlier trips, and where to save them when one is unlocked.
  void SetAchievements(Achievements achievements, std::function<void(Achievements const &)> onChange);
  Achievements const & GetAchievements() const { return m_achievements; }
  PlateLog const & GetPlateLog() const { return m_plates.Log(); }
  /// 20 Questions games so far (record, recent answers), and where to save the log when it changes.
  void SetTwentyLog(TwentyLog log, std::function<void(TwentyLog const &)> onChange);
  TwentyLog const & GetTwentyLog() const { return m_twenty.Log(); }
  /// Map tags of what's around the car, about 2 km out ("fuel", "water_tower", "farm"; see
  /// TwentyThing::tag), so 20 Questions can pick something on the drive. Optional.
  using NearbyThingsLookup = std::function<std::set<std::string>(LatLon const &)>;
  void SetNearbyThingsLookup(NearbyThingsLookup lookup) { m_nearbyThings = std::move(lookup); }
  /// Something the driver said out loud (speech recognition), e.g. "I see a Texas plate".
  /// Same thread as OnFix.
  void OnDriverSaid(double tMs, std::string const & text);
  /// Stopped long enough to be a stop the driver chose (not a red light, not a jam), or crawling
  /// through a parking lot. The plate game and parked-only lines go by this.
  bool Parked() const;
  /// A line to say right now, outside the queue (e.g. the snarky reply to a voice command), picked
  /// like any other so it doesn't repeat. Works between trips too. The driver's {name} and
  /// pronouns ({theycap} {theyre} {them} {their}) are filled in unless `vars` has them.
  std::string Reply(std::string const & key, Vars const & vars = {});
  /// The answer to "are there tolls?" (AppAction::TollsAhead) from the last route update: on one now,
  /// the next toll stretch's distance, none left, or can't tell. Never a price (no data). Loop B165.
  std::string TollReply();
  /// The answer to "is there traffic?" / "why is it so slow?" (AppAction::TrafficAhead): stuck in it, moving
  /// fine here, or can't tell. Only judges where the car is; there's no live traffic feed. Loop B165.
  std::string TrafficReply();
  /// Within this far of the next maneuver (or 20 s at the current speed, whichever is more),
  /// "repeat that" only gets lines that say the direction within the first few words.
  static double constexpr kRepeatFirstWithinM = 400;
  /// Words a near-turn "repeat that" line may have before its {instruction}.
  static int constexpr kRepeatFirstMaxWords = 3;
  /// Whether a line says {instruction} within the first kRepeatFirstMaxWords words.
  static bool InstructionComesFirst(std::string const & line);
  /// Speed for turn timing (game holds, nav waits, seconds to the turn): the fix's speed when known, else
  /// the last speed seen this trip (a tunnel doesn't stop the car), 0 if none yet (loop B82).
  double TimingMps(double speedMps) const;
  /// Reply() in another personality's words, e.g. a new voice saying hello in its own manner before
  /// it takes over. The GPS's own personality is unchanged.
  std::string ReplyAs(Persona persona, std::string const & key, Vars const & vars = {});
  /// On-device model that rewords roasts in real time. Without one, canned lines are spoken.
  void SetRewriter(std::shared_ptr<LineRewriter> rewriter) { m_gate.SetRewriter(std::move(rewriter)); }

  /// Everything queued to be spoken since the last call, oldest first.
  std::vector<Commentary> TakeLines();

  /// A line to open the next trip with, if it starts within kOpenerValidMs of `tMs`
  /// (e.g. "Alright, bossy" when the driver barked the destination).
  void QueueOpener(double tMs, std::string key, Vars vars = {});
  /// The driver just gave a command, so a route change in the next little while is theirs.
  void NoteVoiceCommand() { m_lastVoiceCommandT = m_lastT; }
  /// How this trip is being made, from the route type (Drive, Walk or Bike). Lines and checks follow
  /// it; no speeding on foot. On a drive or a walk the Navigator itself notices dirt and trails, and
  /// makes it an OffRoad trip or a Hike; the same route type again keeps that.
  void SetTravelMode(TravelMode mode);
  /// The driver said how they're traveling ("off-road mode", "back on the road"): that beats
  /// whatever the Navigator noticed on its own.
  void DriverSetTravelMode(TravelMode mode);
  TravelMode GetTravelMode() const { return m_travelMode; }
  TravelMode Mode() const { return m_travelMode; }
  /// The host can show the question and the route types (TakeModeAskToShow(), TakeModeSwitch()), so
  /// on a walking route of more than a couple of miles the GPS asks "Are you actually intending to
  /// walk there?".
  void SetCanAskTravelMode(bool can) { m_canAskMode = can; }
  /// True once when the question was just asked: the host puts up Yes / No buttons.
  bool TakeModeAskToShow() { return std::exchange(m_modeAskShow, false); }
  /// Whether the question still waits for an answer (a spoken one closes it too).
  bool ModeAskOpen() const { return m_modeAskOpen; }
  /// Yes, we really are walking (or no: then it's a drive). False if there was no question open.
  /// A "no" while the car is already moving gets "pick Driving when we're parked", not a tap prompt.
  bool AnswerModeAsk(bool walking);
  /// The next drive's start says "check the mode now and then, I may swap it on you" (it won't).
  void WarnModeOnNextDrive() { m_modeWarnNextDrive = true; }
  /// Not walking after all: the route type the driver has to pick by hand ("I'm going to need you to
  /// click the driving button"). The host shows the route types with it highlighted. Empty otherwise.
  std::optional<TravelMode> TakeModeSwitch() { return std::exchange(m_modeSwitch, std::nullopt); }
  /// The plain direction for the current maneuver, with no jokes on it. Empty with no route.
  std::string const & LastInstructionText() const { return m_lastInstructionText; }
  static double constexpr kOpenerValidMs = 600000;
  std::vector<TripRecord> const & History() const { return m_history; }
  /// Routines learned from past trips (the commute), in local time.
  std::vector<Routine> Routines() const;
  /// "work" or "home" when `at` is there, otherwise `name` (or "the usual place").
  std::string PlaceName(std::optional<LatLon> const & at, std::string const & name) const;

  /// The Kokoro voice of whoever is on the phone with the GPS right now, if they'll be put on the
  /// line; empty otherwise. The host warms that voice up when this changes, so the caller's first
  /// line isn't late.
  std::string const & PhoneCallerVoice() const { return m_call.CallerVoice(); }

  /// "Shut up" keeps the turns (DirectionsOnly); "talk less" halves the remarks. Kept across trips.
  void SetTalkLevel(TalkLevel level);
  /// Which speed cameras get a line (CameraAlerts). Cheap to call on every fix.
  void SetCameraAlerts(CameraAlerts alerts) { m_cameraAlerts = alerts; }
  TalkLevel GetTalkLevel() const { return m_talk; }

  /// Pop quizzes (pop_quiz.hpp) need the driver to answer out loud, so they only happen once the
  /// host says it listens after "quiz.ask" lines. Off until then.
  void SetPopQuizOn(bool on) { m_quizOn = on; }
  /// What the pop quiz card shows: the question while it's asked, then the result for a moment.
  struct QuizCard
  {
    std::string question;
    std::array<std::string, 3> choices;
    /// What the driver went with, -1 for nothing (yet).
    int picked = -1;
    /// The right choice once the quiz is over; -1 while it's still being asked.
    int correct = -1;
    /// Taps count (not on a drive: on a drive it's out loud only).
    bool tappable = false;
    /// Brownie points so far, ever.
    int brownies = 0;
  };
  std::optional<QuizCard> PopQuizCard() const;
  /// A pop quiz is waiting and `spoken` answers it (a choice, or "I don't know"), so it goes to the
  /// quiz before anything else: "Pull to the right and stop" isn't "stop navigation". But the drive
  /// comes first: "where do I turn?" or "cancel the route" isn't an answer, even when a choice shares
  /// a word with it, unless a choice was named by its letter ("B", "option two").
  bool QuizTakes(std::string const & spoken) const;
  /// 20 Questions is on and `spoken` is a move in it (a question, a guess, a hint, giving up or
  /// quitting), so it goes to the game, not the app: "Do you have to go to a gas station to get it?"
  /// isn't "find a gas station", and "Can you use it on foot?" isn't walking directions. The drive still
  /// comes first ("what's the next turn", "cancel the route"). Hosts ask this after QuizTakes.
  bool TwentyTakes(std::string const & spoken) const;
  /// "Can you repeat the question?" while a quiz waits: reads it again (once a quiz) and gives the
  /// driver the full time to answer again. True when it did; hosts ask this before QuizTakes.
  bool QuizReask(double tMs, std::string const & spoken);
  /// The driver tapped a choice on the card. Ignored on a drive.
  void QuizTapped(double tMs, int choice);
  int BrowniePoints() const { return m_said.Counter(kBrowniesKey); }
  /// Correct turns in a row, carried across trips (running_bits.hpp).
  int TurnStreak() const { return m_bits.Streak(); }
  static constexpr char const * kBrowniesKey = "_quiz.brownies";

  bool InTrip() const { return m_engine != nullptr; }
  CritiqueEngine const * Engine() const { return m_engine.get(); }

private:
  void Update(double tMs, RouteSnapshot const & route);
  void StartTrip(double tMs, RouteSnapshot const & route);
  void Say(std::vector<Commentary> const & lines, bool mayRewrite = true);
  /// A navigation-priority line said at `t` (later than now).
  void SayNavLater(double t, std::string const & text);
  void SayNav(double tMs, std::string const & text, double voiceSpeed = 1.0);
  /// The speed the car's progress since the last fix shows (route distance to the same turn shrinking,
  /// or a move between two accurate fixes), or nothing when there's no evidence either way (B147).
  std::optional<double> ProgressSpeed(Fix const & fix, RouteSnapshot const & route) const;
  void TrackStop(Fix const & fix, RouteSnapshot const & route);
  void TrackTraffic(Fix const & fix, RouteSnapshot const & route);
  void ResetTrip();
  /// A maneuver was done: counts lefts in a row and unlocks NASCAR Driver on the third.
  void CountLefts(double t);
  /// "In half a mile, turn left onto Elm Street." for the maneuver in route.
  void SayInstruction(double t, RouteSnapshot const & route);
  /// "right"/"left" for an arrival within reach whose place the host marked; empty otherwise.
  std::string DestinationSideNow(RouteSnapshot const & route) const;
  void SayMissedTurn(double t, bool unseen);
  void TrackPlateGame(double tMs, LatLon const & here, double speedMps, RouteSnapshot const & route);
  void SayPlate(double tMs, PlateGame::Reply const & reply);
  void SayLine(double tMs, Category category, std::string const & key, Vars vars = {},
               Priority priority = Priority::Roast);
  /// `options` for `key` minus the lines claiming a highway when the map says the car is off one
  /// (straight.later, traffic.standstill; loop B144). Unknown road or a motorway: unchanged.
  std::vector<std::string> RoadFit(std::string const & key, std::vector<std::string> options) const;
  /// Hands a line over; "setup || punchline" is split, the punchline after an awkward pause.
  /// `setupVoice`: someone else says the setup of a "setup || punchline" line (a Kokoro voice id).
  void PushLine(Commentary c, std::string const & setupVoice = {});
  /// The running bits' say on one of the engine's lines (running_bits.hpp): it may go out as is,
  /// be swapped for another line, or be dropped (false).
  bool Punch(Commentary & line);
  /// Text for a running-bit line: picked (or its story beat), filled with the driver's details
  /// and the current speed.
  std::string BitText(BitLine const & bit);
  /// No fix for over 30 s before `t` (m_lastFix is still the one before this OnFix): the GPS was out.
  bool FixGapAt(double t) const;
  /// A remark is queued, held for a turn, or a punchline is waiting out its pause.
  bool RemarkWaiting() const;
  /// A remark went out in the last breath, or one is waiting to (loop B51, BI-046).
  bool RemarkJustSaid(double t) const;
  /// "You've done everything right for five minutes. I don't like it."
  void TrackClean(Fix const & fix, RouteSnapshot const & route);
  /// Straight nags and the turn reminders before the final call: fewer of them, from bigger pools
  /// (key + ".more"), and "follow the road" instead of "straight" in a curve. False: drop it.
  bool PunchNag(Commentary & line, double r);
  /// The road bends here or just ahead: the heading has swung lately, or a sharp bend is close.
  bool Bending() const;
  /// Winding road: "Hold on tight." "10 and 2. 10 and 2." (Brandon's curve lines).
  void TrackBends(Fix const & fix, RouteSnapshot const & route);
  bool SwungLately() const;
  /// Cold out: "It's freezing out here. I need to go in your pocket asap." then, once the car's
  /// had time to warm up, the air vent request (Brandon's lines). Hot out: "It's like a sauna in
  /// here.", or the eggs from the grocery store being hard boiled by now.
  void TrackCold(Fix const & fix, RouteSnapshot const & route);
  /// The days after a clock change: "Wasn't it brighter out at this time yesterday?" (fall back)
  /// or "Why am I so tired and why is it so dark out?" (spring forward).
  void TrackClockChange(Fix const & fix, RouteSnapshot const & route);
  /// Leaving a bar late at night: "I'm really proud of you taking one for the team and being a
  /// designated driver." (Brandon's line).
  void TrackNightOut(Fix const & fix, RouteSnapshot const & route);
  /// Seconds the local clock is ahead of UTC at `ms`.
  long UtcOffsetS(double ms) const;
  void TrackFear(Fix const & fix, RouteSnapshot const & route);
  /// "Either there is a new road here or we are completely off course."
  void TrackOffRoad(Fix const & fix);
  void TrackTravelMode(Fix const & fix, RouteSnapshot const & route);
  void ChangeTravelMode(TravelMode mode, char const * why);
  /// Speed that counts as moving along on this kind of trip (a walker never does 9 mph).
  double MovingMps() const;
  /// "I probably should have told you the exit you just passed is the last one for a hundred miles."
  void TrackLastExit(double tMs, RouteSnapshot const & route);
  void TrackExitSkip(Fix const & fix, RouteSnapshot const & route);
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
  double LastTimingMps() const;
  /// The next instruction goes out at once after a turn when the maneuver is this close (loop B81).
  double InstructionNowWithinM() const;
  void TrackWrongWay(Fix const & fix, RouteSnapshot const & route);
  /// "Strange, it feels as if I've been here before." after the car turns around.
  void TrackUTurn(Fix const & fix, RouteSnapshot const & route);
  /// "Starting the journey is always the hardest step..." when the car sits after the trip starts.
  void TrackSlowStart(Fix const & fix);
  /// "Hey did you know there is a curve ahead? I did."
  void TrackCurveAhead(Fix const & fix, RouteSnapshot const & route);
  void TrackFerry(Fix const & fix, RouteSnapshot const & route);
  void HoldStopForFerry(RouteSnapshot const & route);
  void TrackToll(Fix const & fix, RouteSnapshot const & route);
  /// Speed cameras: a warning on the way up to one, and a word as the car passes it.
  void TrackSpeedCamera(Fix const & fix, RouteSnapshot const & route);
  void TrackMusic(double tMs, RouteSnapshot const & route);
  /// The trip started indoors: say so, and maybe remark on it once the car gets out.
  void SayIndoorStart(double tMs, RouteSnapshot const & route);
  void TrackLeftBuilding(Fix const & fix);
  /// "I'm back" on the first fix with a speed after a GPS dropout at road speed (loop B116).
  void TrackSignalBack(Fix const & fix, RouteSnapshot const & route, double lostFromT, double lostAtMps);
  /// Dead stretch with nothing to say about directions: a phone call, I spy, or a useless fact.
  void TrackQuietBits(Fix const & fix, RouteSnapshot const & route);
  void StartCall(double tMs);
  /// A quiet-stretch bit is still going (a call, a game of 20 Questions), so nothing else should start one.
public:
  /// 20 Questions or the plate game is on (or offered), so what's heard goes to the game first.
  /// Now is a calm moment for a personal ask (AskMustWait on the last fix and route), e.g. before a host
  /// asks a file question. No trip: always calm.
  bool CalmForAsk() const;
  bool InGame() const
  {
    return QuizAsking() || m_twenty.Active() || m_plates.GetState() == PlateGame::State::Offered ||
           m_plates.GetState() == PlateGame::State::Playing;
  }

private:
  /// AskMustWait for this fix and route.
  bool AsksWait(double speedMps, RouteSnapshot const & route) const;
  bool QuietBitBusy() const { return m_call.Active() || m_twenty.Active() || QuizAsking(); }
  /// A pop quiz question is waiting for its answer on a trip still going.
  bool QuizAsking() const { return m_quiz.Asking() && m_engine && !m_arrived; }
  /// Springs a pop quiz now; `asked`: the driver asked for it. False if there's no question to ask.
  bool StartPopQuiz(double tMs, bool asked);
  /// Quiet stretch, no turn coming: maybe a pop quiz. Also runs out the clock on one being asked.
  void TrackPopQuiz(Fix const & fix, RouteSnapshot const & route);
  void SayQuizOutcome(double tMs, PopQuiz::Outcome outcome);
  /// A "quiz.*" line, filled in, to say right away.
  Commentary QuizLine(double tMs, std::string const & key, Vars vars = {});
  /// The GPS asks to play 20 Questions (a quiet-stretch bit). False if it can't now.
  bool OfferTwentyQuestions(Fix const & fix);
  /// Lines held near a maneuver, "anyway, where were we" after directions, nudges, giving up.
  void TrackTwentyQuestions(Fix const & fix, RouteSnapshot const & route);
  void SayTwenty(double tMs, TwentyQuestions::Reply const & reply);
  /// The game, picking its lines through m_said (bound here, so a moved Navigator stays right).
  TwentyQuestions & Twenty();
  bool Night(double tMs, LatLon const & here) const;
  /// A "call.*" line, filled in, to say right away.
  Commentary CallLine(double tMs, std::string const & key);
  /// Holds back remarks that would still be playing when the next turn's final call is due (or
  /// while the car is making the turn) and lets them out once it's done; stale ones are dropped.
  std::vector<Commentary> KeepTurnsClear(std::vector<Commentary> lines);
  /// Puts the call on hold for directions, takes it back up, or ends it, as lines go out.
  std::vector<Commentary> FilterForCall(std::vector<Commentary> lines);
  /// Rest areas, highways, farmland, the dark, the weather, signal and drive-thrus (road_bits.hpp).
  void TrackRoadBits(Fix const & fix, RouteSnapshot const & route);
  /// Out on foot or a bike: the incline warning and "Are you actually exercising?".
  void TrackOutdoors(Fix const & fix, RouteSnapshot const & route);
  /// A hike, or a ride on trails: out in nature, not on a street.
  bool InNature() const;
  /// The outdoors quiet-stretch bit to do now (Brandon's), or empty when none is due.
  std::string OutdoorBit(double r) const;
  void SayOutdoorBit(double t, std::string const & key);
  /// One-line bits wait a few trips before coming back, so they stay a surprise.
  bool Rested(std::string const & key) const;
  void MarkSaid(std::string const & key);

  /// {name}, {gps} and the driver's pronouns, for lines about the driver.
  Vars DriverVars() const;
  int Tier() const;

  EngineConfig m_config;
  std::vector<TripRecord> m_history;
  TripRecorded m_onTripRecorded;
  std::unique_ptr<CritiqueEngine> m_engine;
  RewriteGate m_gate;

  double m_lastT = 0;
  /// The newest fix timestamp OnFix took, for dropping replayed fixes.
  double m_lastFixT = -1e18;
  double m_lastVoiceCommandT = -1e12;
  TravelMode m_travelMode = TravelMode::Drive;
  double m_lastHeadingT = -1e18;
  std::string m_lastInstructionText;
  std::optional<double> m_rerouteRoastDueAt;
  // Off the route right at the turn: the miss is said at confirmAt unless a blip brings back the same maneuver.
  struct PendingMiss
  {
    double confirmAt = 0;
    bool unseen = false;  // the fix gap was at the moment it left the route
  };
  std::optional<PendingMiss> m_missPending;
  /// About when the phone is done saying everything handed to it so far.
  double m_talkingUntil = 0;
  /// The trip-time prediction, said a little after the start like a direction but held for turns.
  std::string m_predictionText;
  struct TurnHeld
  {
    Commentary line;
    double heldAt = 0;
    /// Turns completed when it was held.
    int turnsDone = 0;
  };
  std::vector<TurnHeld> m_turnHeld;
  RouteState m_lastState = RouteState::Inactive;
  std::optional<uint32_t> m_turnId;
  /// The route m_turnId is on (RouteSnapshot::routeId).
  uint64_t m_routeId = 0;
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
  /// The navigator called it traffic at some point during this stop: a jam, not a stop the driver chose.
  bool m_stopInTraffic = false;
  /// When the car last pulled away from a stop of a minute or more.
  double m_resumedT = -1e18;
  /// Pulled over at the roadside: said once per stop. The police line once per trip.
  bool m_pullOverSaid = false;
  /// This stop has been checked for a railway crossing (once per stop, at kTrainWaitMs).
  bool m_trainChecked = false;
  /// The current stop began on a ferry: the boat explains it (no longStop, pull-over or resumed lines).
  bool m_stopOnFerry = false;
  /// Ferry lines, once per ferry (BI-112); both reset after getting off one.
  bool m_ferryAheadSaid = false;
  bool m_ferryAboardSaid = false;
  bool m_wasOnFerry = false;
  /// Toll lines (BI-113): one per toll stretch, re-armed after more than kTollFreeGapM of free road.
  bool m_tollSaid = false;
  /// When the current toll stretch came within range; the line is skipped if it can't go soon after.
  std::optional<double> m_tollWindowT;
  /// The driver said "avoid tolls" this trip: a toll anyway is the route's doing, no joke.
  bool m_avoidTollsSaid = false;
  /// The last route update's toll data, for TollReply().
  std::optional<double> m_tollAheadM;
  bool m_tollsKnown = false;
  bool m_onRoute = false;
  CameraAlerts m_cameraAlerts = CameraAlerts::Speeding;
  /// The speed camera coming up, followed until the car passes it.
  struct CameraWatch
  {
    std::string id;
    double aheadM = 0;
    std::optional<double> limitMps;
    /// Its one chance at a warning has come (said or skipped).
    bool warned = false;
    /// The warning was said (not skipped), and whether it caught the driver speeding.
    bool said = false;
    bool warnedSpeeding = false;
  };
  std::optional<CameraWatch> m_camera;
  /// The last line about any camera, and the last easy heads-up (not speeding).
  double m_lastCameraLineT = -1e18;
  double m_lastCameraHeadsUpT = -1e18;
  int m_cameraHeadsUps = 0;
  bool m_policeSaid = false;
  std::optional<double> m_lastLimitMps;
  std::string m_country;
  std::optional<double> m_lastBadSpeedingT;
  /// Since when well over the limit; m_lastBadSpeedingT counts only past the speeding sustain.
  std::optional<double> m_badSpeedingSince;
  TrafficLightLookup m_lightLookup;
  LevelCrossingLookup m_crossingLookup;
  Pronouns m_pronouns = Pronouns::They;
  Housing m_housing = Housing::Unknown;
  std::string m_driverName;

  VenueLookup m_venueLookup;
  SightLookup m_sightLookup;
  MusicLookup m_musicLookup;
  std::function<std::tm(double)> m_localTime;
  bool m_christmasSaid = false;
  bool m_roundaboutSaid = false;
  /// The maneuver the last "get into the left lane" was for; once each.
  std::optional<uint32_t> m_laneSaidFor;
  /// The arrival the destination's side ("on your right") was said for; once each (loop B119).
  std::optional<uint32_t> m_sideSaidFor;
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
  /// When a familiar commute last let an idle remark go unsaid (the next one waits a full gap from there).
  double m_idleSkippedT = -1e18;
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
  bool m_ridesAlone = false;
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
  RoadDistanceLookup m_roadDistanceLookup;
  double m_lastRoadCheckT = -1e18;
  std::optional<double> m_offRoadSince;
  // Noticing an off-road trip or a hike: how long the ground has looked like one.
  TrailLookup m_trailLookup;
  double m_lastTrailCheckT = -1e18;
  std::optional<double> m_dirtSince;
  std::optional<double> m_pavedSince;
  std::optional<double> m_trailSince;
  /// The off-road trip or the hike is the Navigator's own finding, not the route type or the driver.
  bool m_modeFound = false;
  /// A bike trip that's been on trails or dirt a while: mountain biking.
  bool m_mountainBike = false;
  /// "Are we actually walking there?": asked, and switching to driving at this time if no answer.
  bool m_canAskMode = false;
  bool m_modeAskOpen = false;
  /// "No" to the walking question: the drive the driver picks gets "check the mode now and then".
  bool m_modeWarnNextDrive = false;
  bool m_modeAskShow = false;
  std::optional<TravelMode> m_modeSwitch;
  /// The trip count before this trip, given back when the asked-about walk turns into a drive.
  int m_tripsBeforeThis = 0;
  std::optional<double> m_effortSince;
  bool m_inclineSaid = false;
  bool m_effortSaid = false;
  std::set<std::string> m_outdoorBitsSaid;
  double m_lastOffRoadT = -1e18;
  double m_lastExitLineT = -1e18;
  /// The exit maneuver the "skip this first exit" call was said for (loop B100).
  std::optional<uint32_t> m_exitSkipTurnId;
  bool m_onBridge = false;
  double m_lastBridgeYelpT = -1e18;
  double m_lastWaterYelpT = -1e18;
  int m_waterYelps = 0;
  std::set<std::string> m_tagsSaid;
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
  // Traffic-aware routing: when this jam started, how far the car has crept since, and the asks
  // made to the host (JamReporter) this trip.
  JamReporter m_jamReporter;
  double m_jamStartT = -1;
  double m_jamMovedM = 0;
  double m_jamLastFixT = -1;
  double m_lastJamAskT = -1e18;
  int m_jamAsks = 0;
  /// A JamReport is out and the host hasn't dropped it yet.
  bool m_jamReported = false;
  /// The last traffic rebuild kept the same roads: its first maneuver is the one already called.
  bool m_trafficSameRoute = false;
  /// How much delay the last "no way around this" line owned up to (0: none said since the last way around).
  double m_noWayAroundSaidS = 0;
  /// Ask the host about the jam once it's lasted a while, then again if it keeps going.
  void TrackJamForRouting(Fix const & fix);

  // License plate game.
  PlateGame m_plates;
  RegionLookup m_regionLookup;
  ParkingLookup m_parkingLookup;
  /// The last real fix, for judging whether the driver is parked when they talk.
  std::optional<Fix> m_lastFix;
  /// The last fix, trip or not: where the car is when a trip starts.
  std::optional<Fix> m_hereFix;
  /// Last speed the GPS actually reported, and when; carried through short dropouts (see OnFix).
  std::optional<double> m_knownSpeedMps;
  double m_knownSpeedT = 0;
  /// Where the last fix put the car, for reading its speed from progress when the GPS gives none (B147).
  struct Progress
  {
    double t = 0;
    LatLon pos;
    double accuracyM = -1;
    std::optional<uint32_t> turnId;
    uint64_t routeId = 0;
    double distToTurnM = 0;
    bool onRoute = false;
  };
  std::optional<Progress> m_progress;
  /// This fix's speed read from progress, when the GPS gave none (unset when it gave one).
  std::optional<double> m_progressMps;
  /// The route had no matched position yet when the trip would have started; when that was.
  std::optional<double> m_startHeldSince;
  /// Where the car was when the trip started, so a car that has clearly left counts as rolling.
  std::optional<LatLon> m_tripStartPos;
  BuildingLookup m_buildingLookup;
  /// This trip started indoors; the "made it out" line is still to come (key, empty if not).
  std::string m_indoorOutKey;
  std::function<void(PlateLog const &)> m_onPlateLog;
  SaidLog m_said;
  std::function<void(SaidLog const &)> m_onSaid;
  Achievements m_achievements;
  std::function<void(Achievements const &)> m_onAchievements;
  /// The maneuver we were heading for, and how many lefts in a row were done (NASCAR Driver).
  TurnKind m_turnKind = TurnKind::None;
  int m_leftsInARow = 0;
  int m_tripsOnRoute = 0;
  /// This trip is a learned routine driven kFamiliarAfterTrips+ times: optional banter is thinned
  /// until something new happens (a reroute, a jam, an unplanned stop, "talk more").
  bool m_familiarTrip = false;
  bool FamiliarQuiet() const;
  void NotFamiliarAnymore(char const * why);
  double m_tripStartT = 0;
  double m_lastRegionCheckT = -1e18;
  /// Game lines held back while a maneuver is close; directions come first.
  std::vector<Commentary> m_heldPlateLines;

  // 20 Questions.
  TwentyQuestions m_twenty;
  // Pop quizzes.
  PopQuiz m_quiz;
  bool m_quizOn = false;
  TalkLevel m_talk = TalkLevel::Full;
  /// TalkLevel::Less: the next remark is the one let through (they alternate).
  bool m_lessSayNext = true;
  /// Drops what TalkLevel rules out from lines about to be spoken; `punchlines` were already let through.
  void FilterForTalkLevel(std::vector<Commentary> & lines, std::set<std::string> const & punchlines);
  int m_quizzes = 0;
  double m_lastQuizT = -1e18;
  int m_quizUnclear = 0;
  /// The current quiz has been read a second time.
  bool m_quizReasked = false;
  /// The maneuver whose quiet stretch already had its roll of the dice for a quiz.
  std::optional<uint32_t> m_quizRolledFor;
  std::function<void(TwentyLog const &)> m_onTwentyLog;
  NearbyThingsLookup m_nearbyThings;
  /// Game lines held back while a maneuver is close; directions come first.
  std::vector<Commentary> m_heldTwentyLines;
  /// When directions cut into a game, until the GPS picks it back up (or the driver does).
  std::optional<double> m_twentyCutInT;

  // Quiet-stretch bits.
  std::string m_gpsName = "Echo";
  Persona m_persona = Persona::Critic;
  PhoneCall m_call;
  /// Everything said on this call, so the hold and resume lines don't echo the script.
  std::vector<std::string> m_callSaid;
  bool m_callTaken = false;
  int m_quietBits = 0;
  double m_lastQuietBitT = -1e18;
  /// The maneuver whose quiet stretch already had its roll of the dice.
  std::optional<uint32_t> m_quietRolledFor;
  /// The maneuver being driven toward and since when, as the quiet-stretch bits see it.
  std::optional<uint32_t> m_quietTurnId;
  double m_quietTurnSince = -1e18;
  /// When something last cut into a call (m_lastSpokeT is when anything was last handed out).
  double m_lastCutInT = -1e18;

  RoadBits m_road;
  /// Turn streaks, the trip's speeding story, the clean-driving bit, the arrival callback.
  RunningBits m_bits;
  /// Recent headings while moving, for Bending().
  std::deque<std::pair<double, double>> m_bendHeadings;
  double m_curveAheadM = -1;
  double m_lastBendT = -1e18;
  /// Last known air temperature, and the cold-morning bit's progress this trip (0 = not yet).
  std::optional<double> m_tempC;
  int m_coldStage = 0;
  double m_coldSaidT = 0;
  bool m_hotSaid = false;
  /// When the car was last at a grocery store (a stop on the way, or where the trip started).
  double m_groceryT = -1e18;
  bool m_groceriesSaid = false;
  bool m_clockChecked = false;
  /// When "gps.back" was last said; kept across trips (once per 10 minutes).
  double m_signalBackT = -1e18;
  /// The trip started at a bar late at night and its night-out line is still to come; whether the car has left the lot.
  bool m_fromNightlife = false;
  bool m_nightMoved = false;
  int m_bendJokes = 0;
  /// Straight nags on the current leg (by maneuver id), whether it's a long haul ("straight.long"
  /// was said), and the one town leg per trip that gets Brandon's straight chain.
  std::optional<uint32_t> m_straightLeg;
  int m_straightNags = 0;
  std::optional<uint32_t> m_longLeg;
  std::optional<uint32_t> m_chainLeg;
  bool m_chainUsed = false;
  /// When the last road-trip remark went out, so a quiet-stretch bit doesn't land on top of it.
  double m_lastRoadT = -1e18;

  std::optional<int> m_batteryPercent;
  std::optional<bool> m_charging;
};
}  // namespace backseat
