// Bits for the dead stretches of a drive, when there's nothing to say about directions for a
// while: the GPS takes a phone call (one side of it, then sometimes puts the caller on in a second
// voice), plays I spy, or shares a useless fact. The navigator decides when; this holds the call
// scripts, the call's own clock, and whether the sun is up.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace backseat
{
enum class Speaker
{
  Gps,
  /// The other end of the line, once the GPS puts them on. A second voice.
  Caller
};

struct CallBeat
{
  Speaker who = Speaker::Gps;
  /// Followed by a pause while the GPS "listens" to the caller nobody else can hear.
  bool listen = false;
  /// Part of a made-up "update" for the driver inside the script ("Continue straight."). Dropped
  /// once real directions have interrupted the call: the real update happened.
  bool update = false;
  /// With {gps} (the GPS's own name), {name} (the driver) and their pronouns to fill in.
  std::string text;
};

struct CallScript
{
  std::string id;
  /// Who calls, if they're one of the GPS voices ("Adam"); the script is skipped when that's
  /// the voice the driver picked. Empty for everyone else.
  std::string callerName;
  /// Kokoro voice for the caller's lines ("am_adam"); empty when the caller is never put on.
  std::string callerVoice;
  std::vector<CallBeat> beats;
};

/// Every call the GPS can get. The first is Brandon's and is the first call ever taken.
std::vector<CallScript> const & CallScripts();

/// One call in progress: which beat is next and when it may be said. Real directions put it on
/// hold; it picks up where it left off.
class PhoneCall
{
public:
  /// Beats with their text already filled in.
  void Start(double t, std::vector<CallBeat> beats, std::string callerVoice);
  void End();

  bool Active() const { return m_active; }
  bool Held() const { return m_held; }
  double HeldSince() const { return m_heldSince; }
  std::string const & CallerVoice() const { return m_callerVoice; }

  /// The next beat, if it's due at `t` and the call isn't on hold.
  std::optional<CallBeat> Due(double t) const;
  /// The beat from Due() was queued and will be done being said at `spokenUntil`; `listenMs` is
  /// how long the GPS "listens" after it if it's a listening beat.
  void Advance(double spokenUntil, double listenMs);
  /// The queued beats reached the speaker.
  void Delivered() { m_undelivered = 0; }
  /// Directions need the air. Beats queued but not yet delivered are taken back and said later.
  void Hold(double t);
  /// Back to the call; the next beat comes after `afterMs` (the "as I was saying").
  void Resume(double t, double afterMs);
  /// Anything of the call got said (so a hold before then needs no "just a second").
  bool Begun() const { return m_next > 0; }
  /// The last beat said announced an update for the driver ("I have to give them an update").
  bool AnnouncedUpdate() const { return m_next > 0 && m_beats[m_next - 1].update; }
  /// All beats are said.
  bool Done() const { return m_active && m_next >= m_beats.size(); }

private:
  bool m_active = false;
  bool m_held = false;
  double m_heldSince = 0;
  std::vector<CallBeat> m_beats;
  std::string m_callerVoice;
  size_t m_next = 0;
  size_t m_undelivered = 0;
  double m_nextAt = 0;
};

/// Height of the sun above the horizon in degrees at a place and time (ms since the epoch, UTC).
/// Below about -0.8 the sun has set.
double SunElevationDeg(double lat, double lon, double utcMs);
}  // namespace backseat
