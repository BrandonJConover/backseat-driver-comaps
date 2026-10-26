// The license plate game. On a familiar commute with nothing to correct for a while, the GPS
// gets bored and asks the driver to call out out-of-state plates. It judges every one: your
// own state is a waste of its time, a neighbor barely counts, Hawaii or Alaska is a miracle,
// and a made-up state is a grudge. Plates are logged per commute and for life.
// All offline: the states, provinces and their neighbors are hardcoded here.
#pragma once

#include "backseat/engine.hpp"
#include "backseat/lines.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace backseat
{
/// Two-letter code ("CO", "BC", "MX") of the state, province or country named in `spoken`,
/// e.g. "I see a New Mexico plate" -> "NM". Names only, not codes: speech recognition
/// writes "Colorado", and "OK" or "IN" would match ordinary words.
std::optional<std::string> MatchPlateRegion(std::string const & spoken);
/// "Colorado" for "CO". Empty for an unknown code.
std::string PlateRegionName(std::string const & code);
/// Code for a region's name as the map data spells it ("Colorado", "British Columbia").
std::optional<std::string> PlateRegionCode(std::string const & name);
/// Whether two regions share a border (or are across a lake from each other).
bool PlateNeighbors(std::string const & a, std::string const & b);
/// Plates that almost never turn up on a mainland commute: Hawaii, Alaska, the territories.
bool RarePlate(std::string const & code);

enum class YesNo
{
  Yes,
  No,
  Unclear
};
YesNo ParseYesNo(std::string const & spoken);

/// What the driver has spotted, kept between trips by the host.
struct PlateLog
{
  std::set<std::string> lifetime;
  /// Per commute, by RouteKey.
  std::map<std::string, std::set<std::string>> byRoute;
  /// Declined games and made-up states. Read by the grudge ledger when it lands.
  int grudges = 0;
  /// When the game was last played (ms since the epoch); 0 if never.
  double lastPlayedMs = 0;

  std::string Serialize() const;
  static PlateLog Parse(std::string const & text);
};

class PlateGame
{
public:
  enum class State
  {
    Idle,
    Offered,
    Playing,
    Declined
  };

  struct Reply
  {
    std::string key;
    std::string text;
    bool grudge = false;
  };

  /// A commute is a route driven this many times before.
  static int constexpr kCommuteTrips = 3;
  /// Only when the next maneuver is at least this far off (2 miles).
  static double constexpr kQuietStretchM = 3219;
  /// Only when the GPS isn't already fed up (tier 0).
  static int constexpr kMaxFrustration = 33;
  /// No game talk this close to a maneuver (500 ft); directions come first.
  static double constexpr kHoldNearManeuverM = 152;

  explicit PlateGame(Rng rng = {});

  void SetLog(PlateLog log) { m_log = std::move(log); }
  PlateLog const & Log() const { return m_log; }

  /// A new trip on `routeKey`. Forgets this trip's plates and whether the game was offered.
  void NewTrip(std::string routeKey);
  /// The region being driven through, as a code ("CO"), when the host knows it.
  void SetHere(std::optional<std::string> code) { m_here = std::move(code); }

  static bool ShouldOffer(int tripsOnRoute, double distToManeuverM, int frustration);
  bool OfferedThisTrip() const { return m_state != State::Idle; }
  State GetState() const { return m_state; }

  /// The GPS's invitation. Marks the game offered for this trip.
  Reply Offer(int tier);
  /// The driver said something. Nothing when it isn't for the game (no game going on).
  /// `parked`: the car is parked or in a parking lot, where plates are too easy and don't count.
  std::optional<Reply> OnDriverSaid(std::string const & spoken, int tier, bool parked = false);
  /// The GPS asked in its own words ("why don't we play that game you just suggested?").
  void MarkOffered() { m_state = State::Offered; }
  void MarkPlayed(double tMs) { m_log.lastPlayedMs = tMs; }
  /// The tally at arrival, if the game was played this trip.
  std::optional<Reply> WrapUp(int tier);

private:
  Reply Plate(std::string const & code, int tier);
  Reply Make(std::string const & key, int tier, Vars vars = {});
  std::string Pick(std::string const & key, int tier);

  Rng m_rng;
  PlateLog m_log;
  std::string m_routeKey;
  std::optional<std::string> m_here;
  State m_state = State::Idle;
  /// New plates this trip, in the order spotted.
  std::vector<std::string> m_trip;
  /// The first repeat of a trip always gets Brandon's line ("We already have that one champ...").
  bool m_repeatedThisTrip = false;
  std::map<std::string, std::string> m_lastPicked;
};
}  // namespace backseat
