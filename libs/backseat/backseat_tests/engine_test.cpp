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

namespace
{
std::vector<std::string> Texts(std::vector<Commentary> const & lines)
{
  std::vector<std::string> out;
  for (auto const & c : lines)
    out.push_back(c.text);
  return out;
}

CritiqueEngine FirstLineEngine()
{
  EngineConfig c;
  c.rng = [] { return 0.0; };
  return CritiqueEngine(c, 0);
}
}  // namespace

TEST(TurnNagsRunInOrderAndEndWithPleaseStop)
{
  auto e = FirstLineEngine();
  e.OnNav(NavEvent::Instruction(0, "Turn left onto Elm", 400));
  std::vector<std::string> said;
  double const ds[] = {300, 200, 140, 100, 40, 14, 8, 3, 2, 1, 0, 0};
  for (size_t i = 0; i < std::size(ds); ++i)
    for (auto const & t : Texts(e.OnNav(NavEvent::Progress(1000.0 * (i + 1), ds[i]))))
      said.push_back(t);
  for (auto const & t : Texts(e.OnNav(NavEvent::TurnCompleted(13000, true))))
    said.push_back(t);
  std::vector<std::string> const want = {
      "Just in case you forgot, you are turning left in 500 feet.", "By the way, did I mention you should turn left.",
      "You are turning left.", "You are still turning left.", "Please stop turning left."};
  CHECK(said == want);
}

TEST(StraightNagsOnALongStretch)
{
  auto e = FirstLineEngine();
  auto const opening = Texts(e.OnNav(NavEvent::Instruction(0, "Continue straight", 321869)));
  CHECK_EQUAL(opening.size(), size_t{2});
  CHECK(opening.size() == 2 && opening[1].find("for the next 200 miles") != std::string::npos);
  auto f = FirstLineEngine();
  f.OnNav(NavEvent::Instruction(0, "Turn right onto Main", 3000));
  std::vector<std::string> said;
  for (double t = 1000, d = 3000; d > 400; t += 1000, d -= 20)
    for (auto const & s : Texts(f.OnNav(NavEvent::Progress(t, d))))
      said.push_back(s);
  std::vector<std::string> const want = {"Please continue straight.", "You should still be going straight.",
                                         "I'm serious."};
  CHECK(said == want);
}

TEST(NascarOnTheThirdLeftInARow)
{
  auto e = FirstLineEngine();
  auto const & nascar = LinesFor("countdown.nascar", 0);
  std::vector<int> hits;
  char const * const dirs[] = {"left", "left", "left", "left", "right", "left", "left", "left"};
  for (int i = 0; i < 8; ++i)
  {
    double const t = i * 10000.0;
    e.OnNav(NavEvent::Instruction(t, std::string("Turn ") + dirs[i] + " onto Elm", 400));
    for (auto const & s : Texts(e.OnNav(NavEvent::Progress(t + 1000, 140))))
      if (std::find(nascar.begin(), nascar.end(), s) != nascar.end())
        hits.push_back(i);
    e.OnNav(NavEvent::TurnCompleted(t + 2000, true));
  }
  CHECK(hits == (std::vector<int>{2, 7}));
}
