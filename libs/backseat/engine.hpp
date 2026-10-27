// The critique engine: feed it telemetry and navigation events, get back lines to speak.
// Pure logic with an injectable clock (sample timestamps) and RNG. A line-for-line port of
// engine/src/engine.ts; given the same seed and inputs both say exactly the same thing.
#pragma once

#include "backseat/lines.hpp"
#include "backseat/report.hpp"
#include "backseat/types.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace backseat
{
using Rng = std::function<double()>;

/// Small seeded PRNG (mulberry32), bit-for-bit the same as seededRng() in the TS engine.
Rng SeededRng(uint32_t seed);

/// Optional source of fresh lines (e.g. written by a local LLM ahead of time). Returning
/// nothing falls back to the canned lines.
class LineSource
{
public:
  virtual ~LineSource() = default;
  virtual std::optional<std::string> Take(std::string const & key, int tier) = 0;
};

struct EngineConfig
{
  Units units = Units::Mph;
  /// Display units over the limit before it counts as speeding. 0 means 51 in a 50 is a crime.
  int speedingTolerance = 0;
  /// Deceleration (m/s²) that counts as a hard brake. ~0.35 g.
  double hardBrakeMps2 = 3.5;
  /// Acceleration (m/s²) that counts as a jackrabbit start.
  double hardAccelMps2 = 3.0;
  /// Display units under the limit, sustained for tooSlowAfterMs, to get called slow.
  double tooSlowMargin = 15;
  double tooSlowAfterMs = 20000;
  /// Stopped this long (and not arrived) before the GPS gets impatient.
  double longStopAfterMs = 45000;
  /// Minimum gap between any two roasts so the driver can hear the actual directions.
  double minRoastGapMs = 6000;
  /// Re-nag interval while the driver stays over the limit.
  double speedingRenagMs = 20000;
  /// Distances for the "you are turning left in 500 feet / 50 feet" nags.
  double countdownFarM = 152;  // 500 ft
  double countdownNearM = 15;  // 50 ft
  /// While speeding, log a heat-map incident this often so long stretches show up as streaks.
  double speedingLogEveryMs = 5000;
  /// Frustration points lost per minute of good behavior.
  double frustrationDecayPerMin = 6;
  Rng rng;
  LineSource * lineSource = nullptr;
  /// Lines said on earlier trips, so picks avoid them too. Not owned; null keeps it per trip.
  SaidLog * saidLog = nullptr;
};

/// "Turn left onto Elm" -> Left. "Keep right at the fork" -> Right. "Continue" -> Other.
Maneuver TurnDirectionOf(std::string const & text);

class CritiqueEngine
{
public:
  explicit CritiqueEngine(EngineConfig config = {}, double startedAt = 0);

  std::vector<Commentary> OnTelemetry(TelemetrySample const & s);
  std::vector<Commentary> OnNav(NavEvent const & e);

  int Frustration() const;
  TripStats const & Stats() const { return m_stats; }
  /// Everything the driver did wrong, in order, with positions. Persist this per trip.
  std::vector<Incident> const & Incidents() const { return m_incidents; }
  /// Grade + heat map. Valid mid-trip too.
  TripReport Report() const;
  EngineConfig const & Config() const { return m_config; }

private:
  enum class SpeedBand
  {
    Barely,
    Clearly,
    Way
  };

  struct PendingTurn
  {
    TurnDirection dir;
    double since;
    int stage;  // 0..4, see Countdown()
    std::optional<double> atTurnSince;
  };

  struct Heading
  {
    double t;
    double deg;
  };

  void CheckSpeeding(TelemetrySample const & s, TelemetrySample const * prev, std::vector<Commentary> & out);
  void CheckTooSlow(TelemetrySample const & s, std::vector<Commentary> & out);
  void CheckStopped(TelemetrySample const & s, std::vector<Commentary> & out);
  void CheckTurnDirection(TelemetrySample const & s, std::vector<Commentary> & out);
  void CheckNoDestination(TelemetrySample const & s, std::vector<Commentary> & out);
  void Countdown(std::vector<Commentary> & out, double t, double distanceM);
  void WrongTurn(std::vector<Commentary> & out, double t, TurnDirection expected, TurnDirection turned,
                 std::optional<LatLon> at = {});
  std::optional<std::string> Grievance() const;

  bool Roast(std::vector<Commentary> & out, double t, Category category, std::string const & key,
             std::map<std::string, std::string> const & vars, bool urgent = false);
  /// Records an incident at `at`, or at the last GPS position when `at` is empty.
  void Log(Incident incident, std::optional<LatLon> const & at = {});
  void Bump(Category category);
  void Decay(double dtMs);
  int Tier() const;
  std::string Pick(std::string const & key, int tier);
  Commentary Make(double t, Category category, std::string const & text, Priority priority) const;

  EngineConfig m_config;
  double m_frustration = 0;
  std::optional<TelemetrySample> m_lastSample;
  double m_lastRoastAt;
  std::map<Category, double> m_lastByCategory;
  std::map<std::string, std::vector<std::string>> m_recentLines;

  std::optional<double> m_speedingSince;
  std::optional<SpeedBand> m_speedingBand;
  double m_lastSpeedingLineAt;
  std::optional<double> m_slowSince;
  std::optional<double> m_stoppedSince;
  int m_stopCount = 0;
  bool m_braking = false;
  bool m_accelerating = false;
  double m_lastSpeedingLogAt;
  std::optional<std::string> m_lastInstructionText;
  std::optional<double> m_lastInstructionDistanceM;
  std::optional<LatLon> m_position;
  std::optional<std::string> m_trafficAlternate;  // set while stuck in heavy traffic
  /// The left/right turn we're waiting for, so a turn the other way can be caught.
  std::optional<PendingTurn> m_pendingTurn;
  std::vector<Heading> m_headings;
  double m_lastWrongTurnAt;
  bool m_hasDestination = false;
  std::optional<double> m_movingWithoutDestinationSince;
  double m_lastNoDestinationAt;
  std::optional<std::string> m_stopRequestName;
  /// Hazards already announced, so a feed refresh doesn't repeat them.
  std::set<std::string> m_hazardsAnnounced;
  /// A stop the driver agreed to. Did they mean it?
  std::optional<std::string> m_promised;
  int m_stopRequestsAsked = 0;
  /// Set when the driver turned down the GPS's stop. Becomes a payback grievance.
  std::optional<std::string> m_snubbedAt;
  /// Lowest battery warning already given (1 = 20%, 2 = 10%, 3 = 5%).
  int m_batteryWarned = 0;
  std::vector<Incident> m_incidents;
  bool m_arrived = false;
  TripStats m_stats;
};
}  // namespace backseat
