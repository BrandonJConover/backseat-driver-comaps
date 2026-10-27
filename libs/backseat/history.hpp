// Trip memory: the GPS remembers every trip so it can predict how you'll ruin the next one.
// Port of engine/src/history.ts. The host app persists TripRecords.
#pragma once

#include "backseat/engine.hpp"
#include "backseat/types.hpp"

#include <array>
#include <ctime>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace backseat
{
struct TripRecord
{
  std::string routeKey;
  std::string destination;
  double endedAt = 0;
  /// What routing estimated when the trip started.
  double expectedMinutes = 0;
  double actualMinutes = 0;
  double speedingShare = 0;  // 0..1 of the trip spent over the limit
  int missedTurns = 0;
  int hardBrakes = 0;
  std::string grade;
  /// Exactly where the trip was headed, when known (the route key is rounded to ~250 m).
  std::optional<LatLon> to;
};

struct TripPlan
{
  LatLon origin;
  LatLon destination;
  /// What the driver called it, e.g. "Mom's house".
  std::string destinationName;
  double expectedMinutes = 0;
};

enum class Habit
{
  LeadFoot,
  Slowpoke,
  OnTime,
  Unknown
};

struct TripPrediction
{
  std::string text;
  double predictedMinutes = 0;
  Habit habit = Habit::Unknown;
  int timesOnRoute = 0;
};

/// Same origin and destination within ~250 m count as the same route.
std::string RouteKey(LatLon origin, LatLon destination);

/// Turn a finished trip into a record to store.
TripRecord RecordTrip(CritiqueEngine const & engine, TripPlan const & plan);

/// The opening jab when a trip starts, from the driver's habits (actual / estimated time),
/// weighted toward this route.
TripPrediction PredictTrip(std::vector<TripRecord> const & history, TripPlan const & plan, Rng const & rng);
/// A trip the driver makes at about the same time on certain days: the commute.
struct Routine
{
  /// RouteKey() of the trip.
  std::string routeKey;
  /// What the destination was called last time.
  std::string destination;
  std::optional<LatLon> to;
  /// Usual local start time, minutes after midnight.
  int startMinute = 0;
  /// Trips per weekday (0 = Sunday), and the days it's usual on.
  std::array<int, 7> tripsByDay{};
  std::array<bool, 7> usualOn{};
  int trips = 0;
  /// How long it usually takes, in whole minutes (what routing expected, else what it took).
  int usualMinutes = 0;
};

/// The driver's routines, learned from past trips: the same route started within an hour or so of
/// the same time, at least 4 times, usual on the days it happened at least twice.
std::vector<Routine> LearnRoutines(std::vector<TripRecord> const & history,
                                   std::function<std::tm(double)> const & localTime);

struct RoutineBreak
{
  enum class Kind
  {
    /// Usually going somewhere else at this hour.
    Elsewhere,
    /// The usual trip, on a day it isn't usual (work on a Saturday).
    OffDay,
    /// The usual trip, much later than usual.
    Late
  };
  Kind kind;
  Routine routine;
  int minutesLate = 0;
};

/// The routine usually started from `here` around local time `now` (within 45 minutes), if any.
std::optional<Routine> UsualTrip(std::vector<Routine> const & routines, LatLon here, std::tm const & now);

/// A place the driver has driven to, for the list of previous routes.
struct PastPlace
{
  /// What it was called last time.
  std::string destination;
  LatLon to;
  int trips = 0;
  /// When the last trip there ended, ms since the epoch.
  double lastEndedAt = 0;
  int usualMinutes = 0;
};

/// Places driven to before, most trips first (most recent breaks ties), at most `max`. Trips to
/// within ~250 m of each other count as the same place.
std::vector<PastPlace> PastPlaces(std::vector<TripRecord> const & history, size_t max);

/// Whether starting `routeKey` at local time `now` breaks one of the routines.
std::optional<RoutineBreak> CheckRoutine(std::vector<Routine> const & routines, std::string const & routeKey,
                                         std::tm const & now);
}  // namespace backseat
