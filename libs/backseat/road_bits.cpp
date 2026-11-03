#include "backseat/road_bits.hpp"

#include "backseat/debug_log.hpp"
#include "backseat/units.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace backseat
{
namespace
{
/// Look at the map around the car this often, and at the route ahead this often.
double constexpr kSurroundEveryMs = 5000;
double constexpr kAheadEveryMs = 30000;
/// How far down the route to look for rest areas, gas, coffee and viewpoints (~50 miles).
double constexpr kAheadMaxM = 80000;

/// Ambient remarks wait for a quiet moment: this long since anything was said, this far from the
/// next maneuver, and a few minutes into the trip.
double constexpr kAmbientSilenceMs = 20000;
double constexpr kAmbientMinToTurnS = 30;
double constexpr kAmbientAfterStartMs = 3 * 60000;
double constexpr kAmbientMinMps = 5;
/// Things that just happened (signal lost, a drive-thru) only need a short gap.
double constexpr kEventSilenceMs = 8000;
double constexpr kEventMinToTurnS = 15;

/// "Rest area in 3 miles": one 1.5 to 10 km ahead, after this long without a stop.
double constexpr kRestAheadMinM = 1500;
double constexpr kRestAheadMaxM = 10000;
double constexpr kRestAfterDrivingMs = 75 * 60000;
double constexpr kRestAheadEveryMs = 45 * 60000;
/// Gone this far past it: the rest area was passed.
double constexpr kPassedM = 200;
/// The rest-stop survey goes out between these distances past a rest area.
double constexpr kSurveyFromM = 800;
double constexpr kSurveyToM = 6000;
/// A stop at a rest area shorter than this is a "fast stop"; under the first it's just a pause.
double constexpr kRestQuickMinMs = 30000;
double constexpr kRestBackMinMs = 3 * 60000;

/// "No gas stations for the next 50 miles": only when there's that much road with none.
double constexpr kNoGasMinM = 40000;
/// "The nearest coffee is 30 miles away", in the morning.
double constexpr kCoffeeFarM = 25000;
/// "Scenic overlook in 3 miles."
double constexpr kViewpointMinM = 2000;
double constexpr kViewpointMaxM = 6000;

/// On the highway this long, with the next maneuver at least this far off.
double constexpr kHighwayAfterMs = 10 * 60000;
double constexpr kHighwayMinToTurnS = 240;
int constexpr kHighwayPerTrip = 2;
double constexpr kHighwayEveryMs = 30 * 60000;
double constexpr kGravelAfterMs = 15000;
double constexpr kWildAfterMs = 60000;
double constexpr kVillageWithinMs = 60000;
double constexpr kVillageMaxMps = 25;
double constexpr kFarmWithinMs = 10000;
double constexpr kFarmMinMps = 8;
double constexpr kNightAfterStartMs = 10 * 60000;

/// Signal: lost for a minute before saying so; back after at least three.
double constexpr kSignalLostAfterMs = 60000;
double constexpr kSignalBackAfterMs = 3 * 60000;

/// Drive-thru: inside this radius of where the car first stopped is still the line.
double constexpr kDriveThruRadiusM = 80;
double constexpr kDriveThruInAfterMs = 15000;
double constexpr kDriveThruLongMs = 5 * 60000;
double constexpr kRoadStoppedMps = 0.5;
double constexpr kMovingMps = 4;
double constexpr kSpotAfterMs = 10000;
double constexpr kCaughtAfterMs = 20000;

/// How long to wait for an answer.
double constexpr kAnswerMs = 15000;

/// Lines that only play some of the time after their first ever.
double constexpr kSometimes = 0.5;

double RoadGroundM(LatLon const & a, LatLon const & b)
{
  double constexpr kEarthRadiusM = 6371000;
  double const rad = M_PI / 180;
  double const dLat = (b.lat - a.lat) * rad;
  double const dLon = (b.lon - a.lon) * rad;
  double const h = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(a.lat * rad) * std::cos(b.lat * rad) * std::sin(dLon / 2) * std::sin(dLon / 2);
  return 2 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(h)));
}

bool Has(std::vector<std::string> const & v, std::string const & s) { return std::find(v.begin(), v.end(), s) != v.end(); }

bool RoadHasWord(std::string const & words, char const * w) { return words.find(w) != std::string::npos; }

std::string RoadLower(std::string s)
{
  for (auto & c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

/// The note that holds the driver's answer to the bathroom question: "rest" or "gas".
char const * const kBathroomNote = "note.bathroom";
}  // namespace

Weather WeatherFromNws(std::string const & description, double windMps, double gustMps, double tempC)
{
  std::string const d = RoadLower(description);
  auto const has = [&d](char const * w) { return d.find(w) != std::string::npos; };
  Weather w;
  w.snow = has("snow") || has("sleet") || has("ice pellets") || has("freezing");
  w.rain = !w.snow && (has("rain") || has("drizzle") || has("shower") || has("thunderstorm"));
  w.fog = has("fog") || has("haze") || has("mist") || has("smoke");
  // Breezy starts around 20 mph; gusts past 35 mph push a car around.
  w.windy = has("wind") || has("breezy") || windMps >= 9 || gustMps >= 15.5;
  w.clear = !w.rain && !w.snow && !w.fog && (has("sunny") || has("clear") || has("fair"));
  if (!std::isnan(tempC))
    w.tempC = tempC;
  return w;
}

void RoadBits::OnConnectivity(double tMs, bool online)
{
  if (!online)
  {
    if (!m_offlineSince)
      m_offlineSince = tMs;
    return;
  }
  if (!m_offlineSince)
    return;
  double const offMs = tMs - *m_offlineSince;
  m_offlineSince.reset();
  if (m_signalLostSaid && !m_signalBackSaid && offMs >= kSignalBackAfterMs)
  {
    m_signalBackSaid = true;
    Queue(tMs, {"signal.back", {}}, 60000);
  }
}

void RoadBits::OnWeather(double /* tMs */, Weather const & weather) { m_weather = weather; }

void RoadBits::Queue(double tMs, RoadRemark remark, double validMs)
{
  m_pending.push_back({std::move(remark), tMs + validMs});
}

std::string RoadBits::Distance(double meters) const
{
  // Long ways are said in whole miles or kilometers: "50 miles", not "49.7 miles".
  double const unitM = m_units == Units::Mph ? 1609.344 : 1000;
  if (meters >= 10 * unitM)
    return JsNumber(JsRound(meters / unitM)) + (m_units == Units::Mph ? " miles" : " kilometers");
  return FormatDistance(meters, m_units);
}

std::optional<StopoverAhead> RoadBits::Nearest(Stopover kind) const
{
  for (auto const & s : m_ahead.stops)
    if (s.kind == kind && AheadM(s) > 0)
      return s;
  return std::nullopt;
}

void RoadBits::TrackSurroundings(RoadTick const & k)
{
  if (!m_surroundings || k.tMs - m_lastSurroundT < kSurroundEveryMs || k.speedMps < 3)
    return;
  m_lastSurroundT = k.tMs;
  Surroundings const s = m_surroundings(k.here, k.bearingDeg);
  auto const since = [&k](bool on, double & t)
  {
    if (!on)
      t = -1;
    else if (t < 0)
      t = k.tMs;
  };
  since(s.motorway, m_motorwaySince);
  since(s.unpaved, m_unpavedSince);
  since(s.wild, m_wildSince);
  if (s.village && !m_inVillage)
    m_villageEnteredT = k.tMs;
  m_inVillage = s.village;
  if (s.farmLeft != s.farmRight)
  {
    m_farmT = k.tMs;
    m_farmOnLeft = s.farmLeft;
  }
}

void RoadBits::TrackRouteAhead(RoadTick const & k)
{
  if (!m_routeAhead || !k.steady || k.tMs - m_lastAheadT < kAheadEveryMs)
    return;
  m_lastAheadT = k.tMs;
  m_ahead = m_routeAhead(kAheadMaxM);
  std::sort(m_ahead.stops.begin(), m_ahead.stops.end(),
            [](StopoverAhead const & a, StopoverAhead const & b) { return a.distM < b.distM; });
  m_aheadOdoM = m_odoM;
  // Watch the nearest rest area and viewpoint by where they are on the odometer, so passing them
  // shows up between polls.
  if (auto const rest = Nearest(Stopover::RestArea))
  {
    double const at = m_odoM + rest->distM;
    if (!m_restAtOdoM || std::fabs(at - *m_restAtOdoM) > 500)
    {
      m_restAtOdoM = at;
      m_restAnnounced = false;
    }
  }
}

void RoadBits::TrackStops(RoadTick const & k, SaidLog & said)
{
  if (k.speedMps < 0)
    return;
  if (!m_stopSince)
  {
    if (k.speedMps < kRoadStoppedMps)
    {
      m_stopSince = k.tMs;
      m_stopAt = k.here;
      m_spotChecked = false;
      m_caughtThisStop = false;
      if (m_driveThruAt && RoadGroundM(*m_driveThruAt, k.here) <= kDriveThruRadiusM)
        ++m_driveThruCreeps;
      // Still in the drive-thru line: keep its spot.
      if (!m_driveThruAt)
        m_spot = StopSpot::None;
    }
    else if (m_driveThruAt && RoadGroundM(*m_driveThruAt, k.here) > kDriveThruRadiusM && k.speedMps >= kMovingMps)
    {
      // Out of the drive-thru with the food (or out of a parking spot at a fast food place: quiet).
      bool const wasInLine = m_driveThruCreeps >= 1;
      m_driveThruAt.reset();
      m_spot = StopSpot::None;
      if (wasInLine)
        Queue(k.tMs, {"driveThru.leaving", {}}, 60000);
    }
    return;
  }
  double const stoppedMs = k.tMs - *m_stopSince;
  if (k.speedMps >= kMovingMps)
  {
    m_stopSince.reset();
    if (m_spot == StopSpot::RestArea)
    {
      if (stoppedMs >= kRestBackMinMs)
        Queue(k.tMs, {"rest.back", {}}, 60000);
      else if (stoppedMs >= kRestQuickMinMs)
        Queue(k.tMs, {"rest.quick", {}}, 60000);
    }
    // Inching up in the drive-thru line isn't the end of the stop.
    if (!m_driveThruAt)
      m_spot = StopSpot::None;
    if (stoppedMs >= kRestQuickMinMs)
      m_drivingSinceT = k.tMs;
    return;
  }
  if (!m_spotChecked && stoppedMs >= kSpotAfterMs)
  {
    m_spotChecked = true;
    StopSpot const spot = m_stopSpot ? m_stopSpot(m_stopAt) : StopSpot::None;
    if (!m_driveThruAt || spot != StopSpot::None)
      m_spot = spot;
    if (spot == StopSpot::RestArea)
      m_restStopOdoM = m_odoM;
    Trace("road.stop", {{"spot", spot == StopSpot::RestArea    ? "restArea"
                                 : spot == StopSpot::DriveThru ? "driveThru"
                                 : spot == StopSpot::Fuel      ? "fuel"
                                                               : "none"}});
    if (m_spot == StopSpot::DriveThru && !m_driveThruAt)
    {
      m_driveThruAt = m_stopAt;
      m_driveThruSince = *m_stopSince;
      m_driveThruLongSaid = false;
      m_driveThruCreeps = 0;
    }
  }
  if (m_driveThruAt && m_driveThruCreeps >= 1 && stoppedMs >= kDriveThruInAfterMs && !Has(m_saidThisTrip, "driveThru.in"))
  {
    m_saidThisTrip.push_back("driveThru.in");
    Queue(k.tMs, {"driveThru.in", {}}, 60000);
  }
  if (m_driveThruAt && m_driveThruCreeps >= 2 && !m_driveThruLongSaid && k.tMs - m_driveThruSince >= kDriveThruLongMs)
  {
    m_driveThruLongSaid = true;
    Queue(k.tMs, {"driveThru.long", {}}, 60000);
  }
  // The bathroom trap: they said rest stops, and here we are at a gas station (or the other way).
  if (!m_caughtThisStop && stoppedMs >= kCaughtAfterMs && (m_spot == StopSpot::Fuel || m_spot == StopSpot::RestArea))
  {
    m_caughtThisStop = true;
    std::string const answer = said.Recalled(kBathroomNote);
    bool const caught = (answer == "rest" && m_spot == StopSpot::Fuel) || (answer == "gas" && m_spot == StopSpot::RestArea);
    if (caught)
    {
      // Caught once; then the question can come back some other trip.
      said.Forget(kBathroomNote);
      Queue(k.tMs, {"trap.bathroom.caught", {{"said", answer == "rest" ? "rest stops" : "gas stations"}}}, 120000);
    }
  }
}

std::vector<RoadRemark> RoadBits::Tick(RoadTick const & k, SaidLog & said, double r)
{
  std::vector<RoadRemark> out;
  if (m_lastHere)
  {
    double const step = RoadGroundM(*m_lastHere, k.here);
    // A jump (a fix after a tunnel, a bad fix) isn't driving.
    if (step < 2000)
      m_odoM += step;
  }
  m_lastHere = k.here;
  if (m_drivingSinceT < 0)
    m_drivingSinceT = k.tMs - k.tripMs;

  TrackSurroundings(k);
  TrackRouteAhead(k);
  TrackStops(k, said);

  // Sunset during the trip.
  if (!k.night)
    m_sawDaylight = true;
  else if (m_sawDaylight && !m_nightSaid)
  {
    m_nightSaid = true;
    Queue(k.tMs, {"night.sunset", {}}, 10 * 60000);
  }

  // Signal gone for a while.
  if (m_offlineSince && !m_signalLostSaid && k.tMs - *m_offlineSince >= kSignalLostAfterMs &&
      k.speedMps >= std::min(kAmbientMinMps, k.signalMinMps))
  {
    m_signalLostSaid = true;
    Queue(k.tMs, {"signal.lost", {}}, 60000);
  }

  // Passing the rest area being watched.
  if (m_restAtOdoM && m_odoM > *m_restAtOdoM + kPassedM)
  {
    bool const stoppedThere = m_restStopOdoM && std::fabs(*m_restStopOdoM - *m_restAtOdoM) < 1000;
    if (!stoppedThere)
    {
      if (m_restAnnounced)
        Queue(k.tMs, {"rest.passed", {}}, 60000);
      else if (!Has(m_saidThisTrip, "rest.survey") && !Has(m_rolled, "rest.survey"))
      {
        m_rolled.push_back("rest.survey");
        // The first one ever always; after that, half the time.
        if (!said.Recalled("note.survey").empty() ? r < kSometimes : true)
          m_surveyArmed = true;
      }
    }
    m_restPassedOdoM = *m_restAtOdoM;
    m_restAtOdoM.reset();
    m_restAnnounced = false;
  }
  if (m_viewpointAtOdoM && m_odoM > *m_viewpointAtOdoM + kPassedM)
  {
    m_viewpointAtOdoM.reset();
    Queue(k.tMs, {"viewpoint.passed", {}}, 60000);
  }

  // An answer that never came.
  if (m_await != Await::None && k.tMs > m_awaitUntil)
  {
    Await const was = m_await;
    m_await = Await::None;
    if (was == Await::Rate)
      out.push_back({"trap.rate.after", {}});
    else if (was == Await::Survey && m_restPassedOdoM)
      out.push_back({"rest.surveyAfter", {{"distance", Distance(m_odoM - *m_restPassedOdoM)}}});
    if (!out.empty())
      return out;
  }

  if (k.busy || m_await != Await::None)
    return out;

  // Things that just happened, oldest first, once there's a short gap.
  m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(), [&k](Pending const & p) { return k.tMs > p.until; }),
                  m_pending.end());
  bool const eventGap = k.sinceSpokenMs >= kEventSilenceMs && (!k.steady || k.toTurnS >= kEventMinToTurnS);
  if (!m_pending.empty() && eventGap)
  {
    out.push_back(std::move(m_pending.front().remark));
    m_pending.erase(m_pending.begin());
    Trace("road", {{"key", out.back().key}});
    return out;
  }

  // The rest-stop survey, a little way past a rest area.
  if (m_surveyArmed && m_restPassedOdoM && k.steady && k.sinceSpokenMs >= kAmbientSilenceMs &&
      k.toTurnS >= kAmbientMinToTurnS)
  {
    double const past = m_odoM - *m_restPassedOdoM;
    if (past > kSurveyToM)
      m_surveyArmed = false;
    else if (past >= kSurveyFromM)
    {
      m_surveyArmed = false;
      m_saidThisTrip.push_back("rest.survey");
      said.Remember("note.survey", "asked");
      m_await = Await::Survey;
      m_awaitUntil = k.tMs + kAnswerMs;
      Trace("road", {{"key", "rest.survey"}});
      out.push_back({"rest.survey", {}});
      return out;
    }
  }

  if (auto remark = Ambient(k, said, r))
  {
    Trace("road", {{"key", remark->key}});
    out.push_back(std::move(*remark));
  }
  return out;
}

std::optional<RoadRemark> RoadBits::Ambient(RoadTick const & k, SaidLog & said, double r)
{
  if (!k.steady || k.speedMps < kAmbientMinMps || k.tripMs < kAmbientAfterStartMs ||
      k.sinceSpokenMs < kAmbientSilenceMs || k.toTurnS < kAmbientMinToTurnS || m_ambientSaid >= kRoadAmbientPerTrip ||
      k.tMs - m_lastAmbientT < kRoadAmbientEveryMs)
    return std::nullopt;
  auto const once = [this](std::string const & key) { return !Has(m_saidThisTrip, key); };
  // A line that only plays some of the time: the first one ever always, then one roll per trip.
  auto const lucky = [&](std::string const & key)
  {
    if (!said.Said(key))
      return true;
    if (Has(m_rolled, key))
      return false;
    m_rolled.push_back(key);
    return r < kSometimes;
  };
  auto const pick = [&](std::string key, Vars vars = {}) -> std::optional<RoadRemark>
  {
    m_saidThisTrip.push_back(key);
    m_lastAmbientT = k.tMs;
    ++m_ambientSaid;
    return RoadRemark{std::move(key), std::move(vars)};
  };

  // The weather, once it's known, and again if it turns into something else.
  if (m_weather && m_weatherSaid.size() < 2)
  {
    Weather const & w = *m_weather;
    std::string key = w.snow    ? "weather.snow"
                      : w.fog   ? "weather.fog"
                      : w.rain  ? "weather.wet"
                      : w.windy ? "weather.wind"
                      : w.clear ? "weather.clear"
                                : "";
    // Sunny and clear is only worth saying in daylight, and not every trip.
    if (key == "weather.clear" && (k.night || !m_weatherSaid.empty()))
      key.clear();
    if (!key.empty() && !Has(m_weatherSaid, key))
    {
      m_weatherSaid.push_back(key);
      if (key != "weather.clear" || lucky(key))
        return pick(key);
    }
  }

  // A rest area coming up, after a long time without a stop.
  if (m_restAtOdoM && !m_restAnnounced && k.tMs - m_drivingSinceT >= kRestAfterDrivingMs &&
      k.tMs - m_lastRestAheadT >= kRestAheadEveryMs)
  {
    double const ahead = *m_restAtOdoM - m_odoM;
    if (ahead >= kRestAheadMinM && ahead <= kRestAheadMaxM)
    {
      m_restAnnounced = true;
      m_lastRestAheadT = k.tMs;
      return pick("rest.ahead", {{"distance", Distance(ahead)}});
    }
  }

  // No gas for a long way.
  if (once("gas.none") && m_routeAhead && m_ahead.searchedM >= kNoGasMinM && !Nearest(Stopover::Fuel))
  {
    double const left = m_ahead.searchedM - (m_odoM - m_aheadOdoM);
    if (left >= kNoGasMinM)
      return pick("gas.none", {{"distance", Distance(left)}});
  }

  // A scenic overlook: announced, and then "That was it" once it's behind us.
  if (once("viewpoint.ahead") && !Has(m_rolled, "viewpoint.ahead"))
    if (auto const v = Nearest(Stopover::Viewpoint))
    {
      double const ahead = AheadM(*v);
      if (ahead >= kViewpointMinM && ahead <= kViewpointMaxM)
      {
        if (lucky("viewpoint.ahead"))
        {
          m_viewpointAtOdoM = m_odoM + ahead;
          return pick("viewpoint.ahead", {{"distance", Distance(ahead)}});
        }
        m_rolled.push_back("viewpoint.ahead");
      }
    }

  // Morning, and coffee is a long way off.
  if (once("coffee.far") && k.localHour >= 5 && k.localHour <= 10)
    if (auto const c = Nearest(Stopover::Coffee); c && AheadM(*c) >= kCoffeeFarM)
      return pick("coffee.far", {{"distance", Distance(AheadM(*c))}});

  // A long stretch of highway.
  if (m_motorwaySince >= 0 && k.tMs - m_motorwaySince >= kHighwayAfterMs && k.toTurnS >= kHighwayMinToTurnS &&
      m_highwaySaid < kHighwayPerTrip && k.tMs - m_lastHighwayT >= kHighwayEveryMs)
  {
    ++m_highwaySaid;
    m_lastHighwayT = k.tMs;
    return pick("road.highway");
  }

  if (once("road.smallTown") && k.tMs - m_villageEnteredT <= kVillageWithinMs && m_inVillage &&
      k.speedMps <= kVillageMaxMps && lucky("road.smallTown"))
    return pick("road.smallTown");

  if (once("road.gravel") && m_unpavedSince >= 0 && k.tMs - m_unpavedSince >= kGravelAfterMs)
    return pick("road.gravel");

  if (once("road.farm") && k.tMs - m_farmT <= kFarmWithinMs && k.speedMps >= kFarmMinMps && lucky("road.farm"))
    return pick("road.farm", {{"side", m_farmOnLeft ? "left" : "right"}});

  if (once("road.nature") && m_wildSince >= 0 && k.tMs - m_wildSince >= kWildAfterMs)
    return pick("road.nature");

  // Driving in the dark: once a trip, unless the sunset already got its line.
  if (!m_nightSaid && k.night && k.tripMs >= kNightAfterStartMs)
  {
    m_nightSaid = true;
    bool const late = k.localHour >= 23 || (k.localHour >= 0 && k.localHour <= 3);
    return pick(late ? "night.late" : "night.dark");
  }
  return std::nullopt;
}

std::optional<RoadRemark> RoadBits::StartTrap(double tMs, SaidLog & said)
{
  if (m_trapAsked || m_await != Await::None)
    return std::nullopt;
  m_trapAsked = true;
  // The bathroom question until it's answered; then the rating.
  if (said.Recalled(kBathroomNote).empty())
  {
    m_await = Await::Bathroom;
    m_awaitUntil = tMs + kAnswerMs;
    return RoadRemark{"trap.bathroom.ask", {}};
  }
  m_await = Await::Rate;
  m_awaitUntil = tMs + kAnswerMs;
  return RoadRemark{"trap.rate.ask", {}};
}

std::optional<RoadRemark> RoadBits::OnAnswer(double tMs, std::string const & words, SaidLog & said)
{
  if (!AwaitingAnswer(tMs))
    return std::nullopt;
  Await const was = m_await;
  m_await = Await::None;
  switch (was)
  {
  case Await::Bathroom:
  {
    bool const rest = RoadHasWord(words, " rest ");
    bool const gas = RoadHasWord(words, " gas ") || RoadHasWord(words, " station ") || RoadHasWord(words, " stations ");
    if (rest == gas)
      return std::nullopt;
    said.Remember(kBathroomNote, rest ? "rest" : "gas");
    return RoadRemark{"file.saved", {{"answer", rest ? "Rest stops" : "Gas stations"}}};
  }
  case Await::Rate: return RoadRemark{"trap.rate.after", {}};
  case Await::Survey:
    if (!m_restPassedOdoM)
      return std::nullopt;
    return RoadRemark{"rest.surveyAfter", {{"distance", Distance(m_odoM - *m_restPassedOdoM)}}};
  case Await::None: break;
  }
  return std::nullopt;
}

void RoadBits::Reset()
{
  Units const units = m_units;
  auto surroundings = std::move(m_surroundings);
  auto routeAhead = std::move(m_routeAhead);
  auto stopSpot = std::move(m_stopSpot);
  std::optional<double> const offlineSince = m_offlineSince;
  std::optional<Weather> const weather = m_weather;
  *this = RoadBits(units);
  m_surroundings = std::move(surroundings);
  m_routeAhead = std::move(routeAhead);
  m_stopSpot = std::move(stopSpot);
  // Signal and weather carry over from before the trip.
  m_offlineSince = offlineSince;
  m_weather = weather;
}
}  // namespace backseat
