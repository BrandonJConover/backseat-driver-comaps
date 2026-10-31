// The condescending route preview spoken when a trip starts, and the greeting before it:
// "First you're going to turn left, then left again, then right... It's all so simple. You got it?"
// Port of engine/src/rundown.ts.
#pragma once

#include "backseat/engine.hpp"
#include "backseat/history.hpp"

#include <optional>
#include <string>
#include <vector>

namespace backseat
{
struct RouteStep
{
  /// Instruction text from routing, e.g. "Turn left onto Elm Street".
  std::string text;
  enum class Kind
  {
    Left,
    Right,
    UTurn,
    Other
  };
  /// The maneuver, if routing knows it; otherwise it's read from the text.
  std::optional<Kind> maneuver;
};

/// `tripsBefore` is how many trips the driver has started before this one (finished or not); it
/// defaults to the finished ones in `history`. 0 means the first trip ever.
std::string RouteRundown(std::vector<RouteStep> const & steps, std::vector<TripRecord> const & history, Rng const & rng,
                         std::optional<size_t> tripsBefore = std::nullopt);

/// First thing said when a trip starts, before the route rundown.
std::string TripGreeting(std::vector<TripRecord> const & history, Rng const & rng,
                         std::optional<size_t> tripsBefore = std::nullopt);
}  // namespace backseat
