// Finding the place the driver asked for: what the recognizer wrote is turned into something the
// map search can find, retried other ways when it finds nothing, and the results are put in the
// order the driver most likely meant (a name that matches, then a place they've driven to, then
// the nearest).
#pragma once

#include "backseat/history.hpp"
#include "backseat/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace backseat
{
/// The spoken place as a search query: lowercased, with the filler taken off ("um, a place called
/// Joe's near me please" -> "joe's"), letters spelled out joined ("c v s" -> "cvs"), and spoken house
/// numbers (two number words or more) and street ordinals in digits when it reads as an address ("one
/// twenty three main street" -> "123 main street", "forty second street" -> "42nd street"). Brand names
/// keep their words ("five guys"). Running it twice gives the same answer.
std::string SearchableQuery(std::string const & spoken);

/// Other ways to search for `query`, tried in order when the search finds nothing: without "on Main"
/// ("starbucks on main" -> "starbucks"), split words joined ("star bucks" -> "starbucks"), an
/// apostrophe put back or taken off ("mcdonalds" -> "mcdonald's"), a number word in digits ("five
/// guys" -> "5 guys"), and a low street ordinal spelled out ("5th avenue" -> "fifth avenue"). Never
/// `query` itself; at most four.
std::vector<std::string> QueryVariants(std::string const & query);

/// Whether two words are the same word, give or take a recognizer's spelling: same first sound, and at
/// most one letter off for a 5 to 7 letter word, two for a longer one ("ph"/"f", "ck"/"k" and doubled
/// letters don't count). "Chipotlay" is "Chipotle"; "cafe" isn't "care": four letters or fewer must match.
bool WordsClose(std::string const & a, std::string const & b);

/// One search result, for ranking.
struct PlaceCandidate
{
  std::string name;
  /// From the car, meters.
  double distanceM = 0;
  std::optional<LatLon> at;
};

/// The order to offer `candidates` in: names with every word asked for first, then names close enough
/// ("Pizza Hut" for "Joe's Pizza"), then the rest; within each, the ones the driver has driven to before (within ~250 m of a past place), then the nearest.
/// For "gas station", where no name says it, that's familiar first, then nearest. Returns indices.
std::vector<size_t> RankSearchResults(std::string const & query, std::vector<PlaceCandidate> const & candidates,
                                      std::vector<PastPlace> const & past);

/// A place driven to before whose name is what was asked for, for when the map search finds nothing:
/// the driver named it themselves, or it's an address the search can't read back. The most-driven
/// one wins.
std::optional<PastPlace> PastPlaceNamed(std::string const & query, std::vector<PastPlace> const & past);
}  // namespace backseat
