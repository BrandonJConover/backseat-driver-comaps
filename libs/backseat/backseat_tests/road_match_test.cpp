#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/road_match.hpp"
#include "backseat/voice_command.hpp"

#include <algorithm>
#include <cstdio>

using namespace backseat;

namespace
{
std::vector<AppCommand> Said(char const * spoken) { return ParseAppCommands(spoken); }

bool Only(char const * spoken, AppAction action, std::string const & argument = "")
{
  auto const c = Said(spoken);
  bool const ok = c.size() == 1 && c[0].action == action && c[0].argument == argument;
  if (!ok)
    std::printf("  \"%s\": %zu commands, first %s \"%s\"\n", spoken, c.size(), c.empty() ? "-" : ToolFor(c[0].action).name,
                c.empty() ? "" : c[0].argument.c_str());
  return ok;
}

bool NoAvoidRoad(char const * spoken)
{
  auto const c = Said(spoken);
  bool const ok = std::none_of(c.begin(), c.end(), [](AppCommand const & x) { return x.action == AppAction::AvoidRoad; });
  if (!ok)
    std::printf("  \"%s\" heard avoid_road\n", spoken);
  return ok;
}
}  // namespace

TEST(RoadNamesMatchTheMap)
{
  CHECK(RoadNameMatches("main street", "Main St", ""));
  CHECK(RoadNameMatches("Main St.", "Main Street", ""));
  CHECK(RoadNameMatches("main", "North Main Street", ""));
  CHECK(RoadNameMatches("college avenue", "N College Ave", "US 71B"));
  CHECK(RoadNameMatches("martin luther king boulevard", "Martin Luther King Jr Blvd", ""));
  CHECK(RoadNameMatches("highway 71", "", "US 71"));
  CHECK(RoadNameMatches("us 71", "", "US 62;US 71"));
  CHECK(RoadNameMatches("I-49", "", "I 49"));
  CHECK(RoadNameMatches("interstate 49", "Interstate 49", "I 49"));
  CHECK(RoadNameMatches("i 49", "", "I 49"));
  // Not the same road.
  CHECK(!RoadNameMatches("elm street", "Elm Tree Road", ""));
  CHECK(!RoadNameMatches("highway 71", "", "AR 12"));
  CHECK(!RoadNameMatches("highway 71", "", "US 712"));
  CHECK(!RoadNameMatches("i 40", "", "I 49"));
  CHECK(!RoadNameMatches("the highway", "", "I 49"));
  CHECK(!RoadNameMatches("", "Main St", ""));
  CHECK(!RoadNameMatches("main street", "", ""));
}

TEST(AvoidARoadIsHeard)
{
  CHECK(Only("avoid Main Street", AppAction::AvoidRoad, "main street"));
  CHECK(Only("stay off highway 71 please", AppAction::AvoidRoad, "highway 71"));
  CHECK(Only("can you avoid I-49", AppAction::AvoidRoad, "i 49"));
  CHECK(Only("keep me off College Avenue", AppAction::AvoidRoad, "college avenue"));
  CHECK(Only("skip Walton Boulevard", AppAction::AvoidRoad, "walton boulevard"));
  CHECK(Only("stay away from Elm Tree Road", AppAction::AvoidRoad, "elm tree road"));
  // The road the car is on.
  for (char const * here : {"avoid this road", "get me off this road", "ugh, get us off this road", "I hate this road"})
  {
    CHECK(Only(here, AppAction::AvoidRoad, ""));
    CHECK(WordsSupportTool(AppAction::AvoidRoad, "", here));
  }
  CHECK(WordsSupportTool(AppAction::AvoidRoad, "main street", "avoid main street"));
  CHECK(!WordsSupportTool(AppAction::AvoidRoad, "main street", "avoid elm street"));
  // Road kinds stay what they were.
  CHECK(Only("avoid highways", AppAction::AvoidHighways));
  CHECK(Only("stay off the highway", AppAction::AvoidHighways));
  CHECK(Only("skip the toll road", AppAction::AvoidTolls));
  // Not asks to avoid a road.
  for (char const * chatter : {"skip the next stop", "I avoid Main Street on Fridays because of the market",
                               "don't avoid Main Street", "maybe later we can avoid main street",
                               "avoid traffic", "the road is closed", "avoid left turns", "we live on Main Street"})
    CHECK(NoAvoidRoad(chatter));
  CHECK(Only("the road is closed", AppAction::DifferentRoute));
  CHECK(ToolFor(AppAction::AvoidRoad).argument != nullptr);
  CHECK(AppActionNamed("avoid_road") == AppAction::AvoidRoad);
}

TEST(FewerLeftTurnsIsHeard)
{
  for (char const * said : {"fewer left turns", "can we avoid left turns", "I hate left turns", "avoid lefts please",
                            "no more left turns"})
  {
    CHECK(Only(said, AppAction::FewerLefts));
    CHECK(WordsSupportTool(AppAction::FewerLefts, "", said));
  }
  CHECK(Only("left turns are fine", AppAction::AllowLefts));
  CHECK(Only("don't avoid left turns", AppAction::AllowLefts));
  CHECK(Only("I don't mind left turns", AppAction::AllowLefts));
  CHECK(Said("turn left at the light").empty());
  CHECK(AppActionNamed("fewer_left_turns") == AppAction::FewerLefts);
  CHECK(AppActionNamed("allow_left_turns") == AppAction::AllowLefts);
}

TEST(RoutePreferenceRepliesExist)
{
  for (char const * key : {"cmd.avoidRoad", "cmd.avoidRoad.none", "cmd.avoidRoad.notOnRoute", "cmd.avoidRoad.unknown",
                           "cmd.fewerLefts", "cmd.allowLefts"})
    for (int tier = 0; tier < 3; ++tier)
      CHECK(!LinesFor(key, tier).empty());
}
