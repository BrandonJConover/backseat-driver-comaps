// Countdown calls say the distance that is really left (loop B80, BI-074): after a fix gap, or a
// merge that pushes the far mark out, "In a quarter mile" with 650 feet to go is a misleading
// instruction. And the "In 1 mile" reminder never goes out at 0.4 mi.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/units.hpp"

#include <string>
#include <vector>

using namespace backseat;

namespace
{
CritiqueEngine Engine(Units units = Units::Mph)
{
  EngineConfig c;
  c.rng = [] { return 0.0; };
  c.units = units;
  return CritiqueEngine(c, 0);
}

void Drive(CritiqueEngine & e, double t, double mps)
{
  TelemetrySample s;
  s.t = t;
  s.speedMps = mps;
  e.OnTelemetry(s);
}

/// Every {distance} a countdown call carried, in order; plus all text said.
struct Heard
{
  std::vector<std::string> distances;
  std::vector<std::string> texts;
  void Add(std::vector<Commentary> const & lines)
  {
    for (auto const & c : lines)
    {
      texts.push_back(c.text);
      auto const it = c.vars.find("distance");
      if (c.key.rfind("countdown.", 0) == 0 && it != c.vars.end())
        distances.push_back(it->second);
    }
  }
};

/// A 1 Hz approach from `fromM` at a steady speed; what the countdown said.
Heard Approach(Units units, double mps, double fromM, char const * text = "Turn right onto Oak Lane")
{
  auto e = Engine(units);
  Drive(e, 0, mps);
  e.OnNav(NavEvent::Instruction(-20000, text, fromM + 500));
  Heard h;
  double t = 1000;
  for (double d = fromM; d > 0; d -= mps, t += 1000)
  {
    Drive(e, t, mps);
    h.Add(e.OnNav(NavEvent::Progress(t, d)));
  }
  return h;
}
}  // namespace

TEST(AFixGapSaysTheDistanceLeftNotTheFarMark)
{
  for (auto const units : {Units::Mph, Units::Kmh})
  {
    auto e = Engine(units);
    Drive(e, 0, 30);
    e.OnNav(NavEvent::Instruction(-20000, "Turn right onto Oak Lane", 1200));
    Heard h;
    Drive(e, 1000, 30);
    h.Add(e.OnNav(NavEvent::Progress(1000, 600)));
    // The tunnel: next fix 200 m on, well inside the 360 m far mark at 30 m/s.
    Drive(e, 14000, 30);
    h.Add(e.OnNav(NavEvent::Progress(14000, 200)));
    CHECK_EQUAL(h.distances.size(), size_t{1});
    if (h.distances.size() == 1)
      CHECK_EQUAL(h.distances[0], std::string(units == Units::Mph ? "650 feet" : "200 meters"));
    for (auto const & s : h.texts)
      CHECK(s.find("quarter mile") == std::string::npos && s.find("360 meters") == std::string::npos);
  }
}

TEST(ASteadyApproachStillSaysTheMarks)
{
  // Nothing changes on a normal approach: each call says its mark, as before.
  CHECK(Approach(Units::Mph, 30, 900).distances == std::vector<std::string>{"a quarter mile"});
  CHECK(Approach(Units::Mph, 15, 600).distances == std::vector<std::string>{"600 feet"});
  CHECK(Approach(Units::Kmh, 30, 900).distances == std::vector<std::string>{"360 meters"});
  CHECK(Approach(Units::Kmh, 15, 600).distances == std::vector<std::string>{"180 meters"});
  // City speed: far at 500 ft, then the 250 ft mention, both at their marks.
  auto const city = Approach(Units::Mph, 5, 300);
  CHECK(!city.distances.empty() && city.distances[0] == "500 feet");
  for (auto const & d : city.distances)
    CHECK(d == "500 feet" || d == "250 feet");
}

TEST(AGapPastTheMileDoesNotSayInOneMile)
{
  auto e = Engine();
  Drive(e, 0, 30);
  e.OnNav(NavEvent::Instruction(0, "Turn right onto Oak Lane", 4000));
  Heard h;
  Drive(e, 1000, 30);
  h.Add(e.OnNav(NavEvent::Progress(1000, 2000)));
  Drive(e, 45000, 30);
  h.Add(e.OnNav(NavEvent::Progress(45000, 700)));  // 0.43 mi
  for (double t = 46000, d = 670; d > 0; t += 1000, d -= 30)
  {
    Drive(e, t, 30);
    h.Add(e.OnNav(NavEvent::Progress(t, d)));
  }
  for (auto const & s : h.texts)
    CHECK(s.find("In 1 mile") == std::string::npos);
  // The countdown still runs: far mark and the plain final call.
  CHECK(h.distances == std::vector<std::string>{"a quarter mile"});
  bool now = false;
  for (auto const & s : h.texts)
    now = now || s == "Now, turn right onto Oak Lane.";
  CHECK(now);
}

TEST(AStraightRunStillGetsTheMileReminder)
{
  for (auto const units : {Units::Mph, Units::Kmh})
  {
    auto e = Engine(units);
    Drive(e, 0, 30);
    e.OnNav(NavEvent::Instruction(0, "Turn right onto Oak Lane", 4000));
    Heard h;
    for (double t = 1000, d = 3990; d > 0; t += 1000, d -= 30)
    {
      Drive(e, t, 30);
      h.Add(e.OnNav(NavEvent::Progress(t, d)));
    }
    std::string const want = units == Units::Mph ? "In 1 mile, turn right onto Oak Lane."
                                                 : "In 1 kilometer, turn right onto Oak Lane.";
    bool found = false;
    for (auto const & s : h.texts)
      found = found || s == want;
    CHECK(found);
  }
}
