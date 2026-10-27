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
  engine.OnNav(NavEvent::Instruction(-10000, "Turn left onto Elm Street", 400));
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
  e.OnNav(NavEvent::Instruction(-10000, "Turn left onto Elm", 400));
  std::vector<std::string> said;
  double const ds[] = {300, 200, 140, 100, 40, 14, 8, 3, 2, 1, 0, 0};
  for (size_t i = 0; i < std::size(ds); ++i)
    // City speed: the 250 ft mark comes well after the 500 ft call.
    for (auto const & t : Texts(e.OnNav(NavEvent::Progress(1000.0 * (i + 1) + (i >= 4 ? 10000 : 0), ds[i]))))
      said.push_back(t);
  for (auto const & t : Texts(e.OnNav(NavEvent::TurnCompleted(23000, true))))
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
    double const t = i * 20000.0;
    e.OnNav(NavEvent::Instruction(t, std::string("Turn ") + dirs[i] + " onto Elm", 400));
    for (auto const & s : Texts(e.OnNav(NavEvent::Progress(t + 9000, 140))))
      if (std::find(nascar.begin(), nascar.end(), s) != nascar.end())
        hits.push_back(i);
    e.OnNav(NavEvent::TurnCompleted(t + 9500, true));
  }
  CHECK(hits == (std::vector<int>{2, 7}));
}

namespace
{
/// A maneuver from 400 m out through "still" and the completed turn: everything said.
std::vector<std::string> CountdownFor(std::string const & text)
{
  auto e = FirstLineEngine();
  e.OnNav(NavEvent::Instruction(-10000, text, 400));
  std::vector<std::string> said;
  double const ds[] = {300, 140, 70, 40, 3, 0, 0, 0};
  for (size_t i = 0; i < std::size(ds); ++i)
    for (auto const & t : Texts(e.OnNav(NavEvent::Progress(1000.0 * (i + 1) + (i >= 2 ? 10000 : 0), ds[i]))))
      said.push_back(t);
  for (auto const & t : Texts(e.OnNav(NavEvent::TurnCompleted(20000, true))))
    said.push_back(t);
  return said;
}
}  // namespace

TEST(ExitsUseExitWordingNotTurnRight)
{
  std::vector<std::string> const want = {
      "In 500 feet you are going to take the exit on the right.",
      "By the way, did I mention you should take the exit on the right.", "Hey, you should be taking the exit.",
      "You should still be taking the exit.", "Please stop exiting. You've exited."};
  CHECK(CountdownFor("Take the exit on the right onto I-25") == want);
  CHECK(TurnShapeOf("Take the exit on the left") == TurnShape::Exit);
}

TEST(UTurnsAndRoundaboutsGetCountdowns)
{
  std::vector<std::string> const uturn = {
      "In 500 feet you are going to make a U-turn.", "By the way, did I mention you should make a U-turn.",
      "Hey, you should be making a U-turn.", "You should still be making a U-turn.",
      "Please stop making a U-turn. If you keep going, we'll be headed the wrong way again."};
  CHECK(CountdownFor("Make a U-turn") == uturn);
  std::vector<std::string> const roundabout = {
      "In 500 feet you are going to take the roundabout.",
      "By the way, did I mention you should take the roundabout.", "Hey, you should be taking the roundabout.",
      "You should still be going around the roundabout.", "Please stop going around. You're out of the roundabout."};
  CHECK(CountdownFor("Take the roundabout onto Elm Street") == roundabout);
}

namespace
{
TelemetrySample Heading(double t, double deg)
{
  TelemetrySample s = Sample(t, 30, std::nullopt);
  s.headingDeg = deg;
  return s;
}

int WrongTurnLines(std::vector<Commentary> const & lines)
{
  int n = 0;
  for (auto const & c : lines)
    n += c.category == Category::WrongTurn;
  return n;
}
}  // namespace

TEST(FinishingARightAsTheNextLeftIsAnnouncedIsNotAWrongTurn)
{
  auto engine = MakeEngine();
  engine.OnNav(NavEvent::Instruction(0, "Turn right onto Oak", 100));
  std::vector<Commentary> out;
  auto add = [&](std::vector<Commentary> v) { out.insert(out.end(), v.begin(), v.end()); };
  double t = 1000;
  for (int i = 0; i < 8; ++i, t += 1000)
    add(engine.OnTelemetry(Heading(t, 0)));
  // Halfway through the right, the router moves on to the next maneuver: a left, 90 m away.
  for (int i = 1; i <= 2; ++i, t += 1000)
    add(engine.OnTelemetry(Heading(t, 22.5 * i)));
  add(engine.OnNav(NavEvent::TurnCompleted(t, true)));
  add(engine.OnNav(NavEvent::Instruction(t, "Turn left onto Elm Street", 90)));
  for (int i = 3; i <= 4; ++i, t += 1000)
    add(engine.OnTelemetry(Heading(t, 22.5 * i)));
  for (int i = 0; i < 8; ++i, t += 1000)
    add(engine.OnTelemetry(Heading(t, 90)));
  CHECK_EQUAL(WrongTurnLines(out), 0);
  CHECK_EQUAL(engine.Stats().wrongTurns, 0);
}

TEST(TurningTheWrongWayAtTheTurnStillCounts)
{
  auto engine = MakeEngine();
  engine.OnNav(NavEvent::Instruction(0, "Turn left onto Elm Street", 100));
  std::vector<Commentary> out;
  double t = 1000;
  for (int i = 0; i < 8; ++i, t += 1000)
    for (auto & c : engine.OnTelemetry(Heading(t, 0)))
      out.push_back(c);
  for (int i = 1; i <= 4; ++i, t += 1000)
    for (auto & c : engine.OnTelemetry(Heading(t, 22.5 * i)))
      out.push_back(c);
  for (int i = 0; i < 8; ++i, t += 1000)
    for (auto & c : engine.OnTelemetry(Heading(t, 90)))
      out.push_back(c);
  CHECK_EQUAL(WrongTurnLines(out), 1);
}

TEST(ABendFarFromTheNextTurnIsNotAWrongTurn)
{
  auto engine = MakeEngine();
  engine.OnNav(NavEvent::Instruction(0, "Turn left onto Elm Street", 1200));
  engine.OnNav(NavEvent::Progress(500, 1100));
  std::vector<Commentary> out;
  double t = 1000;
  for (int i = 0; i < 8; ++i, t += 1000)
    for (auto & c : engine.OnTelemetry(Heading(t, 0)))
      out.push_back(c);
  for (int i = 1; i <= 4; ++i, t += 1000)
    for (auto & c : engine.OnTelemetry(Heading(t, 22.5 * i)))
      out.push_back(c);
  for (int i = 0; i < 8; ++i, t += 1000)
    for (auto & c : engine.OnTelemetry(Heading(t, 90)))
      out.push_back(c);
  CHECK_EQUAL(WrongTurnLines(out), 0);
}
