#pragma once

#include "backseat/navigator.hpp"
#include "backseat/voice_command.hpp"

#include "platform/location.hpp"

#include "geometry/point2d.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class DataSource;

namespace storage
{
class CountryInfoGetter;
}

namespace routing
{
class Route;
class RoutingSession;
}

/// Connects Backseat Driver's navigator to CoMaps routing: builds route snapshots from the
/// routing session, feeds it real GPS fixes, and hands back what to say in place of the
/// stock turn notifications. Trip history is kept in the settings directory.
class BackseatAdapter
{
public:
  BackseatAdapter();

  /// A real (not extrapolated) GPS fix.
  void OnLocationUpdate(location::GpsInfo const & info, routing::RoutingSession const & session,
                        std::string const & destinationName);

  /// Called when the platform is about to speak: catches route changes since the last fix
  /// and returns the lines to say.
  std::vector<std::string> TakeNotifications(routing::RoutingSession const & session,
                                             std::string const & destinationName);

  /// Map data, for naming the place of an unplanned stop ("Why are we stopping for gas?").
  void SetDataSource(std::function<DataSource &()> getter);
  /// Map regions, so the license plate game knows which state the car is in.
  void SetCountryInfoGetter(std::function<storage::CountryInfoGetter const &()> getter);
  /// What the driver said out loud (speech recognition), e.g. "I see a Texas plate".
  /// Call on the same thread as OnLocationUpdate.
  /// What the driver asked for out loud, and what the GPS says back.
  struct HeardCommand
  {
    enum class Kind
    {
      /// No destination in it.
      None,
      /// Route to `lat`/`lon` (home or work, worked out from past trips).
      RouteTo,
      /// Search for `query` and let the driver pick.
      Search
    };
    Kind kind = Kind::None;
    double lat = 0;
    double lon = 0;
    std::string name;
    std::string query;
    /// App actions for the platform to carry out, in the order asked (cancel, mute, change voice,
    /// zoom...). Destinations, directions, time left, police and the plate game are handled here.
    std::vector<backseat::AppCommand> actions;
    /// The GPS's snarky answer, to say right away (before muting, if that's one of the actions).
    /// Empty when there's nothing to say now.
    std::string reply;
    /// Said after `reply`, each in its own voice (the voice line-up, or a new voice saying hello).
    struct VoiceLine
    {
      std::string voiceId;
      std::string text;
    };
    std::vector<VoiceLine> voiceLines;
  };
  /// The voices the platform can speak in, for "what voice options do you have?" and picking one
  /// by name or code phrase. Without them, asking for the voices just opens the list.
  void SetVoices(std::vector<backseat::VoiceOption> voices) { m_voices = std::move(voices); }
  /// Speech recognition, keyword matching: "avoid tolls and take me home".
  HeardCommand OnDriverSaid(std::string const & text);
  /// The on-device model picked a tool from backseat::AppTools(). `spoken` is what the driver
  /// actually said (for manners). Unknown tools do nothing.
  HeardCommand RunTool(std::string const & name, std::string const & argument, std::string const & spoken);
  /// Traffic lights on the route just built (mercator points). A left turn off a side street
  /// with no light there is treated as a stop sign, since stop signs aren't in the map data.
  void SetTrafficLights(std::vector<m2::PointD> points);
  /// Whether other audio is playing (the platform's audio session), so the GPS can ask for music.
  void SetMusicCheck(std::function<bool()> check) { m_navigator.SetMusicLookup(std::move(check)); }
  /// The platform's on-device language model, for rewording roasts in real time.
  void SetRewriter(std::shared_ptr<backseat::LineRewriter> rewriter) { m_navigator.SetRewriter(std::move(rewriter)); }

  /// Settings keys for who's in the car, written by the platform's Settings screen. Pronouns are
  /// "they", "he" or "she"; the partner is a word like "wife" or a name, empty for nobody.
  static char const * const kPartnerKey;
  static char const * const kPartnerPronounsKey;
  static char const * const kDriverPronounsKey;
  static char const * const kAutopilotKey;
  /// What to call the driver, from first-run setup.
  static char const * const kDriverNameKey;
  /// Rereads those settings; call after the platform changes them.
  void ReloadPersonality();
  /// First-run setup, spoken, Brandon's script:
  ///   SetupLine(Intro)       "Before we begin... My name is Echo. ...the sound your head makes when you tap it."
  ///   SetupLine(Name)        "Your turn. What's your name?"  -> listen, SetupHeard(Name, answer) until true
  ///   SetupLine(WrongName)   "Interesting. I thought it would have been Gary. Can I call you Gary?" -> listen (any answer)
  ///   SetupLine(WhereTo)     "Okay Doug, where are we headed?" -> the answer goes to OnDriverSaid() as usual
  /// Pronouns and Passenger are optional extra questions (SetupHeard saves those answers too).
  enum class SetupStep
  {
    Intro,
    Name,
    WrongName,
    WhereTo,
    Pronouns,
    Passenger
  };
  /// `voiceName` is the voice speaking ("Echo" gets Brandon's opener).
  std::string SetupLine(SetupStep step, std::string const & voiceName = "Echo");
  /// False when the answer wasn't understood; ask again. Saves what it understood to settings.
  bool SetupHeard(SetupStep step, std::string const & spoken);

  /// Said when the app opens with no route, before listening once for a destination: "Where to?",
  /// or a guess at the usual trip ("It's Tuesday morning. Work, I assume?"). A "yes" next routes there.
  std::string AskWhereTo();
  /// Call when AskWhereTo() got no answer (about 45 s of silence). Says "Deciding to play it safe
  /// today, huh?" if the car hasn't moved since; empty otherwise.
  std::string NoAnswerToWhereTo();

  /// Spoken search results, after a HeardCommand Search. The platform runs the search, passes the
  /// top results closest first, speaks the returned line, and then:
  ///  - `pick` set: route to that result right away (one result, or none to pick from: `none`).
  ///  - otherwise listen and pass the answer to PickSearchResult().
  struct SearchResult
  {
    std::string name;
    /// From the car, meters.
    double distanceM = 0;
  };
  struct SearchSpeech
  {
    std::string line;
    std::optional<size_t> pick;
    bool none = false;
  };
  SearchSpeech SpeakSearchResults(std::string const & query, std::vector<SearchResult> results);
  /// "The second one": which result, and the line to say ("Starbucks. Good choice."). No index
  /// means it didn't understand; say the line ("First, second, or third?") and listen again.
  std::pair<std::optional<size_t>, std::string> PickSearchResult(std::string const & spoken);

  /// What the GPS has worked out about the driver, for the profile screen: label and value, e.g.
  /// {"Works at", "Acme Corp"}, {"Commute", "Mon to Fri, around 8:05 AM"}.
  std::vector<std::pair<std::string, std::string>> LearnedFacts() const;

  /// How to voice one line of the last TakeNotifications() batch.
  struct LineVoice
  {
    /// A navigation line: the platform must say it right away, so it never waits on a slow voice.
    bool nav = true;
    /// Suggested speech rate, 1 = normal.
    double speed = 1;
  };

  /// One entry per line of the last TakeNotifications() batch, in the same order.
  std::vector<LineVoice> const & LastLineVoices() const { return m_lastVoices; }

private:
  backseat::RouteSnapshot Snapshot(routing::RoutingSession const & session, std::string const & destinationName) const;
  backseat::Control GuessControl(routing::Route const & route, uint32_t turnIndex) const;
  std::optional<backseat::Place> PlaceAt(backseat::LatLon const & at) const;
  bool InParkingLot(backseat::LatLon const & at) const;
  std::optional<backseat::Venue> VenueAt(backseat::LatLon const & at) const;
  std::optional<backseat::Sight> SightAt(backseat::LatLon const & at, double bearingDeg) const;
  backseat::Bridge BridgeAt(backseat::LatLon const & at, double bearingDeg) const;
  void CheckBattery(double tMs);
  /// Carries out one command, adding to `heard`; returns the reply to say for it.
  std::string Run(backseat::AppCommand const & command, bool polite, HeardCommand & heard);

  backseat::Navigator m_navigator;
  std::function<DataSource &()> m_dataSource;
  std::function<storage::CountryInfoGetter const &()> m_countryInfo;
  mutable std::mutex m_lightsMutex;
  std::vector<m2::PointD> m_trafficLights;
  double m_lastBatteryCheckMs = 0;
  std::vector<LineVoice> m_lastVoices;
  /// For "repeat that" and "how long": the last direction said, and time left on the route.
  std::string m_lastDirection;
  /// The wrong name the setup just called the driver, so the next one is different.
  std::string m_wrongName;
  /// For "Starbucks in Colorado or Starbucks in Alaska?": where the phone is, the first search
  /// after setup asks, then every few searches.
  std::optional<m2::PointD> m_here;
  bool m_askStateNext = false;
  int m_searches = 0;
  /// The usual trip AskWhereTo() guessed, waiting for a "yes".
  struct Guess
  {
    backseat::Routine routine;
    std::string name;
    std::chrono::steady_clock::time_point at;
  };
  std::optional<Guess> m_guess;
  /// Where the phone was when AskWhereTo() asked.
  std::optional<m2::PointD> m_askedAt;
  /// Results last read out, waiting for "the first one".
  std::vector<SearchResult> m_results;
  std::vector<backseat::VoiceOption> m_voices;
  mutable int m_secondsLeft = -1;
};
