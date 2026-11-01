// Running bits: jokes that build over a trip instead of repeating one premise.
//
// The simulated commutes showed a driver who doesn't mess up hears two jokes all trip: "you
// actually turned right" (after nearly every turn) and "still 2 over". So:
//   - Turn praise is rarer and has angles: a streak ("three in a row"), a callback to the turn
//     blown earlier ("that's what 8th Street was supposed to look like"), or the GPS taking credit.
//   - Repeat speeding nags tell one story per trip, a beat at a time (the ticket it's writing,
//     the text to your mother...), and stop when the story's over. Stories rotate across trips.
//   - A driver who makes no mistakes gets the GPS growing suspicious instead of silence.
//   - The arrival grade calls back the trip: the story's ending, the turn that cost a letter, or
//     disbelief at a clean trip.
// Pure logic; Navigator feeds it the engine's stats and incidents and says what it picks.
#pragma once

#include "backseat/lines.hpp"
#include "backseat/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace backseat
{
/// A line to say instead: its key and the extra placeholders it needs.
struct BitLine
{
  std::string key;
  Vars vars;
  /// For an ordered story beat: the beat's index in the key's pool (else the line is picked).
  std::optional<size_t> beat;
};

class RunningBits
{
public:
  /// Speeding stories, one picked per trip (key "speeding.story.<id>", "speeding.end.<id>").
  static std::vector<std::string> const & StoryIds();
  /// Turn streaks that get a line, in order. A streak carries over from trip to trip, so a driver
  /// who never misses hears "three in a row" once, not every morning.
  static std::vector<int> const & StreakMilestones();

  /// `cleanBitOn`: whether this trip may do the "you're driving too well" bit. `streak`: correct
  /// turns in a row so far, carried over from the last trip.
  void NewTrip(bool cleanBitOn, int streak = 0);

  /// An on-time turn just finished (the engine wants to say turnCompleted.onTime). What to say
  /// instead, or nothing. `r` is a random number in [0, 1).
  std::optional<BitLine> OnTimeTurn(TripStats const & stats, std::vector<Incident> const & incidents, double r);

  /// The engine wants to bring up the speeding again (a "still speeding" nag, or a new offense
  /// after the trip's first). The next beat of this trip's story, or nothing: too soon after the
  /// last beat, or the story's told. The caller picks the story (SetStory) the first time, so the
  /// rotation across trips only moves on trips with speeding.
  std::optional<BitLine> StillSpeeding(double t);
  /// The beat StillSpeeding just gave couldn't be said (a turn is coming): it's next again.
  void UndoBeat()
  {
    if (m_storyBeat > 0)
      --m_storyBeat;
    m_lastBeatT = -1e18;
  }
  /// The trip's first speeding remark went out; later ones are the story's.
  void SpeedingSaid() { m_speedingSaid = true; }
  bool SpeedingWasSaid() const { return m_speedingSaid; }
  bool StoryPicked() const { return !m_story.empty(); }
  void SetStory(std::string id) { m_story = std::move(id); }

  /// Tacked onto the arrival line, if anything.
  std::optional<BitLine> Arrival(std::string const & gradeLetter, std::vector<Incident> const & incidents, double r) const;

  /// The "too well" bit: the next beat if it's due at `t`, given when the trip started.
  std::optional<BitLine> CleanBeatDue(double t, double tripStartT, std::vector<Incident> const & incidents);

  /// Catches up on turns done (or blown) since the last look, verdict or not.
  void SyncTurns(TripStats const & stats, std::vector<Incident> const & incidents);

  int StoryBeats() const { return m_storyBeat; }
  int Streak() const { return m_streak; }

private:

  // Turns.
  int m_onTimeSeen = 0;
  int m_badSeen = 0;
  int m_streak = 0;
  size_t m_milestonesSaid = 0;
  /// The street of a turn blown this trip, until a clean turn makes up for it.
  std::optional<std::string> m_redeem;
  int m_plainSkipped = 0;
  int m_plainSaid = 0;
  bool m_firstVerdict = true;

  // Speeding story.
  std::string m_story;
  int m_storyBeat = 0;
  double m_lastBeatT = -1e18;
  bool m_speedingSaid = false;

  // Clean driving.
  bool m_cleanOn = false;
  int m_cleanBeat = 0;
};

/// "Turn right onto 8th Street, then left onto Elm." -> "8th Street". Empty when there's no street.
std::string StreetOf(std::string const & instruction);
/// One step up: "B" -> "B+", "C+" -> "B-"; "A" stays "A".
std::string BetterGrade(std::string const & letter);
}  // namespace backseat
