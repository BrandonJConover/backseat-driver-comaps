// Lane guidance from the map's lanes (OSM turn:lanes). Before, the only lane call was "get over into
// the left lane" when the wanted lanes touched one edge; "use the right two lanes" came out as "Right
// lane, please" and lanes in the middle got nothing. Now the wording counts the lanes, the critic's
// joke follows the plain call, and Brandon's single-lane lines (lane.left / lane.right) are unchanged.
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"

#include <string>
#include <vector>

using namespace backseat;

namespace
{
RouteSnapshot Exit(uint32_t id, double dist, std::vector<bool> lanes)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = id;
  r.turn = TurnKind::ExitRight;
  r.distToTurnM = dist;
  r.nextStreet = "Colfax Avenue";
  r.destination = "Grandma's";
  r.etaSeconds = 900;
  r.plannedTurns = {TurnKind::ExitRight, TurnKind::Destination};
  r.lanes = std::move(lanes);
  r.laneSide = LaneAdviceFor(r.lanes).side;
  return r;
}

Fix At(double tMs, double mps)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.7;
  f.lon = -105;
  f.speedMps = mps;
  return f;
}

struct LaneCall
{
  std::string key;
  std::string text;
};

/// Drives toward the exit from 2.5 km at 25 m/s, 1 Hz; every lane call heard.
std::vector<LaneCall> Approach(std::vector<bool> const & lanes)
{
  EngineConfig c;
  c.rng = SeededRng(11);
  Navigator nav(c);
  std::vector<LaneCall> calls;
  double t = 0;
  for (double d = 2500; d > 20; d -= 25, t += 1000)
  {
    RouteSnapshot const r = Exit(4, d, lanes);
    nav.OnFix(At(t, 25), r);
    nav.Poll(r);
    for (auto const & l : nav.TakeLines())
      if (l.key.rfind("lane.", 0) == 0)
        calls.push_back({l.key, l.text});
  }
  return calls;
}
}  // namespace

TEST(LaneAdviceCountsTheLanes)
{
  auto phrase = [](std::vector<bool> const & l) { return LaneAdviceFor(l).phrase; };
  auto side = [](std::vector<bool> const & l) { return LaneAdviceFor(l).side; };
  // Edges.
  CHECK_EQUAL(phrase({false, false, true}), std::string("right lane"));
  CHECK_EQUAL(phrase({false, false, true, true}), std::string("right two lanes"));
  CHECK_EQUAL(phrase({true, true, true, false}), std::string("left three lanes"));
  CHECK_EQUAL(phrase({true, false}), std::string("left lane"));
  CHECK(side({false, true, true}) == LaneSide::Right);
  CHECK(side({true, true, false}) == LaneSide::Left);
  // The middle.
  CHECK_EQUAL(phrase({false, true, false}), std::string("middle lane"));
  CHECK_EQUAL(phrase({false, true, true, false}), std::string("middle two lanes"));
  CHECK_EQUAL(phrase({false, true, false, false}), std::string("second lane from the left"));
  CHECK_EQUAL(phrase({false, false, true, false}), std::string("second lane from the right"));
  CHECK_EQUAL(phrase({false, true, true, false, false}), std::string("second and third lanes from the left"));
  CHECK_EQUAL(phrase({false, false, true, true, true, false}),
              std::string("second through fourth lanes from the right"));
  CHECK(side({false, true, false}) == LaneSide::None);
  CHECK_EQUAL(LaneAdviceFor({false, true, true, false}).use, 2);
  CHECK_EQUAL(LaneAdviceFor({false, true, true, false}).of, 4);
  // Nothing to say: every lane, no lane, one lane, no data.
  CHECK_EQUAL(phrase({true, true, true}), std::string());
  CHECK_EQUAL(phrase({false, false}), std::string());
  CHECK_EQUAL(phrase({true}), std::string());
  CHECK_EQUAL(phrase({}), std::string());
  CHECK(side({true, true, true}) == LaneSide::None);
  // Scattered lanes: the side is kept (as before), no words.
  CHECK_EQUAL(phrase({true, false, true, false}), std::string());
  CHECK(side({true, false, true, false}) == LaneSide::Left);
  // Too many lanes to count out loud.
  CHECK_EQUAL(phrase({false, false, false, false, false, false, false, false, true, true, false, false, false, false,
                      false, false}),
              std::string());
}

TEST(LaneCallSaysHowManyLanes)
{
  auto const two = Approach({false, false, true, true});
  CHECK_EQUAL(two.size(), size_t(1));
  if (!two.empty())
  {
    CHECK_EQUAL(two[0].key, std::string("lane.many"));
    // Plain call first, the joke after.
    CHECK(two[0].text.rfind("Use the right two lanes.", 0) == 0 ||
          two[0].text.find("RIGHT TWO LANES") != std::string::npos);
  }

  auto const middle = Approach({false, true, false});
  CHECK_EQUAL(middle.size(), size_t(1));
  if (!middle.empty())
  {
    CHECK_EQUAL(middle[0].key, std::string("lane.middle"));
    CHECK(middle[0].text.find("middle lane") != std::string::npos ||
          middle[0].text.find("MIDDLE LANE") != std::string::npos);
  }

  // One lane at the edge: Brandon's pool, untouched.
  auto const one = Approach({false, false, true});
  CHECK_EQUAL(one.size(), size_t(1));
  if (!one.empty())
  {
    CHECK_EQUAL(one[0].key, std::string("lane.right"));
    CHECK_EQUAL(one[0].text, LinesFor("lane.right", 0)[0]);
  }

  // Any lane works: nothing said.
  CHECK(Approach({true, true, true}).empty());
  CHECK(Approach({}).empty());
}

TEST(LanePoolsLeadWithThePlainCall)
{
  // Every new lane line says which lanes before it says anything else, and fills cleanly.
  for (char const * key : {"lane.many", "lane.middle"})
    for (int tier = 0; tier < 3; ++tier)
    {
      auto const & pool = LinesFor(key, tier);
      CHECK(pool.size() >= 5);
      for (auto const & line : pool)
      {
        std::string const filled = Fill(line, {{"lanes", "right two lanes"}});
        CHECK(filled.find('{') == std::string::npos);
        CHECK(filled.find("right two lanes") != std::string::npos || filled.find("RIGHT TWO LANES") != std::string::npos);
        CHECK(line.find("{lanes}") < 20 || line.find("{LANES}") < 20);
      }
    }
  // Brandon's verbatim lines still open the single-lane pools.
  CHECK_EQUAL(LinesFor("lane.left", 0)[0],
              std::string("I'm going to need you to get over into the left lane. Use your blinkers this time."));
  CHECK_EQUAL(LinesFor("lane.right", 0)[0],
              std::string("I'm going to need you to get over into the right lane. Use your blinkers this time."));
}
