// Spoken destination commands: "take me to work", "please navigate to Starbucks". The GPS
// takes it personally when there's no please or good morning first.
#pragma once

#include "backseat/history.hpp"
#include "backseat/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace backseat
{
struct DestinationCommand
{
  /// Where to, as said: "work", "home", "starbucks on main".
  std::string target;
  /// Said with a please, a hello or a good morning.
  bool polite = false;
};

/// The command in `spoken`, if it is one ("take me to work", "navigate to the airport").
std::optional<DestinationCommand> ParseDestinationCommand(std::string const & spoken);

/// "home" or "work" when that's what the driver asked for, as the target names them.
enum class KnownPlace
{
  Home,
  Work
};
std::optional<KnownPlace> KnownPlaceOf(std::string const & target);

/// Where home or work probably is, from past trips. Home is where trips start and end most;
/// work is the most common other destination. Needs at least two trips there to be sure.
std::optional<LatLon> GuessPlace(std::vector<TripRecord> const & history, KnownPlace place);
}  // namespace backseat
