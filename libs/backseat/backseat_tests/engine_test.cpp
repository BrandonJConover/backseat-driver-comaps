#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/lines.hpp"
#include "backseat/units.hpp"

using namespace backseat;

namespace
{
double Mph(double v) { return FromDisplay(v, Units::Mph); }

CritiqueEngine MakeEngine(uint32_t seed = 1)
{
  EngineConfig c;
  c.rng = SeededRng(seed);
  return CritiqueEngine(c, 0);
}

TelemetrySample Sample(double t, double mph, std::optional<double> limitMph)
{
  TelemetrySample s;
  s.t = t;
  s.speedMps = Mph(mph);
  if (limitMph)
    s.speedLimitMps = Mph(*limitMph);
  return s;
}
}  // namespace

TEST(SeededRngMatchesTypeScript)
{
  auto rng = SeededRng(1);
  // Values printed by seededRng(1) in engine/src/engine.ts.
  CHECK_EQUAL(rng(), 0.6270739405881613);
  CHECK_EQUAL(rng(), 0.002735721180215478);
  CHECK_EQUAL(rng(), 0.5274470399599522);
}

TEST(FormatDistanceRoundsLikeAGps)
{
  CHECK_EQUAL(FormatDistance(150, Units::Mph), std::string("500 feet"));
  CHECK_EQUAL(FormatDistance(2414, Units::Mph), std::string("1.5 miles"));
  CHECK_EQUAL(FormatDistance(44, Units::Kmh), std::string("40 meters"));
  CHECK_EQUAL(FormatDistance(1500, Units::Kmh), std::string("1.5 kilometers"));
}

TEST(FillKeepsUnknownsAndShoutsCapitals)
{
  Vars const vars{{"speed", "51"}, {"limit", "50"}, {"name", "Best Buy"}};
  CHECK_EQUAL(Fill("{speed} in a {limit}", vars), std::string("51 in a 50"));
  CHECK_EQUAL(Fill("{SPEED}. IN A {LIMIT}.", vars), std::string("51. IN A 50."));
  CHECK_EQUAL(Fill("To {NAME}!", vars), std::string("To BEST BUY!"));
  CHECK_EQUAL(Fill("{nope} and {}", vars), std::string("{nope} and {}"));
}

TEST(OneOverIsACrime)
{
  auto engine = MakeEngine();
  engine.OnTelemetry(Sample(0, 50, 50));
  auto const out = engine.OnTelemetry(Sample(1000, 51, 50));
  CHECK_EQUAL(out.size(), size_t{1});
  CHECK(out[0].category == Category::Speeding);
  CHECK(out[0].priority == Priority::Roast);
  CHECK_EQUAL(engine.Incidents().size(), size_t{1});
  CHECK_EQUAL(*engine.Incidents()[0].overLimit, 1);
}

TEST(HardBrakeCountsOncePerStop)
{
  auto engine = MakeEngine();
  engine.OnTelemetry(Sample(0, 40, std::nullopt));
  engine.OnTelemetry(Sample(1000, 30, std::nullopt));
  engine.OnTelemetry(Sample(2000, 20, std::nullopt));
  CHECK_EQUAL(engine.Stats().hardBrakes, 1);
}

TEST(TurnDirectionFromText)
{
  CHECK(TurnDirectionOf("Turn left onto Elm") == Maneuver::Left);
  CHECK(TurnDirectionOf("Keep RIGHT at the fork") == Maneuver::Right);
  CHECK(TurnDirectionOf("Make a U-turn to the left") == Maneuver::Other);
  CHECK(TurnDirectionOf("Continue on Leftwich Road") == Maneuver::Other);
}

TEST(CountdownNagsInOrder)
{
  auto engine = MakeEngine();
  engine.OnNav(NavEvent::Instruction(0, "Turn left onto Elm Street", 400));
  CHECK_EQUAL(engine.OnNav(NavEvent::Progress(1000, 140)).size(), size_t{1});
  CHECK_EQUAL(engine.OnNav(NavEvent::Progress(2000, 100)).size(), size_t{0});
  CHECK_EQUAL(engine.OnNav(NavEvent::Progress(3000, 12)).size(), size_t{1});
  CHECK_EQUAL(engine.OnNav(NavEvent::Progress(4000, 2)).size(), size_t{1});
  CHECK_EQUAL(engine.OnNav(NavEvent::Progress(6000, 0)).size(), size_t{0});
  auto const still = engine.OnNav(NavEvent::Progress(8000, 0));
  CHECK_EQUAL(still.size(), size_t{1});
  CHECK(still[0].priority == Priority::Nav);
}

TEST(StopSignLeftIsPayback)
{
  auto engine = MakeEngine();
  engine.OnNav(NavEvent::MissedTurn(0));
  auto const out = engine.OnNav(NavEvent::Instruction(30000, "Turn left onto 2nd Avenue", 150, std::nullopt, Control::StopSign));
  CHECK_EQUAL(out.size(), size_t{2});
  CHECK(out[1].category == Category::Payback);
}

TEST(ArrivalGradesTheTrip)
{
  auto engine = MakeEngine();
  engine.OnNav(NavEvent::Instruction(0, "Turn right onto Oak Avenue", 300));
  engine.OnNav(NavEvent::TurnCompleted(10000, true));
  auto const out = engine.OnNav(NavEvent::Arrived(60000));
  CHECK(!out.empty());
  CHECK(out.back().category == Category::Arrived);
  CHECK_EQUAL(engine.Report().overall.letter, std::string("A"));
  // Nothing more after arriving.
  CHECK(engine.OnTelemetry(Sample(61000, 90, 30)).empty());
}
