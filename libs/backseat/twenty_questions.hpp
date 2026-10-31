// 20 Questions. On a quiet stretch the GPS thinks of something on the drive (the water tower
// up ahead, a deer, your turn signal, itself) and the driver asks yes or no questions out loud.
// It answers every one with contempt, mocks every guess, and gloats when the 20 run out.
// All offline: the things and what's true about them are hardcoded here, and free-form
// questions are matched to those facts by keywords. Things near the route come from the map
// as tags ("fuel", "water_tower"), so the answer can be something the driver actually passes.
#pragma once

#include "backseat/lines.hpp"

#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace backseat
{
/// The facts a question can ask about. Each thing answers yes, no or sometimes to each.
enum class Trait
{
  Alive,
  Animal,
  Plant,
  Person,
  Vehicle,
  Wheels,
  Building,
  Business,
  Food,
  Sign,
  Lights,
  Metal,
  Wood,
  Moves,
  Flies,
  Sky,
  Water,
  BiggerThanCar,
  Small,
  Tall,
  Round,
  OnRoad,
  Roadside,
  ManMade,
  Natural,
  Noise,
  Electric,
  InCar,
  Legs,
  Dangerous,
  Red,
  Orange,
  Yellow,
  Green,
  Blue,
  White,
  Black,
  Brown,
  Gray,
  Count
};

enum class Answer
{
  No,
  Yes,
  Sometimes
};

/// One thing the GPS can be thinking of.
struct TwentyThing
{
  /// As said in a sentence: "a water tower", "your turn signal", "me".
  char const * name;
  /// Other ways the driver might guess it, lowercase ("water tank").
  std::vector<char const *> aliases;
  /// Map tag that puts it near the route ("water_tower"); empty: it's on every drive.
  char const * tag;
  /// Where to say it is, for the opener: "outside", "in this car", "up in the sky".
  char const * where;
  std::vector<Trait> yes;
  std::vector<Trait> sometimes;
  bool dayOnly = false;
  bool nightOnly = false;
  /// Said after the reveal, when there's something to rub in. Empty for none.
  char const * quip = "";

  Answer Has(Trait t) const;
};

std::vector<TwentyThing> const & TwentyThings();
/// The thing the driver named right after a guess cue ("is it a deer", "it's the moon").
/// An index into TwentyThings().
std::optional<size_t> MatchTwentyThing(std::string const & spoken);

/// What the driver said, as the game reads it.
struct TwentyHeard
{
  enum class Kind
  {
    Question,
    Guess,
    /// A guess at something not in the list: "is it a giraffe".
    UnknownGuess,
    Hint,
    GiveUp,
    Quit,
    Unclear
  };
  Kind kind = Kind::Unclear;
  Trait trait = Trait::Count;
  /// "Is it smaller than a car": the answer to BiggerThanCar, flipped.
  bool flipped = false;
  size_t thing = 0;
  /// UnknownGuess: what they guessed, as said ("a giraffe").
  std::string guess;
};
TwentyHeard ParseTwentyHeard(std::string const & spoken);
/// "Let's play 20 questions", "twenty questions": the driver asking for the game by name.
bool AsksForTwentyQuestions(std::string const & spoken);

/// Games played and the answers lately used, kept between trips by the host.
struct TwentyLog
{
  int games = 0;
  int gpsWins = 0;
  int driverWins = 0;
  int grudges = 0;
  /// Names of the things picked lately, oldest first, so a thing doesn't come back for a while.
  std::deque<std::string> recent;

  std::string Serialize() const;
  static TwentyLog Parse(std::string const & text);
};

class TwentyQuestions
{
public:
  enum class State
  {
    Idle,
    Offered,
    Playing,
    Over
  };

  struct Reply
  {
    std::string key;
    std::string text;
    bool grudge = false;
    /// The game ended with this reply (won, lost, given up, quit, timed out).
    bool over = false;
  };

  static int constexpr kQuestions = 20;
  /// Recent answers kept out of the next pick.
  static size_t constexpr kRecentKeep = 25;
  /// Quiet for this long: the GPS says it heard nothing and asks again. After kMaxNudges of those, it
  /// calls the game off, and nobody wins.
  static double constexpr kNudgeAfterMs = 75000;
  static int constexpr kMaxNudges = 3;
  /// No game talk this close to a maneuver (500 ft); directions come first.
  static double constexpr kHoldNearManeuverM = 152;

  /// Picks lines; the navigator passes its SaidLog so lines don't repeat across trips.
  using Picker = std::function<std::string(std::string const & key, int tier)>;

  explicit TwentyQuestions(std::function<double()> rng = {});

  void SetLog(TwentyLog log) { m_log = std::move(log); }
  TwentyLog const & Log() const { return m_log; }
  void SetPicker(Picker picker) { m_picker = std::move(picker); }

  /// A new trip: forgets this trip's game.
  void NewTrip();
  bool OfferedThisTrip() const { return m_state != State::Idle; }
  State GetState() const { return m_state; }
  /// Offered and waiting, or being played: the other quiet-stretch bits keep out of the way.
  bool Active() const { return m_state == State::Offered || m_state == State::Playing; }
  int QuestionsLeft() const { return kQuestions - m_asked; }
  /// What it's thinking of, once picked.
  TwentyThing const * Thing() const;

  /// The GPS asks whether the driver wants to play, and thinks of something. `nearby`: map
  /// tags around the route; `night`: after sunset, so no sun or clouds.
  Reply Offer(std::set<std::string> const & nearby, bool night, double tMs, int tier);
  /// The driver asked for the game by name: no offer, straight to the first question.
  Reply Start(std::set<std::string> const & nearby, bool night, double tMs, int tier);
  /// The driver said something. Nothing when it isn't for the game.
  std::optional<Reply> OnDriverSaid(std::string const & spoken, double tMs, int tier);
  /// Time passing: a nudge when the driver goes quiet, then the GPS gives up on them.
  std::optional<Reply> Tick(double tMs, int tier);
  /// Arrived mid-game: the reveal.
  std::optional<Reply> WrapUp(int tier);
  /// "Anyway. Where were we." after directions cut in.
  Reply Resume(double tMs, int tier);

private:
  void Pick(std::set<std::string> const & nearby, bool night);
  Reply Answered(TwentyHeard const & heard, int tier);
  Reply Guessed(size_t guess, int tier);
  Reply End(std::string const & key, bool gpsWon, int tier, Vars vars = {});
  Reply Make(std::string const & key, int tier, Vars vars = {});
  std::string Line(std::string const & key, int tier);
  Vars Common() const;

  std::function<double()> m_rng;
  Picker m_picker;
  TwentyLog m_log;
  State m_state = State::Idle;
  size_t m_thing = 0;
  bool m_nearRoute = false;
  int m_asked = 0;
  int m_wrongGuesses = 0;
  std::set<int> m_askedTraits;
  double m_lastHeardT = 0;
  bool m_nudged = false;
  int m_nudges = 0;
};
}  // namespace backseat
