#include "backseat/engine.hpp"

#include "backseat/debug_log.hpp"

#include "backseat/lines.hpp"
#include "backseat/units.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <regex>
#include <string_view>

namespace backseat
{
namespace
{
double constexpr kNever = -std::numeric_limits<double>::infinity();
/// A heading swing counts as taking (or botching) the pending turn only this close to it.
double constexpr kWrongTurnWithinM = 120;
/// The far countdown call comes at least this many seconds out at the current speed...
double constexpr kCountdownFarS = 12;
/// ...but never farther than this (half a mile).
double constexpr kCountdownFarMaxM = 805;
/// Countdown calls this soon after the instruction are skipped; it just said the distance.
double constexpr kJustAnnouncedMs = 8000;
/// "Did I mention..." only if the far call was at least this long ago.
double constexpr kMentionAfterFarMs = 10000;
/// The plain "Now, turn left onto Elm Street." comes about this many seconds before the turn...
double constexpr kFinalCallS = 4;
/// ...unless the instruction itself was this recent; it just said the same thing.
double constexpr kFinalAfterInstructionMs = 4000;
/// Legs at least this long get a plain "In 1 mile, turn right onto ..." reminder, which ends the straight nags.
double constexpr kReminderLegM = 2500;
/// speechAware: the final call is over at least this long before the turn...
double constexpr kFinalClearMs = 1500;
/// ...but never comes more than this many seconds out, however much talking is ahead of it.
double constexpr kFinalCallMaxS = 10;
/// speechAware: anything else has to be over at least this long before the final call is due.
double constexpr kBeforeFinalClearMs = 1000;
/// How long a roast takes to say, before it's picked (most are 10-15 words).
double constexpr kRoastSpeakMs = 5000;
/// The pause the phone leaves after each line.
double constexpr kLinePauseMs = 350;
/// speechAware: no remark after a direction for a turn that's closer than this many seconds.
double constexpr kSuffixMinS = 15;
/// Moving slower than this, the car isn't heading for the turn yet (a red light, a jam).
double constexpr kRollingMps = 3;

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
  // A verdict on every single turn gets old; the good ones still count toward the grade.
  case Category::TurnCompleted: return 40000;
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

std::string FirstManeuver(std::string const & text)
{
  static std::regex const then(", then ", std::regex::icase);
  std::smatch m;
  return std::regex_search(text, m, then) ? text.substr(0, m.position(0)) : text;
}

double SpeakingMs(std::string const & text, double speed)
{
  int const words = static_cast<int>(std::count(text.begin(), text.end(), ' ')) + 1;
  return words / 2.7 * 1000 / std::max(0.5, speed);
}

std::string DirectionText(std::string const & text, double distanceM, Units units, Control control)
{
  static std::regex const arriveAt(R"(^arrive at (.+)$)", std::regex::icase);
  static std::regex const arrive(R"(^arrive\b)", std::regex::icase);
  std::string const distance = FormatDistance(distanceM, units);
  std::smatch m;
  if (std::regex_search(text, m, arriveAt))
    return "Your destination, " + m[1].str() + ", is " + distance + " ahead.";
  if (std::regex_search(text, arrive))
    return "Your destination is " + distance + " ahead.";
  // Lights come from the map; stop signs are only a guess (side street onto a bigger road), so
  // they aren't named.
  char const * at = control == Control::TrafficLight ? "at the light, " : "";
  return "In " + distance + ", " + at + LowerFirst(text) + ".";
}

TurnShape TurnShapeOf(std::string const & text)
{
  static std::regex const uturn(R"(\bu-?turn\b)", std::regex::icase);
  static std::regex const roundabout(R"(\b(roundabout|traffic circle|rotary)\b)", std::regex::icase);
  static std::regex const exit(R"(\b(exit|ramp)\b)", std::regex::icase);
  if (std::regex_search(text, uturn))
    return TurnShape::UTurn;
  if (std::regex_search(text, roundabout))
    return TurnShape::Roundabout;
  if (std::regex_search(text, exit))
    return TurnShape::Exit;
  return TurnShape::Turn;
}

namespace
{
/// The countdown pool for this stage: "countdown.far" for a plain turn, "countdown.exit.far" for an
/// exit, and so on.
std::string CountdownKey(TurnShape shape, char const * stage)
{
  switch (shape)
  {
  case TurnShape::Turn: break;
  case TurnShape::Exit: return std::string("countdown.exit.") + stage;
  case TurnShape::UTurn: return std::string("countdown.uturn.") + stage;
  case TurnShape::Roundabout: return std::string("countdown.roundabout.") + stage;
  }
  return std::string("countdown.") + stage;
}

/// What the GPS told you to do, for "I said left."
std::string SaidWhat(TurnShape shape, TurnDirection dir)
{
  switch (shape)
  {
  case TurnShape::UTurn: return "a U-turn";
  case TurnShape::Roundabout: return "the roundabout";
  case TurnShape::Turn:
  case TurnShape::Exit: break;
  }
  return DirName(dir);
}
}  // namespace

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
    // "Turn left onto Elm Street, then right onto Oak Lane": the countdown is about the first one.
    std::string const first = FirstManeuver(e.text);
    Maneuver const dir = e.maneuver ? *e.maneuver : TurnDirectionOf(first);
    TurnShape const shape = TurnShapeOf(first);
    // Already inside the 500 ft mark when announced? Skip straight to the closer nags.
    int const stage = e.distanceM <= FarM() ? 1 : 0;
    // U-turns and roundabouts get the countdown too, without a side.
    if (dir == Maneuver::Other && shape != TurnShape::UTurn && shape != TurnShape::Roundabout)
      m_pendingTurn.reset();
    else
    {
      m_pendingTurn =
          PendingTurn{dir == Maneuver::Right ? TurnDirection::Right : TurnDirection::Left, shape, e.t, stage, {}, false,
                      {}, e.distanceM};
      m_pendingTurn->legM = e.distanceM;
      m_pendingTurn->text = e.text;
    }
    // The heading swing from the turn just finished isn't about this one.
    m_headings.clear();
    m_turnDoneSaid = false;
    // Only real turns count toward NASCAR; an exit on the left isn't a lap.
    if (dir == Maneuver::Left && shape == TurnShape::Turn)
      ++m_leftStreak;
    else if (dir == Maneuver::Right)
    {
      m_leftStreak = 0;
      m_nascarSaid = false;
    }
    if (e.distanceM >= m_config.straightNagMinM)
      m_straight = StraightNag{e.t, e.distanceM, 0, e.distanceM >= m_config.straightLongM};
    else
      m_straight.reset();
    if (e.at && !m_position)
      m_position = e.at;
    int const tier = Tier();
    // The direction comes first, always: a remark goes after it, on one direction in three.
    int const n = m_instructionCount++ % 3;
    double const mps = m_lastSample ? m_lastSample->speedMps : 0;
    bool const turnIsClose = m_config.speechAware && mps >= kRollingMps && e.distanceM / mps < kSuffixMinS;
    std::string const suffix = n == 2 && !turnIsClose ? Pick("instruction.suffix", tier) : std::string();
    std::string const text =
        DirectionText(e.text, e.distanceM, units, dir == Maneuver::Other ? Control::None : e.control) + suffix;
    out.push_back(Make(e.t, Category::Instruction, text, Priority::Nav));
    if (m_straight && m_straight->longHaul)
    {
      // Brandon's line, with the distance in whole miles or kilometers ("the next 200 miles").
      double const unitM = units == Units::Mph ? 1609.344 : 1000;
      std::string const distance = FormatDistance(JsRound(e.distanceM / unitM) * unitM, units);
      out.push_back(Make(e.t, Category::Instruction, Fill(Pick("straight.long", tier), {{"distance", distance}}),
                         Priority::Nav));
      out.back().key = "straight.long";
    }
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
  {
    // Nagged all the way through the turn ("You should still be turning right")? Then the turn ends
    // with "Please stop turning right." instead of the usual verdict.
    bool const nagged = m_pendingTurn && m_pendingTurn->stage >= 4;
    if (nagged && e.onTime)
      SayTurnDone(out, e.t, *m_pendingTurn);
    bool const doneSaid = m_turnDoneSaid;
    m_turnDoneSaid = false;
    m_pendingTurn.reset();
    m_straight.reset();
    m_stats.turnsCompleted++;
    if (!e.onTime)
    {
      m_stats.lateTurns++;
      Log(MakeIncident(e.t, IncidentKind::LateTurn, m_lastInstructionText), e.at);
    }
    Bump(Category::TurnCompleted);
    if (!(doneSaid && e.onTime))
      Roast(out, e.t, Category::TurnCompleted, e.onTime ? "turnCompleted.onTime" : "turnCompleted.late", {}, true);
    break;
  }
  case NavEvent::Type::Progress:
    if (m_pendingTurn)
      m_pendingTurn->distM = e.distanceM;
    NagStraight(out, e.t, e.distanceM);
    Countdown(out, e.t, e.distanceM);
    break;
  case NavEvent::Type::UnplannedStop:
  {
    PlaceKind const kind = e.place ? e.place->kind : PlaceKind::Other;
    std::optional<std::string> const placeName = e.place ? e.place->name : std::nullopt;
    std::string const said =
        m_pendingTurn ? "I said " + SaidWhat(m_pendingTurn->shape, m_pendingTurn->dir) + "." : "I said nothing about stopping.";
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
    if (!e.silent)
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
    out.back().key = "arrived";
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
  // Only plain turns can be checked by heading: a roundabout swings both ways, and a cloverleaf exit on
  // the right loops left.
  if (!m_pendingTurn || m_pendingTurn->shape != TurnShape::Turn || m_pendingTurn->turned || m_pendingTurn->wrongSaid)
    return;
  // Only a swing at the maneuver is about it; a bend or a driveway a mile out is the router's
  // business (missed turn, reroute), not "your other left".
  if (m_pendingTurn->distM > kWrongTurnWithinM)
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
    if (m_pendingTurn->stage >= 4)
      SayTurnDone(out, s.t, *m_pendingTurn);
    // The router says when the turn is really done; until then the countdown stays, just without
    // "you should still be turning".
    m_pendingTurn->turned = true;
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

/// "In 500 feet you are going to turn right." ... "By the way, did I mention you should turn right."
/// ... then, a few seconds out, the plain "Now, turn right onto Oak Lane." ... "You should still be
/// turning right." ... and once the turn is done, "Please stop turning right." (SayTurnDone). Each
/// stage is said once. Long legs also get a plain "In 1 mile, ..." reminder.
void CritiqueEngine::Countdown(std::vector<Commentary> & out, double t, double distanceM)
{
  if (!m_pendingTurn)
    return;
  auto & p = *m_pendingTurn;
  double const far = FarM();
  double const finalM = m_config.speechAware ? FinalDueM(t, p) : FinalM();
  double const mention = m_config.countdownMentionM;
  double const reminderM = m_config.units == Units::Mph ? 1609.344 : 1000;
  if (!p.reminded && p.stage == 0 && p.legM >= kReminderLegM && distanceM <= reminderM && distanceM > 1.5 * far)
  {
    p.reminded = true;
    m_straight.reset();
    out.push_back(Make(t, Category::Instruction, DirectionText(p.text, reminderM, m_config.units), Priority::Nav));
    return;
  }
  char const * stage = nullptr;
  double sayM = finalM;
  // The instruction itself ("In 300 feet, turn left") was just said: a "by the way, in 250 feet"
  // on top of it is noise, and wrong. Those stages are passed over quietly.
  bool const justAnnounced = t - p.since < kJustAnnouncedMs;
  if (p.stage == 0 && distanceM <= far && distanceM > finalM && justAnnounced)
  {
    p.stage = 1;
  }
  else if (p.stage == 1 && !p.mentioned && distanceM <= mention && distanceM > finalM && justAnnounced)
  {
    p.mentioned = true;
  }
  else if (p.stage == 0 && distanceM <= far && distanceM > finalM)
  {
    stage = "far";
    // The third left in a row and beyond: going around in circles, NASCAR style.
    if (p.shape == TurnShape::Turn && p.dir == TurnDirection::Left && m_leftStreak >= 3 && !m_nascarSaid)
    {
      stage = "nascar";
      m_nascarSaid = true;
    }
    sayM = far;
    p.stage = 1;
    p.farAt = t;
  }
  // At speed the 250 ft mark comes seconds after the 500 ft one; once is enough then.
  else if (p.stage == 1 && !p.mentioned && distanceM <= mention && distanceM > finalM &&
           (!p.farAt || t - *p.farAt >= kMentionAfterFarMs))
  {
    stage = "mention";
    sayM = mention;
    p.mentioned = true;
  }
  else if (p.stage <= 1 && distanceM <= finalM && distanceM > 5)
  {
    // The call that matters most, so it's plain and it always comes, mention or not. Right after
    // the instruction it would only say it again: skipped at the usual spot, and when it's only
    // due early (a long call, the phone still talking) it waits for that spot.
    // speechAware: "just said" lasts until the instruction is over, with a breath.
    double const justSaidMs = m_config.speechAware ? std::max(kFinalAfterInstructionMs, SpeakingMs(p.text) + 2500)
                                                   : kFinalAfterInstructionMs;
    if (t - p.since < justSaidMs)
    {
      if (distanceM <= FinalM())
        p.stage = 2;
      return;
    }
    p.stage = 2;
    out.push_back(Make(t, Category::Instruction, "Now, " + LowerFirst(p.text) + ".", Priority::Nav));
    return;
  }
  else if (p.stage <= 2 && distanceM <= 5)
  {
    // Only when the plain call never got its chance (the update jumped straight to the turn).
    if (p.stage < 2)
      stage = "now";
    p.stage = 3;
    p.atTurnSince = t;
  }
  else if (p.stage == 3 && !p.turned && p.atTurnSince && t - *p.atTurnSince >= 3000)
  {
    stage = "still";
    p.stage = 4;
  }
  if (!stage)
    return;
  std::string const key = CountdownKey(p.shape, stage);
  std::string const distance = FormatDistance(sayM, m_config.units);
  // "In 500 feet..." and "Did I mention..." repeat the instruction: they only go out if they're over
  // before the final call.
  if (m_config.speechAware && (std::string_view(stage) == "mention" || std::string_view(stage) == "far" ||
                               std::string_view(stage) == "nascar") &&
      std::max(0.0, m_talkingUntil - t) + kRoastSpeakMs + kBeforeFinalClearMs > MsUntilFinal(t, p))
  {
    TraceThrottled("roast.held", key + "/turn", {{"key", key}, {"why", "turn"}});
    return;
  }
  Vars const vars{{"dir", DirName(p.dir)}, {"distance", distance}};
  std::string const text = Fill(Pick(key, Tier()), vars);
  out.push_back(Make(t, Category::Instruction, Capitalize(text), Priority::Nav));
  out.back().key = key;
  out.back().vars = vars;
}

/// Where the plain final call goes: about 4 seconds out at the current speed, 50 ft at least.
double CritiqueEngine::FinalM() const
{
  double const mps = m_lastSample ? m_lastSample->speedMps : 0;
  return std::max(m_config.countdownNearM, mps * kFinalCallS);
}

double CritiqueEngine::FinalDueM(double t, PendingTurn const & p) const
{
  double const base = FinalM();
  double const mps = m_lastSample ? m_lastSample->speedMps : 0;
  if (mps < kRollingMps)
    return base;
  std::string const call = "Now, " + LowerFirst(p.text) + ".";
  double const needMs = std::max(0.0, m_talkingUntil - t) + SpeakingMs(call) + kLinePauseMs + kFinalClearMs;
  return std::max(base, mps * std::min(needMs / 1000, kFinalCallMaxS));
}

double CritiqueEngine::MsUntilFinal(double t, PendingTurn const & p) const
{
  double const mps = m_lastSample ? m_lastSample->speedMps : 0;
  if (mps < kRollingMps)
    return 1e12;
  return (p.distM - FinalDueM(t, p)) / mps * 1000;
}

bool CritiqueEngine::TurnNeedsQuiet(double t, double speakMs) const
{
  if (!m_config.speechAware || !m_pendingTurn)
    return false;
  auto const & p = *m_pendingTurn;
  // The final call is out: quiet until the turn is done, or until the car gives up on it.
  if (p.stage >= 2)
    return !p.atTurnSince || t - *p.atTurnSince < 10000;
  return std::max(0.0, m_talkingUntil - t) + speakMs + kBeforeFinalClearMs > MsUntilFinal(t, p);
}

/// Where the "In 500 feet" call goes: 500 ft in town, farther out at speed so there are still
/// ~12 seconds to get over (about 0.2 miles at 65 mph), never more than half a mile.
double CritiqueEngine::FarM() const
{
  double const mps = m_lastSample ? m_lastSample->speedMps : 0;
  return std::min(std::max(m_config.countdownFarM, mps * kCountdownFarS), kCountdownFarMaxM);
}

/// The turn is done after the full countdown: "Please stop turning right." Once per maneuver.
void CritiqueEngine::SayTurnDone(std::vector<Commentary> & out, double t, PendingTurn const & p)
{
  if (m_turnDoneSaid)
    return;
  m_turnDoneSaid = true;
  std::string const text = Fill(Pick(CountdownKey(p.shape, "done"), Tier()), {{"dir", DirName(p.dir)}});
  out.push_back(Make(t, Category::Instruction, Capitalize(text), Priority::Nav));
}

/// A long way to the next maneuver: "Please continue straight." ... "You should still be going
/// straight." ... "I'm serious." Only while the car is actually covering ground, and never inside
/// the turn countdown's range.
void CritiqueEngine::NagStraight(std::vector<Commentary> & out, double t, double distanceM)
{
  if (!m_straight || (!m_straight->longHaul && m_straight->stage >= 3) || distanceM <= 2 * m_config.countdownFarM)
    return;
  auto & s = *m_straight;
  double const every = s.longHaul ? m_config.straightLongNagEveryMs : m_config.straightNagEveryMs;
  if (t - s.since < every * (s.stage + 1) || s.lastM - distanceM < 100)
    return;
  // After the first three, a long haul alternates the last two.
  static char const * const kKeys[] = {"straight.please", "straight.still", "straight.serious"};
  char const * const key = kKeys[s.stage < 3 ? s.stage : 1 + (s.stage - 3) % 2];
  s.stage++;
  s.lastM = distanceM;
  out.push_back(Make(t, Category::Instruction, Capitalize(Pick(key, Tier())), Priority::Nav));
  out.back().key = key;
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
  // One "other left" per maneuver. The countdown keeps going: if the heading read was wrong, the
  // driver still needs the turn called, and if the turn was wrong, the reroute replaces it.
  if (m_pendingTurn)
    m_pendingTurn->wrongSaid = true;
  m_lastWrongTurnAt = t;
  m_stats.wrongTurns++;
  Log(MakeIncident(t, IncidentKind::WrongTurn, m_lastInstructionText), at);
  Roast(out, t, Category::WrongTurn, std::string("wrongTurn.") + DirName(expected), {}, true);
}

void CritiqueEngine::CheckSpeeding(TelemetrySample const & s, TelemetrySample const * prev, std::vector<Commentary> & out)
{
  Units const units = m_config.units;
  bool const backoff = m_config.speedingRenagMaxMs > 0;
  int const over = s.speedLimitMps ? static_cast<int>(JsRound(ToDisplay(s.speedMps, units))) -
                                         static_cast<int>(JsRound(ToDisplay(*s.speedLimitMps, units)))
                                   : 0;
  if (!s.speedLimitMps || over <= m_config.speedingTolerance)
  {
    m_speedingSince.reset();
    if (!m_underLimitSince)
      m_underLimitSince = s.t;
    // Under the limit for a while: the next time over is a new offense, and the nags start over.
    if (!backoff || s.t - *m_underLimitSince >= m_config.speedingResetMs)
    {
      m_speedingBand.reset();
      m_speedingRenags = 0;
    }
    return;
  }
  m_underLimitSince.reset();
  int const speed = static_cast<int>(JsRound(ToDisplay(s.speedMps, units)));
  int const limit = static_cast<int>(JsRound(ToDisplay(*s.speedLimitMps, units)));

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
  else
  {
    double const renagMs = backoff ? std::min(m_config.speedingRenagMaxMs,
                                              m_config.speedingRenagMs * std::pow(2.0, std::min(m_speedingRenags, 10)))
                                   : m_config.speedingRenagMs;
    if (s.t - m_lastSpeedingLineAt >= renagMs && Roast(out, s.t, Category::Speeding, "speeding.still", vars))
    {
      m_lastSpeedingLineAt = s.t;
      ++m_speedingRenags;
    }
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
  {
    TraceThrottled("roast.held", key + "/cooldown",
                   {{"key", key}, {"why", "cooldown"}, {"wait_s", LogNum((CooldownMs(category) - (t - lastSame)) / 1000)}});
    return false;
  }
  if (!urgent && t - m_lastRoastAt < m_config.minRoastGapMs)
  {
    TraceThrottled("roast.held", key + "/gap",
                   {{"key", key}, {"why", "gap"}, {"wait_s", LogNum((m_config.minRoastGapMs - (t - m_lastRoastAt)) / 1000)}});
    return false;
  }
  if (!urgent && category != Category::TurnCompleted && t - m_lastNavAt < m_config.navBreathMs)
  {
    TraceThrottled("roast.held", key + "/breath", {{"key", key}, {"why", "breath"}});
    return false;
  }
  if (!urgent && TurnNeedsQuiet(t, kRoastSpeakMs))
  {
    TraceThrottled("roast.held", key + "/turn", {{"key", key}, {"why", "turn"}});
    return false;
  }

  if (category != Category::TurnCompleted)
    Bump(category);
  std::string const text = Fill(Pick(key, Tier()), vars);
  m_lastRoastAt = t;
  m_lastByCategory[category] = t;
  m_stats.roastsDelivered++;
  Trace("roast", {{"key", key}, {"tier", std::to_string(Tier())}, {"frustration", std::to_string(Frustration())},
                  {"urgent", urgent ? "yes" : "no"}});
  out.push_back(Make(t, category, text, Priority::Roast));
  out.back().key = key;
  out.back().vars = vars;
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
  if (priority == Priority::Nav)
    m_lastNavAt = t;
  // Fed up talks a touch faster, never rushed: at most 10% faster at full frustration.
  c.voiceSpeed = RoundTo(1 + m_frustration / 1000, 2);
  if (m_config.speechAware)
    m_talkingUntil = std::max(m_talkingUntil, t) + SpeakingMs(c.text, c.voiceSpeed) + kLinePauseMs;
  return c;
}
}  // namespace backseat
