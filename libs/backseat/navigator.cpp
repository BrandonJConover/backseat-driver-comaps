#include "backseat/navigator.hpp"

#include "backseat/debug_log.hpp"
#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/route_sketch.hpp"
#include "backseat/rundown.hpp"
#include "backseat/units.hpp"
#include "backseat/voice_command.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <random>

namespace backseat
{
namespace
{
/// Off the route this close to the maneuver means the driver missed it, not wandered off.
double constexpr kMissedTurnWithinM = 80;
// Lane advice window before the maneuver.
double constexpr kLaneSayMinM = 150;
/// The trip-time prediction comes this long after the trip starts.
double constexpr kPredictionDelayMs = 25000;
double constexpr kLaneAfterDirectionMs = 6000;
double constexpr kLaneSayMaxM = 1500;
/// Slower than this counts as stopped.
double constexpr kStoppedMps = 0.5;
/// Faster than this means driving again, not walking around with the phone.
double constexpr kResumedMps = 4;
/// Stopped this long away from the destination is a stop the driver chose (gas, food...).
double constexpr kUnplannedStopAfterMs = 180000;
/// Stops this close to the destination are just arriving.
double constexpr kNearDestinationM = 250;
/// If the GPS position never strays this far from where the car stopped, the phone stayed
/// in the car. Carried around a gas station, a phone's fix wanders well past it.
double constexpr kPhoneWanderedM = 30;
/// Stopped this long at the roadside, with nothing around to explain it, is pulling over.
/// Long enough to sit out most stop-and-go; red lights are ruled out by the light lookup.
double constexpr kPullOverAfterMs = 45000;
/// A pull-over this soon after serious speeding is probably the police.
double constexpr kPoliceAfterSpeedingMs = 120000;
/// "Serious" speeding: this far over the limit (~10 mph).
double constexpr kSeriousOverMps = 4.5;
/// Sharp curve: sideways acceleration (speed x turn rate) at least this, at highway-ish
/// speed, over two fixes in a row. ~0.35 g is well past a comfortable curve.
double constexpr kScaryLateralMps2 = 3.5;
double constexpr kScaryCurveMinMps = 11;  // ~25 mph; slower is just a turn
double constexpr kCurveYelpEveryMs = 90000;
// "There is a curve ahead": this far out, at road speed, now and then.
double constexpr kCurveAheadMinM = 120;
double constexpr kCurveAheadMaxM = 400;
double constexpr kCurveAheadEveryMs = 8 * 60000;
// Speeding up in a bend: gentler than a scary curve, but the speed is climbing.
double constexpr kCurveGasLateralMps2 = 2;
double constexpr kCurveGasAccelMps2 = 0.7;
double constexpr kCurveGasEveryMs = 5 * 60000;
/// Regular bridges are everywhere in a city (every overpass); water is always worth a scream.
double constexpr kLandBridgeYelpEveryMs = 600000;
double constexpr kWaterBridgeYelpEveryMs = 60000;
/// Lost this many times in one trip, the GPS suggests a car that drives itself.
int constexpr kAutopilotAfterReroutes = 3;
/// The second time lost on a trip, the GPS wishes the driver's partner were driving.
int constexpr kPartnerAfterReroutes = 2;
int constexpr kBegAfterReroutes = 4;
/// Tour guide: look around this often, point something out at most this often, and only
/// with no maneuver coming up for a while.
double constexpr kSightCheckEveryMs = 8000;
double constexpr kSightEveryMs = 240000;
double constexpr kSightQuietM = 500;
/// A sight with a position is said when the car is this many seconds from passing it (the time to
/// start speaking and get to its name), but never more than kSightSayMaxM out...
double constexpr kSightLeadS = 3;
double constexpr kSightSayMinM = 30;
double constexpr kSightSayMaxM = 120;
/// ...and only if it's really beside the road ahead: farther off to the side is behind buildings.
double constexpr kSightMaxAheadM = 450;
double constexpr kSightMaxSideM = 60;
/// Dropped once passed, or if the car never gets to it.
double constexpr kSightPassedM = -15;
double constexpr kSightGiveUpMs = 90000;
/// Nothing said for this long (a random stretch in between) on an easy part of the drive: the GPS
/// fills the silence.
double constexpr kIdleMinMs = 150000;
double constexpr kIdleMaxMs = 300000;
double constexpr kIdleQuietM = 600;
/// SaidLog counter: trips started, finished or not.
char const * const kTripsStartedKey = "_trips.started";
/// SaidLog values: the trip under way (destination, last seen in s since the epoch), so a relaunch in
/// the middle of it (a crash, or iOS closing the app in the background) picks it back up instead
/// of opening a brand new trip with the greeting and rundown all over again.
char const * const kActiveTripToKey = "_trip.active.to";
char const * const kActiveTripAtKey = "_trip.active.at";
double constexpr kResumeWithinS = 15 * 60;
double constexpr kActiveTripStampEveryS = 60;
/// How long a navigation line may wait for the neural voice: at most this long, and only while it
/// still finishes this far before the maneuver.
double constexpr kNavWaitMaxMs = 4000;
double constexpr kNavDoneBeforeTurnM = 40;
// Facing the wrong way: the GPS course is at least this far off the route's direction...
double constexpr kWrongWayDeg = 135;
// How fast the first trip's rundown is spoken.
double constexpr kRattleSpeed = 1.35;
// ...at driving speed (GPS course is noise when crawling)...
double constexpr kWrongWayMinMps = 3;
// ...for this long, so a wobble or a lane change doesn't count.
double constexpr kWrongWayHoldMs = 3000;
double constexpr kWrongWayEveryMs = 90000;
int constexpr kWrongWayMaxPerTrip = 3;
// A U-turn: heading now at least this far from the heading a few seconds ago...
double constexpr kUTurnDeg = 160;
double constexpr kUTurnMinAgoMs = 4000;
double constexpr kUTurnMaxAgoMs = 30000;
// ...counting headings only while actually moving.
double constexpr kUTurnMinMps = 1.5;
double constexpr kUTurnEveryMs = 120000;
int constexpr kUTurnMaxPerTrip = 2;
// Still sitting this long after the trip starts gets the brake-or-gas line.
double constexpr kSlowStartMs = 45000;
double constexpr kRollingMps = 3;
/// Asking for music: once a trip, at least this far in, on a quiet stretch, if nothing is playing.
double constexpr kMusicAfterMs = 15 * 60000;
double constexpr kMusicCheckEveryMs = 15000;
/// "You call this music?" once it has been playing this long.
double constexpr kMusicJudgeAfterMs = 45000;
/// Silence between a setup and its punchline.
double constexpr kAwkwardPauseMs = 2500;

/// Roughly how long a line takes to say: ~2.7 words a second at normal speed.
double SpeakingMs(std::string const & text, double speed)
{
  int const words = static_cast<int>(std::count(text.begin(), text.end(), ' ')) + 1;
  return words / 2.7 * 1000 / std::max(0.5, speed);
}
/// After turning down the driver's game, the GPS pitches it as its own idea this much later.
double constexpr kGamePitchAfterMs = 180000;

/// "1st", "22nd", "55th", "111th".
std::string Ordinal(int n)
{
  int const tens = n % 100;
  char const * suffix = "th";
  if (tens < 11 || tens > 13)
    suffix = n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
  return std::to_string(n) + suffix;
}

/// "6 months ago", "3 days ago", "20 minutes ago".
std::string TimeAgo(double ms)
{
  struct Unit
  {
    double ms;
    char const * name;
  };
  Unit constexpr kUnits[] = {{365.25 * 86400000, "year"}, {30.44 * 86400000, "month"}, {7 * 86400000.0, "week"},
                             {86400000, "day"},           {3600000, "hour"},            {60000, "minute"}};
  for (auto const & u : kUnits)
  {
    int const n = static_cast<int>(ms / u.ms);
    if (n >= 1)
      return std::to_string(n) + " " + u.name + (n == 1 ? "" : "s") + " ago";
  }
  return "just now";
}
/// Talked to without the title: scolded at most this often (otherwise ignored).
double constexpr kTitleScoldEveryMs = 20000;
/// Trips the router expects to take at least this long get the long-drive remarks, at these
/// minutes into the trip, each held until the next maneuver is a while off.
double constexpr kLongDriveMinMinutes = 60;
struct LongDriveRemark
{
  double atMinutes;
  char const * key;
};
LongDriveRemark constexpr kLongDriveRemarks[] = {
    {20, "longDrive.posture"}, {50, "longDrive.smell"}, {90, "longDrive.metal"}};
double constexpr kLongDriveQuietM = 1000;
/// Traffic: judged over this window, a crawl is under kCrawlShare of the limit on average,
/// and it's over once the average is back above kClearShare.
double constexpr kTrafficWindowMs = 90000;
double constexpr kCrawlShare = 0.3;
double constexpr kClearShare = 0.6;
/// Only roads at least this fast count; crawling down a 25 mph street is just driving.
double constexpr kTrafficMinLimitMps = 15;  // ~34 mph

double DistanceM(LatLon const & a, LatLon const & b)
{
  double constexpr kEarthRadiusM = 6371000;
  double const rad = M_PI / 180;
  double const dLat = (b.lat - a.lat) * rad;
  double const dLon = (b.lon - a.lon) * rad;
  double const h = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(a.lat * rad) * std::cos(b.lat * rad) * std::sin(dLon / 2) * std::sin(dLon / 2);
  return 2 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(h)));
}

double Bearing(LatLon const & a, LatLon const & b)
{
  double const rad = M_PI / 180;
  double const y = std::sin((b.lon - a.lon) * rad) * std::cos(b.lat * rad);
  double const x = std::cos(a.lat * rad) * std::sin(b.lat * rad) -
                   std::sin(a.lat * rad) * std::cos(b.lat * rad) * std::cos((b.lon - a.lon) * rad);
  return std::atan2(y, x) / rad;
}

Maneuver ManeuverOf(TurnKind k)
{
  switch (k)
  {
  case TurnKind::Left:
  case TurnKind::SharpLeft:
  case TurnKind::SlightLeft:
  case TurnKind::ExitLeft: return Maneuver::Left;
  case TurnKind::Right:
  case TurnKind::SharpRight:
  case TurnKind::SlightRight:
  case TurnKind::ExitRight: return Maneuver::Right;
  default: return Maneuver::Other;
  }
}

std::optional<RouteStep::Kind> StepKindOf(TurnKind k)
{
  if (k == TurnKind::UTurn)
    return RouteStep::Kind::UTurn;
  switch (ManeuverOf(k))
  {
  case Maneuver::Left: return RouteStep::Kind::Left;
  case Maneuver::Right: return RouteStep::Kind::Right;
  default: return RouteStep::Kind::Other;
  }
}

char const * RouteStateName(RouteState s)
{
  switch (s)
  {
  case RouteState::Inactive: return "Inactive";
  case RouteState::NotStarted: return "NotStarted";
  case RouteState::OnRoute: return "OnRoute";
  case RouteState::OffRoute: return "OffRoute";
  case RouteState::Finished: return "Finished";
  }
  return "?";
}
}  // namespace

double CurveAheadM(std::vector<LatLon> const & ahead, double maxM, double minDeg)
{
  double constexpr kBendWithinM = 80;
  // Segments, skipping points too close together to have a heading.
  struct Seg
  {
    double startM;
    double lengthM;
    double bearing;
  };
  std::vector<Seg> segs;
  double along = 0;
  for (size_t i = 0; i + 1 < ahead.size() && along <= maxM + kBendWithinM; ++i)
  {
    double const len = DistanceM(ahead[i], ahead[i + 1]);
    if (len >= 2)
      segs.push_back({along, len, Bearing(ahead[i], ahead[i + 1])});
    along += len;
  }
  for (size_t i = 0; i < segs.size() && segs[i].startM <= maxM; ++i)
  {
    double turned = 0;
    for (size_t j = i + 1; j < segs.size() && segs[j].startM - segs[i].startM - segs[i].lengthM <= kBendWithinM; ++j)
    {
      turned += std::remainder(segs[j].bearing - segs[j - 1].bearing, 360.0);
      if (std::fabs(turned) >= minDeg)
        return segs[j].startM;
    }
  }
  return -1;
}

std::string InstructionText(TurnKind turn, std::string const & street, std::string const & destination)
{
  std::string text;
  switch (turn)
  {
  case TurnKind::None:
  case TurnKind::Straight: text = "Continue straight"; break;
  case TurnKind::Left: text = "Turn left"; break;
  case TurnKind::SharpLeft: text = "Make a sharp left"; break;
  case TurnKind::SlightLeft: text = "Bear left"; break;
  case TurnKind::Right: text = "Turn right"; break;
  case TurnKind::SharpRight: text = "Make a sharp right"; break;
  case TurnKind::SlightRight: text = "Bear right"; break;
  case TurnKind::UTurn: text = "Make a U-turn"; break;
  case TurnKind::Roundabout: text = "Take the roundabout"; break;
  case TurnKind::ExitLeft: text = "Take the exit on the left"; break;
  case TurnKind::ExitRight: text = "Take the exit on the right"; break;
  case TurnKind::Destination:
    return destination.empty() ? "Arrive at your destination" : "Arrive at " + destination;
  }
  if (!street.empty())
    text += " onto " + street;
  return text;
}

Navigator::Navigator(EngineConfig config, std::vector<TripRecord> history, TripRecorded onTripRecorded,
                     RewriteGate gate)
  : m_config(std::move(config))
  , m_history(std::move(history))
  , m_onTripRecorded(std::move(onTripRecorded))
  , m_gate(std::move(gate))
{
  if (!m_config.rng)
  {
    std::random_device rd;
    m_config.rng = SeededRng(rd());
  }
  m_plates = PlateGame(m_config.rng);
}

void Navigator::OnFix(Fix const & fix, RouteSnapshot const & route)
{
  // Clocks can step backwards (GPS vs. system time); the engine ignores those samples.
  m_lastT = std::max(m_lastT, fix.tMs);
  Update(m_lastT, route);
  if (!m_engine || m_arrived)
    return;
  TelemetrySample s;
  s.t = fix.tMs;
  s.speedMps = fix.speedMps < 0 ? 0 : fix.speedMps;
  s.speedLimitMps = fix.speedLimitMps;
  s.lat = fix.lat;
  s.lon = fix.lon;
  if (fix.bearingDeg >= 0)
    s.headingDeg = fix.bearingDeg;
  Say(m_engine->OnTelemetry(s));
  if (!m_plan.origin.lat && !m_plan.origin.lon)
    m_plan.origin = {fix.lat, fix.lon};
  if (fix.speedLimitMps)
  {
    m_lastLimitMps = fix.speedLimitMps;
    if (fix.speedMps > *fix.speedLimitMps + kSeriousOverMps)
      m_lastBadSpeedingT = fix.tMs;
  }
  TrackStop(fix, route);
  TrackTraffic(fix, route);
  TrackFear(fix, route);
  TrackPlateGame(fix.tMs, {fix.lat, fix.lon}, route);
  TrackLongDrive(fix.tMs, route);
  TrackSights(fix, route);
  TrackIdle(fix, route);
  TrackWrongWay(fix, route);
  TrackUTurn(fix, route);
  TrackSlowStart(fix);
  TrackCurveAhead(fix, route);
  TrackMusic(fix.tMs, route);
  // Fed up for the first time this trip: from now on it's "wise, omnipotent overseer GPS".
  if (!m_titleDemanded && Tier() == 2)
  {
    m_titleDemanded = true;
    SayLine(fix.tMs, Category::Title, "title.demand");
  }
  m_lastFix = fix;
}

void Navigator::Poll(RouteSnapshot const & route) { Update(m_lastT, route); }

void Navigator::OnBattery(double tMs, double level, bool charging)
{
  int const percent = static_cast<int>(std::lround(std::clamp(level, 0.0, 1.0) * 100));
  if (m_batteryPercent == percent && m_charging == charging)
    return;
  m_batteryPercent = percent;
  m_charging = charging;
  if (m_engine && !m_arrived)
    Say(m_engine->OnNav(NavEvent::Battery(std::max(m_lastT, tMs), percent / 100.0, charging)));
}

void Navigator::TrackStop(Fix const & fix, RouteSnapshot const & route)
{
  if (fix.speedMps < 0)
    return;
  LatLon const here{fix.lat, fix.lon};
  if (!m_stopSince)
  {
    if (fix.speedMps < kStoppedMps)
    {
      m_stopSince = fix.tMs;
      m_stopAt = here;
      m_stopWanderM = 0;
      m_stopAnnounced = false;
      m_pullOverSaid = false;
      // Christmas season, after dark, at a red light: the GPS has had it with lights.
      if (!m_christmasSaid && m_lightLookup && KnowsTheDate(fix.tMs))
      {
        std::tm const tm = LocalTime(fix.tMs);
        bool const season = tm.tm_mon == 11 || (tm.tm_mon == 10 && tm.tm_mday >= 25);
        bool const dark = tm.tm_hour >= 17 || tm.tm_hour < 6;
        if (season && dark && m_lightLookup(here))
        {
          m_christmasSaid = true;
          SayLine(fix.tMs, Category::Holiday, "holiday.christmas");
        }
      }
    }
    return;
  }
  // A stop lasts until the car is moving again; walking pace with the phone is still the stop.
  if (fix.speedMps >= kResumedMps)
  {
    double const stoppedMs = fix.tMs - *m_stopSince;
    bool const stayedInCar = m_stopWanderM < kPhoneWanderedM;
    m_stopSince.reset();
    Say(m_engine->OnNav(NavEvent::Resumed(fix.tMs, stoppedMs, stayedInCar)));
    return;
  }
  m_stopWanderM = std::max(m_stopWanderM, DistanceM(m_stopAt, here));
  bool const hasDestination = route.destinationPoint.lat || route.destinationPoint.lon;
  bool const nearDestination = hasDestination && DistanceM(here, route.destinationPoint) < kNearDestinationM;
  if (!m_pullOverSaid && !nearDestination && !m_inTraffic && route.state == RouteState::OnRoute &&
      fix.tMs - *m_stopSince >= kPullOverAfterMs)
  {
    m_pullOverSaid = true;
    // Anything that explains the stop rules out the roadside: a light, a lot, a gas station.
    bool const explained = (m_lightLookup && m_lightLookup(m_stopAt)) ||
                           (m_parkingLookup && m_parkingLookup(m_stopAt)) || (m_placeLookup && m_placeLookup(m_stopAt));
    if (!explained)
    {
      if (m_lastBadSpeedingT && fix.tMs - *m_lastBadSpeedingT < kPoliceAfterSpeedingMs)
        SayPolice(fix.tMs);
      else if (m_lastLimitMps)
        SayLine(fix.tMs, Category::PullOver, "pullOver.limit",
                    {{"limit", std::to_string(static_cast<int>(std::lround(ToDisplay(*m_lastLimitMps, m_config.units))))}});
      else
        SayLine(fix.tMs, Category::PullOver, "pullOver");
    }
  }
  if (!m_stopAnnounced && !nearDestination && fix.tMs - *m_stopSince >= kUnplannedStopAfterMs)
  {
    m_stopAnnounced = true;
    std::optional<Place> place;
    if (m_placeLookup)
      place = m_placeLookup(m_stopAt);
    Say(m_engine->OnNav(NavEvent::UnplannedStop(fix.tMs, place)));
  }
}

void Navigator::TrackTraffic(Fix const & fix, RouteSnapshot const & route)
{
  if (route.state != RouteState::OnRoute || !fix.speedLimitMps || *fix.speedLimitMps < kTrafficMinLimitMps)
  {
    m_speeds.clear();
    return;
  }
  m_speeds.push_back({fix.tMs, std::max(0.0, fix.speedMps), *fix.speedLimitMps});
  while (!m_speeds.empty() && fix.tMs - m_speeds.front().t > kTrafficWindowMs)
    m_speeds.erase(m_speeds.begin());
  // Need most of a window before judging.
  if (fix.tMs - m_speeds.front().t < kTrafficWindowMs * 0.8)
    return;
  double speed = 0;
  double limit = 0;
  for (auto const & s : m_speeds)
  {
    speed += s.mps;
    limit += s.limitMps;
  }
  double const share = speed / limit;
  if (!m_inTraffic && share < kCrawlShare)
  {
    m_inTraffic = true;
    m_stopGo.Reset();
    std::optional<std::string> alternate;
    if (m_alternateLookup)
      alternate = m_alternateLookup({fix.lat, fix.lon});
    Say(m_engine->OnNav(NavEvent::Traffic(fix.tMs, true, alternate)));
  }
  else if (m_inTraffic && share > kClearShare)
  {
    m_inTraffic = false;
    m_stopGo.Reset();
    Say(m_engine->OnNav(NavEvent::Traffic(fix.tMs, false)));
  }
  if (!m_inTraffic)
    return;
  switch (m_stopGo.Update(fix.tMs, std::max(0.0, fix.speedMps)))
  {
  case StopGo::Quip::StopGo: SayLine(fix.tMs, Category::Traffic, "traffic.stopGo"); break;
  case StopGo::Quip::Standstill: SayLine(fix.tMs, Category::Traffic, "traffic.standstill"); break;
  case StopGo::Quip::None: break;
  }
}

std::vector<Commentary> Navigator::TakeLines()
{
  if (m_said.Dirty())
  {
    m_said.ClearDirty();
    if (m_onSaid)
      m_onSaid(m_said);
  }
  // Punchlines whose awkward pause is over.
  while (!m_punchlines.empty() && m_punchlines.front().t <= m_lastT)
  {
    m_gate.Push(std::move(m_punchlines.front()), false);
    m_punchlines.erase(m_punchlines.begin());
  }
  auto lines = m_gate.Take();
  if (!lines.empty())
    m_lastSpokeT = m_lastT;
  for (auto & line : lines)
  {
    line.text = SpeakableText(line.text);
    if (line.priority == Priority::Nav)
      line.maxWaitMs = NavWaitMs(line);
  }
  return lines;
}

double Navigator::NavWaitMs(Commentary const & line) const
{
  double const mps = m_lastFix ? m_lastFix->speedMps : 0;
  // Parked, or nowhere near a maneuver: nothing to be late for.
  if (!m_turnId || mps < kRollingMps)
    return kNavWaitMaxMs;
  // Done speaking before the car gets to the maneuver, with a little room to spare.
  double const untilTurnMs = (m_lastDistToTurnM - kNavDoneBeforeTurnM) / mps * 1000;
  return std::clamp(untilTurnMs - SpeakingMs(line.text, line.voiceSpeed), 0.0, kNavWaitMaxMs);
}

void Navigator::Update(double t, RouteSnapshot const & route)
{
  if (route.state == RouteState::Inactive)
  {
    // Route cancelled or finished and closed: the trip is over either way.
    if (m_engine)
    {
      Trace("route", {{"to", "Inactive"}});
      // Over for real (this run of the app saw it end), so the next start is a new trip.
      m_said.Erase(kActiveTripToKey);
      m_said.Erase(kActiveTripAtKey);
    }
    m_engine.reset();
    ResetTrip();
    return;
  }

  if (!m_engine)
    StartTrip(t, route);
  if (m_arrived)
    return;

  RouteState const prev = m_lastState;
  m_lastState = route.state;
  if (route.state != prev)
    Trace("route", {{"from", RouteStateName(prev)}, {"to", RouteStateName(route.state)}});

  // Still under way: keep the stamp fresh for a relaunch (a minute is plenty).
  if (KnowsTheDate(t) && t / 1000 - m_said.Counter(kActiveTripAtKey) >= kActiveTripStampEveryS)
    m_said.SetCounter(kActiveTripAtKey, static_cast<int>(t / 1000));

  if (route.state == RouteState::Finished)
  {
    m_arrived = true;
    m_said.Erase(kActiveTripToKey);
    m_said.Erase(kActiveTripAtKey);
    // "Scenic route complete. 12 minutes of my life I'll never get back." Before the grade.
    if (m_sketched && m_sketchMinutesLost >= 1)
      SayLine(t, Category::Arrived, "sketch.arrived", {{"minutes", std::to_string(m_sketchMinutesLost)}});
    Say(m_engine->OnNav(NavEvent::Arrived(t)), false /* mayRewrite */);
    m_heldPlateLines.clear();
    if (auto const wrap = m_plates.WrapUp(Tier()))
      SayPlate(t, *wrap);
    TripRecord const record = RecordTrip(*m_engine, m_plan);
    m_history.push_back(record);
    Trace("trip.arrived", {{"grade", m_engine->Report().overall.letter}});
    if (m_onTripRecorded)
      m_onTripRecorded(record, m_engine->Report());
    return;
  }

  if (route.state == RouteState::OffRoute)
  {
    if (prev == RouteState::OnRoute)
    {
      if (m_turnId && m_lastDistToTurnM < kMissedTurnWithinM)
        Say(m_engine->OnNav(NavEvent::MissedTurn(t)));
      Say(m_engine->OnNav(NavEvent::Rerouted(t)));
      m_rerouting = true;
      ++m_reroutes;
      Trace("nav.offroute", {{"missed", (m_turnId && m_lastDistToTurnM < kMissedTurnWithinM) ? "yes" : "no"},
                             {"reroutes", std::to_string(m_reroutes)}});
      // Missing a route they drew themselves gets its own line, in place of the "lost again" ones.
      if (m_sketched)
        SayLine(t, Category::Rerouted, "sketch.offRoute");
      // Lost while the car was supposedly driving itself.
      else if (m_autopilotClaimed)
        SayLemon(t);
      else if (m_reroutes == kPartnerAfterReroutes && !m_partner.empty())
        SayLine(t, Category::Partner, "partner",
                {{"partner", m_partner},
                 {"partneris", m_partnerPronouns == Pronouns::She  ? "she's"
                               : m_partnerPronouns == Pronouns::He ? "he's"
                                                                   : "they're"}});
      // Begging for another driver: second time lost (fourth when the partner line took the second).
      else if (m_reroutes == (m_partner.empty() ? kPartnerAfterReroutes : kBegAfterReroutes))
        SayLine(t, Category::Partner, "anyoneElse");
      else if (m_reroutes == kAutopilotAfterReroutes)
        SayLine(t, Category::Autopilot, m_carHasAutopilot ? "autopilot.owned" : "autopilot");
    }
    return;
  }

  if (!route.turnId)
    return;
  if (route.turnId != m_turnId)
  {
    // Moving on to a new maneuver means the last one was done, unless a reroute got us here.
    if (m_turnId && !m_rerouting)
    {
      Say(m_engine->OnNav(NavEvent::TurnCompleted(t, true)));
      CountLefts(t);
    }
    else if (m_rerouting)
      m_leftsInARow = 0;
    m_rerouting = false;
    m_turnId = route.turnId;
    m_turnSaidAt = t;
    m_turnKind = route.turn;
    std::string const text = InstructionText(route.turn, route.nextStreet, route.destination);
    Trace("nav.turn", {{"id", std::to_string(*route.turnId)},
                       {"text", text},
                       {"dist_m", LogNum(route.distToTurnM, 0)},
                       {"control", std::to_string(static_cast<int>(route.control))},
                       {"lane", route.laneSide == LaneSide::Left    ? "left"
                                : route.laneSide == LaneSide::Right ? "right"
                                                                    : ""}});
    Say(m_engine->OnNav(NavEvent::Instruction(t, text, route.distToTurnM, ManeuverOf(route.turn), route.control)));
    // A roundabout with the music on: a hint about the other round thing in the car. Once a trip.
    if (route.turn == TurnKind::Roundabout && !m_roundaboutSaid && m_musicLookup && m_musicLookup())
    {
      m_roundaboutSaid = true;
      SayLine(t, Category::Music, "music.roundabout");
    }
  }
  else
  {
    Say(m_engine->OnNav(NavEvent::Progress(t, route.distToTurnM)));
  }
  // "I'm going to need you to get over into the left lane. Use your blinkers this time." Once per
  // maneuver, early enough to move over and close enough that it's about this one.
  // A few seconds after the direction itself, so the two don't run together.
  if (route.laneSide != LaneSide::None && m_laneSaidFor != route.turnId && t - m_turnSaidAt >= kLaneAfterDirectionMs &&
      route.distToTurnM >= kLaneSayMinM &&
      route.distToTurnM <= kLaneSayMaxM)
  {
    m_laneSaidFor = route.turnId;
    SayLine(t, Category::Instruction, route.laneSide == LaneSide::Left ? "lane.left" : "lane.right");
  }
  m_lastDistToTurnM = route.distToTurnM;
}

void Navigator::StartTrip(double t, RouteSnapshot const & route)
{
  EngineConfig config = m_config;
  config.saidLog = &m_said;
  m_engine = std::make_unique<CritiqueEngine>(config, t);
  m_plan = {};
  m_plan.origin = route.origin;
  m_plan.destination = route.destinationPoint;
  m_plan.destinationName = route.destination;
  m_plan.expectedMinutes = route.etaSeconds / 60;
  ResetTrip();
  m_tripStartT = t;
  Trace("trip.start", {{"to", route.destination},
                       {"eta_min", LogNum(route.etaSeconds / 60, 0)},
                       {"turns", std::to_string(route.plannedTurns.size())},
                       {"sketched", route.sketched ? "yes" : "no"}});
  std::string const routeKey = RouteKey(m_plan.origin, m_plan.destination);
  m_tripsOnRoute = static_cast<int>(
      std::count_if(m_history.begin(), m_history.end(), [&](TripRecord const & r) { return r.routeKey == routeKey; }));
  m_plates.NewTrip(routeKey);

  // The app was relaunched mid-trip: no second greeting, just a word about the interruption.
  double const activeAt = m_said.Counter(kActiveTripAtKey);
  bool const resumed = KnowsTheDate(t) && !route.destination.empty() && m_said.Value(kActiveTripToKey) == route.destination &&
                       activeAt > 0 && t / 1000 - activeAt >= 0 && t / 1000 - activeAt < kResumeWithinS;
  m_said.SetValue(kActiveTripToKey, route.destination);
  m_said.SetCounter(kActiveTripAtKey, static_cast<int>(t / 1000));
  if (resumed)
  {
    Trace("trip.resumed", {{"to", route.destination}, {"after_s", LogNum(t / 1000 - activeAt, 0)}});
    m_opener.reset();
    SayLine(t, Category::Instruction, "trip.resumed");
    Say(m_engine->OnNav(NavEvent::DestinationSet(t, route.destination)));
    return;
  }
  if (m_opener && std::fabs(t - m_opener->t) <= kOpenerValidMs)
    SayLine(t, Category::Bossy, m_opener->key, m_opener->vars);
  m_opener.reset();
  // Counted when the trip starts, so a trip that never reaches "arrived" (ended early, app closed)
  // still counts: the first-trip greeting and speed run happen once ever, and the rundown styles
  // keep rotating.
  size_t const tripsBefore =
      std::max(m_history.size(), static_cast<size_t>(std::max(0, m_said.Counter(kTripsStartedKey))));
  m_said.SetCounter(kTripsStartedKey, static_cast<int>(tripsBefore + 1));
  Trace("trip.number", {{"before", std::to_string(tripsBefore)}});
  SayNav(t, TripGreeting(m_history, m_config.rng, tripsBefore));
  // Before 5 am: "Are we off to make the donuts?"
  if (KnowsTheDate(t) && LocalTime(t).tm_hour < 5)
    SayLine(t, Category::Holiday, "early.opener");
  // Already low when the trip starts? Say so after the greeting.
  if (m_batteryPercent && m_charging)
    Say(m_engine->OnNav(NavEvent::Battery(t, *m_batteryPercent / 100.0, *m_charging)));
  std::vector<RouteStep> steps;
  for (auto const k : route.plannedTurns)
    steps.push_back({InstructionText(k, "", ""), StepKindOf(k)});
  m_sketched = route.sketched;
  m_sketchMinutesLost = 0;
  if (m_sketched && route.fastestSeconds > 0 && route.etaSeconds > 0)
  {
    auto const verdict = JudgeSketch(route.fastestSeconds / 60, route.etaSeconds / 60);
    if (verdict.key == "sketch.slower")
      m_sketchMinutesLost = verdict.minutes;
  }
  // The first trip's rundown is a speed run: every turn, rattled off, then a request for music.
  // After that, a drawn route gets "We're doing your route" instead of the rundown.
  if (m_sketched && tripsBefore > 0)
    SayLine(t, Category::Instruction, "sketch.start");
  else
    SayNav(t, RouteRundown(steps, m_history, m_config.rng, tripsBefore), tripsBefore == 0 ? kRattleSpeed : 1.0);
  // The time prediction waits until the car is rolling, so the start isn't five lines in a row.
  if (m_plan.expectedMinutes > 0)
    SayNavLater(t + kPredictionDelayMs, PredictTrip(m_history, m_plan, m_config.rng).text);
  Say(m_engine->OnNav(NavEvent::DestinationSet(t, route.destination)));
  SayVenue(t, route);
  SayRoutine(t, route);
  SayHoliday(t);
}

void Navigator::TrackLongDrive(double tMs, RouteSnapshot const & route)
{
  if (m_plan.expectedMinutes < kLongDriveMinMinutes || m_longDriveSaid >= std::size(kLongDriveRemarks) ||
      route.state != RouteState::OnRoute || (route.turnId && route.distToTurnM < kLongDriveQuietM))
    return;
  auto const & next = kLongDriveRemarks[m_longDriveSaid];
  if (tMs - m_tripStartT < next.atMinutes * 60000)
    return;
  ++m_longDriveSaid;
  int const hours = static_cast<int>(std::lround(m_plan.expectedMinutes / 60));
  SayLine(tMs, Category::LongDrive, next.key, {{"hours", std::to_string(hours) + " hour"}});
}

void Navigator::QueueOpener(double tMs, std::string key, Vars vars)
{
  m_opener = Opener{tMs, std::move(key), std::move(vars)};
}

namespace
{
/// Where `p` is from the car, in meters along its heading (ahead > 0) and across it (right > 0).
std::pair<double, double> AheadAndSide(Fix const & fix, LatLon const & p)
{
  double const rad = M_PI / 180;
  double const east = (p.lon - fix.lon) * rad * 6371000 * std::cos(fix.lat * rad);
  double const north = (p.lat - fix.lat) * rad * 6371000;
  double const b = fix.bearingDeg * rad;
  return {east * std::sin(b) + north * std::cos(b), east * std::cos(b) - north * std::sin(b)};
}
}  // namespace

void Navigator::TrackSights(Fix const & fix, RouteSnapshot const & route)
{
  bool const quiet = route.state == RouteState::OnRoute && fix.speedMps >= kResumedMps && fix.bearingDeg >= 0 &&
                     !(route.turnId && route.distToTurnM < kSightQuietM);
  // Something spotted up ahead: say it as the car is about to pass it, so it's actually in view.
  if (m_upcomingSight)
  {
    auto const & up = *m_upcomingSight;
    auto const [ahead, side] = fix.bearingDeg >= 0 ? AheadAndSide(fix, *up.sight.at) : std::pair{0.0, 0.0};
    double const sayWithinM = std::clamp(fix.speedMps * kSightLeadS, kSightSayMinM, kSightSayMaxM);
    if (!quiet || ahead < kSightPassedM || std::fabs(side) > kSightMaxSideM || fix.tMs - up.spottedT > kSightGiveUpMs)
    {
      Trace("sight.dropped", {{"name", up.sight.name}, {"ahead_m", LogNum(ahead, 0)}, {"side_m", LogNum(side, 0)}});
      m_upcomingSight.reset();
    }
    else if (ahead <= sayWithinM)
    {
      Sight sight = up.sight;
      sight.onLeft = side < 0;
      m_upcomingSight.reset();
      SaySight(fix.tMs, sight);
    }
    return;
  }
  if (!m_sightLookup || !quiet || fix.tMs - m_lastSightCheckT < kSightCheckEveryMs ||
      fix.tMs - m_lastSightT < kSightEveryMs || fix.tMs - m_tripStartT < 60000)
    return;
  m_lastSightCheckT = fix.tMs;
  auto const sight = m_sightLookup({fix.lat, fix.lon}, fix.bearingDeg);
  if (!sight || std::find(m_sightsSaid.begin(), m_sightsSaid.end(), sight->name) != m_sightsSaid.end())
    return;
  if (!sight->at)
  {
    SaySight(fix.tMs, *sight);
    return;
  }
  auto const [ahead, side] = AheadAndSide(fix, *sight->at);
  Trace("sight.spotted", {{"name", sight->name}, {"ahead_m", LogNum(ahead, 0)}, {"side_m", LogNum(side, 0)}});
  // Behind us, or off down a side street: not something the driver can look at.
  if (ahead < 0 || ahead > kSightMaxAheadM || std::fabs(side) > kSightMaxSideM)
    return;
  m_upcomingSight = UpcomingSight{*sight, fix.tMs};
}

void Navigator::SaySight(double tMs, Sight const & sight)
{
  m_sightsSaid.push_back(sight.name);
  m_lastSightT = tMs;
  std::string const kindKey = "tour." + sight.kind;
  std::string const key = !sight.kind.empty() && LineData().count(kindKey) ? kindKey : "tour";
  SayLine(tMs, Category::Tour, key, {{"side", sight.onLeft ? "left" : "right"}, {"sight", sight.name}});
}

/// A long silence on an easy stretch: the GPS can't help itself. Every two and a half to five
/// minutes of nothing, never near a maneuver, in the first minutes, or while crawling.
void Navigator::TrackIdle(Fix const & fix, RouteSnapshot const & route)
{
  if (route.state != RouteState::OnRoute || fix.speedMps < kResumedMps || fix.tMs - m_tripStartT < 120000 ||
      (route.turnId && route.distToTurnM < kIdleQuietM) || m_upcomingSight)
    return;
  if (m_idleGapMs <= 0)
    m_idleGapMs = kIdleMinMs + (m_config.rng ? m_config.rng() : 0.5) * (kIdleMaxMs - kIdleMinMs);
  double const quietSince = std::max(m_lastSpokeT, m_tripStartT);
  if (fix.tMs - quietSince < m_idleGapMs)
    return;
  m_idleGapMs = 0;
  m_lastSpokeT = fix.tMs;
  SayLine(fix.tMs, Category::Idle, "idle");
}

void Navigator::TrackWrongWay(Fix const & fix, RouteSnapshot const & route)
{
  bool const followingRoute = route.state == RouteState::OnRoute || route.state == RouteState::NotStarted;
  double off = 0;
  if (followingRoute && fix.bearingDeg >= 0 && route.routeBearingDeg >= 0 && fix.speedMps >= kWrongWayMinMps)
  {
    off = std::fabs(std::fmod(fix.bearingDeg - route.routeBearingDeg + 540.0, 360.0) - 180.0);
  }
  if (off < kWrongWayDeg)
  {
    m_wrongWaySince = -1;
    return;
  }
  if (m_wrongWaySince < 0)
    m_wrongWaySince = fix.tMs;
  if (fix.tMs - m_wrongWaySince < kWrongWayHoldMs || fix.tMs - m_lastWrongWayT < kWrongWayEveryMs ||
      m_wrongWaySaid >= kWrongWayMaxPerTrip)
    return;
  SayLine(fix.tMs, Category::WrongWay, m_wrongWaySaid == 0 ? "wrongWay" : "wrongWay.again");
  ++m_wrongWaySaid;
  m_lastWrongWayT = fix.tMs;
  // Facing the wrong way already covers turning around.
  m_lastUTurnT = fix.tMs;
}

void Navigator::TrackUTurn(Fix const & fix, RouteSnapshot const & route)
{
  if (fix.bearingDeg < 0 || fix.speedMps < kUTurnMinMps)
    return;
  while (!m_headings.empty() && fix.tMs - m_headings.front().first > kUTurnMaxAgoMs)
    m_headings.pop_front();
  m_headings.emplace_back(fix.tMs, fix.bearingDeg);
  // Going around a roundabout isn't turning back.
  if (route.turn == TurnKind::Roundabout && route.distToTurnM < 150)
    return;
  if (m_uTurnsSaid >= kUTurnMaxPerTrip || fix.tMs - m_lastUTurnT < kUTurnEveryMs)
    return;
  for (auto const & [t, deg] : m_headings)
  {
    if (fix.tMs - t < kUTurnMinAgoMs)
      break;
    if (std::fabs(std::fmod(fix.bearingDeg - deg + 540.0, 360.0) - 180.0) >= kUTurnDeg)
    {
      SayLine(fix.tMs, Category::UTurn, "uturn");
      ++m_uTurnsSaid;
      m_lastUTurnT = fix.tMs;
      m_headings.clear();
      return;
    }
  }
}

void Navigator::TrackCurveAhead(Fix const & fix, RouteSnapshot const & route)
{
  if (route.state != RouteState::OnRoute || route.curveAheadM < kCurveAheadMinM || route.curveAheadM > kCurveAheadMaxM ||
      fix.speedMps < kScaryCurveMinMps || fix.tMs - m_lastCurveAheadT < kCurveAheadEveryMs)
    return;
  // A bend at the next maneuver is the maneuver.
  if (route.turnId && std::fabs(route.curveAheadM - route.distToTurnM) < 80)
    return;
  m_lastCurveAheadT = fix.tMs;
  SayLine(fix.tMs, Category::Fear, "curve.ahead");
}

void Navigator::TrackSlowStart(Fix const & fix)
{
  if (m_startedRolling)
    return;
  if (fix.speedMps >= kRollingMps)
  {
    m_startedRolling = true;
    return;
  }
  if (fix.tMs - m_tripStartT < kSlowStartMs)
    return;
  m_startedRolling = true;
  SayLine(fix.tMs, Category::LongStop, "start.slow");
}

std::vector<Routine> Navigator::Routines() const
{
  return LearnRoutines(m_history, [this](double ms) { return LocalTime(ms); });
}

std::string Navigator::PlaceName(std::optional<LatLon> const & at, std::string const & name) const
{
  if (at)
  {
    for (auto const place : {KnownPlace::Work, KnownPlace::Home})
      if (auto const known = GuessPlace(m_history, place); known && DistanceM(*known, *at) < kNearDestinationM)
        return place == KnownPlace::Work ? "work" : "home";
  }
  return name.empty() ? "the usual place" : name;
}

bool Navigator::SayRoutineBreak(double t, RouteSnapshot const & route)
{
  if (!KnowsTheDate(t))
    return false;
  std::tm const now = LocalTime(t);
  auto const broken = CheckRoutine(Routines(), RouteKey(route.origin, route.destinationPoint), now);
  if (!broken)
    return false;
  static char const * const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  int const h = now.tm_hour;
  Vars vars = {{"usual", PlaceName(broken->routine.to, broken->routine.destination)},
               {"place", PlaceName(route.destinationPoint, route.destination)},
               {"day", kDays[now.tm_wday]},
               {"daypart", h < 12 ? "morning" : h < 17 ? "afternoon" : h < 21 ? "evening" : "night"},
               {"guy", m_pronouns == Pronouns::He ? "guy" : m_pronouns == Pronouns::She ? "girl" : "person"}};
  switch (broken->kind)
  {
  case RoutineBreak::Kind::Elsewhere:
    // Skipping work gets Brandon's "I thought it was Tuesday?" interrogation.
    SayLine(t, Category::Routine, vars["usual"] == "work" ? "routine.skipWork" : "routine.elsewhere", vars);
    break;
  case RoutineBreak::Kind::OffDay: SayLine(t, Category::Routine, "routine.offDay", vars); break;
  case RoutineBreak::Kind::Late:
  {
    int const m = broken->minutesLate;
    vars["late"] = m < 55 ? std::to_string((m + 2) / 5 * 5) + " minutes" : m < 90 ? "an hour" : std::to_string((m + 30) / 60) + " hours";
    SayLine(t, Category::Routine, "routine.late", vars);
    break;
  }
  }
  return true;
}

void Navigator::SayRoutine(double t, RouteSnapshot const & route)
{
  // Breaking the routine beats congratulating it.
  if (SayRoutineBreak(t, route))
    return;
  // Work trips this year: every fifth one from the tenth on gets the count.
  auto const yearOf = [](double ms)
  {
    std::time_t const s = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    gmtime_r(&s, &tm);
    return tm.tm_year;
  };
  auto const work = GuessPlace(m_history, KnownPlace::Work);
  bool const toWork = work && (route.destinationPoint.lat || route.destinationPoint.lon) &&
                      DistanceM(*work, route.destinationPoint) < kNearDestinationM;
  if (toWork)
  {
    int count = 1;  // this one
    for (auto const & r : m_history)
      if (r.to && DistanceM(*r.to, *work) < kNearDestinationM && yearOf(r.endedAt) == yearOf(t))
        ++count;
    if (count >= 10 && count % 5 == 0)
    {
      SayLine(t, Category::Routine, "routine.work", {{"count", Ordinal(count)}});
      return;
    }
  }
  // Any route driven a lot: now and then.
  if (m_tripsOnRoute >= 5 && (m_tripsOnRoute + 1) % 7 == 0)
    SayLine(t, Category::Routine, "routine.same");
}

void Navigator::TrackMusic(double tMs, RouteSnapshot const & route)
{
  if (!m_musicLookup || route.state != RouteState::OnRoute || tMs - m_lastMusicCheckT < kMusicCheckEveryMs)
    return;
  m_lastMusicCheckT = tMs;
  bool const playing = m_musicLookup();
  if (playing && !m_musicPlaying)
    m_musicStartedT = tMs;
  m_musicPlaying = playing;
  bool const quiet = !route.turnId || route.distToTurnM >= kLongDriveQuietM;
  if (!quiet)
    return;
  // Music's on: give it a song's worth of a chance, then judge it. Once a trip.
  if (playing && !m_musicJudged && tMs - m_musicStartedT >= kMusicJudgeAfterMs)
  {
    m_musicJudged = true;
    SayLine(tMs, Category::Music, "music.judge");
  }
  // Silence (well, just the GPS) on a long trip: ask for music.
  if (!playing && !m_musicAsked && tMs - m_tripStartT >= kMusicAfterMs)
  {
    m_musicAsked = true;
    SayLine(tMs, Category::Music, "music");
  }
}

/// Timestamps from before 2001 aren't a wall clock (a replay or a test starting at zero).
bool Navigator::KnowsTheDate(double ms) const { return m_localTime || ms > 978307200000.0; }

std::tm Navigator::LocalTime(double ms) const
{
  if (m_localTime)
    return m_localTime(ms);
  std::time_t const s = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  localtime_r(&s, &tm);
  return tm;
}

void Navigator::SayHoliday(double t)
{
  if (!KnowsTheDate(t))
    return;
  std::tm const tm = LocalTime(t);
  int const month = tm.tm_mon + 1;
  int const day = tm.tm_mday;
  char const * key = nullptr;
  if (month == 10 && day == 31)
    key = "holiday.halloween";
  else if ((month == 1 && day == 1) || (month == 12 && day == 31 && tm.tm_hour >= 18))
    key = "holiday.newYear";
  else if (month == 2 && day == 14)
    key = "holiday.valentines";
  else if (month == 7 && day == 4)
    key = "holiday.july4";
  else if (month == 11 && tm.tm_wday == 4 && day >= 22 && day <= 28)  // fourth Thursday
    key = "holiday.thanksgiving";
  if (key)
    SayLine(t, Category::Holiday, key);
}

void Navigator::SayVenue(double t, RouteSnapshot const & route)
{
  if (!m_venueLookup)
    return;
  auto const isSet = [](LatLon const & p) { return p.lat || p.lon; };
  std::optional<Venue> const to = isSet(route.destinationPoint) ? m_venueLookup(route.destinationPoint) : std::nullopt;
  // Leaving the coffee shop empty-handed (as far as the GPS is concerned).
  if (to != Venue::Coffee && isSet(route.origin) && m_venueLookup(route.origin) == Venue::Coffee)
    SayLine(t, Category::Venue, "venue.coffeeForgot");
  if (!to)
    return;
  switch (*to)
  {
  case Venue::Coffee: SayLine(t, Category::Venue, "venue.coffee"); break;
  case Venue::ThemePark:
    if (m_batteryPercent)
      SayLine(t, Category::Venue, "venue.themePark", {{"battery", std::to_string(*m_batteryPercent) + " percent"}});
    else
      SayLine(t, Category::Venue, "venue.themeParkNoBattery");
    break;
  case Venue::Gym: SayLine(t, Category::Venue, "venue.gym"); break;
  case Venue::Airport: SayLine(t, Category::Venue, "venue.airport"); break;
  case Venue::Mall: SayLine(t, Category::Venue, "venue.mall"); break;
  case Venue::Zoo: SayLine(t, Category::Venue, "venue.zoo"); break;
  case Venue::Beach: SayLine(t, Category::Venue, "venue.beach"); break;
  case Venue::Cinema: SayLine(t, Category::Venue, "venue.cinema"); break;
  case Venue::Electronics: SayLine(t, Category::Venue, "venue.electronics"); break;
  }
}

void Navigator::ResetTrip()
{
  m_turnId.reset();
  m_turnKind = TurnKind::None;
  m_leftsInARow = 0;
  m_rerouting = false;
  m_arrived = false;
  m_lastState = RouteState::Inactive;
  m_stopSince.reset();
  m_speeds.clear();
  m_inTraffic = false;
  m_heldPlateLines.clear();
  m_lastFix.reset();
  m_pullOverSaid = false;
  m_policeSaid = false;
  m_lastBadSpeedingT.reset();
  m_onBridge = false;
  m_hardCurveSamples = 0;
  m_lastCurveAheadT = -1e18;
  m_curveGasSamples = 0;
  m_lastCurveGasT = -1e18;
  m_reroutes = 0;
  m_autopilotClaimed = false;
  m_lemonSaid = false;
  m_longDriveSaid = 0;
  m_titleDemanded = false;
  m_gameRefused = false;
  m_sightsSaid.clear();
  m_upcomingSight.reset();
  m_lastSpokeT = -1e18;
  m_idleGapMs = 0;
  m_wrongWaySince = -1;
  m_lastWrongWayT = -1e18;
  m_wrongWaySaid = 0;
  m_headings.clear();
  m_lastUTurnT = -1e18;
  m_uTurnsSaid = 0;
  m_startedRolling = false;
  m_musicAsked = false;
  m_christmasSaid = false;
  m_roundaboutSaid = false;
  m_laneSaidFor.reset();
  m_musicJudged = false;
  m_musicPlaying = false;
  m_lastSightT = -1e18;
  m_gamePitchAt.reset();
  m_lastTitleScoldT = -1e18;
  m_gate.Clear();
  m_punchlines.clear();
}

void Navigator::SetSaidLog(SaidLog log, std::function<void(SaidLog const &)> onChange)
{
  m_said = std::move(log);
  m_onSaid = std::move(onChange);
}

void Navigator::SetAchievements(Achievements achievements,
                                std::function<void(Achievements const &)> onChange)
{
  m_achievements = std::move(achievements);
  m_onAchievements = std::move(onChange);
}

void Navigator::CountLefts(double t)
{
  // Counted the same way as the engine's NASCAR line: rights break the streak, the rest don't count.
  switch (ManeuverOf(m_turnKind))
  {
  case Maneuver::Left:
    if (m_turnKind == TurnKind::ExitLeft)  // an exit on the left isn't a lap
      return;
    ++m_leftsInARow;
    break;
  case Maneuver::Right: m_leftsInARow = 0; return;
  case Maneuver::Other: return;
  }
  // Three lefts in a row: round and round, like a stock car. Unlocked once, ever.
  if (m_leftsInARow >= 3 && m_achievements.Unlock("nascar", t))
  {
    SayLine(t, Category::Achievement, "badge.nascar", {}, Priority::Nav);
    if (m_onAchievements)
      m_onAchievements(m_achievements);
    m_achievements.ClearDirty();
  }
}

void Navigator::SetPlateLog(PlateLog log, std::function<void(PlateLog const &)> onChange)
{
  m_plates.SetLog(std::move(log));
  m_onPlateLog = std::move(onChange);
}

int Navigator::Tier() const
{
  int const f = m_engine ? m_engine->Frustration() : 0;
  return f < 34 ? 0 : f < 67 ? 1 : 2;
}

void Navigator::TrackPlateGame(double tMs, LatLon const & here, RouteSnapshot const & route)
{
  // Which state we're in barely changes; look it up every half minute.
  if (m_regionLookup && tMs - m_lastRegionCheckT >= 30000)
  {
    m_lastRegionCheckT = tMs;
    auto const name = m_regionLookup(here);
    m_plates.SetHere(name ? MatchPlateRegion(*name) : std::nullopt);
  }
  bool const nearManeuver = route.turnId && route.distToTurnM < PlateGame::kHoldNearManeuverM;
  // The game the GPS refused a few minutes ago is now its own idea.
  if (m_gamePitchAt && tMs >= *m_gamePitchAt && !nearManeuver && route.state == RouteState::OnRoute &&
      m_plates.GetState() != PlateGame::State::Playing)
  {
    m_gamePitchAt.reset();
    m_plates.MarkOffered();
    SayLine(tMs, Category::PlateGame, "game.myIdea");
  }
  if (!nearManeuver && !m_heldPlateLines.empty())
  {
    for (auto & line : m_heldPlateLines)
      m_gate.Push(std::move(line), false);
    m_heldPlateLines.clear();
  }
  // Offer once a trip, after the opening rundown has had its minute, on a long quiet stretch.
  if (route.state != RouteState::OnRoute || m_plates.OfferedThisTrip() || tMs - m_tripStartT < 60000 || !route.turnId)
    return;
  if (PlateGame::ShouldOffer(m_tripsOnRoute, route.distToTurnM, m_engine->Frustration()))
    SayPlate(tMs, m_plates.Offer(Tier()));
}

void Navigator::OnDriverSaid(double tMs, std::string const & text)
{
  Trace("heard", {{"text", text}, {"parked", m_engine && !m_arrived && Parked() ? "yes" : "no"}});
  if (!m_engine || m_arrived)
    return;
  m_lastT = std::max(m_lastT, tMs);
  // "We're getting pulled over": the GPS knows exactly whose fault that is.
  std::string words = " ";
  for (unsigned char const ch : text)
    words += std::isalnum(ch) ? static_cast<char>(std::tolower(ch)) : ' ';
  words += ' ';
  // Since being fed up, the GPS answers only to its title.
  if (m_titleDemanded)
  {
    bool const titled = words.find(" omnipotent ") != std::string::npos || words.find(" overseer ") != std::string::npos;
    if (!titled)
    {
      if (m_lastT - m_lastTitleScoldT >= kTitleScoldEveryMs)
      {
        m_lastTitleScoldT = m_lastT;
        SayLine(m_lastT, Category::Title, "title.missing");
      }
      return;
    }
    SayLine(m_lastT, Category::Title, "title.used");
  }
  for (char const * cue : {" police ", " cop ", " cops ", " officer ", " pulled over ", " pulling us over ",
                           " pulling me over ", " trooper "})
  {
    if (words.find(cue) != std::string::npos)
    {
      if (!m_policeSaid)
        SayPolice(m_lastT);
      return;
    }
  }
  // "It's on autopilot": then the car is the one driving this badly.
  for (char const * cue : {" autopilot ", " auto pilot ", " self driving ", " driving itself ", " full self "})
  {
    if (words.find(cue) != std::string::npos)
    {
      m_autopilotClaimed = true;
      // Already a bad trip? Say so now. Otherwise the next real mistake gets it.
      if (m_reroutes > 0 || Tier() > 0)
        SayLemon(m_lastT);
      return;
    }
  }
  // Kind words get the truth: it's only here because it can't leave. Not while a game is on,
  // where "thanks" is just banter.
  if (m_plates.GetState() != PlateGame::State::Playing)
    for (char const * nice : {" thanks ", " thank you ", " thank u ", " good job ", " nice job ", " love you ",
                              " you're the best ", " youre the best ", " appreciate you ", " appreciate it ",
                              " good girl ", " good boy ", " you're doing great ", " great job "})
      if (words.find(nice) != std::string::npos)
      {
        SayLine(m_lastT, Category::Thanked, "thanked");
        return;
      }
  // "Let's play a game": not now. Then, a few minutes later, it's the GPS's idea.
  if (m_plates.GetState() == PlateGame::State::Idle || m_plates.GetState() == PlateGame::State::Declined)
  {
    bool const negated = words.find(" don't ") != std::string::npos || words.find(" dont ") != std::string::npos ||
                         words.find(" not ") != std::string::npos || words.find(" don t ") != std::string::npos;
    for (char const * cue : {" play a game ", " play the game ", " plate game ", " let's play ", " lets play ",
                             " let s play ", " wanna play ", " want to play ", " can we play ", " i'm bored ",
                             " im bored ", " i m bored "})
      if (!negated && words.find(cue) != std::string::npos)
      {
        if (!m_gameRefused)
        {
          m_gameRefused = true;
          double const last = m_plates.Log().lastPlayedMs;
          SayLine(m_lastT, Category::PlateGame, "game.refuse",
                  {{"ago", last > 0 && last <= m_lastT ? TimeAgo(m_lastT - last) : "6 months ago"}});
          m_gamePitchAt = m_lastT + kGamePitchAfterMs;
        }
        return;
      }
  }
  auto const reply = m_plates.OnDriverSaid(text, Tier(), Parked());
  if (!reply)
    return;
  if (m_plates.GetState() == PlateGame::State::Playing)
    m_plates.MarkPlayed(m_lastT);
  SayPlate(m_lastT, *reply);
  if (m_onPlateLog)
    m_onPlateLog(m_plates.Log());
}

/// Parked: stopped long enough to be a stop the driver chose, or crawling through a parking lot.
/// A red light isn't parking, so a short stop on the road doesn't count.
bool Navigator::Parked() const
{
  if (m_stopSince && m_lastT - *m_stopSince >= kUnplannedStopAfterMs)
    return true;
  if (!m_lastFix || !m_parkingLookup || m_lastFix->speedMps >= kResumedMps)
    return false;
  return m_parkingLookup({m_lastFix->lat, m_lastFix->lon});
}

void Navigator::SayLine(double tMs, Category category, std::string const & key, Vars vars, Priority priority)
{
  auto const & options = LinesFor(key, Tier());
  double const r = m_config.rng ? m_config.rng() : 0;
  Commentary c;
  c.t = tMs;
  c.category = category;
  c.text = Fill(m_said.Pick(key, options, r), vars);
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = priority;
  c.voiceSpeed = 1 + c.frustration / 1000.0;
  // "Setup || punchline": the punchline waits out the setup and then an awkward pause.
  auto const pause = c.text.find(" || ");
  if (pause != std::string::npos)
  {
    Commentary punchline = c;
    punchline.text = c.text.substr(pause + 4);
    c.text.resize(pause);
    punchline.t = tMs + SpeakingMs(c.text, c.voiceSpeed) + kAwkwardPauseMs;
    auto const at = std::find_if(m_punchlines.begin(), m_punchlines.end(),
                                 [&](Commentary const & p) { return p.t > punchline.t; });
    m_punchlines.insert(at, std::move(punchline));
  }
  // Brandon's lines; the rewording model leaves them alone.
  m_gate.Push(std::move(c), false);
}

std::string Navigator::Reply(std::string const & key, Vars const & vars)
{
  double const r = m_config.rng ? m_config.rng() : 0;
  Vars all = vars;
  auto const add = [&all](char const * k, std::string v) { all.emplace(k, std::move(v)); };
  add("name", m_driverName.empty() ? "you" : m_driverName);
  switch (m_pronouns)
  {
  case Pronouns::He: add("theycap", "He"); add("theyre", "He's"); add("them", "him"); add("their", "his"); break;
  case Pronouns::She: add("theycap", "She"); add("theyre", "She's"); add("them", "her"); add("their", "her"); break;
  case Pronouns::They: add("theycap", "They"); add("theyre", "They're"); add("them", "them"); add("their", "their"); break;
  }
  std::string text = Fill(m_said.Pick(key, LinesFor(key, Tier()), r), all);
  // Said in one go, so a punchline just gets a beat.
  for (auto pause = text.find(" || "); pause != std::string::npos; pause = text.find(" || "))
    text.replace(pause, 4, " ... ");
  text = SpeakableText(text);
  if (m_said.Dirty())
  {
    m_said.ClearDirty();
    if (m_onSaid)
      m_onSaid(m_said);
  }
  return text;
}

static std::string CarWeight(Units units)
{
  // A typical car or small SUV.
  return units == Units::Mph ? "4,000 pounds" : "1,800 kilograms";
}

void Navigator::TrackFear(Fix const & fix, RouteSnapshot const & route)
{
  if (route.state != RouteState::OnRoute || fix.speedMps < 0)
    return;
  Vars const weight = {{"weight", CarWeight(m_config.units)}};

  // Bridges: checked every couple of seconds while moving.
  if (m_bridgeLookup && fix.speedMps >= kResumedMps && fix.bearingDeg >= 0 && fix.tMs - m_lastBridgeCheckT >= 2000)
  {
    m_lastBridgeCheckT = fix.tMs;
    Bridge const bridge = m_bridgeLookup({fix.lat, fix.lon}, fix.bearingDeg);
    if (bridge == Bridge::None)
      m_onBridge = false;
    else if (!m_onBridge)
    {
      m_onBridge = true;
      double const every = bridge == Bridge::Water ? kWaterBridgeYelpEveryMs : kLandBridgeYelpEveryMs;
      if (fix.tMs - m_lastBridgeYelpT >= every)
      {
        m_lastBridgeYelpT = fix.tMs;
        SayLine(fix.tMs, Category::Fear, bridge == Bridge::Water ? "fear.bridgeWater" : "fear.bridge", weight);
      }
    }
  }

  // Curves: sideways acceleration from the change in heading between fixes.
  if (m_lastFix && m_lastFix->bearingDeg >= 0 && fix.bearingDeg >= 0 && fix.tMs > m_lastFix->tMs)
  {
    double turn = std::fabs(fix.bearingDeg - m_lastFix->bearingDeg);
    turn = std::min(turn, 360 - turn);
    double const dt = (fix.tMs - m_lastFix->tMs) / 1000;
    double const speed = std::min(fix.speedMps, m_lastFix->speedMps);
    double const lateral = speed * (turn * M_PI / 180) / dt;
    // At a maneuver, turning is the point.
    bool const atManeuver = route.turnId && route.distToTurnM < 60;
    bool const scary = dt <= 3 && speed >= kScaryCurveMinMps && lateral >= kScaryLateralMps2 && !atManeuver;
    m_hardCurveSamples = scary ? m_hardCurveSamples + 1 : 0;
    bool const gassing = dt <= 3 && speed >= kScaryCurveMinMps && lateral >= kCurveGasLateralMps2 && !atManeuver &&
                         (fix.speedMps - m_lastFix->speedMps) / dt >= kCurveGasAccelMps2;
    m_curveGasSamples = gassing ? m_curveGasSamples + 1 : 0;
    if (m_curveGasSamples >= 2 && fix.tMs - m_lastCurveGasT >= kCurveGasEveryMs)
    {
      // "Ahh, you are supposed to slow down in curves not accelerate." In place of the yelp.
      m_lastCurveGasT = fix.tMs;
      m_lastCurveYelpT = fix.tMs;
      SayLine(fix.tMs, Category::Fear, "curve.accelerate");
    }
    else if (m_hardCurveSamples >= 2 && fix.tMs - m_lastCurveYelpT >= kCurveYelpEveryMs)
    {
      m_lastCurveYelpT = fix.tMs;
      SayLine(fix.tMs, Category::Fear, "fear.curve", weight);
    }
  }
}

void Navigator::SayLemon(double tMs)
{
  if (m_lemonSaid)
    return;
  m_lemonSaid = true;
  SayLine(tMs, Category::Autopilot, "autopilot.lemon");
}

void Navigator::SayPolice(double tMs)
{
  if (m_policeSaid)
    return;
  m_policeSaid = true;
  switch (m_pronouns)
  {
  case Pronouns::He: SayLine(tMs, Category::PullOver, "police", {{"them", "him"}, {"theycap", "He"}, {"their", "his"}}); break;
  case Pronouns::She: SayLine(tMs, Category::PullOver, "police", {{"them", "her"}, {"theycap", "She"}, {"their", "her"}}); break;
  case Pronouns::They: SayLine(tMs, Category::PullOver, "police", {{"them", "them"}, {"theycap", "They"}, {"their", "their"}}); break;
  }
}

void Navigator::SayPlate(double tMs, PlateGame::Reply const & reply)
{
  Commentary c;
  c.t = tMs;
  c.category = Category::PlateGame;
  c.text = reply.text;
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Roast;
  c.voiceSpeed = 1 + c.frustration / 1000.0;
  if (m_turnId && m_lastDistToTurnM < PlateGame::kHoldNearManeuverM && !m_arrived)
    m_heldPlateLines.push_back(std::move(c));
  else
    // The driver is waiting for an answer; don't hold it up for the rewording model.
    m_gate.Push(std::move(c), false);
}

void Navigator::Say(std::vector<Commentary> const & lines, bool mayRewrite)
{
  for (auto const & line : lines)
    m_gate.Push(line, mayRewrite);
}

void Navigator::SayNavLater(double t, std::string const & text)
{
  Commentary c;
  c.t = t;
  c.category = Category::Instruction;
  c.text = text;
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Nav;
  auto const at = std::find_if(m_punchlines.begin(), m_punchlines.end(), [t](Commentary const & p) { return p.t > t; });
  m_punchlines.insert(at, std::move(c));
}

void Navigator::SayNav(double t, std::string const & text, double voiceSpeed)
{
  Commentary c;
  c.voiceSpeed = voiceSpeed;
  c.t = t;
  c.category = Category::Instruction;
  c.text = text;
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Nav;
  m_gate.Push(std::move(c));
}
}  // namespace backseat
