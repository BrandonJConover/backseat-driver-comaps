// Matching a road the driver names ("avoid Main Street", "stay off highway 71") to the roads on the
// route, and finding that road in what was said.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace backseat
{
/// Whether `spoken` ("main street", "highway 71", "I-49") names the road with this map name and
/// number ("Main St", "US 71"). Abbreviations count ("St", "Ave", "Hwy"); "Elm Street" isn't
/// "Elm Tree Road", and a number has to be on the road's own sign.
bool RoadNameMatches(std::string const & spoken, std::string const & name, std::string const & ref);

/// In `words` (CommandWords form: " lower case words "), an ask to keep off a road: where it was
/// said and the road, or an empty road for the one the car is on ("avoid this road", "get me off
/// this road"). Nothing for road kinds ("avoid the highway", "avoid tolls") or anything else.
std::optional<std::pair<size_t, std::string>> AvoidRoadAsk(std::string const & words);
}  // namespace backseat
