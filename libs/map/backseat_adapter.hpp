#pragma once

#include "backseat/debug_log.hpp"
#include "backseat/mileage.hpp"
#include "backseat/navigator.hpp"
#include "backseat/road_bits.hpp"
#include "backseat/voice_command.hpp"

#include "platform/location.hpp"

#include "geometry/point2d.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
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
  ~BackseatAdapter();
  BackseatAdapter(BackseatAdapter const &) = delete;
  BackseatAdapter & operator=(BackseatAdapter const &) = delete;

  /// The drive log (Settings > Backseat Driver > Share drive log): what the GPS did on recent
  /// drives, kept on the phone so test drives can be troubleshot. On by default.
  bool DebugLogEnabled() const { return m_debugLog.Enabled(); }
  void SetDebugLogEnabled(bool on);
  /// Writes the whole log to `path`, starting with `header` (app version, device, voice state).
  bool ExportDebugLog(std::string const & path, backseat::LogFields const & header) const
  {
    return m_debugLog.ExportTo(path, header);
  }
  void ClearDebugLog() { m_debugLog.Clear(); }
  /// For events only the platform sees (which voice spoke, speech recognition, audio interruptions).
  void LogEvent(std::string const & ev, backseat::LogFields const & fields) { backseat::Trace(ev, fields); }

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
    /// RunTool: the words didn't support that tool, so nothing ran.
    bool unsupported = false;
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
  /// The on-device model that rewords roasts. Off unless the "BackseatRewriteLines" setting is on:
  /// on the first test drives its rewrites were worse than the written lines (refusals, chatter,
  /// lines that said the opposite), and the written pools are big enough to stay fresh.
  void SetRewriter(std::shared_ptr<backseat::LineRewriter> rewriter);

  /// Settings keys for who's in the car, written by the platform's Settings screen. Pronouns are
  /// "they", "he" or "she"; the partner is a word like "wife" or a name, empty for nobody.
  static char const * const kPartnerKey;
  static char const * const kPartnerPronounsKey;
  static char const * const kDriverPronounsKey;
  static char const * const kAutopilotKey;
  /// What to call the driver, from first-run setup.
  static char const * const kDriverNameKey;
  /// The driver's age as typed in Settings ("34"), empty if never given.
  static char const * const kDriverAgeKey;
  /// Mileage settings, all "true"/"false": the log itself (default off), talking about drives the
  /// driver didn't start (default on), and home being the main place of business (default off).
  static char const * const kMileageOnKey;
  static char const * const kMileageTalkKey;
  static char const * const kHomeIsWorkplaceKey;
  /// Whose business the mileage log tracks: "" the driver's own, "wife", or a name ("Sarah").
  static char const * const kBusinessOwnerKey;
  /// Rereads those settings; call after the platform changes them.
  void ReloadPersonality();
  /// Call when the driver opens their profile in Settings, and EndProfileEdit() when they leave it.
  /// If they changed their name, pronouns or age by hand, EndProfileEdit() returns what to say:
  /// "Changing it up, I see. Okay, Brandon... I mean, Brenda." Empty otherwise.
  void BeginProfileEdit();
  std::string EndProfileEdit();
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

  /// The GPS's memory of the driver, kept forever in its own file. Everything learned in setup,
  /// Settings, conversation and from trips lands here.
  bool Remember(std::string const & topic, std::string const & fact);
  bool Forget(std::string const & topic);
  std::vector<backseat::DriverMemory::Memory> const & Memories() const { return m_memory.All(); }
  /// Instructions for the on-device model when the driver just talks: the jaded persona, the rules
  /// (app actions only through do_app_action, remember_fact for things worth keeping), and memory.
  std::string ChatInstructions(std::string const & voiceName);

  /// "For your file" questions, asked one at a time at a quiet moment (not while turning): favorite
  /// ice cream, color, bed side, snack, and later the snack again with different choices (the
  /// Portal cake trap). Empty if there's nothing to ask now (at most one every 6 hours). Listen once
  /// after it and pass the answer to FileAnswer(), which returns the reply.
  std::string FileQuestion();
  std::string FileAnswer(std::string const & spoken);
  /// The Niceness slider in settings does nothing except this: call when the driver lets go of it,
  /// speak the line, then slide it all the way back to the mean side.
  /// The first time ever it laughs at them ("You thought you could make me nicer?") and remembers
  /// that they tried; after that it just laughs.
  std::string NicenessMoved();
  /// Once the slider is back on the mean side: "Don't touch my slider."
  std::string NicenessSlidBack() { return m_navigator.Reply("niceness.after"); }
  /// The driver dragged the map away from the car while navigating: "Put your map away. We don't
  /// live in the 90s anymore. We went digital." At most once every 20 minutes; empty otherwise.
  std::string MapBrowsed();
  /// The driver started typing a destination instead of saying it: "I see we aren't on speaking
  /// terms. Well, I for one am not offended." At most once every 20 minutes; empty otherwise.
  std::string TypingDestination();
  /// The platform felt a rough stretch while driving (several hard vertical jolts within a few
  /// seconds): "You know there is an expression about taking the road less traveled... I don't
  /// think it mentions pot holes." Once a trip at most, and only while navigating.
  std::string RoughRoad();
  /// After the arrival line, at most once every 20 hours: "Test 47 complete. On a scale of one to
  /// ten, how satisfied were you?" Empty otherwise. Listen once, then pass the answer to
  /// SurveyAnswer(), which says it was recorded (and ignored) and keeps it in memory.
  std::string ArrivalSurvey();
  std::string SurveyAnswer(std::string const & spoken);

  /// The mileage log (backseat/mileage.hpp). It does nothing unless the driver turned on "Log my
  /// drives automatically" (kMileageOnKey). The platform feeds every location fix, navigating or
  /// not; `activity` follows backseat::Activity (0 unknown ... 5 automotive). Returns a line to say,
  /// or empty: "Oh, we're driving?" when a drive starts without a route, "Welcome to Utah" once per
  /// drive out of the home state, and a comment when a drive is logged.
  std::string OnDriveSample(double lat, double lon, double speedMps, double accuracyM, int activity, double tMs);
  /// The OS says we arrived somewhere (CLVisit) or the app is going away: ends the drive, if any.
  std::string MileageFinish();
  bool MileageOn() const;
  std::vector<backseat::LoggedDrive> const & MileageLog() const { return m_driveLog; }
  backseat::MileageYear MileageSummary(int year) const;
  std::string MileageCsv(int year) const;
  /// How many other unsorted drives go between the same two places, for "Do the same for 3 similar
  /// trips?" before calling ReassignDrive().
  size_t SimilarDrives(size_t index) const;
  /// Sorts drive `index` (and the similar ones too, if asked) and returns what to say about it.
  std::string ReassignDrive(size_t index, backseat::Bucket bucket, bool similarToo, std::string const & purpose);
  void TagPlace(std::string const & name, double lat, double lon, backseat::PlaceRole role);
  std::vector<backseat::MileagePlace> const & TaggedPlaces() const { return m_taggedPlaces; }
  /// Right after "Log my drives automatically" is turned on, the first time: "Who owns the
  /// business?" Empty after that.
  std::string MileageTurnedOn();
  /// A mileage line just asked something ("was that your house?", "whose business?", "was that trip
  /// for your wife's business?"). After speaking it, the platform listens once and passes what was
  /// heard to MileageAnswer(), which records it and returns what to say (empty if it didn't
  /// understand; the question then lapses).
  bool MileageListening() const { return m_mileageAsk != MileageAsk::None; }
  std::string MileageAnswer(std::string const & spoken);
  std::string BusinessOwner() const;
  /// For the Mileage screen's "Business owner" field.
  void SetBusinessOwner(std::string const & owner);
  /// In December and January, once a year: "This year you drove 1,204 miles for work..." Empty
  /// otherwise, or with nothing logged.
  std::string MileageYearEnd();

  /// Draw-your-own route (RoutingManager::ApplyRouteSketch). RoutingManager reports every newly
  /// built route; one without a drawing is remembered as the fastest way to its finish.
  void OnRouteBuilt(double totalSeconds, m2::PointD const & finish, bool sketched);
  /// Call after ApplyRouteSketch() and before BuildRoute(). Once the drawn route is built,
  /// TakeSketchVerdict() has the GPS's opinion of it.
  void SketchApplied(bool addedStop, bool trimmed) { m_pendingSketch = {addedStop, trimmed}; }
  /// "Your route takes 7 minutes longer than mine...", "Another stop. Sure.", or empty. Once.
  std::string TakeSketchVerdict() { return std::exchange(m_sketchVerdict, std::string()); }
  /// After ClearRouteSketch(): "Oh, now you want my route."
  std::string SketchCleared() { return m_navigator.Reply("sketch.cleared"); }
  /// A place driven to before, for the "Where to?" list.
  struct PastRoute
  {
    /// What to show: "Home", "Work", or what it was called ("Starbucks").
    std::string label;
    /// What it was called last time (the search text if there's no exact spot).
    std::string destination;
    double lat = 0;
    double lon = 0;
    int trips = 0;
    /// How long it usually takes, whole minutes; 0 if unknown.
    int usualMinutes = 0;
  };
  /// Previous destinations, most driven first (most recent breaks ties).
  std::vector<PastRoute> PastRoutes(size_t max) const;
  /// Call when AskWhereTo() got no answer (about 45 s of silence). Says "Deciding to play it safe
  /// today, huh?" if the car hasn't moved since; empty otherwise.
  std::string NoAnswerToWhereTo();
  /// A line from the voice preview: "cmd.voice", "voice.preview.complaint", "voice.preview.notMissing".
  std::string VoicePreviewLine(std::string const & key, std::string const & voice);
  /// A game is waiting for the driver, so what they say is an answer, not a request.
  bool InGame() const { return m_navigator.InGame(); }
  /// Kokoro's first line after the system voice had to stand in: it coughs (the platform plays a
  /// cough sound first) and blames something it ate.
  std::string VoiceRecovered() { return m_navigator.Reply("voice.recovered"); }

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
    /// `pick` doesn't look like what was asked for: listen, and pass the answer to ConfirmSearchResult().
    bool confirm = false;
  };
  SearchSpeech SpeakSearchResults(std::string const & query, std::vector<SearchResult> results);
  /// "The second one": which result, and the line to say ("Starbucks. Good choice."). No index
  /// means it didn't understand; say the line ("First, second, or third?") and listen again.
  /// With `confirm`, the pick doesn't look like what was asked for: say the line, listen, and pass the
  /// answer to ConfirmSearchResult() before routing.
  struct SearchPick
  {
    std::optional<size_t> index;
    std::string line;
    bool confirm = false;
  };
  SearchPick PickSearchResult(std::string const & spoken);
  /// The answer to "Latheron Place? Say yes." Empty when nobody answered. Whether to route, and the line.
  std::pair<bool, std::string> ConfirmSearchResult(std::string const & spoken);

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
    /// Navigation lines: how long the line may wait for the neural voice before it's too late
    /// (from the distance and speed to the next maneuver). Wait at least the usual 1.2 s.
    double maxWaitMs = 0;
    /// Someone else talking (the caller in a fake phone call): a Kokoro voice id. Empty means the
    /// GPS's own voice.
    std::string voice;
    /// A 20 Questions line: the driver answers out loud, so the platform listens after it.
    bool listen = false;
  };

  /// One entry per line of the last TakeNotifications() batch, in the same order.
  std::vector<LineVoice> const & LastLineVoices() const { return m_lastVoices; }

  /// The report card of the last trip that reached its destination in this app run.
  struct LastTrip
  {
    /// Seconds since the epoch, when the report was made.
    double at = 0;
    backseat::TripRecord record;
    backseat::TripReport report;
    /// The grade of the trip before it (to the same place if there was one).
    std::optional<std::string> previousGrade;
  };
  std::optional<LastTrip> LastTripReport() const
  {
    std::lock_guard<std::mutex> lock(m_lastTripMutex);
    return m_lastTrip;
  }
  /// The GPS's own name, the voice the driver picked ("Echo"): the fake phone calls answer with it.
  void SetGpsName(std::string const & name) { m_navigator.SetGpsName(name); }
  /// A caller's voice to warm up, once, when a call that will put them on starts. Empty otherwise.
  std::string TakeCallerVoiceToWarm() { return std::exchange(m_callerVoiceToWarm, std::string()); }

private:
  backseat::RouteSnapshot Snapshot(routing::RoutingSession const & session, std::string const & destinationName) const;
  backseat::Control GuessControl(routing::Route const & route, uint32_t turnIndex) const;
  std::optional<backseat::Place> PlaceAt(backseat::LatLon const & at) const;
  bool InParkingLot(backseat::LatLon const & at) const;
  /// The building whose footprint covers `at`, deepest first. Nothing in a parking garage, carport,
  /// canopy, gas station or parking lot: starting a trip there is normal.
  std::optional<backseat::Building> BuildingAt(backseat::LatLon const & at) const;
  std::optional<backseat::Venue> VenueAt(backseat::LatLon const & at) const;

public:
  /// Draw mode: a finger stroke moved onto the car roads it traced (route_sketch.hpp SnapStroke).
  /// Empty if there's no map data there or the stroke covers too much ground to look up quickly.
  std::vector<backseat::LatLon> SnapToRoads(std::vector<backseat::LatLon> const & stroke) const;
  /// Road-trip remarks (road_bits.hpp): what's around the car, what's along the route ahead, and
  /// what kind of place the car stopped at.
  backseat::Surroundings SurroundingsAt(backseat::LatLon const & p, double bearingDeg) const;
  backseat::RouteAhead RouteAheadWithin(double maxM);
  backseat::StopSpot StopSpotAt(backseat::LatLon const & p) const;
  /// From the platform: the phone has (or lost) a data connection; the weather where the car is.
  void OnConnectivity(bool online);
  /// `tempC`: the observation's temperature.value (°C), NaN when it's null.
  void OnWeather(std::string const & description, double windMps, double gustMps,
                 double tempC = std::numeric_limits<double>::quiet_NaN());
  /// 20 Questions: what's within about 2 km (TwentyThing tags: "water_tower", "bridge", "farm"...).
  std::set<std::string> NearbyThings(backseat::LatLon const & at) const;
  /// How wound up the GPS is this trip, 0-100 (0 with no trip), for the driving screen's Patience meter.
  int Frustration() const
  {
    auto const * engine = m_navigator.Engine();
    return engine ? engine->Frustration() : 0;
  }

private:
  std::optional<backseat::Sight> SightAt(backseat::LatLon const & at, double bearingDeg) const;
  backseat::Bridge BridgeAt(backseat::LatLon const & at, double bearingDeg) const;
  /// Meters to the nearest mapped car road, up to kRoadSearchM; that far if none, -1 with no map.
  double RoadDistanceAt(backseat::LatLon const & at) const;
  /// Right after the car drives past a highway exit (staying on the highway): how far along the
  /// route the next exit is, -1 if the trip ends first. Nothing on every other update.
  std::optional<double> PassedExitNextM(routing::RoutingSession const & session, double speedMps);
  void CheckBattery(double tMs);
  /// Carries out one command, adding to `heard`; returns the reply to say for it.
  std::string Run(backseat::AppCommand const & command, bool polite, HeardCommand & heard);

  /// First, so it's there before the navigator and gone after it.
  backseat::DebugLog m_debugLog;
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
  /// The highway exit the car is passing, and the last one already counted as passed.
  std::optional<m2::PointD> m_exitAt;
  std::optional<m2::PointD> m_exitPassed;
  double m_exitCheckAt = 0;
  bool m_askStateNext = false;
  int m_searches = 0;
  backseat::CommandRepeats m_commandRepeats;
  void ChoseSearchPlace(std::string const & place);
  std::string m_searchQuery;
  std::string m_placeToConfirm;
  bool m_bossyAboutSearch = false;
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
  /// The profile as it was when Settings opened it, until EndProfileEdit().
  std::optional<backseat::DriverProfile> m_profileBefore;
  backseat::DriverMemory m_memory;
  /// Copies settings and learned facts into memory.
  void SyncMemory();
  /// The memory topic FileQuestion() asked about, until FileAnswer().
  std::string m_fileQuestion;
  /// Polite destination requests, for the occasional "you're so basic" recap.
  int m_navigates = 0;
  double m_mapBrowsedAt = -1e18;
  double m_typingSaidAt = -1e18;
  double m_roughSaidAt = -1e18;
  /// Waiting for a rating after ArrivalSurvey().
  bool m_surveyAsked = false;
  backseat::DriveDetector m_drives;
  /// For the drive log's drive start and end marks.
  bool m_wasInTrip = false;
  std::vector<backseat::LoggedDrive> m_driveLog;
  std::vector<backseat::MileagePlace> m_taggedPlaces;
  /// Region lookups are cached until the car has moved a couple of kilometers.
  std::optional<m2::PointD> m_regionAt;
  std::string m_region;
  /// "Welcome to Utah" was already said on this drive.
  bool m_saidOutOfState = false;
  enum class MileageAsk
  {
    None,
    Home,
    Owner,
    Business
  };
  /// What the last mileage line asked, until MileageAnswer().
  MileageAsk m_mileageAsk = MileageAsk::None;
  backseat::LatLon m_askedHomeAt;
  size_t m_askedDrive = 0;
  /// "I know we are just getting to know each other and all, but the place we just left, was that
  /// your house?" when a drive starts from where the phone sleeps and Home isn't tagged. Empty
  /// otherwise. At most three times, a day apart.
  std::string AskAboutHome();
  /// Home and the regular workplace from trips, plus the places the driver tagged.
  std::vector<backseat::MileagePlace> MileagePlaces() const;
  backseat::MileageSettings MileageSettingsNow() const;
  std::string OnDriveEnded(backseat::Drive const & drive);
  void SaveDriveLog() const;
  /// The current route was drawn, and the last fastest route (time and where it ended).
  bool m_routeSketched = false;
  double m_fastestSeconds = 0;
  std::optional<m2::PointD> m_fastestTo;
  struct PendingSketch
  {
    bool addedStop = false;
    bool trimmed = false;
  };
  std::optional<PendingSketch> m_pendingSketch;
  std::string m_sketchVerdict;
  /// Results last read out, waiting for "the first one".
  std::vector<SearchResult> m_results;
  std::vector<backseat::VoiceOption> m_voices;
  mutable int m_secondsLeft = -1;
  std::string m_lastCallerVoice;
  std::string m_callerVoiceToWarm;
  mutable std::mutex m_lastTripMutex;
  std::optional<LastTrip> m_lastTrip;
  /// The route being followed, for the road-trip lookups (set with each location update).
  std::shared_ptr<routing::Route const> m_route;
  /// Rest areas, gas, coffee and viewpoints along m_route, by distance from its start; scanned once
  /// per route in the background.
  mutable std::mutex m_scanMutex;
  routing::Route const * m_scannedRoute = nullptr;
  bool m_scanning = false;
  std::vector<std::pair<backseat::Stopover, double>> m_scannedStops;
};
