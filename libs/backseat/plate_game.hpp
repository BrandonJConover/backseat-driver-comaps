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
/// Every region the driver says they saw, in the order said and without repeats: "a Florida and a
/// Georgia" -> FL, GA. Nicknames count ("the Lone Star State", "Mass"); negated ones don't
/// ("a Florida but no Georgia" -> FL; "no Texas or Ohio plates" -> none).
std::vector<std::string> MatchPlateRegions(std::string const & spoken);
/// "Colorado" for "CO". Empty for an unknown code.
std::string PlateRegionName(std::string const & code);
/// Code for a region's name as the map data spells it ("Colorado", "British Columbia").
std::optional<std::string> PlateRegionCode(std::string const & name);
/// Whether two regions share a border (or are across a lake from each other).
bool PlateNeighbors(std::string const & a, std::string const & b);
/// Plates that almost never turn up on a mainland commute: Hawaii, Alaska, the territories.
bool RarePlate(std::string const & code);
/// "No Texas plates yet", "haven't seen Ohio", "not a Georgia plate": the region is named but not seen.
/// "No way, Hawaii!" and "No, Texas!" are still sightings.
bool NegatedSighting(std::string const & spoken);
/// "I'm done", "game over", "no more plates": the driver wants the plate game to stop.
bool QuitsPlateGame(std::string const & spoken);

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
    Declined,
    /// The driver called it off mid-game ("I'm done", "game over"). Nothing more until the next trip.
    Over
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
  /// No game talk this close to a maneuver (500 ft, or 12 s at the current speed if that is farther; the
  /// navigator scales it); directions come first.
  static double constexpr kHoldNearManeuverM = 152;
  /// Unmatched talk this soon after the last game exchange is probably a plate we misheard.
  static double constexpr kChatterWindowMs = 20000;

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
  /// The driver said something. Nothing when it isn't for the game (no game going on, a sighting
  /// that is negated, or chatter that doesn't sound like the game).
  /// `parked`: the car is parked or in a parking lot, where plates are too easy and don't count.
  /// `tMs`: when it was said. Chatter within kChatterWindowMs of the last game exchange gets "which
  /// state?"; later chatter is ignored. Without a time every unmatched utterance gets "which state?".
  std::optional<Reply> OnDriverSaid(std::string const & spoken, int tier, bool parked = false,
                                    std::optional<double> tMs = std::nullopt);
  /// The GPS asked in its own words ("why don't we play that game you just suggested?").
  void MarkOffered() { m_state = State::Offered; }
  void MarkPlayed(double tMs) { m_log.lastPlayedMs = tMs; }
  /// The tally at arrival, if the game was played this trip.
  std::optional<Reply> WrapUp(int tier);

private:
  Reply Plate(std::string const & code, int tier);
  /// Several plates in one breath: logs the others quietly and answers for the most exciting one.
  Reply Plates(std::vector<std::string> const & codes, int tier);
  /// How a plate would be judged, best first: 4 rare, 3 far, 2 neighbor, 1 already seen, 0 home state.
  int Rank(std::string const & code) const;
  /// Logs a new plate (not the home state, not a repeat). False when there was nothing to log.
  bool Record(std::string const & code);
  Reply Make(std::string const & key, int tier, Vars vars = {});
  std::string Pick(std::string const & key, int tier);
  Reply Tally(int tier);

  Rng m_rng;
  PlateLog m_log;
  std::string m_routeKey;
  std::optional<std::string> m_here;
  State m_state = State::Idle;
  /// New plates this trip, in the order spotted.
  std::vector<std::string> m_trip;
  /// The first repeat of a trip always gets Brandon's line ("We already have that one champ...").
  bool m_repeatedThisTrip = false;
  /// When the game last answered the driver; unset before the first exchange.
  std::optional<double> m_lastExchangeMs;
  std::map<std::string, std::string> m_lastPicked;
};
}  // namespace backseat
