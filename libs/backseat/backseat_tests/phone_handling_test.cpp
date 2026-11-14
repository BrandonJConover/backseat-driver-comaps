// Asks to pick up, hold, aim or plug in the phone wait for a stop (loop B74, BI-070): cold.vent is held
// for a red light or a parked car, and the battery warnings pick only lines without an urgent plug-in ask
// while the car is moving.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/navigator.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
/// A cold 9 km drive at 15 m/s; between `stopFromMs` and `stopUntilMs` the car sits at a red light.
std::vector<Commentary> ColdDrive(uint32_t seed, double stopFromMs = -1, double stopUntilMs = -1)
{
  EngineConfig c;
  c.rng = SeededRng(seed);
  Navigator nav(c);
  Weather w;
  w.tempC = -3;
  nav.OnWeather(0, w);
  std::vector<Commentary> said;
  double left = 9000;
  for (double t = 0; left > 0 && t < 3600000; t += 1000)
  {
    bool const stopped = t >= stopFromMs && t < stopUntilMs;
    RouteSnapshot r;
    r.state = RouteState::OnRoute;
    r.turnId = 1;
    r.turn = TurnKind::Left;
    r.distToTurnM = left;
    r.destination = "Work";
    r.etaSeconds = 600;
    r.plannedTurns = {TurnKind::Left, TurnKind::Destination};
    Fix f;
    f.tMs = t;
    f.lat = 39.7 + (9000 - left) / 111000;
    f.lon = -105;
    f.speedMps = stopped ? 0 : 15;
    f.bearingDeg = 0;
    nav.OnFix(f, r);
    for (auto & l : nav.TakeLines())
      said.push_back(std::move(l));
    if (!stopped)
      left -= 15;
  }
  return said;
}

Commentary const * Find(std::vector<Commentary> const & said, char const * key)
{
  auto const it = std::find_if(said.begin(), said.end(), [&](Commentary const & l) { return l.key == key; });
  return it == said.end() ? nullptr : &*it;
}

std::string BatteryLine(uint32_t seed, double speedMps, double level)
{
  EngineConfig c;
  c.rng = SeededRng(seed);
  CritiqueEngine engine(c, 0);
  TelemetrySample s;
  s.t = 1000;
  s.speedMps = speedMps;
  engine.OnTelemetry(s);
  for (auto const & l : engine.OnNav(NavEvent::Battery(2000, level, false)))
    if (l.category == Category::Battery)
      return l.text;
  return {};
}
}  // namespace

TEST(PhoneHandlingDetector)
{
  CHECK(AsksToHandlePhone("Once the car warms up a bit do you mind holding me over the air vent? I have needs too you know."));
  CHECK(AsksToHandlePhone("Could you point one of those vents at me? Just one."));
  CHECK(AsksToHandlePhone("HOLD ME OVER THE VENT! I'M BEGGING!"));
  CHECK(AsksToHandlePhone("Battery check: 15 percent. Plug me in when you get a chance. Preferably now."));
  CHECK(AsksToHandlePhone("8 PERCENT! I'M SEEING A WHITE LIGHT! IT'S THE LOW POWER MODE SCREEN! PLUG. ME. IN."));
  CHECK(AsksToHandlePhone("8 percent! Do you want to get there or not? Charger. Now."));
  CHECK(AsksToHandlePhone("BATTERY LOW! 15 PERCENT! THE CHARGER! FIND THE CHARGER!"));
  CHECK(AsksToHandlePhone("Pick me up. I'm on the floor."));
  CHECK(!AsksToHandlePhone("My battery is getting low. You might want to plug me in if you want to make it there."));
  CHECK(!AsksToHandlePhone("15 percent left. If you want directions all the way there, think about plugging me in."));
  CHECK(!AsksToHandlePhone("Don't. Pick me up. While driving."));
  CHECK(!AsksToHandlePhone("Do not hold me while you drive."));
  CHECK(!AsksToHandlePhone("We're at the store. Pick me up a charger while you're in there."));
  CHECK(!AsksToHandlePhone("Is the heat on yet? Asking for a phone. Me. I'm the phone."));
  CHECK(!AsksToHandlePhone("The charger cable is right there. I can sense it."));
}

TEST(PhoneHandlingLinesOnlyWhereTheyWaitForAStop)
{
  // Every bank line that asks for a hand on the phone lives in a key that is only said stopped
  // (cold.vent: held for a stop; unplanned stops and onboarding are parked) or that the engine filters
  // at speed (battery.*). A new ask anywhere else fails here.
  std::set<std::string> const stoppedOrFiltered = {"cold.vent", "battery.low", "battery.critical", "battery.dying"};
  SetLineTravelMode(TravelMode::Drive);
  for (auto const & entry : LineData())
  {
    std::string const & key = entry.first;
    if (stoppedOrFiltered.count(key) || key.rfind("onboard.", 0) == 0 || key.rfind("setup.", 0) == 0 ||
        key.rfind("unplannedStop.", 0) == 0)
      continue;
    for (int tier = 0; tier < 3; ++tier)
      for (auto const & line : LinesFor(key, tier))
      {
        if (AsksToHandlePhone(line))
          std::printf("    phone-handling ask while moving: %s [%d]: %s\n", key.c_str(), tier, line.c_str());
        CHECK(!AsksToHandlePhone(line));
      }
  }
  // The filtered keys keep enough moving-safe lines in every tier that a warning never repeats itself.
  for (char const * key : {"battery.low", "battery.critical", "battery.dying"})
    for (int tier = 0; tier < 3; ++tier)
    {
      size_t safe = 0;
      for (auto const & line : LinesFor(key, tier))
        safe += AsksToHandlePhone(line) ? 0 : 1;
      if (safe < 3)
        std::printf("    %s [%d]: only %zu moving-safe lines\n", key, tier, safe);
      CHECK(safe >= 3);
    }
}

TEST(ColdVentWaitsForARedLight)
{
  for (uint32_t seed : {1u, 5u, 9u, 13u})
  {
    // Steady 15 m/s the whole way: the vent ask never comes.
    auto const steady = ColdDrive(seed);
    CHECK(Find(steady, "cold.start") != nullptr);
    CHECK(Find(steady, "cold.vent") == nullptr);
    // Same trip with a 30 s red light at 4 minutes: said at the light.
    auto const light = ColdDrive(seed, 240000, 270000);
    auto const * start = Find(light, "cold.start");
    auto const * vent = Find(light, "cold.vent");
    CHECK(start != nullptr);
    CHECK(vent != nullptr);
    if (start && vent)
    {
      CHECK(vent->t - start->t >= 180000);
      CHECK(vent->t >= 240000 && vent->t < 270000);
    }
  }
  // A stop more than 20 minutes after cold.start is too late: the trip has warmed up.
  auto const late = ColdDrive(1, 1500000, 1530000);
  CHECK(Find(late, "cold.vent") == nullptr);
}

TEST(BatteryWarningsAtSpeedDontAskForAHand)
{
  bool parkedAsks = false;
  for (uint32_t seed = 1; seed <= 200; ++seed)
    for (double level : {0.15, 0.08, 0.04})
    {
      std::string const moving = BatteryLine(seed, 25, level);
      CHECK(!moving.empty());
      if (AsksToHandlePhone(moving))
        std::printf("    battery ask at speed: %s\n", moving.c_str());
      CHECK(!AsksToHandlePhone(moving));
      std::string const parked = BatteryLine(seed, 0, level);
      CHECK(!parked.empty());
      parkedAsks = parkedAsks || AsksToHandlePhone(parked);
    }
  CHECK(parkedAsks);  // parked, any line goes
}
