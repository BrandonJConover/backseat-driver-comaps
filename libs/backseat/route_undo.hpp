// "Cancel the new route", "go back to the old route": remembers the last few destinations, so a route
// started by mistake (a misheard "Yeah" made into a place) can be taken back.
#pragma once

#include "backseat/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace backseat
{
class RouteUndo
{
public:
  /// Finishes this close together are the same destination (a reroute, not a new route).
  static double constexpr kSameM = 250;
  /// A route that ended longer ago than this is history, not something to go back to.
  static double constexpr kRememberMs = 20 * 60 * 1000;
  /// A route that lasted less than this was likely the mistake, so going back skips it when there's
  /// an older one.
  static double constexpr kMistakeMs = 3 * 60 * 1000;
  /// A route closed and built again to the same place within this is a rebuild of the same trip.
  static double constexpr kRebuildMs = 2 * 60 * 1000;

  struct Destination
  {
    LatLon at;
    std::string name;
    double startedMs = 0;
    double endedMs = 0;
  };

  /// A route was built to `finish`. A reroute to the same place changes nothing.
  void Built(double tMs, LatLon finish);
  /// What the current route's destination is called, once known ("Starbucks").
  void Named(std::string const & name);
  /// The route was closed (cancelled, or the trip ended).
  void Closed(double tMs);

  struct Plan
  {
    enum class Kind
    {
      /// No route now and none lately: nothing to undo.
      Nothing,
      /// Route back to `to`.
      Restore,
      /// No route before this one: just drop it.
      Cancel,
    };
    Kind kind = Kind::Nothing;
    Destination to;
  };
  /// What undoing does at `tMs`. A restore is taken off the list (it comes back with its Built()).
  Plan Undo(double tMs);

  bool HasRoute() const { return m_now.has_value(); }

private:
  std::optional<Destination> m_now;
  /// Oldest first.
  std::vector<Destination> m_before;
};
}  // namespace backseat
