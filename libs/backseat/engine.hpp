// The critique engine: feed it telemetry and navigation events, get back lines to speak.
// Pure logic with an injectable clock (sample timestamps) and RNG. A line-for-line port of
// engine/src/engine.ts; given the same seed and inputs both say exactly the same thing.
#pragma once

#include "backseat/lines.hpp"
#include "backseat/report.hpp"
#include "backseat/types.hpp"

#include <algorithm>
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
  /// The same, where the limit is 55 mph or more. Below 0: speedingTolerance. Not in the TS engine.
  int speedingToleranceFast = -1;
  /// "Barely" lines at most this often. 0: no limit (the TS engine's behavior).
  double speedingBarelyEveryMs = 0;
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
  double minRoastGapMs = 9000;
  /// After a direction, no roasts for this long (except urgent ones and turn verdicts): let it sink in.
  double navBreathMs = 5000;
  /// Re-nag interval while the driver stays over the limit.
  double speedingRenagMs = 40000;
  /// With this set, each "still speeding" nag waits twice as long as the one before, up to this
  /// long, so an hour at 4 over doesn't run through every line twice; and a dip under the limit
  /// doesn't count as a fresh offense. Both start over after speedingResetMs under the limit.
  /// 0 keeps the plain speedingRenagMs (the TS engine's behavior).
  double speedingRenagMaxMs = 0;
  double speedingResetMs = 30000;
  /// Distances for the "you are turning left in 500 feet / 50 feet" nags.
  double countdownFarM = 152;  // 500 ft
  double countdownNearM = 15;  // 50 ft
  /// "By the way, did I mention you should turn right." Said in place of the 50 ft nag.
  double countdownMentionM = 76;  // 250 ft
  /// Speed that counts as rolling along (heading checks, "how long until the turn"). A walker
  /// rolls at 1 m/s; a car at 3.
  double rollingMps = 3;
  /// A turn announced this long ago and still not reached is forgotten (heading checks, countdown).
  /// Three minutes in a car; on foot the next turn can be ten minutes away.
  double pendingTurnForgetMs = 180000;
  /// A stretch at least this long to the next maneuver gets "Please continue straight." nags,
  /// one every straightNagEveryMs of driving, three at most.
  double straightNagMinM = 1000;
  double straightNagEveryMs = 45000;
  /// A stretch this long (about 10 miles) opens with "I don't think I'm needed for this, but ok."
  /// and then chimes in now and then for as long as it lasts.
  double straightLongM = 16000;
  double straightLongNagEveryMs = 600000;
  /// While speeding, log a heat-map incident this often so long stretches show up as streaks.
  double speedingLogEveryMs = 5000;
  /// Frustration points lost per minute of good behavior.
  double frustrationDecayPerMin = 6;
  /// Keep the turn calls clear of everything else, from a rough idea of how long the phone is still
  /// talking (SetTalkingUntil): no roast or "did I mention" that would still be playing when the
  /// final "Now, ..." call is due, that call early enough to be over before the turn, and no remark
  /// tacked onto a direction for a turn that's seconds away. The Navigator turns it on; off by
  /// default so the TS engine's parity trace still holds.
  bool speechAware = false;
  Rng rng;
  LineSource * lineSource = nullptr;
  /// Lines said on earlier trips, so picks avoid them too. Not owned; null keeps it per trip.
  SaidLog * saidLog = nullptr;
};

/// "Turn left onto Elm" -> Left. "Keep right at the fork" -> Right. "Continue" -> Other.
Maneuver TurnDirectionOf(std::string const & text);

/// What kind of maneuver an instruction is, for the countdown's wording.
enum class TurnShape
{
  Turn,
  Exit,
  UTurn,
  Roundabout
};

/// "Make a U-turn" -> UTurn. "Take the roundabout" -> Roundabout. "Take the exit on the left" -> Exit.
TurnShape TurnShapeOf(std::string const & text);

/// "Turn left onto Elm Street, then right onto Oak Lane" -> "Turn left onto Elm Street".
std::string FirstManeuver(std::string const & text);

/// The spoken direction: "In 500 feet, turn left onto Elm Street.", "In a quarter mile, at the light,
/// turn right onto Oak Lane.", "Your destination, Sundance Lane, is 400 feet ahead."
std::string DirectionText(std::string const & text, double distanceM, Units units, Control control = Control::None);

/// Roughly how long a line takes to say: ~2.7 words a second at normal speed.
double SpeakingMs(std::string const & text, double speed = 1);

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

  /// The phone is busy saying what it was handed until about `t` (speechAware only).
  void SetTalkingUntil(double t) { m_talkingUntil = std::max(m_talkingUntil, t); }
  /// A line that takes `speakMs` to say would get in the way of the next turn: it would still be
  /// playing when the final call is due, or the final call is out and the turn isn't done yet.
  /// Always false unless speechAware.
  bool TurnNeedsQuiet(double t, double speakMs) const;

private:
  enum class SpeedBand
  {
    Barely,
    Clearly,
    Way
  };

  struct PendingTurn
  {
    TurnDirection dir;  // meaningless for a U-turn or roundabout
    TurnShape shape;
    double since;
    int stage;  // 0..4, see Countdown()
    std::optional<double> atTurnSince;
    bool mentioned = false;
    /// When the far ("in 500 feet") call was said.
    std::optional<double> farAt;
    /// Distance to the maneuver at the last instruction or progress update.
    double distM = 0;
    /// Distance when it was announced, and what was announced ("Turn left onto Elm Street").
    double legM = 0;
    std::string text;
    /// The 1 mile reminder was said.
    bool reminded = false;
    /// The heading already swung the right way at the maneuver.
    bool turned = false;
    /// "Your other left" was already said for this maneuver.
    bool wrongSaid = false;
  };

  /// "Please continue straight." ... "You should still be going straight." ... "I'm serious."
  struct StraightNag
  {
    double since;
    double lastM;
    int stage;  // lines said so far; a long haul keeps going after 3
    bool longHaul;
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
  void NagStraight(std::vector<Commentary> & out, double t, double distanceM);
  void SayTurnDone(std::vector<Commentary> & out, double t, PendingTurn const & p);
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
  double FarM() const;
  double FinalM() const;
  /// Where the final call goes with speechAware: FinalM(), or farther out when the call is long or
  /// the phone is still talking, so it's over before the car gets to the turn.
  double FinalDueM(double t, PendingTurn const & p) const;
  /// Milliseconds until the final call for `p` is due; huge when the car isn't rolling.
  double MsUntilFinal(double t, PendingTurn const & p) const;
  mutable double m_talkingUntil = 0;
  double m_lastRoastAt;
  /// Set by Make() for every navigation line.
  mutable double m_lastNavAt = -1e18;
  int m_instructionCount = 0;
  std::map<Category, double> m_lastByCategory;
  std::map<std::string, std::vector<std::string>> m_recentLines;

  std::optional<double> m_speedingSince;
  std::optional<SpeedBand> m_speedingBand;
  double m_lastSpeedingLineAt;
  double m_lastBarelyLineAt = -1e18;
  /// "Still speeding" nags since the driver was last under the limit for a while.
  int m_speedingRenags = 0;
  std::optional<double> m_underLimitSince;
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
  std::optional<StraightNag> m_straight;
  /// "Please stop turning right." already said for this maneuver, in place of the turn praise.
  bool m_turnDoneSaid = false;
  /// Lefts in a row (a right resets it), for "Just like a NASCAR driver", once per run of lefts.
  int m_leftStreak = 0;
  bool m_nascarSaid = false;
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
