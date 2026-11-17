// One safe PickIndex for every random pick (loop B164, BI-146). Before it, CritiqueEngine::Pick
// cast floor(rng() * n) straight to size_t: an rng returning 1.0 or more indexed past the end, and
// NaN, negative or inf was undefined behavior there and in the plate game, 20 Questions and the
// navigator's outdoor pick. A host's random source is outside our control, so a broken one must
// still pick a real line.
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/engine.hpp"
#include "backseat/navigator.hpp"
#include "backseat/pick_index.hpp"
#include "backseat/plate_game.hpp"
#include "backseat/rundown.hpp"
#include "backseat/twenty_questions.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace backseat;
using namespace backseat_tests;

namespace
{
double const kNan = std::numeric_limits<double>::quiet_NaN();
double const kInf = std::numeric_limits<double>::infinity();
std::vector<double> const kBroken = {1.0, 1.5, -0.2, kNan, kInf, -kInf, 1e300, -0.0};

void CheckSpoken(std::string const & text, char const * where)
{
  if (text.empty() || text.find('{') != std::string::npos)
    throw ::backseat_tests::Failure{std::string(where) + ": bad line \"" + text + "\""};
}
}  // namespace

TEST(PickIndexClampsEveryDraw)
{
  CHECK_EQUAL(PickIndex(0.0, 5), size_t{0});
  CHECK_EQUAL(PickIndex(0.5, 5), size_t{2});
  CHECK_EQUAL(PickIndex(0.99, 5), size_t{4});
  CHECK_EQUAL(PickIndex(std::nextafter(1.0, 0.0), 5), size_t{4});
  CHECK_EQUAL(PickIndex(std::nextafter(1.0, 0.0), size_t{1} << 40), (size_t{1} << 40) - 1);
  CHECK_EQUAL(PickIndex(1.0, 5), size_t{4});
  CHECK_EQUAL(PickIndex(1.5, 5), size_t{4});
  CHECK_EQUAL(PickIndex(kInf, 5), size_t{4});
  CHECK_EQUAL(PickIndex(1e300, 5), size_t{4});
  CHECK_EQUAL(PickIndex(-0.2, 5), size_t{0});
  CHECK_EQUAL(PickIndex(-kInf, 5), size_t{0});
  CHECK_EQUAL(PickIndex(kNan, 5), size_t{0});
  CHECK_EQUAL(PickIndex(-0.0, 5), size_t{0});
  CHECK_EQUAL(PickIndex(0.7, 1), size_t{0});
  CHECK_EQUAL(PickIndex(0.7, 0), size_t{0});
  CHECK_EQUAL(PickIndex(kNan, 0), size_t{0});
}

TEST(PickIndexMatchesFloorForGoodDraws)
{
  // For r in [0, 1) the pick is exactly floor(r * n), so seeded drives (and the TS parity trace)
  // say the same lines as before.
  auto rng = SeededRng(7);
  for (int i = 0; i < 20000; ++i)
  {
    double const r = rng();
    size_t const n = 1 + static_cast<size_t>(i % 37);
    CHECK_EQUAL(PickIndex(r, n), static_cast<size_t>(std::floor(r * static_cast<double>(n))));
  }
}

TEST(BrokenRngDrivesAWholeTrip)
{
  for (double const v : kBroken)
  {
    EngineConfig c;
    c.rng = [v] { return v; };
    Navigator nav(c);
    SimOptions o;
    o.shape = TripShape::Town;
    o.seed = 3;
    auto const r = SimulateTrip(nav, o);
    CHECK(!r.spoken.empty());
    for (auto const & s : r.spoken)
      CheckSpoken(s.line.text, "trip");
  }
}

TEST(BrokenRngCritiqueEnginePicks)
{
  for (double const v : kBroken)
  {
    EngineConfig c;
    c.rng = [v] { return v; };
    CritiqueEngine e(c, 0);
    // The same key over and over walks the fresh/recent pools (was index n with rng() == 1).
    for (int i = 0; i < 12; ++i)
    {
      for (auto const & line : e.OnNav(NavEvent::MissedTurn(60000.0 * (i + 1))))
        CheckSpoken(line.text, "engine");
    }
  }
}

TEST(BrokenRngGamesStillPickRealLines)
{
  for (double const v : kBroken)
  {
    auto const rng = [v] { return v; };
    TwentyQuestions twenty(rng);
    double t = 0;
    CheckSpoken(twenty.Start({}, false, t, 0).text, "20Q start");
    for (std::string const said : {"is it alive", "is it bigger than a car", "give me a hint", "I give up"})
    {
      t += 5000;
      if (auto const reply = twenty.OnDriverSaid(said, t, 1))
        CheckSpoken(reply->text, "20Q");
    }
    PlateGame plates(rng);
    CheckSpoken(plates.Offer(1).text, "plates offer");
    for (std::string const said : {"yes", "I saw Texas", "Ohio", "saw an Ontario plate"})
      if (auto const reply = plates.OnDriverSaid(said, 1))
        CheckSpoken(reply->text, "plates");
  }
}

TEST(RundownSurvivesBrokenAndEmptyRng)
{
  std::vector<RouteStep> steps(3);
  steps[0].text = "Turn left onto Elm Street";
  steps[1].text = "Turn right onto Main Street";
  steps[2].text = "Arrive at destination";
  for (double const v : kBroken)
  {
    Rng const rng = [v] { return v; };
    CheckSpoken(RouteRundown(steps, {}, rng, 3), "rundown");
    CheckSpoken(TripGreeting({}, rng, 3), "greeting");
  }
  Rng const none;
  CheckSpoken(RouteRundown(steps, {}, none, 3), "rundown, no rng");
}
