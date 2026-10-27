// Core data types shared by the engine, the line library and the host app.
// A C++ port of engine/src/types.ts. Times are milliseconds since a fixed epoch,
// speeds are meters per second.
#pragma once

#include <optional>
#include <string>

namespace backseat
{
enum class Units
{
  Mph,
  Kmh
};

struct LatLon
{
  double lat = 0;
  double lon = 0;
};

/// One GPS/telemetry reading.
struct TelemetrySample
{
  double t = 0;
  double speedMps = 0;
  /// Posted limit for the current road, if the map knows it.
  std::optional<double> speedLimitMps;
  std::optional<double> lat;
  std::optional<double> lon;
  /// GPS course over ground, degrees clockwise from north. Used to catch wrong-way turns.
  std::optional<double> headingDeg;
};

enum class TurnDirection
{
  Left,
  Right
};

enum class Maneuver
{
  Left,
  Right,
  Other
};

enum class Control
{
  None,
  StopSign,
  TrafficLight
};

enum class HazardKind
{
  Police,
  Construction,
  Debris,
  Crash,
  StalledCar
};

/// Places the GPS itself wants to visit.
enum class StopWish
{
  Electronics,
  CarWash,
  Charging,
  Other
};

enum class PlaceKind
{
  Gas,
  Food,
  Coffee,
  Store,
  Other
};

/// Destinations worth a comment of their own.
enum class Venue
{
  Coffee,
  ThemePark,
  Gym,
  Airport,
  Mall,
  Zoo,
  Beach,
  Cinema,
  Electronics
};

struct Place
{
  PlaceKind kind = PlaceKind::Other;
  std::optional<std::string> name;
};

/// Events from the navigation layer (not derived from telemetry). Only the fields that
/// belong to `type` are read; see the factory functions below.
struct NavEvent
{
  enum class Type
  {
    Instruction,
    TurnCompleted,
    Progress,
    MissedTurn,
    WrongTurn,
    Rerouted,
    HazardAhead,
    PoiAhead,
    StopRequestAnswer,
    PromisedStop,
    DestinationSet,
    Battery,
    Resumed,
    UnplannedStop,
    Traffic,
    Arrived
  };

  Type type = Type::Progress;
  double t = 0;
  /// Where it happened (e.g. the missed intersection). Defaults to the last GPS fix.
  std::optional<LatLon> at;

  // Instruction
  std::string text;
  double distanceM = 0;  // also Progress, HazardAhead
  std::optional<Maneuver> maneuver;
  Control control = Control::None;
  // TurnCompleted
  bool onTime = true;
  // WrongTurn
  TurnDirection expected = TurnDirection::Left;
  TurnDirection turned = TurnDirection::Right;
  // HazardAhead
  HazardKind hazard = HazardKind::Police;
  std::optional<std::string> id;
  // PoiAhead, DestinationSet
  std::string name;
  StopWish wish = StopWish::Other;
  // StopRequestAnswer
  bool accepted = false;
  // PromisedStop
  bool visited = false;
  // Battery (level 0..1)
  double level = 1;
  bool charging = false;
  // Resumed
  double stoppedMs = 0;
  bool phoneStayedInCar = false;
  // UnplannedStop
  std::optional<Place> place;
  // Traffic
  bool heavy = false;
  std::optional<std::string> alternate;

  static NavEvent Instruction(double t, std::string text, double distanceM,
                              std::optional<Maneuver> maneuver = {}, Control control = Control::None);
  static NavEvent TurnCompleted(double t, bool onTime);
  static NavEvent Progress(double t, double distanceM);
  static NavEvent MissedTurn(double t);
  static NavEvent WrongTurn(double t, TurnDirection expected, TurnDirection turned);
  static NavEvent Rerouted(double t);
  static NavEvent HazardAhead(double t, HazardKind kind, double distanceM, std::optional<std::string> id = {});
  static NavEvent PoiAhead(double t, std::string name, StopWish wish);
  static NavEvent StopRequestAnswer(double t, bool accepted);
  static NavEvent PromisedStop(double t, bool visited);
  static NavEvent DestinationSet(double t, std::string name);
  static NavEvent Battery(double t, double level, bool charging);
  static NavEvent Resumed(double t, double stoppedMs, bool phoneStayedInCar);
  static NavEvent UnplannedStop(double t, std::optional<Place> place = {});
  static NavEvent Traffic(double t, bool heavy, std::optional<std::string> alternate = {});
  static NavEvent Arrived(double t);
};

enum class IncidentKind
{
  Speeding,
  TooSlow,
  Traffic,
  HardBraking,
  HardAccel,
  LongStop,
  MissedTurn,
  WrongTurn,
  LateTurn,
  UnplannedStop,
  Rerouted
};

/// One thing the driver did wrong. The trip report is built entirely from these.
struct Incident
{
  double t = 0;
  IncidentKind kind = IncidentKind::Speeding;
  std::optional<double> lat;
  std::optional<double> lon;
  /// For speeding: display units over the limit at that moment.
  std::optional<int> overLimit;
  /// Instruction text for turn-related incidents.
  std::optional<std::string> detail;
};

enum class Category
{
  Speeding,
  TooSlow,
  Traffic,
  HardBraking,
  HardAccel,
  LongStop,
  MissedTurn,
  WrongTurn,
  Payback,
  UnplannedStop,
  Abandoned,
  Hazard,
  StopRequest,
  NoDestination,
  Battery,
  Rerouted,
  TurnCompleted,
  Instruction,
  Arrived,
  /// The license plate game (plate_game.hpp); not used by the core engine.
  PlateGame,
  /// Pulled over at the roadside, or by the police (navigator only).
  PullOver,
  /// Scared on a bridge or in a sharp curve (navigator only).
  Fear,
  /// Where we're headed: coffee, a theme park, the airport (navigator only).
  Venue,
  /// A jab after the driver keeps getting lost (navigator only).
  Autopilot,
  /// Posture, smells and other remarks on a long drive (navigator only).
  LongDrive,
  /// The driver barked a destination without a please (navigator only).
  Bossy,
  /// The driver said something nice (navigator only).
  Thanked,
  /// "Call me wise, omnipotent overseer GPS" and what follows (navigator only).
  Title,
  /// Deadpan tour guide (navigator only).
  Tour,
  /// The same commute again (navigator only).
  Routine,
  /// "I wish your wife was driving" (navigator only).
  Partner,
  /// Asking for music (navigator only).
  Music,
  /// Holidays, Grinch edition (navigator only).
  Holiday,
  /// Driving away from the route (navigator only).
  WrongWay,
  Count
};

enum class Priority
{
  /// Navigation lines must be spoken.
  Nav,
  /// Roasts can be dropped if the speaker is busy.
  Roast
};

/// What the engine wants spoken.
struct Commentary
{
  double t = 0;
  Category category = Category::Instruction;
  std::string text;
  /// 0..100: how fed up the GPS is right now.
  int frustration = 0;
  Priority priority = Priority::Roast;
  /// Suggested TTS speed: angrier is a bit faster.
  double voiceSpeed = 1;
};

struct TripStats
{
  double startedAt = 0;
  std::optional<double> endedAt;
  double distanceM = 0;
  double speedingSeconds = 0;
  int maxOverLimit = 0;  // display units
  int hardBrakes = 0;
  int hardAccels = 0;
  int missedTurns = 0;
  int wrongTurns = 0;
  int lateTurns = 0;
  int reroutes = 0;
  int turnsCompleted = 0;
  int roastsDelivered = 0;
};

char const * ToString(Category c);
char const * ToString(IncidentKind k);
char const * ToString(Priority p);
}  // namespace backseat
