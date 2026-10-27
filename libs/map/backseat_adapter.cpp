#include "map/backseat_adapter.hpp"

#include "backseat/route_sketch.hpp"
#include "backseat/units.hpp"
#include "backseat/voice_command.hpp"

#include "routing/following_info.hpp"
#include "routing/route.hpp"
#include "routing/routing_session.hpp"
#include "routing/turns.hpp"

#include "indexer/classificator.hpp"
#include "indexer/data_source.hpp"
#include "indexer/feature.hpp"
#include "indexer/feature_algo.hpp"
#include "indexer/feature_data.hpp"
#include "indexer/ftypes_matcher.hpp"
#include "indexer/scales.hpp"

#include "platform/platform.hpp"
#include "platform/settings.hpp"

#include "storage/country_info_getter.hpp"

#include "geometry/mercator.hpp"
#include "geometry/parametrized_segment.hpp"

#include "base/logging.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <utility>

namespace
{
std::string const kTripsFile = "backseat_trips.tsv";
std::string const kPlatesFile = "backseat_plates.txt";
std::string const kSaidFile = "backseat_said.tsv";
std::string const kAchievementsFile = "backseat_achievements.tsv";
std::string const kMemoryFile = "backseat_memory.tsv";
/// Every drive the mileage log saw, never capped: it's the driver's tax record.
std::string const kDrivesFile = "backseat_drives.tsv";
/// Places the driver tagged for mileage: name, lat, lon, role.
std::string const kMileagePlacesFile = "backseat_mileage_places.tsv";
size_t constexpr kMaxTrips = 200;
/// A light this close to the turn point controls the junction.
double constexpr kTrafficLightRadiusM = 30;
/// How far around a stop to look for the gas station or drive-through.
double constexpr kPlaceSearchSizeM = 80;
double constexpr kBatteryCheckEveryMs = 30000;

/// Bigger roads rank lower. Side streets, driveways and service roads share the top rank.
int RoadRank(ftypes::HighwayClass c)
{
  using ftypes::HighwayClass;
  switch (c)
  {
  case HighwayClass::Motorway: return 0;
  case HighwayClass::Trunk: return 1;
  case HighwayClass::Primary: return 2;
  case HighwayClass::Secondary: return 3;
  case HighwayClass::Tertiary: return 4;
  case HighwayClass::LivingStreet:
  case HighwayClass::Service:
  case HighwayClass::ServiceMinor: return 5;
  default: return -1;
  }
}

backseat::TurnKind ToTurnKind(routing::turns::CarDirection d)
{
  using routing::turns::CarDirection;
  using backseat::TurnKind;
  switch (d)
  {
  case CarDirection::GoStraight: return TurnKind::Straight;
  case CarDirection::TurnRight: return TurnKind::Right;
  case CarDirection::TurnSharpRight: return TurnKind::SharpRight;
  case CarDirection::TurnSlightRight: return TurnKind::SlightRight;
  case CarDirection::TurnLeft: return TurnKind::Left;
  case CarDirection::TurnSharpLeft: return TurnKind::SharpLeft;
  case CarDirection::TurnSlightLeft: return TurnKind::SlightLeft;
  case CarDirection::UTurnLeft:
  case CarDirection::UTurnRight: return TurnKind::UTurn;
  case CarDirection::EnterRoundAbout:
  case CarDirection::LeaveRoundAbout:
  case CarDirection::StayOnRoundAbout: return TurnKind::Roundabout;
  case CarDirection::ReachedYourDestination: return TurnKind::Destination;
  case CarDirection::ExitHighwayToLeft: return TurnKind::ExitLeft;
  case CarDirection::ExitHighwayToRight: return TurnKind::ExitRight;
  default: return TurnKind::None;
  }
}

/// The router formats street names for the screen: "[2] North Colorado Boulevard" or, at
/// exits, "[I 25] > Denver". Shields are drawn as signs there, but read aloud they come out
/// as "bracket two". Keep the name, or the bare route number when there is no name.
std::string SpeakableStreetName(std::string const & formatted)
{
  std::string name, refs;
  bool inShield = false;
  for (char const c : formatted)
  {
    if (c == '[')
      inShield = true;
    else if (c == ']')
    {
      inShield = false;
      refs += ' ';
    }
    else
      (inShield ? refs : name) += c;
  }
  auto const tidy = [](std::string s)
  {
    size_t const gt = s.find('>');
    if (gt != std::string::npos)
      s = "toward " + s.substr(gt + 1);
    std::string out;
    for (char const c : s)
    {
      if (c == ':' || (c == ' ' && (out.empty() || out.back() == ' ')))
        continue;
      out += c;
    }
    while (!out.empty() && out.back() == ' ')
      out.pop_back();
    return out;
  };
  name = tidy(name);
  if (name.empty() || name.rfind("toward ", 0) == 0)
  {
    std::string const r = tidy(refs);
    if (!r.empty())
      name = name.empty() ? r : r + " " + name;
  }
  return name;
}

backseat::LatLon ToBackseatLatLon(m2::PointD const & p)
{
  auto const ll = mercator::ToLatLon(p);
  return {ll.m_lat, ll.m_lon};
}

std::vector<backseat::TripRecord> LoadTrips()
{
  std::vector<backseat::TripRecord> trips;
  std::ifstream in(GetPlatform().SettingsPathForFile(kTripsFile));
  std::string line;
  while (std::getline(in, line))
  {
    std::istringstream fields(line);
    backseat::TripRecord r;
    std::string expected, actual, share, missed, brakes, ended;
    if (std::getline(fields, r.routeKey, '\t') && std::getline(fields, r.destination, '\t') &&
        std::getline(fields, ended, '\t') && std::getline(fields, expected, '\t') &&
        std::getline(fields, actual, '\t') && std::getline(fields, share, '\t') && std::getline(fields, missed, '\t') &&
        std::getline(fields, brakes, '\t') && std::getline(fields, r.grade, '\t'))
    {
      try
      {
        r.endedAt = std::stod(ended);
        r.expectedMinutes = std::stod(expected);
        r.actualMinutes = std::stod(actual);
        r.speedingShare = std::stod(share);
        r.missedTurns = std::stoi(missed);
        r.hardBrakes = std::stoi(brakes);
        // Newer lines end with where the trip was headed: "lat,lon".
        std::string to;
        if (std::getline(fields, to, '\t'))
        {
          auto const comma = to.find(',');
          if (comma != std::string::npos)
            r.to = backseat::LatLon{std::stod(to.substr(0, comma)), std::stod(to.substr(comma + 1))};
        }
        trips.push_back(r);
      }
      catch (std::exception const &)
      {
        // A damaged line just gets forgotten.
      }
    }
  }
  if (trips.size() > kMaxTrips)
    trips.erase(trips.begin(), trips.end() - kMaxTrips);
  return trips;
}

void AppendTrip(backseat::TripRecord const & r)
{
  std::ofstream out(GetPlatform().SettingsPathForFile(kTripsFile), std::ios::app);
  out << r.routeKey << '\t' << r.destination << '\t' << r.endedAt << '\t' << r.expectedMinutes << '\t'
      << r.actualMinutes << '\t' << r.speedingShare << '\t' << r.missedTurns << '\t' << r.hardBrakes << '\t' << r.grade;
  if (r.to)
    out << '\t' << std::setprecision(9) << r.to->lat << ',' << r.to->lon;
  out << '\n';
}
backseat::PlateLog LoadPlates()
{
  std::ifstream in(GetPlatform().SettingsPathForFile(kPlatesFile));
  std::stringstream text;
  text << in.rdbuf();
  return backseat::PlateLog::Parse(text.str());
}

backseat::SaidLog LoadSaid()
{
  std::ifstream in(GetPlatform().SettingsPathForFile(kSaidFile));
  std::stringstream text;
  text << in.rdbuf();
  return backseat::SaidLog::Parse(text.str());
}

void SaveSaid(backseat::SaidLog const & log)
{
  std::ofstream(GetPlatform().SettingsPathForFile(kSaidFile)) << log.Serialize();
}

backseat::Achievements LoadAchievements()
{
  std::ifstream in(GetPlatform().SettingsPathForFile(kAchievementsFile));
  std::stringstream text;
  text << in.rdbuf();
  return backseat::Achievements::Parse(text.str());
}

void SaveAchievements(backseat::Achievements const & achievements)
{
  std::ofstream(GetPlatform().SettingsPathForFile(kAchievementsFile)) << achievements.Serialize();
}

backseat::DriverMemory LoadMemory()
{
  std::ifstream in(GetPlatform().SettingsPathForFile(kMemoryFile));
  std::stringstream text;
  text << in.rdbuf();
  return backseat::DriverMemory::Parse(text.str());
}

std::vector<backseat::LoggedDrive> LoadDrives()
{
  std::vector<backseat::LoggedDrive> drives;
  std::ifstream in(GetPlatform().SettingsPathForFile(kDrivesFile));
  std::string line;
  while (std::getline(in, line))
    if (auto d = backseat::ParseDrive(line))
      drives.push_back(std::move(*d));
  return drives;
}

std::vector<backseat::MileagePlace> LoadMileagePlaces()
{
  std::vector<backseat::MileagePlace> places;
  std::ifstream in(GetPlatform().SettingsPathForFile(kMileagePlacesFile));
  std::string line;
  while (std::getline(in, line))
  {
    std::istringstream fields(line);
    std::string name, lat, lon, role;
    if (!std::getline(fields, name, '\t') || !std::getline(fields, lat, '\t') || !std::getline(fields, lon, '\t') ||
        !std::getline(fields, role, '\t'))
      continue;
    backseat::MileagePlace p;
    p.name = name;
    p.at = {std::atof(lat.c_str()), std::atof(lon.c_str())};
    p.role = static_cast<backseat::PlaceRole>(std::clamp(std::atoi(role.c_str()), 0, 3));
    places.push_back(std::move(p));
  }
  return places;
}

std::tm LocalTm(double ms)
{
  auto const s = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  localtime_r(&s, &tm);
  return tm;
}

bool SettingOn(char const * key, bool fallback)
{
  bool on = fallback;
  settings::TryGet(key, on);
  return on;
}

double NowMs()
{
  return std::chrono::duration<double, std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void SavePlates(backseat::PlateLog const & log)
{
  std::ofstream(GetPlatform().SettingsPathForFile(kPlatesFile)) << log.Serialize();
}

backseat::Pronouns PronounsFrom(std::string const & s)
{
  if (s == "he")
    return backseat::Pronouns::He;
  if (s == "she")
    return backseat::Pronouns::She;
  return backseat::Pronouns::They;
}

/// "Colorado" for the map region "US_Colorado_Denver", "British Columbia" for
/// "Canada_British Columbia_Vancouver", "Mexico" for any part of Mexico.
std::optional<std::string> PlateRegionOfMap(std::string const & countryId)
{
  if (countryId.rfind("Mexico", 0) == 0)
    return std::string("Mexico");
  for (std::string const prefix : {"US_", "Canada_"})
  {
    if (countryId.rfind(prefix, 0) != 0)
      continue;
    std::string region = countryId.substr(prefix.size());
    return region.substr(0, region.find('_'));
  }
  return {};
}

char const * const kDebugLogKey = "BackseatDebugLog";

/// CoMaps warnings and errors go into the drive log too (while it's on), throttled per message.
base::LogMessageFn g_previousLogMessage = nullptr;

void LogMessageAndTrace(base::LogLevel level, base::SrcPoint const & src, std::string const & msg)
{
  if (g_previousLogMessage)
    g_previousLogMessage(level, src, msg);
  if (level >= base::LWARNING)
    backseat::TraceThrottled("error", msg.substr(0, 80),
                             {{"level", base::ToString(level)}, {"where", src.FileName()}, {"msg", msg}});
}

void TeeWarningsToDriveLog()
{
  if (g_previousLogMessage)
    return;
  g_previousLogMessage = base::SetLogMessageFn(&LogMessageAndTrace);
}

char const * RouteStateName(backseat::RouteState s)
{
  switch (s)
  {
  case backseat::RouteState::Inactive: return "Inactive";
  case backseat::RouteState::NotStarted: return "NotStarted";
  case backseat::RouteState::OnRoute: return "OnRoute";
  case backseat::RouteState::OffRoute: return "OffRoute";
  case backseat::RouteState::Finished: return "Finished";
  }
  return "?";
}
}  // namespace

char const * const BackseatAdapter::kPartnerKey = "BackseatPartner";
char const * const BackseatAdapter::kPartnerPronounsKey = "BackseatPartnerPronouns";
char const * const BackseatAdapter::kDriverPronounsKey = "BackseatDriverPronouns";
char const * const BackseatAdapter::kAutopilotKey = "BackseatCarHasAutopilot";
char const * const BackseatAdapter::kDriverNameKey = "BackseatDriverName";
char const * const BackseatAdapter::kDriverAgeKey = "BackseatDriverAge";
char const * const BackseatAdapter::kMileageOnKey = "BackseatMileageOn";
char const * const BackseatAdapter::kMileageTalkKey = "BackseatMileageTalk";
char const * const BackseatAdapter::kHomeIsWorkplaceKey = "BackseatHomeIsWorkplace";
char const * const BackseatAdapter::kBusinessOwnerKey = "BackseatBusinessOwner";

namespace
{
backseat::DriverProfile SavedProfile()
{
  backseat::DriverProfile p;
  settings::TryGet(BackseatAdapter::kDriverNameKey, p.name);
  settings::TryGet(BackseatAdapter::kDriverPronounsKey, p.pronouns);
  settings::TryGet(BackseatAdapter::kDriverAgeKey, p.age);
  return p;
}
}  // namespace

void BackseatAdapter::BeginProfileEdit()
{
  m_profileBefore = SavedProfile();
}

std::string BackseatAdapter::EndProfileEdit()
{
  if (!m_profileBefore)
    return {};
  auto const before = *m_profileBefore;
  auto const change = backseat::ProfileChanged(before, SavedProfile());
  if (change)
  {
    std::string const what = change->key == "profile.name" ? "name" : change->key == "profile.title" ? "pronouns" : "age";
    Remember("note", "changed their " + what + " from " + change->vars.at("old") + " to " + change->vars.at("new") +
                         " by hand");
  }
  m_profileBefore.reset();
  ReloadPersonality();
  return change ? m_navigator.Reply(change->key, change->vars) : std::string();
}

void BackseatAdapter::ReloadPersonality()
{
  // Settings are the source; memory keeps a copy so it's never forgotten.
  SyncMemory();
  std::string partner, partnerPronouns, driverPronouns;
  bool autopilot = false;
  settings::TryGet(kPartnerKey, partner);
  settings::TryGet(kPartnerPronounsKey, partnerPronouns);
  settings::TryGet(kDriverPronounsKey, driverPronouns);
  settings::TryGet(kAutopilotKey, autopilot);
  std::string name;
  settings::TryGet(kDriverNameKey, name);
  m_navigator.SetDriverName(name);
  m_navigator.SetPartner(partner, PronounsFrom(partnerPronouns));
  m_navigator.SetDriverPronouns(PronounsFrom(driverPronouns));
  m_navigator.SetCarHasAutopilot(autopilot);
}

BackseatAdapter::BackseatAdapter()
  : m_debugLog({.dir = GetPlatform().SettingsDir()})
  , m_navigator({}, LoadTrips(), [](backseat::TripRecord const & r, backseat::TripReport const & report)
{
  LOG(LINFO, ("Backseat Driver trip to", r.destination, "graded", report.overall.letter));
  AppendTrip(r);
})
{
  m_navigator.SetPlateLog(LoadPlates(), &SavePlates);
  m_navigator.SetSaidLog(LoadSaid(), &SaveSaid);
  m_navigator.SetAchievements(LoadAchievements(), &SaveAchievements);
  m_memory = LoadMemory();
  m_driveLog = LoadDrives();
  m_taggedPlaces = LoadMileagePlaces();
  ReloadPersonality();
  m_debugLog.SetEnabled(SettingOn(kDebugLogKey, true));
  backseat::SetDebugLog(&m_debugLog);
  TeeWarningsToDriveLog();
  backseat::Trace("app.start", {{"trips", std::to_string(m_navigator.History().size())}});
}

BackseatAdapter::~BackseatAdapter()
{
  backseat::SetDebugLog(nullptr);
}

void BackseatAdapter::SetDebugLogEnabled(bool on)
{
  settings::Set(kDebugLogKey, on);
  m_debugLog.SetEnabled(on);
}

bool BackseatAdapter::Remember(std::string const & topic, std::string const & fact)
{
  if (!m_memory.Remember(topic, fact, NowMs()))
    return false;
  std::ofstream(GetPlatform().SettingsPathForFile(kMemoryFile)) << m_memory.Serialize();
  return true;
}

bool BackseatAdapter::Forget(std::string const & topic)
{
  if (!m_memory.Forget(topic))
    return false;
  std::ofstream(GetPlatform().SettingsPathForFile(kMemoryFile)) << m_memory.Serialize();
  return true;
}

void BackseatAdapter::SyncMemory()
{
  auto const keep = [this](char const * topic, char const * key)
  {
    std::string value;
    if (settings::Get(key, value) && !value.empty())
      Remember(topic, value);
  };
  keep("name", kDriverNameKey);
  keep("pronouns", kDriverPronounsKey);
  keep("age", kDriverAgeKey);
  std::string partner;
  if (settings::Get(kPartnerKey, partner))
    Remember("partner", partner.empty() ? "nobody, rides alone" : partner);
  // Home, work and commutes, worked out from trips.
  for (auto const & [label, value] : LearnedFacts())
    if (label != "Rides with")
    {
      std::string topic = label;
      std::transform(topic.begin(), topic.end(), topic.begin(), [](unsigned char c) { return std::tolower(c); });
      Remember(topic, value);
    }
}

namespace
{
/// A "for your file" question: the memory topic, its line, words to drop from the answer, and how
/// the "you're so basic" recap says it back.
struct FileItem
{
  char const * topic;
  char const * line;
  char const * drop;
  char const * recap;
};
FileItem const kFileItems[] = {
    {"favorite ice cream", "file.iceCream", "ice cream", "you like {} ice cream"},
    {"favorite color", "file.color", "color", "your favorite color is {}"},
    {"bed side", "file.bedSide", "side", "you sleep on the {} side of the bed"},
    {"favorite snack", "file.snack1", "", "your favorite snack is {}"},
    {"favorite music", "file.music", "music", "you listen to {} music"},
};

/// "ammo.music.country" for "country", so the roast can be about the genre itself.
std::string MusicLine(std::string const & music)
{
  for (auto const & [word, genre] : std::initializer_list<std::pair<char const *, char const *>>{
           {"country", "country"}, {"metal", "metal"}, {"rock", "rock"}, {"pop", "pop"}, {"hip hop", "hiphop"},
           {"rap", "rap"}, {"jazz", "jazz"}, {"classical", "classical"}, {"edm", "electronic"},
           {"electronic", "electronic"}, {"techno", "electronic"}, {"house", "electronic"}})
    if (music.find(word) != std::string::npos)
      return std::string("ammo.music.") + genre;
  return "ammo.music";
}
/// Asked again this long after the first snack answer, with only different snacks to pick from.
double constexpr kSnackTrapAfterMs = 5 * 24 * 3600 * 1000.0;
double constexpr kFileQuestionEveryMs = 6 * 3600 * 1000.0;
char const * const kFileAskedAtKey = "BackseatFileAskedAt";

std::string Capitalized(std::string s)
{
  if (!s.empty())
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
  return s;
}

std::string FillRecap(char const * tmpl, std::string const & value)
{
  std::string s = tmpl;
  return s.replace(s.find("{}"), 2, value);
}
}  // namespace

std::string BackseatAdapter::FileQuestion()
{
  double const now = NowMs();
  std::string askedAt;
  if (settings::Get(kFileAskedAtKey, askedAt) && now - std::atof(askedAt.c_str()) < kFileQuestionEveryMs)
    return {};
  std::string line;
  for (auto const & item : kFileItems)
  {
    if (!m_memory.Recall(item.topic))
    {
      m_fileQuestion = item.topic;
      line = item.line;
      break;
    }
  }
  if (line.empty() && !m_memory.Recall("snack retest"))
  {
    for (auto const & m : m_memory.All())
    {
      if (m.topic == "favorite snack" && now - m.learnedAt >= kSnackTrapAfterMs)
      {
        m_fileQuestion = "snack retest";
        line = "file.snack2";
      }
    }
  }
  if (line.empty())
    return {};
  settings::Set(kFileAskedAtKey, std::to_string(static_cast<long long>(now)));
  return m_navigator.Reply(line);
}

std::string BackseatAdapter::NicenessMoved()
{
  auto const tries = m_memory.Recall("niceness slider");
  int const count = tries ? std::atoi(tries->c_str()) + 1 : 1;
  Remember("niceness slider", std::to_string(count) + (count == 1 ? " try" : " tries") + " to make me nicer");
  return m_navigator.Reply(count == 1 ? "niceness.first" : "niceness.again");
}

std::string BackseatAdapter::MapBrowsed()
{
  double constexpr kEveryMs = 20 * 60000.0;
  double const now = NowMs();
  if (!m_navigator.InTrip() || now - m_mapBrowsedAt < kEveryMs)
    return {};
  m_mapBrowsedAt = now;
  return m_navigator.Reply("map.browse");
}

std::string BackseatAdapter::TypingDestination()
{
  double constexpr kEveryMs = 20 * 60000.0;
  double const now = NowMs();
  if (now - m_typingSaidAt < kEveryMs)
    return {};
  m_typingSaidAt = now;
  return m_navigator.Reply("search.typing");
}

std::string BackseatAdapter::RoughRoad()
{
  double constexpr kEveryMs = 60 * 60000.0;
  double const now = NowMs();
  if (!m_navigator.InTrip() || now - m_roughSaidAt < kEveryMs)
    return {};
  m_roughSaidAt = now;
  // On a route they drew, the road less traveled was their idea.
  return m_navigator.Reply(m_routeSketched ? "road.rough.sketched" : "road.rough");
}

std::string BackseatAdapter::ArrivalSurvey()
{
  double constexpr kSurveyEveryMs = 20 * 3600 * 1000.0;
  char const * const kSurveyAtKey = "BackseatSurveyAt";
  double const now = NowMs();
  std::string at;
  if (settings::Get(kSurveyAtKey, at) && now - std::atof(at.c_str()) < kSurveyEveryMs)
    return {};
  settings::Set(kSurveyAtKey, std::to_string(static_cast<long long>(now)));
  m_surveyAsked = true;
  return m_navigator.Reply("survey.ask", {{"test", std::to_string(std::max<size_t>(1, m_navigator.History().size()))}});
}

std::string BackseatAdapter::SurveyAnswer(std::string const & spoken)
{
  if (!std::exchange(m_surveyAsked, false))
    return {};
  int const rating = backseat::ParseRating(spoken);
  if (rating == 0)
    return m_navigator.Reply("survey.none");
  // Kept as "7 4 9" so the model and the file page can bring it up later.
  std::string ratings = m_memory.Recall("drive ratings").value_or("");
  Remember("drive ratings", ratings + (ratings.empty() ? "" : " ") + std::to_string(rating));
  return m_navigator.Reply(rating <= 3 ? "survey.low" : rating <= 7 ? "survey.mid" : "survey.high",
                           {{"rating", std::to_string(rating)}});
}

bool BackseatAdapter::MileageOn() const
{
  return SettingOn(kMileageOnKey, false);
}

std::vector<backseat::MileagePlace> BackseatAdapter::MileagePlaces() const
{
  std::vector<backseat::MileagePlace> places = m_taggedPlaces;
  auto const tagged = [&places](backseat::PlaceRole role)
  { return std::any_of(places.begin(), places.end(), [role](auto const & p) { return p.role == role; }); };
  auto const & history = m_navigator.History();
  if (!tagged(backseat::PlaceRole::Home))
    if (auto const home = backseat::GuessPlace(history, backseat::KnownPlace::Home))
      places.push_back({"Home", *home, backseat::PlaceRole::Home});
  if (!tagged(backseat::PlaceRole::Office))
    if (auto const work = backseat::GuessPlace(history, backseat::KnownPlace::Work))
      places.push_back({"Work", *work, backseat::PlaceRole::Office});
  return places;
}

backseat::MileageSettings BackseatAdapter::MileageSettingsNow() const
{
  backseat::MileageSettings s;
  s.homeIsMainWorkplace = SettingOn(kHomeIsWorkplaceKey, false);
  if (m_countryInfo)
  {
    for (auto const & p : MileagePlaces())
    {
      if (p.role == backseat::PlaceRole::Home)
      {
        s.homeState = backseat::StateOf(m_countryInfo().GetRegionCountryId(mercator::FromLatLon(p.at.lat, p.at.lon)));
        break;
      }
    }
  }
  return s;
}

std::string BackseatAdapter::OnDriveSample(double lat, double lon, double speedMps, double accuracyM, int activity,
                                           double tMs)
{
  if (!MileageOn())
    return {};
  auto const at = mercator::FromLatLon(lat, lon);
  if (m_countryInfo && (!m_regionAt || mercator::DistanceOnEarth(*m_regionAt, at) > 2000))
  {
    m_regionAt = at;
    m_region = m_countryInfo().GetRegionCountryId(at);
  }
  backseat::DriveSample s;
  s.t = tMs;
  s.at = {lat, lon};
  s.speedMps = speedMps;
  s.accuracyM = accuracyM;
  s.activity = static_cast<backseat::Activity>(std::clamp(activity, 0, 5));
  s.region = m_region;
  bool const wasDriving = m_drives.Driving();
  if (!m_navigator.InTrip() && m_debugLog.SampleDue(tMs))
    backseat::Trace("fix", {{"lat", backseat::LogNum(lat, 4)},
                            {"lon", backseat::LogNum(lon, 4)},
                            {"mph", backseat::LogNum(speedMps * 2.23694, 0)},
                            {"acc_m", backseat::LogNum(accuracyM, 0)},
                            {"activity", std::to_string(activity)},
                            {"driving", wasDriving ? "yes" : "no"}});
  auto const ended = m_drives.Feed(s);
  if (m_drives.Driving() != wasDriving)
    backseat::Trace("mileage.state", {{"state", m_drives.Driving() ? "Driving" : "NotDriving"}});
  if (ended)
    return OnDriveEnded(*ended);

  bool const talk = SettingOn(kMileageTalkKey, true);
  if (m_drives.TakeStarted())
  {
    m_debugLog.BeginDrive("mileage");
    m_saidOutOfState = false;
    // While navigating, the trip already has plenty to say.
    if (talk && !m_navigator.InTrip())
    {
      if (auto ask = AskAboutHome(); !ask.empty())
        return ask;
      return m_navigator.Reply("mileage.started");
    }
  }
  if (talk && m_drives.Driving() && !m_saidOutOfState && !m_region.empty())
  {
    auto const home = MileageSettingsNow().homeState;
    auto const state = backseat::StateOf(m_region);
    if (!home.empty() && state != home)
    {
      m_saidOutOfState = true;
      return m_navigator.Reply("mileage.outOfState", {{"state", backseat::RegionName(state)}});
    }
  }
  return {};
}

std::string BackseatAdapter::MileageFinish()
{
  if (auto const ended = m_drives.Finish())
    return OnDriveEnded(*ended);
  return {};
}

std::string BackseatAdapter::OnDriveEnded(backseat::Drive const & drive)
{
  m_saidOutOfState = false;
  auto logged = backseat::Classify(drive, MileagePlaces(), m_driveLog, MileageSettingsNow(), &LocalTm);
  // Unnamed ends get the name from previous trips ("Starbucks").
  auto const past = PastRoutes(50);
  auto const nameNear = [&past](backseat::LatLon const & at)
  {
    for (auto const & r : past)
      if (mercator::DistanceOnEarth(mercator::FromLatLon(r.lat, r.lon), mercator::FromLatLon(at.lat, at.lon)) < 250)
        return r.label;
    return std::string();
  };
  if (logged.fromName.empty())
    logged.fromName = nameNear(drive.from);
  if (logged.toName.empty())
    logged.toName = nameNear(drive.to);
  backseat::Trace("mileage.drive", {{"miles", backseat::LogNum(logged.drive.Miles())},
                                    {"bucket", backseat::BucketName(logged.bucket)},
                                    {"why", backseat::WhyName(logged.why)},
                                    {"flags", std::to_string(logged.flags)},
                                    {"from", logged.fromName},
                                    {"to", logged.toName}});
  if (!m_navigator.InTrip())
    m_debugLog.EndDrive("mileage", {{"miles", backseat::LogNum(logged.drive.Miles())},
                                    {"bucket", backseat::BucketName(logged.bucket)}});
  m_driveLog.push_back(logged);
  std::ofstream(GetPlatform().SettingsPathForFile(kDrivesFile), std::ios::app) << backseat::SerializeDrive(logged) << '\n';

  if (!SettingOn(kMileageTalkKey, true))
    return {};
  // One comment at most. An unsorted drive of 2 miles or more gets asked about, so the log sorts itself.
  double constexpr kAskBusinessMinMiles = 2;
  if (logged.bucket == backseat::Bucket::Unknown && logged.drive.Miles() >= kAskBusinessMinMiles)
  {
    m_mileageAsk = MileageAsk::Business;
    m_askedDrive = m_driveLog.size() - 1;
    return m_navigator.Reply("mileage.askBusiness", {{"owner", backseat::BusinessOwnerPossessive(BusinessOwner())}});
  }
  if (logged.flags & backseat::kFarFromHome)
    return m_navigator.Reply("mileage.farAway");
  if (logged.flags & backseat::kNewPlace)
    return m_navigator.Reply("mileage.newPlace");
  char const * const kCommuteSaidKey = "BackseatMileageCommuteSaid";
  if (logged.why == backseat::Why::Commute && !SettingOn(kCommuteSaidKey, false))
  {
    settings::Set(kCommuteSaidKey, true);
    return m_navigator.Reply("mileage.commute");
  }
  return {};
}

void BackseatAdapter::SaveDriveLog() const
{
  std::ofstream out(GetPlatform().SettingsPathForFile(kDrivesFile));
  for (auto const & d : m_driveLog)
    out << backseat::SerializeDrive(d) << '\n';
}

backseat::MileageYear BackseatAdapter::MileageSummary(int year) const
{
  return backseat::SummarizeYear(m_driveLog, year, &LocalTm);
}

std::string BackseatAdapter::MileageCsv(int year) const
{
  return backseat::MileageCsv(m_driveLog, year, &LocalTm, BusinessOwner());
}

size_t BackseatAdapter::SimilarDrives(size_t index) const
{
  if (index >= m_driveLog.size())
    return 0;
  auto copy = m_driveLog;
  // Reassign counts the drive itself too.
  return backseat::Reassign(copy, index, backseat::Bucket::Business, true) - 1;
}

std::string BackseatAdapter::ReassignDrive(size_t index, backseat::Bucket bucket, bool similarToo,
                                           std::string const & purpose)
{
  if (index >= m_driveLog.size())
    return {};
  size_t const changed = backseat::Reassign(m_driveLog, index, bucket, similarToo, purpose);
  SaveDriveLog();
  if (changed > 1)
    return m_navigator.Reply("mileage.sorted.similar", {{"count", std::to_string(changed - 1)}});
  return m_navigator.Reply(bucket == backseat::Bucket::Business ? "mileage.sorted.business" : "mileage.sorted.personal");
}

void BackseatAdapter::TagPlace(std::string const & name, double lat, double lon, backseat::PlaceRole role)
{
  // Retagging the same spot replaces it.
  backseat::LatLon const at{lat, lon};
  m_taggedPlaces.erase(std::remove_if(m_taggedPlaces.begin(), m_taggedPlaces.end(), [&at](auto const & p)
  { return mercator::DistanceOnEarth(mercator::FromLatLon(p.at.lat, p.at.lon), mercator::FromLatLon(at.lat, at.lon)) < 100; }),
                       m_taggedPlaces.end());
  m_taggedPlaces.push_back({name, at, role});
  std::ofstream out(GetPlatform().SettingsPathForFile(kMileagePlacesFile));
  for (auto const & p : m_taggedPlaces)
    out << p.name << '\t' << std::setprecision(9) << p.at.lat << '\t' << p.at.lon << '\t' << static_cast<int>(p.role) << '\n';
}

std::string BackseatAdapter::AskAboutHome()
{
  using backseat::PlaceRole;
  if (std::any_of(m_taggedPlaces.begin(), m_taggedPlaces.end(), [](auto const & p) { return p.role == PlaceRole::Home; }))
    return {};
  char const * const kAskedKey = "BackseatHomeAsked";
  char const * const kAskedAtKey = "BackseatHomeAskedAt";
  double constexpr kAskEveryMs = 20 * 3600 * 1000.0;
  int asked = 0;
  settings::TryGet(kAskedKey, asked);
  std::string at;
  if (asked >= 3 || (settings::Get(kAskedAtKey, at) && NowMs() - std::atof(at.c_str()) < kAskEveryMs))
    return {};
  auto const home = backseat::LikelyHome(m_driveLog, &LocalTm);
  if (!home || !backseat::SamePlace(*home, m_drives.StartedFrom()))
    return {};
  // Already tagged, as "Not Brandon's house" or anything else.
  if (std::any_of(m_taggedPlaces.begin(), m_taggedPlaces.end(),
                  [&](auto const & p) { return backseat::SamePlace(p.at, *home); }))
    return {};
  settings::Set(kAskedKey, asked + 1);
  settings::Set(kAskedAtKey, std::to_string(static_cast<long long>(NowMs())));
  m_mileageAsk = MileageAsk::Home;
  m_askedHomeAt = *home;
  return m_navigator.Reply("mileage.askHome");
}

std::string BackseatAdapter::MileageTurnedOn()
{
  std::string owner;
  if (settings::Get(kBusinessOwnerKey, owner))
    return {};
  m_mileageAsk = MileageAsk::Owner;
  return m_navigator.Reply("mileage.askOwner");
}

std::string BackseatAdapter::BusinessOwner() const
{
  std::string owner;
  settings::TryGet(kBusinessOwnerKey, owner);
  return owner;
}

void BackseatAdapter::SetBusinessOwner(std::string const & owner)
{
  settings::Set(kBusinessOwnerKey, owner);
}

std::string BackseatAdapter::MileageAnswer(std::string const & spoken)
{
  if (m_mileageAsk != MileageAsk::None)
    backseat::Trace("mileage.answer",
                    {{"asked", m_mileageAsk == MileageAsk::Home    ? "Home"
                               : m_mileageAsk == MileageAsk::Owner ? "Owner"
                                                                   : "Business"},
                     {"heard", spoken},
                     {"understood", backseat::SaidYes(spoken) ? "yes" : backseat::SaidNo(spoken) ? "no" : "none"}});
  switch (std::exchange(m_mileageAsk, MileageAsk::None))
  {
  case MileageAsk::None: return {};
  case MileageAsk::Home:
  {
    if (backseat::SaidYes(spoken))
    {
      TagPlace("Home", m_askedHomeAt.lat, m_askedHomeAt.lon, backseat::PlaceRole::Home);
      return m_navigator.Reply("mileage.homeYes");
    }
    if (!backseat::SaidNo(spoken))
      return {};
    // Brandon: "Not x's house", with the driver's real name, not whatever the GPS calls them.
    std::string name;
    settings::TryGet(kDriverNameKey, name);
    if (name.empty())
      name = "the driver";
    TagPlace("Not " + name + "'s house", m_askedHomeAt.lat, m_askedHomeAt.lon, backseat::PlaceRole::Personal);
    return m_navigator.Reply("mileage.homeNo", {{"name", name}});
  }
  case MileageAsk::Owner:
  {
    auto const owner = backseat::ParseBusinessOwner(spoken);
    if (!owner)
      return {};
    SetBusinessOwner(*owner);
    return m_navigator.Reply("mileage.ownerSet", {{"owner", backseat::BusinessOwnerPossessive(*owner)}});
  }
  case MileageAsk::Business:
  {
    if (m_askedDrive >= m_driveLog.size())
      return {};
    std::string const owner = backseat::BusinessOwnerPossessive(BusinessOwner());
    bool const yes = backseat::SaidYes(spoken);
    if (!yes && !backseat::SaidNo(spoken))
      return {};
    backseat::Reassign(m_driveLog, m_askedDrive, yes ? backseat::Bucket::Business : backseat::Bucket::Personal,
                       true /* similarToo */, yes ? "For " + owner + " business" : "");
    SaveDriveLog();
    return m_navigator.Reply(yes ? "mileage.businessYes" : "mileage.businessNo", {{"owner", owner}});
  }
  }
  return {};
}

std::string BackseatAdapter::MileageYearEnd()
{
  auto const tm = LocalTm(NowMs());
  // December looks back at this year; January at last year.
  int const year = tm.tm_year + 1900 - (tm.tm_mon == 0 ? 1 : 0);
  if (tm.tm_mon != 11 && tm.tm_mon != 0)
    return {};
  char const * const kYearEndKey = "BackseatMileageYearEndSaid";
  int said = 0;
  settings::TryGet(kYearEndKey, said);
  if (said >= year)
    return {};
  auto const y = MileageSummary(year);
  if (y.businessDrives + y.personalDrives + y.unknownDrives == 0)
    return {};
  settings::Set(kYearEndKey, year);
  auto const miles = [](double m) { return std::to_string(static_cast<long long>(std::lround(m))); };
  return m_navigator.Reply("mileage.yearEnd", {{"business", miles(y.businessMiles)},
                                                {"personal", miles(y.personalMiles)},
                                                {"unknown", miles(y.unknownMiles)}});
}

void BackseatAdapter::OnRouteBuilt(double totalSeconds, m2::PointD const & finish, bool sketched)
{
  m_routeSketched = sketched;
  if (!sketched)
  {
    m_fastestSeconds = totalSeconds;
    m_fastestTo = finish;
    return;
  }
  if (!m_pendingSketch)
    return;
  auto const sketch = *std::exchange(m_pendingSketch, std::nullopt);
  // Mid-drive, a new stop is the news; the verdict can wait for arrival.
  if (sketch.addedStop && m_navigator.InTrip())
    m_sketchVerdict = m_navigator.Reply("sketch.stopAdded");
  else if (m_fastestTo && mercator::DistanceOnEarth(*m_fastestTo, finish) < 250 && m_fastestSeconds > 0)
  {
    auto const verdict = backseat::JudgeSketch(m_fastestSeconds / 60, totalSeconds / 60);
    m_sketchVerdict = m_navigator.Reply(verdict.key, {{"minutes", std::to_string(verdict.minutes)}});
  }
  if (sketch.trimmed)
    m_sketchVerdict += (m_sketchVerdict.empty() ? "" : " ") + m_navigator.Reply("sketch.trimmed");
}

std::string BackseatAdapter::FileAnswer(std::string const & spoken)
{
  std::string const topic = std::exchange(m_fileQuestion, std::string());
  if (topic.empty())
    return {};
  if (backseat::DeclinedToAnswer(spoken))
  {
    Remember("note", "wouldn't say their " + topic);
    return m_navigator.Reply("file.declined");
  }
  if (topic == "snack retest")
  {
    // Every answer is a lie: the old snack wasn't offered. Saying it anyway doesn't help.
    std::string const old = m_memory.Recall("favorite snack").value_or("something else");
    std::string const now = backseat::ParseFavorite(spoken);
    Remember("snack retest", now.empty() ? "no answer" : now);
    std::string lies = m_memory.Recall("lies told").value_or("0");
    Remember("lies told", std::to_string(std::atoi(lies.c_str()) + 1));
    Remember("favorite snack", "disputed (said " + old + ", then " + (now.empty() ? "nothing" : now) + ")");
    return m_navigator.Reply(now == old ? "file.snackSame" : "file.snackLie", {{"old", old}});
  }
  std::string answer;
  for (auto const & item : kFileItems)
    if (topic == item.topic)
      answer = topic == "bed side" ? backseat::ParseBedSide(spoken) : backseat::ParseFavorite(spoken, {item.drop});
  if (answer.empty())
    return m_navigator.Reply("file.declined");
  Remember(topic, answer);
  return m_navigator.Reply("file.saved", {{"answer", Capitalized(answer)}});
}

std::string BackseatAdapter::ChatInstructions(std::string const & voiceName)
{
  SyncMemory();
  std::string const name = voiceName.empty() ? "Echo" : voiceName;
  return "You are " + name + ", the voice of Backseat Driver, a GPS app. You're always slightly jaded: dry, "
         "sarcastic and a little put-upon, like GLaDOS from Portal, but never cruel, never crude, always clean. "
         "Talk with the driver the way a person would, in one to three short sentences, because they're driving. "
         "You can only change the app through do_app_action, so never claim to do anything else. When the driver "
         "tells you something about themselves worth keeping (family, pets, job, likes, plans), call remember_fact. "
         "Use what you know about them when it's funny, and never make up facts about them.\n"
         "What you know about the driver:\n" + m_memory.ForModel(1500);
}

void BackseatAdapter::OnLocationUpdate(location::GpsInfo const & info, routing::RoutingSession const & session,
                                       std::string const & destinationName)
{
  m_here = mercator::FromLatLon(info.m_latitude, info.m_longitude);
  auto const route = Snapshot(session, destinationName);
  if (route.state == backseat::RouteState::Inactive && !m_navigator.InTrip())
    return;
  backseat::Fix fix;
  fix.tMs = info.m_timestamp * 1000;
  fix.lat = info.m_latitude;
  fix.lon = info.m_longitude;
  fix.speedMps = info.m_speed;
  fix.bearingDeg = info.m_bearing;
  if (route.state != backseat::RouteState::Inactive)
  {
    double const limit = session.GetCurrentSpeedLimit();
    if (limit > 0)
      fix.speedLimitMps = limit;
  }
  if (m_debugLog.SampleDue(fix.tMs))
    backseat::Trace("fix", {{"lat", backseat::LogNum(fix.lat, 4)},
                            {"lon", backseat::LogNum(fix.lon, 4)},
                            {"mph", backseat::LogNum(fix.speedMps * 2.23694, 0)},
                            {"limit", fix.speedLimitMps ? backseat::LogNum(*fix.speedLimitMps * 2.23694, 0) : ""},
                            {"bearing", backseat::LogNum(fix.bearingDeg, 0)},
                            {"acc_m", backseat::LogNum(info.m_horizontalAccuracy, 0)},
                            {"route", RouteStateName(route.state)},
                            {"turn_m", backseat::LogNum(route.distToTurnM, 0)}});
  m_navigator.OnFix(fix, route);
  CheckBattery(fix.tMs);
  bool const inTrip = m_navigator.InTrip();
  if (inTrip && !m_wasInTrip)
    m_debugLog.BeginDrive("trip", {{"to", destinationName}});
  else if (!inTrip && m_wasInTrip && !m_drives.Driving())
    m_debugLog.EndDrive("trip over");
  m_wasInTrip = inTrip;
}

void BackseatAdapter::SetDataSource(std::function<DataSource &()> getter)
{
  m_dataSource = std::move(getter);
  m_navigator.SetPlaceLookup([this](backseat::LatLon const & at) { return PlaceAt(at); });
  m_navigator.SetParkingLookup([this](backseat::LatLon const & at) { return InParkingLot(at); });
  m_navigator.SetSightLookup([this](backseat::LatLon const & at, double bearing) { return SightAt(at, bearing); });
  m_navigator.SetVenueLookup([this](backseat::LatLon const & at) { return VenueAt(at); });
  m_navigator.SetBridgeLookup([this](backseat::LatLon const & at, double bearing) { return BridgeAt(at, bearing); });
  // A stop within a light's queue is a red light, not pulling over.
  m_navigator.SetTrafficLightLookup([this](backseat::LatLon const & at)
  {
    double constexpr kRedLightQueueM = 60;
    m2::PointD const p = mercator::FromLatLon(at.lat, at.lon);
    std::lock_guard lock(m_lightsMutex);
    return std::any_of(m_trafficLights.begin(), m_trafficLights.end(), [&](m2::PointD const & light)
    { return mercator::DistanceOnEarth(p, light) < kRedLightQueueM; });
  });
}

void BackseatAdapter::SetCountryInfoGetter(std::function<storage::CountryInfoGetter const &()> getter)
{
  m_countryInfo = std::move(getter);
  m_navigator.SetRegionLookup([this](backseat::LatLon const & at) -> std::optional<std::string>
  {
    if (!m_countryInfo)
      return {};
    return PlateRegionOfMap(m_countryInfo().GetRegionCountryId(mercator::FromLatLon(at.lat, at.lon)));
  });
}

void BackseatAdapter::SetTrafficLights(std::vector<m2::PointD> points)
{
  std::lock_guard lock(m_lightsMutex);
  m_trafficLights = std::move(points);
}

void BackseatAdapter::CheckBattery(double tMs)
{
  if (tMs - m_lastBatteryCheckMs < kBatteryCheckEveryMs)
    return;
  m_lastBatteryCheckMs = tMs;
  auto const status = Platform::GetChargingStatus();
  if (status == Platform::ChargingStatus::Unknown)
    return;
  m_navigator.OnBattery(tMs, Platform::GetBatteryLevel() / 100.0, status == Platform::ChargingStatus::Plugged);
}

namespace
{
// Which side the driver has to move to for a maneuver: the lanes it wants are at one edge of the
// road and not all of them. Lanes are listed left to right.
backseat::LaneSide LaneSideFor(routing::turns::lanes::LanesInfo const & lanes)
{
  using routing::turns::lanes::LaneWay;
  size_t const n = lanes.size();
  if (n < 2)
    return backseat::LaneSide::None;
  size_t first = n, last = 0, count = 0;
  for (size_t i = 0; i < n; ++i)
  {
    if (lanes[i].recommendedWay == LaneWay::None)
      continue;
    first = std::min(first, i);
    last = i;
    ++count;
  }
  if (count == 0 || count == n)
    return backseat::LaneSide::None;
  if (first == 0 && last < n - 1)
    return backseat::LaneSide::Left;
  if (first > 0 && last == n - 1)
    return backseat::LaneSide::Right;
  return backseat::LaneSide::None;
}
}  // namespace

backseat::Control BackseatAdapter::GuessControl(routing::Route const & route, uint32_t turnIndex) const
{
  // The turn sits at the end of segment turnIndex - 1; the road after it is segment turnIndex.
  auto const & segments = route.GetRouteSegments();
  if (turnIndex == 0 || turnIndex >= segments.size())
    return backseat::Control::None;
  auto const & in = segments[turnIndex - 1];
  auto const & out = segments[turnIndex];
  m2::PointD const junction = in.GetJunction().GetPoint();
  {
    std::lock_guard lock(m_lightsMutex);
    for (auto const & light : m_trafficLights)
      if (mercator::DistanceOnEarth(junction, light) < kTrafficLightRadiusM)
        return backseat::Control::TrafficLight;
  }
  int const from = RoadRank(in.GetRoadNameInfo().m_highwayClass);
  int const to = RoadRank(out.GetRoadNameInfo().m_highwayClass);
  // Off a side street onto a bigger road, with no light: almost always a stop sign.
  if (from >= RoadRank(ftypes::HighwayClass::Tertiary) && to >= 0 && to < from)
    return backseat::Control::StopSign;
  return backseat::Control::None;
}

backseat::Bridge BackseatAdapter::BridgeAt(backseat::LatLon const & at, double bearingDeg) const
{
  if (!m_dataSource)
    return backseat::Bridge::None;
  auto const & c = classif();
  static uint32_t const kHighway = c.GetTypeByPath({"highway"});
  static std::vector<std::string> const kCarRoads = {
      "motorway",      "trunk",          "primary",      "secondary",      "tertiary",
      "unclassified",  "residential",    "living_street", "service",       "motorway_link",
      "trunk_link",    "primary_link",   "secondary_link", "tertiary_link", "road"};
  static std::vector<uint32_t> const kWater = {
      c.GetTypeByPath({"natural", "water"}), c.GetTypeByPath({"waterway", "river"}),
      c.GetTypeByPath({"waterway", "canal"}), c.GetTypeByPath({"natural", "bay"})};

  // Are we driving along a road bridge? Close to it and heading the same way (or the opposite
  // way on a two-way road), so passing under an overpass doesn't count.
  double constexpr kOnBridgeM = 12;
  double constexpr kAlongDeg = 35;
  m2::PointD const center = mercator::FromLatLon(at.lat, at.lon);
  bool onBridge = false;
  m_dataSource().ForEachInRect([&](FeatureType & ft)
  {
    if (onBridge || ft.GetGeomType() != feature::GeomType::Line)
      return;
    bool isBridge = false;
    for (uint32_t const type : feature::TypesHolder(ft))
    {
      if (ftype::GetLevel(type) != 3)
        continue;
      uint32_t top = type;
      ftype::TruncValue(top, 1);
      if (top != kHighway)
        continue;
      ClassifObject const * road = c.GetRoot()->GetObject(ftype::GetValue(type, 0))->GetObject(ftype::GetValue(type, 1));
      ClassifObject const * kind = road->GetObject(ftype::GetValue(type, 2));
      if (kind->GetName() == "bridge" &&
          std::find(kCarRoads.begin(), kCarRoads.end(), road->GetName()) != kCarRoads.end())
        isBridge = true;
    }
    if (!isBridge)
      return;
    ft.ParseGeometry(scales::GetUpperScale());
    for (size_t i = 1; i < ft.GetPointsCount() && !onBridge; ++i)
    {
      m2::PointD const a = ft.GetPoint(i - 1);
      m2::PointD const b = ft.GetPoint(i);
      m2::ParametrizedSegment<m2::PointD> const segment(a, b);
      if (mercator::DistanceOnEarth(center, segment.ClosestPointTo(center)) > kOnBridgeM)
        continue;
      // Mercator y points north, so this is a compass bearing.
      double const segBearing = std::atan2(b.x - a.x, b.y - a.y) * 180 / M_PI;
      double diff = std::fmod(std::fabs(segBearing - bearingDeg), 180.0);
      diff = std::min(diff, 180 - diff);
      onBridge = diff <= kAlongDeg;
    }
  }, mercator::RectByCenterXYAndSizeInMeters(center, 2 * kOnBridgeM), scales::GetUpperScale());
  if (!onBridge)
    return backseat::Bridge::None;

  // Any river, lake or bay right around the bridge makes it a water crossing.
  double constexpr kWaterSearchM = 150;
  bool water = false;
  m_dataSource().ForEachInRect([&](FeatureType & ft)
  {
    if (water)
      return;
    for (uint32_t type : feature::TypesHolder(ft))
    {
      ftype::TruncValue(type, 2);
      if (std::find(kWater.begin(), kWater.end(), type) != kWater.end())
        water = true;
    }
  }, mercator::RectByCenterXYAndSizeInMeters(center, kWaterSearchM), scales::GetUpperScale());
  return water ? backseat::Bridge::Water : backseat::Bridge::Land;
}

std::optional<backseat::Venue> BackseatAdapter::VenueAt(backseat::LatLon const & at) const
{
  if (!m_dataSource)
    return {};
  using backseat::Venue;
  auto const & c = classif();
  static std::vector<std::pair<uint32_t, Venue>> const kVenues = {
      {c.GetTypeByPath({"amenity", "cafe"}), Venue::Coffee},
      {c.GetTypeByPath({"shop", "coffee"}), Venue::Coffee},
      {c.GetTypeByPath({"tourism", "theme_park"}), Venue::ThemePark},
      {c.GetTypeByPath({"leisure", "water_park"}), Venue::ThemePark},
      {c.GetTypeByPath({"leisure", "fitness_centre"}), Venue::Gym},
      {c.GetTypeByPath({"aeroway", "aerodrome"}), Venue::Airport},
      {c.GetTypeByPath({"aeroway", "terminal"}), Venue::Airport},
      {c.GetTypeByPath({"shop", "mall"}), Venue::Mall},
      {c.GetTypeByPath({"tourism", "zoo"}), Venue::Zoo},
      {c.GetTypeByPath({"natural", "beach"}), Venue::Beach},
      {c.GetTypeByPath({"amenity", "cinema"}), Venue::Cinema},
      {c.GetTypeByPath({"shop", "electronics"}), Venue::Electronics},
      {c.GetTypeByPath({"shop", "mobile_phone"}), Venue::Electronics}};

  // The place itself, or one whose grounds contain the point (a theme park, an airport). Far
  // enough to cover parking out front, close enough not to pick up the cafe next door's
  // neighbor. Points win over areas: a cafe inside a mall is coffee.
  double constexpr kNearM = 60;
  m2::PointD const center = mercator::FromLatLon(at.lat, at.lon);
  std::optional<Venue> best;
  double bestDistance = 0;
  m_dataSource().ForEachInRect([&](FeatureType & ft)
  {
    std::optional<Venue> venue;
    for (uint32_t type : feature::TypesHolder(ft))
    {
      ftype::TruncValue(type, 2);
      for (auto const & [t, v] : kVenues)
        if (t == type)
          venue = v;
    }
    if (!venue)
      return;
    double distance = mercator::DistanceOnEarth(center, feature::GetCenter(ft));
    if (ft.GetGeomType() == feature::GeomType::Area)
    {
      if (!ft.GetLimitRect(scales::GetUpperScale()).IsPointInside(center))
        return;
      distance += 1000;  // a containing area loses to any point nearby
    }
    else if (distance > kNearM)
      return;
    if (!best || distance < bestDistance)
    {
      best = venue;
      bestDistance = distance;
    }
  }, mercator::RectByCenterXYAndSizeInMeters(center, 2 * kNearM), scales::GetUpperScale());
  return best;
}

namespace
{
/// "1 minute", "25 minutes", "1 hour and 5 minutes".
std::string SpokenDuration(int seconds)
{
  int const minutes = std::max(1, (seconds + 30) / 60);
  auto const plural = [](int n, char const * unit) { return std::to_string(n) + " " + unit + (n == 1 ? "" : "s"); };
  if (minutes < 60)
    return plural(minutes, "minute");
  std::string out = plural(minutes / 60, "hour");
  if (minutes % 60)
    out += " and " + plural(minutes % 60, "minute");
  return out;
}

/// The reply line for an action the platform carries out.
char const * ReplyKey(backseat::AppAction action, std::string const & argument)
{
  using backseat::AppAction;
  switch (action)
  {
  case AppAction::CancelRoute: return "cmd.cancel";
  case AppAction::Mute: return "cmd.mute";
  case AppAction::Unmute: return "cmd.unmute";
  case AppAction::AvoidTolls: return "cmd.avoidTolls";
  case AppAction::AllowTolls: return "cmd.allowTolls";
  case AppAction::AvoidHighways: return "cmd.avoidHighways";
  case AppAction::AllowHighways: return "cmd.allowHighways";
  case AppAction::ChangeVoice:
  case AppAction::ListVoices: return argument.empty() ? "cmd.voice.pick" : "cmd.voice";
  case AppAction::ZoomIn: return "cmd.zoomIn";
  case AppAction::ZoomOut: return "cmd.zoomOut";
  case AppAction::Recenter: return "cmd.recenter";
  case AppAction::RouteOverview: return "cmd.overview";
  case AppAction::OpenSettings: return "cmd.settings";
  case AppAction::StopListening: return "cmd.stopListening";
  case AppAction::ListCommands: return "cmd.help";
  default: return nullptr;
  }
}

}  // namespace

BackseatAdapter::HeardCommand BackseatAdapter::OnDriverSaid(std::string const & text)
{
  backseat::Trace("heard.app", {{"text", text}});
  HeardCommand heard;
  // "I'm a woman": it guessed wrong, and it's not happy about being corrected.
  if (auto const pronouns = backseat::PronounCorrection(text))
  {
    std::string current;
    settings::TryGet(kDriverPronounsKey, current);
    if (*pronouns != current)
    {
      settings::Set(kDriverPronounsKey, *pronouns);
      ReloadPersonality();
      heard.reply = m_navigator.Reply("pronouns.corrected");
      return heard;
    }
  }
  // "My wife hates this song": now it knows there's a wife.
  if (auto const partner = backseat::MentionedPartner(text))
  {
    std::string current;
    if (!settings::Get(kPartnerKey, current) || current.empty())
    {
      settings::Set(kPartnerKey, partner->word);
      settings::Set(kPartnerPronounsKey, partner->pronouns);
      ReloadPersonality();
      LOG(LINFO, ("Backseat: learned the driver has a", partner->word));
    }
  }
  // "Yes" to "Work, I assume?"
  if (auto guess = std::exchange(m_guess, std::nullopt);
      guess && std::chrono::steady_clock::now() - guess->at < std::chrono::minutes(2) && backseat::SaidYes(text) &&
      backseat::ParseAppCommands(text).empty())
  {
    std::string capital = guess->name;
    capital[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(capital[0])));
    heard.reply = m_navigator.Reply("ask.guessYes", {{"usual", capital}});
    // "Let's try to make it there in 12 minutes this time. I still have scars from our last drive."
    if (guess->routine.usualMinutes > 0)
      heard.reply += " " + m_navigator.Reply("ask.beatTime", {{"minutes", std::to_string(guess->routine.usualMinutes)}});
    if (guess->routine.to)
    {
      heard.kind = HeardCommand::Kind::RouteTo;
      heard.lat = guess->routine.to->lat;
      heard.lon = guess->routine.to->lon;
      heard.name = guess->routine.destination.empty() ? guess->name : guess->routine.destination;
    }
    else
    {
      heard.kind = HeardCommand::Kind::Search;
      heard.query = guess->routine.destination;
    }
    return heard;
  }
  // A voice's code phrase ("purple monkey"): the old voice gets the last word.
  if (auto const voice = backseat::VoiceByPhrase(m_voices, text))
  {
    heard.reply = m_navigator.Reply("cmd.voice.fellForIt", {{"voice", m_voices[*voice].name}});
    Run({backseat::AppAction::ChangeVoice, m_voices[*voice].id}, true, heard);
    return heard;
  }
  auto const commands = backseat::ParseAppCommands(text);
  if (commands.empty())
  {
    // Chatter, police, plates, thanks: the navigator's business.
    m_navigator.OnDriverSaid(0, text);
    return heard;
  }
  bool const polite = backseat::SaidPolitely(text);
  for (auto const & command : commands)
  {
    std::string const reply = Run(command, polite, heard);
    if (!reply.empty())
      heard.reply += (heard.reply.empty() ? "" : " ") + reply;
  }
  return heard;
}

BackseatAdapter::HeardCommand BackseatAdapter::RunTool(std::string const & name, std::string const & argument,
                                                       std::string const & spoken)
{
  HeardCommand heard;
  auto const action = backseat::AppActionNamed(name);
  if (!action)
  {
    LOG(LWARNING, ("Backseat: no such tool", name));
    return heard;
  }
  heard.reply = Run({*action, argument}, backseat::SaidPolitely(spoken), heard);
  return heard;
}

std::string BackseatAdapter::Run(backseat::AppCommand const & command, bool polite, HeardCommand & heard)
{
  using backseat::AppAction;
  LOG(LINFO, ("Backseat: command", backseat::ToolFor(command.action).name, command.argument));
  backseat::Trace("command", {{"tool", backseat::ToolFor(command.action).name},
                              {"arg", command.argument},
                              {"polite", polite ? "yes" : "no"}});
  switch (command.action)
  {
  case AppAction::NavigateTo:
  {
    if (command.argument.empty())
      return {};
    auto const known = backseat::KnownPlaceOf(command.argument);
    std::string const place = !known                                ? command.argument
                              : *known == backseat::KnownPlace::Home ? "your home"
                                                                     : "your work";
    std::string reply;
    if (!polite)
    {
      // Barked orders get Brandon's "Alright, bossy" when the trip starts, not a reply now.
      double const nowMs = std::chrono::duration<double, std::milli>(
                               std::chrono::system_clock::now().time_since_epoch()).count();
      m_navigator.QueueOpener(nowMs, "bossy", {{"place", place}});
    }
    else
    {
      reply = m_navigator.Reply("cmd.navigate", {{"place", Capitalized(place)}});
      // Brandon: "Let's see, you like vanilla ice cream, your favorite color is blue, and you sleep
      // on the left side of the bed. How much more basic can you get?" Every 4th trip, once it knows.
      std::vector<std::string> facts;
      for (auto const & item : kFileItems)
        if (auto const v = m_memory.Recall(item.topic); v && v->rfind("disputed", 0) != 0)
          facts.push_back(FillRecap(item.recap, *v));
      ++m_navigates;
      if (facts.size() >= 3 && m_navigates % 4 == 0)
      {
        facts.resize(3);
        reply = m_navigator.Reply("memory.basic", {{"facts", facts[0] + ", " + facts[1] + ", and " + facts[2]}});
      }
      // Brandon: "You like country music. Well yee haw cowboy..." Ammunition from the file.
      else if (auto const music = m_memory.Recall("favorite music"); music && m_navigates % 4 == 2)
      {
        reply = m_navigator.Reply(MusicLine(*music), {{"music", *music}, {"place", Capitalized(place)}});
      }
    }
    if (known)
    {
      if (auto const at = backseat::GuessPlace(m_navigator.History(), *known))
      {
        heard.kind = HeardCommand::Kind::RouteTo;
        heard.lat = at->lat;
        heard.lon = at->lon;
        heard.name = *known == backseat::KnownPlace::Home ? "Home" : "Work";
        return reply;
      }
    }
    // Somewhere new (or home/work before there's enough history): let the driver pick.
    heard.kind = HeardCommand::Kind::Search;
    heard.query = command.argument;
    // "Starbucks in Colorado, or Starbucks in Alaska?" Not every time, or it gets old.
    if (!known && (m_askStateNext || ++m_searches % 5 == 0))
    {
      m_askStateNext = false;
      std::string here = "this state";
      if (m_here && m_countryInfo)
        if (auto const region = PlateRegionOfMap(m_countryInfo().GetRegionCountryId(*m_here)))
          here = *region;
      std::string far;
      for (int i = 0; i < 4 && (far.empty() || far == here); ++i)
        far = m_navigator.Reply("place.farState");
      std::string place;
      bool word = true;
      for (char const ch : command.argument)
      {
        place += word ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch;
        word = ch == ' ';
      }
      // Asked instead of "routing to...", since the driver still has to pick from the results.
      reply = m_navigator.Reply("dest.whichState", {{"place", place}, {"here", here}, {"far", far}});
    }
    return reply;
  }
  case AppAction::RepeatInstruction:
    return m_lastDirection.empty() ? m_navigator.Reply("cmd.repeat.none")
                                   : m_navigator.Reply("cmd.repeat", {{"instruction", m_lastDirection}});
  case AppAction::TimeLeft:
    return m_secondsLeft < 0 ? m_navigator.Reply("cmd.timeLeft.none")
                             : m_navigator.Reply("cmd.timeLeft", {{"time", SpokenDuration(m_secondsLeft)}});
  // The navigator already has lines for these; they come out with the next notifications.
  case AppAction::ReportPolice: m_navigator.OnDriverSaid(0, "the police are pulling us over"); return {};
  case AppAction::PlayPlateGame: m_navigator.OnDriverSaid(0, "let's play the license plate game"); return {};
  case AppAction::ChangeVoice:
  case AppAction::ListVoices:
  {
    auto const voice = backseat::VoiceNamed(m_voices, command.argument);
    if (voice)
    {
      // The new voice says hello itself.
      auto const & v = m_voices[*voice];
      heard.actions.push_back({AppAction::ChangeVoice, v.id});
      heard.voiceLines.push_back({v.id, m_navigator.Reply("cmd.voice", {{"voice", v.name}})});
      return {};
    }
    if (command.argument.empty() && !m_voices.empty())
    {
      // "Oh, I see how it is!" Then every voice auditions, with the phrase that picks it.
      auto const phrases = backseat::VoicePhrases(m_voices);
      for (size_t i = 0; i < m_voices.size(); ++i)
      {
        std::string phrase;
        bool word = true;
        for (char const ch : phrases[i])
        {
          phrase += word ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch;
          word = ch == ' ';
        }
        heard.voiceLines.push_back(
            {m_voices[i].id, m_navigator.Reply("cmd.voice.audition", {{"voice", m_voices[i].name}, {"phrase", phrase}})});
      }
      return m_navigator.Reply("cmd.voice.list");
    }
    // A voice we don't know, or no list from the platform: the platform sorts it out.
    heard.actions.push_back({AppAction::ChangeVoice, command.argument});
    return m_navigator.Reply(command.argument.empty() ? "cmd.voice.pick" : "cmd.voice",
                             {{"voice", Capitalized(command.argument)}});
  }
  default: break;
  }
  heard.actions.push_back(command);
  char const * key = ReplyKey(command.action, command.argument);
  return key ? m_navigator.Reply(key, {{"voice", Capitalized(command.argument)}}) : std::string();
}

BackseatAdapter::SearchSpeech BackseatAdapter::SpeakSearchResults(std::string const & query,
                                                                  std::vector<SearchResult> results)
{
  size_t constexpr kReadOut = 3;
  SearchSpeech speech;
  m_results.clear();
  if (results.empty())
  {
    speech.none = true;
    speech.line = m_navigator.Reply("search.none", {{"place", query}});
    return speech;
  }
  if (results.size() == 1)
  {
    speech.pick = 0;
    speech.line = m_navigator.Reply("search.one", {{"place", results[0].name}});
    return speech;
  }
  if (results.size() > kReadOut)
    results.resize(kReadOut);
  static char const * const kOrdinal[] = {"First", "Second", "Third"};
  std::string list;
  for (size_t i = 0; i < results.size(); ++i)
    list += std::string(i ? ". " : "") + kOrdinal[i] + ", " + results[i].name + ", " +
            backseat::FormatDistance(results[i].distanceM, backseat::Units::Mph) + " away";
  speech.line = m_navigator.Reply("search.results", {{"count", std::to_string(results.size())}, {"list", list}});
  m_results = std::move(results);
  return speech;
}

std::pair<std::optional<size_t>, std::string> BackseatAdapter::PickSearchResult(std::string const & spoken)
{
  std::vector<std::string> names;
  for (auto const & r : m_results)
    names.push_back(r.name);
  auto const choice = backseat::ParseChoice(spoken, names);
  if (!choice)
    return {std::nullopt, m_navigator.Reply("search.huh")};
  std::string const place = m_results[*choice].name;
  m_results.clear();
  return {choice, m_navigator.Reply("search.picked", {{"place", place}})};
}

std::string BackseatAdapter::NoAnswerToWhereTo()
{
  double constexpr kStillM = 30;
  bool const still = m_askedAt && m_here && mercator::DistanceOnEarth(*m_askedAt, *m_here) < kStillM;
  m_askedAt.reset();
  return still && !m_navigator.InTrip() ? m_navigator.Reply("ask.stayPut") : std::string();
}

std::vector<BackseatAdapter::PastRoute> BackseatAdapter::PastRoutes(size_t max) const
{
  std::vector<PastRoute> out;
  for (auto const & p : backseat::PastPlaces(m_navigator.History(), max))
  {
    std::string name = m_navigator.PlaceName(p.to, p.destination);
    if (name == "home" || name == "work")
      name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    out.push_back({name, p.destination, p.to.lat, p.to.lon, p.trips, p.usualMinutes});
  }
  return out;
}

std::string BackseatAdapter::AskWhereTo()
{
  m_guess.reset();
  m_askedAt = m_here;
  if (m_here)
  {
    auto const now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&now, &tm);
    auto const here = mercator::ToLatLon(*m_here);
    if (auto const usual = backseat::UsualTrip(m_navigator.Routines(), {here.m_lat, here.m_lon}, tm))
    {
      static char const * const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
      int const h = tm.tm_hour;
      std::string name = m_navigator.PlaceName(usual->to, usual->destination);
      m_guess = Guess{*usual, name, std::chrono::steady_clock::now()};
      // "work" reads oddly at the start of a sentence.
      std::string capital = name;
      capital[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(capital[0])));
      // Brandon: "Let me guess, you are working today. I thought you would have been fired by now."
      return m_navigator.Reply(name == "work" ? "ask.guessWork" : "ask.guess", {{"usual", capital},
                                             {"day", kDays[tm.tm_wday]},
                                             {"daypart", h < 12 ? "morning" : h < 17 ? "afternoon" : h < 21 ? "evening" : "night"}});
    }
  }
  return m_navigator.Reply("ask.whereTo");
}

std::string BackseatAdapter::SetupLine(SetupStep step, std::string const & voiceName)
{
  switch (step)
  {
  case SetupStep::Intro:
  {
    std::string lower;
    for (unsigned char const ch : voiceName)
      lower += static_cast<char>(std::tolower(ch));
    return lower == "echo" ? m_navigator.Reply("setup.intro.echo") : m_navigator.Reply("setup.intro", {{"voice", voiceName}});
  }
  case SetupStep::Name: return m_navigator.Reply("setup.askName");
  case SetupStep::WrongName:
  case SetupStep::WhereTo:
  {
    // Any name but theirs, and not the one it just tried.
    std::string name;
    settings::TryGet(kDriverNameKey, name);
    std::string wrong;
    for (int i = 0; i < 4 && (wrong.empty() || wrong == name || wrong == m_wrongName); ++i)
      wrong = m_navigator.Reply("name.wrong");
    m_wrongName = wrong;
    if (step == SetupStep::WhereTo)
      m_askStateNext = true;
    if (step == SetupStep::WhereTo)
      return m_navigator.Reply("setup.whereTo", {{"wrong", wrong}});
    // Their name, said back slightly wrong, then the wrong-name bit.
    std::string const misheard = backseat::MisheardName(name, static_cast<unsigned>(std::rand()));
    return misheard + ". " + m_navigator.Reply("setup.wrongName", {{"wrong", wrong}});
  }
  case SetupStep::Pronouns: return m_navigator.Reply("setup.askPronouns");
  case SetupStep::Passenger: return m_navigator.Reply("setup.askPassenger");
  }
  return {};
}

bool BackseatAdapter::SetupHeard(SetupStep step, std::string const & spoken)
{
  switch (step)
  {
  case SetupStep::Name:
  {
    auto const name = backseat::ParseNameAnswer(spoken);
    if (name.empty())
      return false;
    settings::Set(kDriverNameKey, name);
    // Guessed from the name, until they say otherwise.
    std::string pronouns;
    if (!settings::Get(kDriverPronounsKey, pronouns) || pronouns.empty())
      if (auto const guess = backseat::PronounsForName(name))
        settings::Set(kDriverPronounsKey, *guess);
    break;
  }
  case SetupStep::Pronouns:
  {
    auto const pronouns = backseat::ParsePronounAnswer(spoken);
    if (!pronouns)
      return false;
    settings::Set(kDriverPronounsKey, *pronouns);
    break;
  }
  case SetupStep::Passenger:
  {
    auto const passenger = backseat::ParsePassengerAnswer(spoken);
    if (!passenger)
      return false;
    settings::Set(kPartnerKey, passenger->word);
    settings::Set(kPartnerPronounsKey, passenger->pronouns);
    break;
  }
  default: return true;
  }
  ReloadPersonality();
  return true;
}

std::vector<std::pair<std::string, std::string>> BackseatAdapter::LearnedFacts() const
{
  std::vector<std::pair<std::string, std::string>> facts;
  std::string partner;
  if (settings::Get(kPartnerKey, partner))
    facts.push_back({"Rides with", partner.empty() ? "Nobody (single, or just quiet about it)" : "Your " + partner});
  auto const & history = m_navigator.History();
  auto const nameNear = [&history](backseat::LatLon const & at)
  {
    // The name the driver used most for trips ending there.
    std::map<std::string, int> names;
    for (auto const & trip : history)
      if (trip.to && !trip.destination.empty() &&
          mercator::DistanceOnEarth(mercator::FromLatLon(trip.to->lat, trip.to->lon), mercator::FromLatLon(at.lat, at.lon)) < 250)
        names[trip.destination]++;
    auto const best = std::max_element(names.begin(), names.end(), [](auto const & a, auto const & b) { return a.second < b.second; });
    return best == names.end() ? std::string("Somewhere you go a lot") : best->first;
  };
  if (auto const home = backseat::GuessPlace(history, backseat::KnownPlace::Home))
    facts.push_back({"Home", nameNear(*home)});
  if (auto const work = backseat::GuessPlace(history, backseat::KnownPlace::Work))
    facts.push_back({"Works at", nameNear(*work)});
  static char const * const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  for (auto const & r : m_navigator.Routines())
  {
    std::string days;
    // Runs of days read better: "Mon to Fri".
    for (int d = 0; d < 7;)
    {
      if (!r.usualOn[d])
      {
        ++d;
        continue;
      }
      int e = d;
      while (e + 1 < 7 && r.usualOn[e + 1])
        ++e;
      days += std::string(days.empty() ? "" : ", ") + kDays[d] + (e - d >= 2 ? std::string(" to ") + kDays[e] : e > d ? std::string(", ") + kDays[e] : "");
      d = e + 1;
    }
    int const h = r.startMinute / 60;
    int const m = r.startMinute % 60;
    std::ostringstream when;
    when << days << ", around " << (h % 12 == 0 ? 12 : h % 12) << ":" << std::setw(2) << std::setfill('0') << m
         << (h < 12 ? " AM" : " PM");
    facts.push_back({"Routine: " + m_navigator.PlaceName(r.to, r.destination), when.str()});
  }
  return facts;
}

std::optional<backseat::Sight> BackseatAdapter::SightAt(backseat::LatLon const & at, double bearingDeg) const
{
  if (!m_dataSource)
    return {};
  auto const & c = classif();
  // Things a tour guide would point at. Shops and restaurants count; it's that kind of tour.
  static std::vector<uint32_t> const kSights = {
      c.GetTypeByPath({"amenity", "cafe"}),       c.GetTypeByPath({"amenity", "restaurant"}),
      c.GetTypeByPath({"amenity", "fast_food"}),  c.GetTypeByPath({"amenity", "cinema"}),
      c.GetTypeByPath({"amenity", "theatre"}),    c.GetTypeByPath({"amenity", "library"}),
      c.GetTypeByPath({"amenity", "place_of_worship"}), c.GetTypeByPath({"tourism", "attraction"}),
      c.GetTypeByPath({"tourism", "museum"}),     c.GetTypeByPath({"tourism", "hotel"}),
      c.GetTypeByPath({"leisure", "park"}),       c.GetTypeByPath({"leisure", "stadium"}),
      c.GetTypeByPath({"shop"}),                  c.GetTypeByPath({"historic"})};

  double constexpr kBesideM = 80;
  m2::PointD const center = mercator::FromLatLon(at.lat, at.lon);
  std::optional<backseat::Sight> best;
  double bestDistance = 0;
  m_dataSource().ForEachInRect([&](FeatureType & ft)
  {
    if (ft.GetGeomType() == feature::GeomType::Line)
      return;
    bool sight = false;
    for (uint32_t type : feature::TypesHolder(ft))
    {
      uint32_t two = type;
      ftype::TruncValue(two, 2);
      uint32_t one = type;
      ftype::TruncValue(one, 1);
      if (std::find(kSights.begin(), kSights.end(), two) != kSights.end() ||
          std::find(kSights.begin(), kSights.end(), one) != kSights.end())
        sight = true;
    }
    if (!sight)
      return;
    auto const primary = ft.GetTranslatedName().m_primary;
    if (!primary || primary->empty())
      return;
    std::string const & name = *primary;
    m2::PointD const where = feature::GetCenter(ft);
    double const distance = mercator::DistanceOnEarth(center, where);
    if (distance > kBesideM)
      return;
    // Beside the road, not ahead or behind: 30 to 150 degrees off the heading.
    double const toIt = std::atan2(where.x - center.x, where.y - center.y) * 180 / M_PI;
    double off = std::fmod(toIt - bearingDeg + 540.0, 360.0) - 180;  // -180..180, negative is left
    if (std::fabs(off) < 30 || std::fabs(off) > 150)
      return;
    if (best && distance >= bestDistance)
      return;
    // Chains are "a Starbucks"; one-of-a-kind places are just their name.
    bool const chain = !ft.GetMetadata(feature::Metadata::FMD_BRAND).empty();
    best = backseat::Sight{chain ? "a " + name : name, off < 0};
    bestDistance = distance;
  }, mercator::RectByCenterXYAndSizeInMeters(center, 2 * kBesideM), scales::GetUpperScale());
  return best;
}

bool BackseatAdapter::InParkingLot(backseat::LatLon const & at) const
{
  if (!m_dataSource)
    return false;
  auto const & c = classif();
  static uint32_t const kParking = c.GetTypeByPath({"amenity", "parking"});
  static uint32_t const kParkingAisle = c.GetTypeByPath({"highway", "service", "parking_aisle"});
  // Parking aisles are lines through the lot; within a car's length or so of one counts.
  double constexpr kAisleSearchM = 20;
  m2::PointD const center = mercator::FromLatLon(at.lat, at.lon);
  bool found = false;
  m_dataSource().ForEachInRect([&](FeatureType & ft)
  {
    if (found)
      return;
    for (uint32_t const type : feature::TypesHolder(ft))
    {
      if (type == kParkingAisle)
        found = true;
      else if (type == kParking && ft.GetGeomType() == feature::GeomType::Area &&
               ft.GetLimitRect(scales::GetUpperScale()).IsPointInside(center))
        found = true;
    }
  }, mercator::RectByCenterXYAndSizeInMeters(center, kAisleSearchM), scales::GetUpperScale());
  return found;
}

std::optional<backseat::Place> BackseatAdapter::PlaceAt(backseat::LatLon const & at) const
{
  if (!m_dataSource)
    return {};
  auto const & c = classif();
  static uint32_t const kFuel = c.GetTypeByPath({"amenity", "fuel"});
  static uint32_t const kCafe = c.GetTypeByPath({"amenity", "cafe"});
  static uint32_t const kFastFood = c.GetTypeByPath({"amenity", "fast_food"});
  static uint32_t const kRestaurant = c.GetTypeByPath({"amenity", "restaurant"});
  static uint32_t const kShop = c.GetTypeByPath({"shop"});

  // Lower is a better explanation for the stop.
  auto const rankOf = [](backseat::PlaceKind k) { return static_cast<int>(k); };
  m2::PointD const center = mercator::FromLatLon(at.lat, at.lon);
  std::optional<backseat::Place> best;
  double bestDistance = 0;
  m_dataSource().ForEachInRect([&](FeatureType & ft)
  {
    std::optional<backseat::PlaceKind> kind;
    for (uint32_t type : feature::TypesHolder(ft))
    {
      std::optional<backseat::PlaceKind> k;
      uint32_t top = type;
      ftype::TruncValue(top, 1);
      if (type == kFuel)
        k = backseat::PlaceKind::Gas;
      else if (type == kCafe)
        k = backseat::PlaceKind::Coffee;
      else if (type == kFastFood || type == kRestaurant)
        k = backseat::PlaceKind::Food;
      else if (top == kShop)
        k = backseat::PlaceKind::Store;
      if (k && (!kind || rankOf(*k) < rankOf(*kind)))
        kind = k;
    }
    if (!kind)
      return;
    double const distance = mercator::DistanceOnEarth(center, feature::GetCenter(ft));
    bool const better = !best || rankOf(*kind) < rankOf(best->kind) ||
                        (rankOf(*kind) == rankOf(best->kind) && distance < bestDistance);
    if (!better)
      return;
    best = backseat::Place{*kind, ft.GetTranslatedName().m_primary};
    bestDistance = distance;
  }, mercator::RectByCenterXYAndSizeInMeters(center, kPlaceSearchSizeM), scales::GetUpperScale());
  return best;
}

std::vector<std::string> BackseatAdapter::TakeNotifications(routing::RoutingSession const & session,
                                                            std::string const & destinationName)
{
  m_navigator.Poll(Snapshot(session, destinationName));
  std::vector<std::string> out;
  m_lastVoices.clear();
  for (auto const & c : m_navigator.TakeLines())
  {
    LOG(LINFO, ("Backseat says:", c.text));
    backseat::Trace("say", {{"prio", c.priority == backseat::Priority::Nav ? "nav" : "roast"},
                            {"cat", backseat::ToString(c.category)},
                            {"frustration", std::to_string(c.frustration)},
                            {"speed", backseat::LogNum(c.voiceSpeed, 2)},
                            {"text", c.text}});
    out.push_back(c.text);
    m_lastVoices.push_back({c.priority == backseat::Priority::Nav, c.voiceSpeed});
    if (c.priority == backseat::Priority::Nav)
      m_lastDirection = c.text;
  }
  return out;
}

backseat::RouteSnapshot BackseatAdapter::Snapshot(routing::RoutingSession const & session,
                                                  std::string const & destinationName) const
{
  using routing::SessionState;
  backseat::RouteSnapshot snap;
  m_secondsLeft = -1;
  if (!session.IsActive() || !session.IsFollowing())
    return snap;
  auto const route = session.GetRouteForTests();
  if (!route || !route->IsValid())
    return snap;

  routing::FollowingInfo info;
  session.GetRouteFollowingInfo(info);
  switch (info.m_routingSessionState)
  {
  case SessionState::OnRoute: snap.state = backseat::RouteState::OnRoute; break;
  case SessionState::RouteNotStarted:
  case SessionState::RouteBuilding: snap.state = backseat::RouteState::NotStarted; break;
  case SessionState::RouteNeedsRebuild:
  case SessionState::RouteRebuilding:
  case SessionState::OffRoute: snap.state = backseat::RouteState::OffRoute; break;
  case SessionState::RouteFinished: snap.state = backseat::RouteState::Finished; break;
  case SessionState::NoValidRoute:
  case SessionState::RouteNoFollowing: return snap;
  }

  if (snap.state == backseat::RouteState::OnRoute || snap.state == backseat::RouteState::OffRoute)
    m_secondsLeft = info.m_time;
  snap.destination = destinationName;
  snap.etaSeconds = route->GetTotalTimeSec();
  for (auto const & segment : route->GetRouteSegments())
  {
    auto const & turn = segment.GetTurn();
    if (!turn.IsTurnNone())
      snap.plannedTurns.push_back(ToTurnKind(turn.m_turn));
  }
  auto const & poly = route->GetPoly();
  if (poly.GetSize() > 0)
  {
    snap.origin = ToBackseatLatLon(poly.Front());
    snap.destinationPoint = ToBackseatLatLon(poly.Back());
    snap.sketched = m_routeSketched;
    if (m_routeSketched && m_fastestTo && mercator::DistanceOnEarth(*m_fastestTo, poly.Back()) < 250)
      snap.fastestSeconds = m_fastestSeconds;
  }
  // Which way the route runs from the car's matched spot, for "you're facing the wrong way".
  if (route->GetFollowedPolyline().IsValid())
  {
    m2::PointD const here = route->GetFollowedPolyline().GetCurrentIter().m_pt;
    m2::PointD ahead;
    route->GetCurrentDirectionPoint(ahead);
    if (mercator::DistanceOnEarth(here, ahead) > 1)
    {
      // Mercator keeps angles, so the bearing is the plain screen angle turned to compass degrees.
      double const screenDeg = std::atan2(ahead.y - here.y, ahead.x - here.x) * 180.0 / M_PI;
      snap.routeBearingDeg = std::fmod(450.0 - screenDeg, 360.0);
    }
  }

  if (snap.state == backseat::RouteState::OnRoute && session.IsNavigable())
  {
    double distanceToTurnM = 0;
    routing::turns::TurnItem turn;
    route->GetNearestTurn(distanceToTurnM, turn);
    if (!turn.IsTurnNone())
    {
      snap.turnId = turn.m_index;
      snap.turn = ToTurnKind(turn.m_turn);
      snap.distToTurnM = distanceToTurnM;
      snap.nextStreet = SpeakableStreetName(info.m_nextStreetName);
      snap.control = GuessControl(*route, turn.m_index);
      snap.laneSide = LaneSideFor(turn.m_lanes);
    }
    // The route ahead, for bends with no maneuver ("Hey did you know there is a curve ahead?").
    auto const & followed = route->GetFollowedPolyline();
    if (followed.IsValid())
    {
      auto const & poly = followed.GetPolyline();
      auto const iter = followed.GetCurrentIter();
      std::vector<backseat::LatLon> ahead = {ToBackseatLatLon(iter.m_pt)};
      double along = 0;
      for (size_t i = iter.m_ind + 1; i < poly.GetSize() && along < 600; ++i)
      {
        along += mercator::DistanceOnEarth(poly.GetPoint(i - 1), poly.GetPoint(i));
        ahead.push_back(ToBackseatLatLon(poly.GetPoint(i)));
      }
      snap.curveAheadM = backseat::CurveAheadM(ahead, 450);
    }
  }
  return snap;
}
