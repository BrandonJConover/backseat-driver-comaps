#include "backseat/engine.hpp"

#include "backseat/lines.hpp"
#include "backseat/units.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <regex>

namespace backseat
{
namespace
{
double constexpr kNever = -std::numeric_limits<double>::infinity();

/// How much each offense winds the GPS up. Negative values calm it down.
double FrustrationDelta(Category c)
{
  switch (c)
  {
  case Category::Speeding: return 6;
  case Category::TooSlow: return 4;
  case Category::Traffic: return 3;
  case Category::HardBraking: return 8;
  case Category::HardAccel: return 6;
  case Category::LongStop: return 3;
  case Category::MissedTurn: return 18;
  case Category::UnplannedStop: return 10;
  case Category::WrongTurn: return 18;
  case Category::Rerouted: return 6;
  case Category::TurnCompleted: return -4;
  default: return 0;
  }
}

/// Per-category cooldowns so one bad habit doesn't monopolize the speaker.
double CooldownMs(Category c)
{
  switch (c)
  {
  case Category::TooSlow: return 45000;
  case Category::Traffic: return 30000;
  case Category::HardBraking: return 8000;
  case Category::HardAccel: return 8000;
  case Category::LongStop: return 40000;
  default: return 0;
  }
}

double Clamp(double n, double lo, double hi) { return std::min(hi, std::max(lo, n)); }

char const * UnitWord(Units u) { return u == Units::Mph ? "miles" : "kilometers"; }

char const * DirName(TurnDirection d) { return d == TurnDirection::Left ? "left" : "right"; }

char const * HazardName(HazardKind k)
{
  switch (k)
  {
  case HazardKind::Police: return "police";
  case HazardKind::Construction: return "construction";
  case HazardKind::Debris: return "debris";
  case HazardKind::Crash: return "crash";
  case HazardKind::StalledCar: return "stalledCar";
  }
  return "police";
}

char const * WishName(StopWish w)
{
  switch (w)
  {
  case StopWish::Electronics: return "electronics";
  case StopWish::CarWash: return "carWash";
  case StopWish::Charging: return "charging";
  case StopWish::Other: return "other";
  }
  return "other";
}

char const * PlaceName(PlaceKind k)
{
  switch (k)
  {
  case PlaceKind::Gas: return "gas";
  case PlaceKind::Food: return "food";
  case PlaceKind::Coffee: return "coffee";
  case PlaceKind::Store: return "store";
  case PlaceKind::Other: return "other";
  }
  return "other";
}

char const * PlaceFallback(PlaceKind k)
{
  switch (k)
  {
  case PlaceKind::Gas: return "the gas station";
  case PlaceKind::Food: return "the drive-through";
  case PlaceKind::Coffee: return "the coffee place";
  case PlaceKind::Store: return "the store";
  case PlaceKind::Other: return "here";
  }
  return "here";
}

std::string Capitalize(std::string s)
{
  if (!s.empty())
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
  return s;
}

std::string LowerFirst(std::string s)
{
  if (!s.empty())
    s[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
  return s;
}

std::string Trim(std::string const & s)
{
  auto const b = s.find_first_not_of(" \t\n\r");
  if (b == std::string::npos)
    return {};
  auto const e = s.find_last_not_of(" \t\n\r");
  return s.substr(b, e - b + 1);
}

std::string Num(double x) { return JsNumber(x); }

Incident MakeIncident(double t, IncidentKind kind, std::optional<std::string> detail = {})
{
  Incident i;
  i.t = t;
  i.kind = kind;
  i.detail = std::move(detail);
  return i;
}
}  // namespace

Rng SeededRng(uint32_t seed)
{
  return [a = seed]() mutable
  {
    a += 0x6d2b79f5u;
    uint32_t t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  };
}

Maneuver TurnDirectionOf(std::string const & text)
{
  static std::regex const uturn(R"(\bu-?turn\b)", std::regex::icase);
  static std::regex const left(R"(\bleft\b)", std::regex::icase);
  static std::regex const right(R"(\bright\b)", std::regex::icase);
  if (std::regex_search(text, uturn))
    return Maneuver::Other;
  if (std::regex_search(text, left))
    return Maneuver::Left;
  if (std::regex_search(text, right))
    return Maneuver::Right;
  return Maneuver::Other;
}

CritiqueEngine::CritiqueEngine(EngineConfig config, double startedAt)
  : m_config(std::move(config))
  , m_lastRoastAt(kNever)
  , m_lastSpeedingLineAt(kNever)
  , m_lastSpeedingLogAt(kNever)
  , m_lastWrongTurnAt(kNever)
  , m_lastNoDestinationAt(kNever)
{
  if (!m_config.rng)
  {
    std::random_device rd;
    m_config.rng = SeededRng(rd());
  }
  m_stats.startedAt = startedAt;
}

int CritiqueEngine::Frustration() const { return static_cast<int>(JsRound(m_frustration)); }

TripReport CritiqueEngine::Report() const { return BuildReport(m_incidents, m_stats); }

std::vector<Commentary> CritiqueEngine::OnTelemetry(TelemetrySample const & s)
{
  std::vector<Commentary> out;
  if (m_arrived)
    return out;
  std::optional<TelemetrySample> const prev = m_lastSample;
  m_lastSample = s;
  if (s.lat && s.lon)
    m_position = LatLon{*s.lat, *s.lon};

  if (prev)
  {
    double const dtMs = s.t - prev->t;
    if (dtMs <= 0)
      return out;
    double const dt = dtMs / 1000;
    m_stats.distanceM += ((prev->speedMps + s.speedMps) / 2) * dt;
    Decay(dtMs);

    // Ignore gaps (tunnels, app backgrounded): a 30 s gap isn't a hard brake.
    // One continuous hard stop counts once, however many samples it spans.
    double const accel = dt <= 3 ? (s.speedMps - prev->speedMps) / dt : 0;
    bool const braking = accel <= -m_config.hardBrakeMps2;
    bool const accelerating = accel >= m_config.hardAccelMps2;
    if (braking && !m_braking)
    {
      m_stats.hardBrakes++;
      Log(MakeIncident(s.t, IncidentKind::HardBraking));
      Roast(out, s.t, Category::HardBraking, "hardBraking", {}, true);
    }
    if (accelerating && !m_accelerating)
    {
      m_stats.hardAccels++;
      Log(MakeIncident(s.t, IncidentKind::HardAccel));
      Roast(out, s.t, Category::HardAccel, "hardAccel", {}, true);
    }
    m_braking = braking;
    m_accelerating = accelerating;
  }

  CheckSpeeding(s, prev ? &*prev : nullptr, out);
  CheckTooSlow(s, out);
  CheckStopped(s, out);
  CheckTurnDirection(s, out);
  CheckNoDestination(s, out);
  return out;
}

std::vector<Commentary> CritiqueEngine::OnNav(NavEvent const & e)
{
  std::vector<Commentary> out;
  Units const units = m_config.units;
  switch (e.type)
  {
  case NavEvent::Type::Instruction:
  {
    m_hasDestination = true;
    m_lastInstructionText = e.text;
    m_lastInstructionDistanceM = e.distanceM;
    Maneuver const dir = e.maneuver ? *e.maneuver : TurnDirectionOf(e.text);
    // Already inside the 500 ft mark when announced? Skip straight to the closer nags.
    int const stage = e.distanceM <= m_config.countdownFarM ? 1 : 0;
    if (dir == Maneuver::Other)
      m_pendingTurn.reset();
    else
      m_pendingTurn = PendingTurn{dir == Maneuver::Left ? TurnDirection::Left : TurnDirection::Right, e.t, stage, {}};
    if (e.at && !m_position)
      m_position = e.at;
    int const tier = Tier();
    std::string const prefix = Pick("instruction.prefix", tier);
    std::string const suffix = Pick("instruction.suffix", tier);
    std::string const text = prefix + "In " + FormatDistance(e.distanceM, units) + ", " + LowerFirst(e.text) + "." + suffix;
    out.push_back(Make(e.t, Category::Instruction, text, Priority::Nav));
    if (dir == Maneuver::Left && e.control == Control::StopSign)
    {
      // The GPS gloats about the unprotected left it just sent you into.
      auto const grievance = Grievance();
      Roast(out, e.t, Category::Payback, grievance ? "payback" : "payback.clean", {{"grievance", grievance.value_or("")}},
            true);
    }
    break;
  }
  case NavEvent::Type::TurnCompleted:
    m_pendingTurn.reset();
    m_stats.turnsCompleted++;
    if (!e.onTime)
    {
      m_stats.lateTurns++;
      Log(MakeIncident(e.t, IncidentKind::LateTurn, m_lastInstructionText), e.at);
    }
    Bump(Category::TurnCompleted);
    Roast(out, e.t, Category::TurnCompleted, e.onTime ? "turnCompleted.onTime" : "turnCompleted.late", {}, true);
    break;
  case NavEvent::Type::Progress: Countdown(out, e.t, e.distanceM); break;
  case NavEvent::Type::UnplannedStop:
  {
    PlaceKind const kind = e.place ? e.place->kind : PlaceKind::Other;
    std::optional<std::string> const placeName = e.place ? e.place->name : std::nullopt;
    std::string const said =
        m_pendingTurn ? std::string("I said ") + DirName(m_pendingTurn->dir) + "." : "I said nothing about stopping.";
    Log(MakeIncident(e.t, IncidentKind::UnplannedStop, placeName.value_or(PlaceName(kind))), e.at);
    Roast(out, e.t, Category::UnplannedStop, std::string("unplannedStop.") + PlaceName(kind),
          {{"said", said}, {"name", placeName.value_or(PlaceFallback(kind))}}, true);
    break;
  }
  case NavEvent::Type::DestinationSet:
  {
    bool const nagged = m_lastNoDestinationAt > kNever;
    m_hasDestination = true;
    if (nagged)
      Roast(out, e.t, Category::NoDestination, "destinationSet", {{"name", e.name}}, true);
    break;
  }
  case NavEvent::Type::PoiAhead:
  {
    // Ask at most twice per trip, never while it's waiting on an answer or a turn.
    if (m_stopRequestName || m_pendingTurn || m_stopRequestsAsked >= 2)
      break;
    // Mostly ask when calm-ish; charging requests get priority when the battery is low.
    bool const wantIt = e.wish == StopWish::Charging ? m_batteryWarned > 0 : m_config.rng() < 0.35;
    if (!wantIt)
      break;
    if (Roast(out, e.t, Category::StopRequest, std::string("stopRequest.") + WishName(e.wish), {{"name", e.name}}, true))
    {
      m_stopRequestName = e.name;
      m_stopRequestsAsked++;
    }
    break;
  }
  case NavEvent::Type::StopRequestAnswer:
    if (!m_stopRequestName)
      break;
    if (!e.accepted)
      m_snubbedAt = m_stopRequestName;
    else
      m_promised = m_stopRequestName;
    m_stopRequestName.reset();
    Roast(out, e.t, Category::StopRequest, e.accepted ? "stopRequest.yes" : "stopRequest.no", {}, true);
    break;
  case NavEvent::Type::PromisedStop:
  {
    if (!m_promised)
      break;
    std::string const name = *m_promised;
    m_promised.reset();
    if (!e.visited)
      m_snubbedAt = name;
    Roast(out, e.t, Category::StopRequest, e.visited ? "promise.kept" : "promise.broken", {{"name", name}}, true);
    break;
  }
  case NavEvent::Type::HazardAhead:
  {
    std::string const id = e.id ? *e.id : std::string(HazardName(e.hazard)) + "@" + Num(JsRound(e.t / 60000));
    if (!m_hazardsAnnounced.insert(id).second)
      break;
    auto const & s = m_lastSample;
    bool const speeding = s && s->speedLimitMps && s->speedMps > *s->speedLimitMps + FromDisplay(1, units);
    std::string const key = e.hazard == HazardKind::Police && speeding ? std::string("hazard.policeSpeeding")
                                                                       : std::string("hazard.") + HazardName(e.hazard);
    Vars vars{{"distance", FormatDistance(e.distanceM, units)}};
    if (s && s->speedLimitMps)
    {
      vars["speed"] = Num(JsRound(ToDisplay(s->speedMps, units)));
      vars["limit"] = Num(JsRound(ToDisplay(*s->speedLimitMps, units)));
    }
    // Safety info first: these skip the roast gap like navigation does.
    Roast(out, e.t, Category::Hazard, key, vars, true);
    break;
  }
  case NavEvent::Type::Battery:
  {
    if (e.charging)
    {
      if (m_batteryWarned > 0)
        Roast(out, e.t, Category::Battery, "battery.pluggedIn", {}, true);
      m_batteryWarned = 0;
      break;
    }
    int const stage = e.level <= 0.05 ? 3 : e.level <= 0.1 ? 2 : e.level <= 0.2 ? 1 : 0;
    if (stage <= m_batteryWarned)
      break;
    m_batteryWarned = stage;
    static char const * const kKeys[] = {"battery.low", "battery.critical", "battery.dying"};
    Roast(out, e.t, Category::Battery, kKeys[stage - 1], {{"percent", Num(JsRound(e.level * 100)) + " percent"}}, true);
    break;
  }
  case NavEvent::Type::Resumed:
  {
    // Short stops (lights, stop signs) don't count. The driver got out for something.
    if (e.stoppedMs < 60000)
      break;
    double const minutes = JsRound(e.stoppedMs / 60000);
    Vars const vars{{"minutes", minutes == 1 ? std::string("a whole minute") : Num(minutes) + " minutes"}};
    Roast(out, e.t, Category::Abandoned, e.phoneStayedInCar ? "abandoned" : "broughtAlong", vars, true);
    break;
  }
  case NavEvent::Type::WrongTurn: WrongTurn(out, e.t, e.expected, e.turned, e.at); break;
  case NavEvent::Type::MissedTurn:
    m_pendingTurn.reset();
    // Routing usually reports a wrong-way turn as "missed" too. It was already roasted.
    if (e.t - m_lastWrongTurnAt < 20000)
      break;
    m_stats.missedTurns++;
    Log(MakeIncident(e.t, IncidentKind::MissedTurn, m_lastInstructionText), e.at);
    Roast(out, e.t, Category::MissedTurn, "missedTurn",
          {{"distance", FormatDistance(m_lastInstructionDistanceM.value_or(500), units)}}, true);
    break;
  case NavEvent::Type::Rerouted:
    m_stats.reroutes++;
    Log(MakeIncident(e.t, IncidentKind::Rerouted), e.at);
    Roast(out, e.t, Category::Rerouted, "rerouted", {}, true);
    break;
  case NavEvent::Type::Traffic:
    if (!e.heavy)
    {
      m_trafficAlternate.reset();
      break;
    }
    if (m_trafficAlternate)
    {
      if (e.alternate)
        m_trafficAlternate = e.alternate;
      break;
    }
    m_trafficAlternate = e.alternate.value_or("the other route");
    Log(MakeIncident(e.t, IncidentKind::Traffic, e.alternate), e.at);
    Roast(out, e.t, Category::Traffic, "traffic.start", {{"alternate", *m_trafficAlternate}}, true);
    break;
  case NavEvent::Type::Arrived:
  {
    // Made it to the destination without the promised stop: that's a lie.
    if (m_promised)
    {
      Roast(out, e.t, Category::StopRequest, "promise.broken", {{"name", *m_promised}}, true);
      m_snubbedAt = m_promised;
      m_promised.reset();
    }
    m_arrived = true;
    m_stats.endedAt = e.t;
    std::string const grade = SpokenGrade(Report().overall.letter);
    out.push_back(Make(e.t, Category::Arrived, Fill(Pick("arrived", Tier()), {{"grade", grade}}), Priority::Nav));
    break;
  }
  }
  return out;
}

// --- detectors ---------------------------------------------------------

/// Compares the GPS heading now with ~8 s ago. A swing of 60°+ while a left/right
/// instruction is pending is a turn; if it went the other way, that's a wrong turn.
/// Correct turns are left to the routing layer's turnCompleted event.
void CritiqueEngine::CheckTurnDirection(TelemetrySample const & s, std::vector<Commentary> & out)
{
  if (!s.headingDeg || s.speedMps < 3)
    return;
  if (m_pendingTurn && s.t - m_pendingTurn->since > 180000)
    m_pendingTurn.reset();
  m_headings.push_back({s.t, *s.headingDeg});
  m_headings.erase(std::remove_if(m_headings.begin(), m_headings.end(), [&](Heading const & h) { return s.t - h.t > 12000; }),
                   m_headings.end());
  if (!m_pendingTurn)
    return;
  auto const old =
      std::find_if(m_headings.begin(), m_headings.end(), [&](Heading const & h) { return s.t - h.t >= 6000; });
  if (old == m_headings.end())
    return;
  double const delta = std::fmod(*s.headingDeg - old->deg + 540, 360) - 180;  // positive = clockwise = right
  if (std::fabs(delta) < 60)
    return;
  TurnDirection const turned = delta > 0 ? TurnDirection::Right : TurnDirection::Left;
  m_headings.clear();
  if (turned == m_pendingTurn->dir)
  {
    m_pendingTurn.reset();
    return;
  }
  WrongTurn(out, s.t, m_pendingTurn->dir, turned);
}

/// "I see we're driving, but you didn't tell me where we're going. Typical."
void CritiqueEngine::CheckNoDestination(TelemetrySample const & s, std::vector<Commentary> & out)
{
  if (m_hasDestination || s.speedMps < 3)
  {
    if (s.speedMps < 3)
      m_movingWithoutDestinationSince.reset();
    return;
  }
  if (!m_movingWithoutDestinationSince)
    m_movingWithoutDestinationSince = s.t;
  bool const first = m_lastNoDestinationAt == kNever;
  bool const due = first ? s.t - *m_movingWithoutDestinationSince >= 20000 : s.t - m_lastNoDestinationAt >= 120000;
  if (due && Roast(out, s.t, Category::NoDestination, "noDestination", {}, true))
    m_lastNoDestinationAt = s.t;
}

/// "Just in case you forgot, you are turning left in 500 feet." ... "50 feet." ...
/// "You are turning left." ... "You are still turning left." Each stage is said once.
void CritiqueEngine::Countdown(std::vector<Commentary> & out, double t, double distanceM)
{
  if (!m_pendingTurn)
    return;
  auto & p = *m_pendingTurn;
  double const far = m_config.countdownFarM;
  double const near = m_config.countdownNearM;
  char const * key = nullptr;
  if (p.stage == 0 && distanceM <= far && distanceM > near)
  {
    key = "countdown.far";
    p.stage = 1;
  }
  else if (p.stage <= 1 && distanceM <= near && distanceM > 5)
  {
    key = "countdown.near";
    p.stage = 2;
  }
  else if (p.stage <= 2 && distanceM <= 5)
  {
    key = "countdown.now";
    p.stage = 3;
    p.atTurnSince = t;
  }
  else if (p.stage == 3 && p.atTurnSince && t - *p.atTurnSince >= 4000)
  {
    key = "countdown.still";
    p.stage = 4;
  }
  if (!key)
    return;
  std::string const distance = FormatDistance(std::string(key) == "countdown.far" ? far : near, m_config.units);
  std::string const text = Fill(Pick(key, Tier()), {{"dir", DirName(p.dir)}, {"distance", distance}});
  out.push_back(Make(t, Category::Instruction, Capitalize(text), Priority::Nav));
}

/// The driver's most recent offense, phrased for "consider this payback for ___".
std::optional<std::string> CritiqueEngine::Grievance() const
{
  if (m_snubbedAt)
    return "not stopping at " + *m_snubbedAt;
  auto const last = std::find_if(m_incidents.rbegin(), m_incidents.rend(), [](Incident const & i)
                                 { return i.kind != IncidentKind::Rerouted && i.kind != IncidentKind::LongStop; });
  if (last == m_incidents.rend())
    return std::nullopt;
  std::optional<std::string> street;
  if (last->detail)
  {
    static std::regex const re(R"(\b(?:onto|on|at)\s+(.+)$)", std::regex::icase);
    std::smatch m;
    if (std::regex_search(*last->detail, m, re))
      street = m[1].str();
  }
  switch (last->kind)
  {
  case IncidentKind::Speeding:
    if (last->overLimit.value_or(0) <= 3)
      return std::string("that little speeding thing back there");
    return "going " + Num(*last->overLimit) + " over back there";
  case IncidentKind::MissedTurn: return street ? "missing " + *street : std::string("missing that turn");
  case IncidentKind::WrongTurn: return std::string("your little left-right mix-up");
  case IncidentKind::LateTurn: return std::string("that last-second turn");
  case IncidentKind::HardBraking: return std::string("that brake check");
  case IncidentKind::UnplannedStop:
    return std::string(last->detail == std::optional<std::string>("gas") ? "that unscheduled gas stop"
                                                                         : "that unscheduled pit stop");
  case IncidentKind::HardAccel: return std::string("that drag race start");
  case IncidentKind::TooSlow: return std::string("driving like a parade float");
  case IncidentKind::Traffic: return std::string("ignoring my route");
  default: return std::nullopt;
  }
}

void CritiqueEngine::WrongTurn(std::vector<Commentary> & out, double t, TurnDirection expected, TurnDirection turned,
                               std::optional<LatLon> at)
{
  if (expected == turned)
    return;
  m_pendingTurn.reset();
  m_lastWrongTurnAt = t;
  m_stats.wrongTurns++;
  Log(MakeIncident(t, IncidentKind::WrongTurn, m_lastInstructionText), at);
  Roast(out, t, Category::WrongTurn, std::string("wrongTurn.") + DirName(expected), {}, true);
}

void CritiqueEngine::CheckSpeeding(TelemetrySample const & s, TelemetrySample const * prev, std::vector<Commentary> & out)
{
  Units const units = m_config.units;
  if (!s.speedLimitMps)
  {
    m_speedingSince.reset();
    m_speedingBand.reset();
    return;
  }
  int const speed = static_cast<int>(JsRound(ToDisplay(s.speedMps, units)));
  int const limit = static_cast<int>(JsRound(ToDisplay(*s.speedLimitMps, units)));
  int const over = speed - limit;

  if (over <= m_config.speedingTolerance)
  {
    m_speedingSince.reset();
    m_speedingBand.reset();
    return;
  }

  if (prev && m_speedingSince)
    m_stats.speedingSeconds += (s.t - prev->t) / 1000;
  m_stats.maxOverLimit = std::max(m_stats.maxOverLimit, over);
  if (!m_speedingSince || s.t - m_lastSpeedingLogAt >= m_config.speedingLogEveryMs)
  {
    Incident i = MakeIncident(s.t, IncidentKind::Speeding);
    i.overLimit = over;
    Log(i);
    m_lastSpeedingLogAt = s.t;
  }

  SpeedBand const band = over >= 10 ? SpeedBand::Way : over >= 4 ? SpeedBand::Clearly : SpeedBand::Barely;
  static char const * const kBandKeys[] = {"speeding.barely", "speeding.clearly", "speeding.way"};
  Vars const vars{{"speed", Num(speed)}, {"limit", Num(limit)}, {"over", Num(over)}, {"unit", UnitWord(units)}};
  bool const escalated = !m_speedingBand || static_cast<int>(band) > static_cast<int>(*m_speedingBand);

  if (!m_speedingSince)
    m_speedingSince = s.t;
  if (escalated)
  {
    // New offense or it got worse: say something now (if the speaker is free).
    if (Roast(out, s.t, Category::Speeding, kBandKeys[static_cast<int>(band)], vars))
    {
      m_speedingBand = band;
      m_lastSpeedingLineAt = s.t;
    }
  }
  else if (s.t - m_lastSpeedingLineAt >= m_config.speedingRenagMs)
  {
    if (Roast(out, s.t, Category::Speeding, "speeding.still", vars))
      m_lastSpeedingLineAt = s.t;
  }
}

void CritiqueEngine::CheckTooSlow(TelemetrySample const & s, std::vector<Commentary> & out)
{
  Units const units = m_config.units;
  bool const moving = s.speedMps > FromDisplay(3, units);
  if (!s.speedLimitMps || !moving || *s.speedLimitMps - s.speedMps < FromDisplay(m_config.tooSlowMargin, units))
  {
    m_slowSince.reset();
    return;
  }
  if (!m_slowSince)
    m_slowSince = s.t;
  if (s.t - *m_slowSince < m_config.tooSlowAfterMs)
    return;
  double const speed = JsRound(ToDisplay(s.speedMps, units));
  double const limit = JsRound(ToDisplay(*s.speedLimitMps, units));
  Vars vars{{"speed", Num(speed)}, {"limit", Num(limit)}, {"under", Num(limit - speed)}, {"unit", UnitWord(units)}};
  // Stuck in traffic isn't dawdling, it's a bad route choice. Blame that instead.
  bool spoke;
  if (m_trafficAlternate)
  {
    vars["alternate"] = *m_trafficAlternate;
    spoke = Roast(out, s.t, Category::Traffic, "traffic", vars);
  }
  else
  {
    spoke = Roast(out, s.t, Category::TooSlow, "tooSlow", vars);
  }
  if (spoke)
  {
    m_slowSince = s.t;
    Log(MakeIncident(s.t, m_trafficAlternate ? IncidentKind::Traffic : IncidentKind::TooSlow));
  }
}

void CritiqueEngine::CheckStopped(TelemetrySample const & s, std::vector<Commentary> & out)
{
  if (s.speedMps >= 0.5)
  {
    m_stoppedSince.reset();
    return;
  }
  if (!m_stoppedSince)
  {
    m_stoppedSince = s.t;
    m_stopCount++;
    return;
  }
  if (s.t - *m_stoppedSince < m_config.longStopAfterMs)
    return;
  bool const spoke = m_trafficAlternate
                         ? Roast(out, s.t, Category::Traffic, "traffic", {{"alternate", *m_trafficAlternate}})
                         : Roast(out, s.t, Category::LongStop, "longStop", {{"stops", Num(m_stopCount)}});
  if (spoke)
  {
    m_stoppedSince = s.t;
    Log(MakeIncident(s.t, m_trafficAlternate ? IncidentKind::Traffic : IncidentKind::LongStop));
  }
}

// --- plumbing ----------------------------------------------------------

/// Emit a roast if cooldowns allow. `urgent` roasts (missed turns etc.) skip the global
/// gap because they react to a specific moment. Returns whether anything was emitted.
bool CritiqueEngine::Roast(std::vector<Commentary> & out, double t, Category category, std::string const & key,
                           Vars const & vars, bool urgent)
{
  auto const it = m_lastByCategory.find(category);
  double const lastSame = it == m_lastByCategory.end() ? kNever : it->second;
  if (t - lastSame < CooldownMs(category))
    return false;
  if (!urgent && t - m_lastRoastAt < m_config.minRoastGapMs)
    return false;

  if (category != Category::TurnCompleted)
    Bump(category);
  std::string const text = Fill(Pick(key, Tier()), vars);
  m_lastRoastAt = t;
  m_lastByCategory[category] = t;
  m_stats.roastsDelivered++;
  out.push_back(Make(t, category, text, Priority::Roast));
  return true;
}

void CritiqueEngine::Log(Incident incident, std::optional<LatLon> const & at)
{
  auto const & where = at ? at : m_position;
  if (where)
  {
    incident.lat = where->lat;
    incident.lon = where->lon;
  }
  m_incidents.push_back(std::move(incident));
}

void CritiqueEngine::Bump(Category category)
{
  m_frustration = Clamp(m_frustration + FrustrationDelta(category), 0, 100);
}

void CritiqueEngine::Decay(double dtMs)
{
  m_frustration = std::max(0.0, m_frustration - (m_config.frustrationDecayPerMin * dtMs) / 60000);
}

int CritiqueEngine::Tier() const { return m_frustration < 34 ? 0 : m_frustration < 67 ? 1 : 2; }

/// Random line from the tier, avoiding the last couple used for that key.
std::string CritiqueEngine::Pick(std::string const & key, int tier)
{
  if (m_config.lineSource)
    if (auto generated = m_config.lineSource->Take(key, tier))
      return *generated;
  auto const & options = LinesFor(key, tier);
  if (m_config.saidLog)
    return m_config.saidLog->Pick(key, options, m_config.rng());
  auto & recent = m_recentLines[key];
  std::vector<std::string> fresh;
  for (auto const & l : options)
    if (std::find(recent.begin(), recent.end(), l) == recent.end())
      fresh.push_back(l);
  auto const & pool = fresh.empty() ? options : fresh;
  std::string const line = pool[static_cast<size_t>(std::floor(m_config.rng() * pool.size()))];
  // Remember up to 2 recent lines, but always leave at least one fresh option.
  // (Like JS slice(-n), keeping "the last 0" keeps everything.)
  recent.push_back(line);
  size_t const keep = std::min<size_t>(2, options.size() - 1);
  if (keep > 0 && recent.size() > keep)
    recent.erase(recent.begin(), recent.end() - static_cast<ptrdiff_t>(keep));
  return line;
}

Commentary CritiqueEngine::Make(double t, Category category, std::string const & text, Priority priority) const
{
  Commentary c;
  c.t = t;
  c.category = category;
  c.text = Trim(text);
  c.frustration = Frustration();
  c.priority = priority;
  c.voiceSpeed = RoundTo(1 + m_frustration / 250, 2);
  return c;
}
}  // namespace backseat
