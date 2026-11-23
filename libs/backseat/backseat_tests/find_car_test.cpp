// "Where did I park?" Google Maps and Apple Maps both drop a parked-car pin; Backseat keeps the spot
// where a drive ended (or where the driver said "remember where I parked") and walks them back to it.
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"
#include "backseat/voice_command.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
bool Heard(std::string const & text, AppAction action)
{
  for (auto const & c : ParseAppCommands(text))
    if (c.action == action)
      return true;
  return false;
}

std::vector<AppAction> Actions(std::string const & text)
{
  std::vector<AppAction> out;
  for (auto const & c : ParseAppCommands(text))
    out.push_back(c.action);
  return out;
}

LatLon constexpr kCar{39.7392, -104.9903};
double constexpr kMPerDegLat = 111195;
LatLon North(double m) { return {kCar.lat + m / kMPerDegLat, kCar.lon}; }
}  // namespace

TEST(ParkedCarLineRoundTrips)
{
  ParkedCar const car{kCar, 1763900000000, true};
  auto const back = ParkedCarFromLine(ParkedCarLine(car));
  CHECK(back.has_value());
  CHECK(std::abs(back->at.lat - kCar.lat) < 1e-7);
  CHECK(std::abs(back->at.lon - kCar.lon) < 1e-7);
  CHECK_EQUAL(back->tMs, car.tMs);
  CHECK(back->saved);
  for (std::string const bad : {"", "39.7,-104.9", "abc,1,2,0", "95,0,1,0", "39,-200,1,0", "39,1,-5,0", "39,1,nan,0",
                                "39,1,2,0x", "1,2,3,4,5,6"})
    CHECK(!ParkedCarFromLine(bad) || bad == "1,2,3,4,5,6");
}

TEST(ParkedAgoReadsNaturally)
{
  CHECK_EQUAL(ParkedAgoWords(2 * 60000), std::string("for a few minutes"));
  CHECK_EQUAL(ParkedAgoWords(22 * 60000), std::string("for about 20 minutes"));
  CHECK_EQUAL(ParkedAgoWords(70 * 60000), std::string("for about an hour"));
  CHECK_EQUAL(ParkedAgoWords(3.2 * 3600000), std::string("for about 3 hours"));
  CHECK_EQUAL(ParkedAgoWords(26 * 3600000.0), std::string("since yesterday"));
  CHECK_EQUAL(ParkedAgoWords(4 * 86400000.0), std::string("for 4 days"));
  CHECK_EQUAL(ParkedAgoWords(-3600000), std::string());
}

TEST(FindCarAnswerPicksTheRightCall)
{
  ParkedCar const car{kCar, 0, false};
  double const now = 30 * 60000;
  auto const walk = FindCarAnswer(car, North(400), now, Units::Mph, false);
  CHECK_EQUAL(walk.key, std::string("car.find"));
  CHECK(walk.walkTo.has_value());
  CHECK_EQUAL(walk.vars.at("direction"), std::string("south"));
  CHECK_EQUAL(walk.vars.at("ago"), std::string("for about 30 minutes"));
  CHECK(!walk.vars.at("distance").empty());

  auto const here = FindCarAnswer(car, North(15), now, Units::Mph, false);
  CHECK_EQUAL(here.key, std::string("car.here"));
  CHECK(!here.walkTo);

  auto const far = FindCarAnswer(car, North(30000), now, Units::Kmh, false);
  CHECK_EQUAL(far.key, std::string("car.far"));
  CHECK(!far.walkTo);

  CHECK_EQUAL(FindCarAnswer(std::nullopt, North(400), now, Units::Mph, false).key, std::string("car.unknown"));
  CHECK_EQUAL(FindCarAnswer(car, North(400), now, Units::Mph, true).key, std::string("car.driving"));
  // No fix: still walk there; the router starts from the phone.
  auto const blind = FindCarAnswer(car, std::nullopt, now, Units::Mph, false);
  CHECK_EQUAL(blind.key, std::string("car.find"));
  CHECK(blind.walkTo.has_value());
}

TEST(WhereDidIParkIsHeard)
{
  for (char const * said : {"where did I park", "Where's my car?", "take me back to my car", "find my car please",
                            "I can't find my car", "walk me to the car", "where did we park"})
  {
    auto const actions = Actions(said);
    CHECK_EQUAL(actions.size(), size_t{1});
    CHECK(Heard(said, AppAction::FindMyCar));
  }
  for (char const * said : {"remember where I parked", "I parked here", "save my parking spot"})
  {
    auto const actions = Actions(said);
    CHECK_EQUAL(actions.size(), size_t{1});
    CHECK(Heard(said, AppAction::SaveParking));
  }
  // Not about the car.
  CHECK(!Heard("take me back to where we started", AppAction::FindMyCar));
  CHECK(Heard("take me back to where we started", AppAction::TakeMeBack));
  CHECK(!Heard("where am I", AppAction::FindMyCar));
  CHECK(!Heard("find a parking garage", AppAction::FindMyCar));
  CHECK(!Heard("I don't know where I parked last week", AppAction::SaveParking));
}

TEST(FindCarToolNeedsCarWords)
{
  CHECK(WordsSupportTool(AppAction::FindMyCar, "", "where did I park"));
  CHECK(!WordsSupportTool(AppAction::FindMyCar, "", "tell me a joke"));
  CHECK(WordsSupportTool(AppAction::SaveParking, "", "save my spot"));
  CHECK_EQUAL(std::string(ToolFor(AppAction::FindMyCar).name), std::string("find_my_car"));
  CHECK_EQUAL(std::string(ToolFor(AppAction::SaveParking).name), std::string("save_parking"));
}

TEST(CarLinesExistInEveryTier)
{
  for (char const * key : {"car.find", "car.here", "car.far", "car.unknown", "car.driving", "car.saved", "car.saveMoving"})
    for (int tier = 0; tier < 3; ++tier)
      CHECK(LinesFor(key, tier).size() >= 3);
}
