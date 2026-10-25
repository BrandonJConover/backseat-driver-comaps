// Replays drives recorded from the TypeScript engine (core/tools/record-trace.ts in the
// backseat-driver repo) and checks the C++ engine says exactly the same things.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/history.hpp"
#include "backseat/rundown.hpp"
#include "backseat/units.hpp"

#include <fstream>
#include <memory>

using namespace backseat;

namespace
{
std::vector<std::string> Split(std::string const & s, char sep)
{
  std::vector<std::string> out;
  size_t start = 0;
  while (true)
  {
    size_t const p = s.find(sep, start);
    out.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
    if (p == std::string::npos)
      return out;
    start = p + 1;
  }
}

std::string Join(std::vector<std::string> const & parts, size_t from, size_t to, char sep)
{
  std::string out;
  for (size_t i = from; i < to; ++i)
    out += (i > from ? std::string(1, sep) : "") + parts[i];
  return out;
}

double D(std::string const & s) { return std::stod(s); }

std::optional<double> OptD(std::string const & s)
{
  if (s == "-")
    return std::nullopt;
  return std::stod(s);
}

std::optional<std::string> OptS(std::string const & s)
{
  if (s == "-")
    return std::nullopt;
  return s;
}

TurnDirection Dir(std::string const & s) { return s == "left" ? TurnDirection::Left : TurnDirection::Right; }

HazardKind Hazard(std::string const & s)
{
  if (s == "police") return HazardKind::Police;
  if (s == "construction") return HazardKind::Construction;
  if (s == "debris") return HazardKind::Debris;
  if (s == "crash") return HazardKind::Crash;
  return HazardKind::StalledCar;
}

StopWish Wish(std::string const & s)
{
  if (s == "electronics") return StopWish::Electronics;
  if (s == "carWash") return StopWish::CarWash;
  if (s == "charging") return StopWish::Charging;
  return StopWish::Other;
}

PlaceKind PlaceOf(std::string const & s)
{
  if (s == "gas") return PlaceKind::Gas;
  if (s == "food") return PlaceKind::Food;
  if (s == "coffee") return PlaceKind::Coffee;
  if (s == "store") return PlaceKind::Store;
  return PlaceKind::Other;
}

NavEvent ParseNav(std::vector<std::string> const & f)
{
  char const type = f[0][0];
  double const t = D(f[1]);
  switch (type)
  {
  case 'I':
  {
    std::optional<Maneuver> m;
    if (f[4] == "left") m = Maneuver::Left;
    else if (f[4] == "right") m = Maneuver::Right;
    else if (f[4] == "other") m = Maneuver::Other;
    Control const c = f[5] == "stopSign" ? Control::StopSign : f[5] == "trafficLight" ? Control::TrafficLight : Control::None;
    return NavEvent::Instruction(t, f[2], D(f[3]), m, c);
  }
  case 'C': return NavEvent::TurnCompleted(t, f[2] == "1");
  case 'P': return NavEvent::Progress(t, D(f[2]));
  case 'M': return NavEvent::MissedTurn(t);
  case 'W': return NavEvent::WrongTurn(t, Dir(f[2]), Dir(f[3]));
  case 'R': return NavEvent::Rerouted(t);
  case 'H': return NavEvent::HazardAhead(t, Hazard(f[2]), D(f[3]), OptS(f[4]));
  case 'O': return NavEvent::PoiAhead(t, f[2], Wish(f[3]));
  case 'A': return NavEvent::StopRequestAnswer(t, f[2] == "1");
  case 'S': return NavEvent::PromisedStop(t, f[2] == "1");
  case 'D': return NavEvent::DestinationSet(t, f[2]);
  case 'B': return NavEvent::Battery(t, D(f[2]), f[3] == "1");
  case 'U': return NavEvent::Resumed(t, D(f[2]), f[3] == "1");
  case 'X':
  {
    std::optional<Place> place;
    if (f[2] != "-")
      place = Place{PlaceOf(f[2]), OptS(f[3])};
    return NavEvent::UnplannedStop(t, place);
  }
  case 'F': return NavEvent::Traffic(t, f[2] == "1", OptS(f[3]));
  default: return NavEvent::Arrived(t);
  }
}

std::string Describe(Commentary const & c)
{
  return std::string(ToString(c.category)) + "\t" + ToString(c.priority) + "\t" + std::to_string(c.frustration) + "\t" +
         JsNumber(c.voiceSpeed) + "\t" + c.text;
}

char const * HabitName(Habit h)
{
  switch (h)
  {
  case Habit::LeadFoot: return "leadFoot";
  case Habit::Slowpoke: return "slowpoke";
  case Habit::OnTime: return "onTime";
  case Habit::Unknown: return "unknown";
  }
  return "?";
}

/// Each expected ">" line must match the next thing the C++ side said.
class Expectations
{
public:
  void Said(std::vector<std::string> lines) { m_pending.insert(m_pending.end(), lines.begin(), lines.end()); }

  void Expect(std::string const & expected, size_t lineNo)
  {
    ++checked;
    if (m_next >= m_pending.size())
      throw backseat_tests::Failure{"trace line " + std::to_string(lineNo) + ": expected\n    " + expected +
                                    "\n  but C++ said nothing more"};
    std::string const & got = m_pending[m_next++];
    if (got != expected)
      throw backseat_tests::Failure{"trace line " + std::to_string(lineNo) + ":\n    got:      " + got +
                                    "\n    expected: " + expected};
  }

  void ExpectSilence(size_t lineNo)
  {
    if (m_next < m_pending.size())
      throw backseat_tests::Failure{"trace line " + std::to_string(lineNo) + ": C++ also said\n    " + m_pending[m_next]};
    m_pending.clear();
    m_next = 0;
  }

  size_t checked = 0;

private:
  std::vector<std::string> m_pending;
  size_t m_next = 0;
};
}  // namespace

TEST(ParityWithTypeScriptEngine)
{
  std::ifstream in(BACKSEAT_TRACE_PATH);
  CHECK(in.good());
  std::unique_ptr<CritiqueEngine> engine;
  Expectations said;
  size_t lineNo = 0;
  size_t scenarios = 0;
  std::string line;
  while (std::getline(in, line))
  {
    ++lineNo;
    if (line.empty())
      continue;
    auto const f = Split(line, '\t');
    if (f[0] == ">")
    {
      said.Expect(Join(f, 1, f.size(), '\t'), lineNo);
      continue;
    }
    said.ExpectSilence(lineNo);
    char const type = f[0][0];
    if (type == 'E')
    {
      EngineConfig c;
      c.units = f[2] == "kmh" ? Units::Kmh : Units::Mph;
      c.rng = SeededRng(static_cast<uint32_t>(std::stoul(f[1])));
      engine = std::make_unique<CritiqueEngine>(c, 0);
      ++scenarios;
    }
    else if (type == 'T')
    {
      TelemetrySample s;
      s.t = D(f[1]);
      s.speedMps = D(f[2]);
      s.speedLimitMps = OptD(f[3]);
      s.lat = OptD(f[4]);
      s.lon = OptD(f[5]);
      s.headingDeg = OptD(f[6]);
      std::vector<std::string> out;
      for (auto const & c : engine->OnTelemetry(s))
        out.push_back(Describe(c));
      said.Said(out);
    }
    else if (type == '=')
    {
      auto const r = engine->Report();
      auto const & st = engine->Stats();
      std::string top = "-";
      if (!r.heatmap.empty())
        top = JsNumber(r.heatmap[0].lat) + "," + JsNumber(r.heatmap[0].lon) + "," + JsNumber(r.heatmap[0].weight) + "," +
              std::to_string(r.heatmap[0].count);
      std::vector<std::string> const parts = {std::to_string(r.overall.score), r.overall.letter,
                                              std::to_string(r.instructions.score), std::to_string(r.driving.score),
                                              std::to_string(st.hardBrakes), std::to_string(st.hardAccels),
                                              std::to_string(st.missedTurns), std::to_string(st.wrongTurns),
                                              std::to_string(st.lateTurns), std::to_string(st.reroutes),
                                              std::to_string(st.turnsCompleted), std::to_string(st.roastsDelivered),
                                              std::to_string(st.maxOverLimit), JsNumber(RoundTo(st.speedingSeconds, 3)),
                                              JsNumber(RoundTo(st.distanceM, 3)), std::to_string(engine->Incidents().size()),
                                              std::to_string(r.heatmap.size()), top};
      CHECK_EQUAL(Join(parts, 0, parts.size(), '\t'), Join(f, 1, f.size(), '\t'));
    }
    else if (type == 'Y')
    {
      TripPlan plan;
      plan.origin = {D(f[2]), D(f[3])};
      plan.destination = {D(f[4]), D(f[5])};
      plan.destinationName = f[6];
      plan.expectedMinutes = D(f[7]);
      std::vector<TripRecord> history;
      if (f[8] != "-")
      {
        for (auto const & rec : Split(f[8], ';'))
        {
          auto const p = Split(rec, ',');
          size_t const n = p.size();  // the route key itself contains commas
          TripRecord r;
          r.routeKey = Join(p, 0, n - 6, ',');
          r.destination = p[n - 6];
          r.expectedMinutes = D(p[n - 5]);
          r.actualMinutes = D(p[n - 4]);
          r.missedTurns = std::stoi(p[n - 3]);
          r.hardBrakes = std::stoi(p[n - 2]);
          r.grade = p[n - 1];
          history.push_back(r);
        }
      }
      std::vector<RouteStep> steps;
      if (f[9] != "-")
      {
        for (auto const & st : Split(f[9], ';'))
        {
          auto const colon = st.find(':');
          std::string const kind = st.substr(0, colon);
          RouteStep step{st.substr(colon + 1), std::nullopt};
          if (kind == "left") step.maneuver = RouteStep::Kind::Left;
          else if (kind == "right") step.maneuver = RouteStep::Kind::Right;
          else if (kind == "uturn") step.maneuver = RouteStep::Kind::UTurn;
          else if (kind == "other") step.maneuver = RouteStep::Kind::Other;
          steps.push_back(step);
        }
      }
      auto const rng = SeededRng(static_cast<uint32_t>(std::stoul(f[1])));
      std::string const greeting = TripGreeting(history, rng);
      std::string const rundown = RouteRundown(steps, history, rng);
      auto const p = PredictTrip(history, plan, rng);
      said.Said({"greeting\t" + greeting, "rundown\t" + rundown,
                 "predict\t" + JsNumber(p.predictedMinutes) + "\t" + HabitName(p.habit) + "\t" +
                     std::to_string(p.timesOnRoute) + "\t" + p.text});
    }
    else if (type == 'V')
    {
      EngineConfig c;
      c.rng = SeededRng(5);
      CritiqueEngine e(c, 0);
      for (auto [t, speed] : {std::pair{0.0, 20.0}, {1000.0, 21.0}, {2000.0, 10.0}})
      {
        TelemetrySample s;
        s.t = t;
        s.speedMps = speed;
        s.speedLimitMps = 13.4;
        e.OnTelemetry(s);
      }
      e.OnNav(NavEvent::Arrived(754321));
      auto const r = RecordTrip(e, {{39.7391, -104.9902}, {39.761, -105.0123}, "Work", 14});
      std::vector<std::string> const parts = {r.routeKey, JsNumber(r.actualMinutes), JsNumber(r.speedingShare),
                                              std::to_string(r.hardBrakes), r.grade};
      CHECK_EQUAL(Join(parts, 0, parts.size(), '\t'), Join(f, 1, f.size(), '\t'));
    }
    else
    {
      std::vector<std::string> out;
      for (auto const & c : engine->OnNav(ParseNav(f)))
        out.push_back(Describe(c));
      said.Said(out);
    }
  }
  said.ExpectSilence(lineNo);
  CHECK(scenarios >= 10);
  CHECK(said.checked > 1000);
}
