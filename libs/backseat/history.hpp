// Trip memory: the GPS remembers every trip so it can predict how you'll ruin the next one.
// Port of engine/src/history.ts. The host app persists TripRecords.
#pragma once

#include "backseat/engine.hpp"
#include "backseat/types.hpp"

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
}  // namespace backseat
