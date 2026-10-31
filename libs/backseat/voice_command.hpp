// Spoken destination commands: "take me to work", "please navigate to Starbucks". The GPS
// takes it personally when there's no please or good morning first.
#pragma once

#include "backseat/history.hpp"
#include "backseat/types.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace backseat
{
struct DestinationCommand
{
  /// Where to, as said: "work", "home", "starbucks on main".
  std::string target;
  /// Said with a please, a hello or a good morning.
  bool polite = false;
};

/// The command in `spoken`, if it is one ("take me to work", "navigate to the airport").
std::optional<DestinationCommand> ParseDestinationCommand(std::string const & spoken);

/// "home" or "work" when that's what the driver asked for, as the target names them.
enum class KnownPlace
{
  Home,
  Work
};
std::optional<KnownPlace> KnownPlaceOf(std::string const & target);

/// Said with a please, a hello or a good morning.
bool SaidPolitely(std::string const & spoken);

/// Everything the app can do by voice. The platform's on-device model gets these as tools
/// (MCP-style: a name, what it does, its argument); phones without one use ParseAppCommands().
enum class AppAction
{
  NavigateTo,
  CancelRoute,
  Mute,
  Unmute,
  RepeatInstruction,
  TimeLeft,
  ReportPolice,
  PlayPlateGame,
  AvoidTolls,
  AllowTolls,
  AvoidHighways,
  AllowHighways,
  ChangeVoice,
  ListVoices,
  ZoomIn,
  ZoomOut,
  Recenter,
  RouteOverview,
  OpenSettings,
  StopListening,
  ListCommands,
  FindAlongRoute,
  SkipStop,
  NextStop,
  SpeedLimit,
  CurrentSpeed,
  CurrentRoad,
  NextTurn,
  ArrivalTime,
  DifferentRoute,
  TakeMeBack,
  BeNicer,
  BeMeaner,
  GradeSoFar,
  RoastMe,
  TellJoke,
  BlameGps,
  WhatDoYouKnow,
  ForgetThat,
  SetTravelMode,
  Count
};

struct AppTool
{
  AppAction action;
  /// Tool name, snake_case: "cancel_route".
  char const * name;
  /// What it does, for the model.
  char const * description;
  /// What the argument is, or null when it takes none.
  char const * argument;
};

/// One per AppAction, in enum order.
std::vector<AppTool> const & AppTools();
AppTool const & ToolFor(AppAction action);
std::optional<AppAction> AppActionNamed(std::string const & name);

struct AppCommand
{
  AppAction action;
  /// NavigateTo: the place. ChangeVoice: the voice, or empty to just open the voice list.
  /// SetTravelMode: "walk", "bike", "drive" or "offroad".
  /// FindAlongRoute: what to find. ForgetThat: what to forget, or empty for the last thing read out.
  std::string argument;
};

/// The commands in what the driver said, in the order said: "avoid tolls and take me home"
/// gives AvoidTolls, NavigateTo("home"). Police and the plate game are left to the navigator,
/// which already listens for them.
std::vector<AppCommand> ParseAppCommands(std::string const & spoken);

/// Whether `heard` is the GPS's own `line` coming back through the mic: most of its meaningful words
/// are in the line, or three words in a row are. One word isn't enough to tell ("work", "yes").
bool IsEchoOf(std::string const & heard, std::string const & line);

/// What the GPS said lately, to catch its own voice in the mic after it stops talking.
class SpokenLines
{
public:
  /// How long a line can come back: speech still queued, room reverb, a slow recognizer.
  static double constexpr kWindowMs = 20000;

  void Add(double tMs, std::string line);
  /// The recent line `heard` is an echo of, if any.
  std::optional<std::string> EchoOf(double tMs, std::string const & heard);

private:
  struct Spoken
  {
    double tMs;
    std::string line;
  };
  std::vector<Spoken> m_lines;
};

/// Whether `name` is plausibly what was asked for: at least half the meaningful words of `asked` are
/// in it. "Peace Lutheran" isn't "Latheron Place", however much the search thinks so.
bool PlaceMatches(std::string const & asked, std::string const & name);

/// Not an answer worth saving: "so let's see", "um", or the question said back ("favorite color").
bool IsJunkAnswer(std::string const & topic, std::string const & fact);
/// The saved topic meant by "my favorite color" or "the ice cream one": named by topic, else by its fact.
std::optional<std::string> MemoryTopicNamed(DriverMemory const & memory, std::string const & named);

/// "Yeah", "um", "okay": nothing to act on, unless something asked for an answer.
bool IsFiller(std::string const & spoken);

/// Whether the words support the tool the model picked. It made "Yeah" into navigate_to Melissa Drive
/// and "Are you metal?" into list_voices. A place needs a go-ahead word ("take me", "navigate") or to
/// have been said; everything else needs a word about it.
bool WordsSupportTool(AppAction action, std::string const & argument, std::string const & spoken);

/// Stops a command from running twice: the recognizer and the model both repeat themselves, and each
/// extra navigate_to restarts the search or the route.
class CommandRepeats
{
public:
  static double constexpr kRepeatMs = 15000;
  /// A route that long in the making has failed, and asking again is fair.
  static double constexpr kBuildingMaxMs = 60000;

  /// Whether `command` repeats a recent one. If not, it's remembered as the latest.
  bool IsRepeat(AppCommand const & command, double tMs);
  /// The route started: it's no longer being built.
  void RouteStarted() { m_buildingSince.reset(); }

private:
  struct Ran
  {
    double tMs;
    std::string key;
  };
  std::vector<Ran> m_ran;
  std::optional<double> m_buildingSince;
  std::optional<double> m_lastNavigateAt;
  std::string m_buildingTo;
};

/// "Yes", "yeah", "yep", "you got it"...
bool SaidYes(std::string const & spoken);
/// "No", "nope", "nah", "not really"...
bool SaidNo(std::string const & spoken);

/// Whose business the mileage log tracks, said out loud. "Me", "mine", "I do" -> "" (the driver's
/// own). "My wife", "my wife's" -> "wife" (likewise husband, partner, girlfriend, boyfriend). "Sarah",
/// "it's Sarah's" -> "Sarah". Empty optional if unclear.
std::optional<std::string> ParseBusinessOwner(std::string const & spoken);
/// How to say it: "" -> "your", "wife" -> "your wife's", "Sarah" -> "Sarah's".
std::string BusinessOwnerPossessive(std::string const & owner);

/// First-run setup answers, said out loud.
/// "My name is Brandon", "call me B", "it's Brandon", "Brandon" -> "Brandon". Empty if unclear.
std::string ParseNameAnswer(std::string const & spoken);
/// The answer to a "for your file" question, cleaned up: "Um, I'd say vanilla ice cream" -> "vanilla"
/// when `drop` is {"ice cream"}. Lowercase, at most four words; empty if nothing's left.
std::string ParseFavorite(std::string const & spoken, std::vector<std::string> const & drop = {});
/// "Left side, I guess" -> "left". Empty if it wasn't left, right or middle.
std::string ParseBedSide(std::string const & spoken);
/// "None of your business", "pass", "I don't know": the driver won't say.
bool DeclinedToAnswer(std::string const & spoken);
/// A 1 to 10 rating, said as a digit or a word ("a seven", "10", "solid eight"). 0 if there's none.
int ParseRating(std::string const & spoken);
/// The driver's name, heard slightly wrong on purpose: "Brandon" -> "Brendon". `variant` picks
/// among the ways to get it wrong. Never returns the name unchanged.
std::string MisheardName(std::string const & name, unsigned variant = 0);
/// A guess at "he" or "she" from a common first name ("Brandon" -> "he"). Empty when it can't tell.
std::optional<std::string> PronounsForName(std::string const & name);
/// The driver setting the record straight ("I'm a woman", "my pronouns are they/them"): the
/// pronouns they mean. Empty when they didn't.
std::optional<std::string> PronounCorrection(std::string const & spoken);
/// "he", "she" or "they" ("I'm a guy" -> "he").
std::optional<std::string> ParsePronounAnswer(std::string const & spoken);

struct Passenger
{
  /// "wife", "husband", "girlfriend"... Empty: it's just the driver.
  std::string word;
  /// "he", "she" or "they".
  std::string pronouns = "they";
};
/// "Usually my wife" -> wife/she. "No, just me" -> nobody.
std::optional<Passenger> ParsePassengerAnswer(std::string const & spoken);
/// A partner mentioned in passing ("my wife hates this song"), for learning about the driver.
std::optional<Passenger> MentionedPartner(std::string const & spoken);

/// Which search result the driver meant: "the first one", "number two", "the closest", "the one on
/// Main" (a word only that result's name has). `names` are the results read out, closest first.
std::optional<size_t> ParseChoice(std::string const & spoken, std::vector<std::string> const & names);

/// A voice the platform can speak in.
struct VoiceOption
{
  /// The platform's id ("am_echo").
  std::string id;
  /// What it's called out loud ("Echo").
  std::string name;
};

/// The silly code phrase that picks each voice, one per voice in order: Echo is "purple monkey"
/// and Adam "elephant shoe" (Brandon's), the rest come from a pool. Lowercase.
std::vector<std::string> VoicePhrases(std::vector<VoiceOption> const & voices);
/// How many times `spoken` says `name`, ignoring case and tolerating a close miss by the speech
/// recognizer: "Bela" and "Nicol" count, "Isabella" doesn't count for "Bella". A name of several
/// words ("banana phone") must be said whole.
int CountVoiceName(std::string const & spoken, std::string const & name);
/// The voice whose code phrase is in `spoken`, as an index into `voices`.
std::optional<size_t> VoiceByPhrase(std::vector<VoiceOption> const & voices, std::string const & spoken);
/// The voice called `said` (by name or id, any case), as an index into `voices`.
std::optional<size_t> VoiceNamed(std::vector<VoiceOption> const & voices, std::string const & said);

/// Who the driver says they are, as the Settings screen saved it. Pronouns are "he", "she" or
/// "they"; age is whatever was typed ("34"), empty if never given.
struct DriverProfile
{
  std::string name;
  std::string pronouns;
  std::string age;
};

/// What to say when the driver edits their own profile by hand (Brandon: "Changing it up, I see",
/// then it calls them the old thing and corrects itself). `key` is the line ("profile.name",
/// "profile.title", "profile.age" or "profile.younger") and `vars` fill {old} and {new}. A name
/// change wins over a pronoun change, which wins over age. Nothing for no change, or for filling in
/// something that was empty (that's setup, not changing it up).
struct ProfileChange
{
  std::string key;
  std::map<std::string, std::string> vars;
};
std::optional<ProfileChange> ProfileChanged(DriverProfile const & before, DriverProfile const & after);

/// Where home or work probably is, from past trips. Home is where trips start and end most;
/// work is the most common other destination. Needs at least two trips there to be sure.
std::optional<LatLon> GuessPlace(std::vector<TripRecord> const & history, KnownPlace place);
}  // namespace backseat
