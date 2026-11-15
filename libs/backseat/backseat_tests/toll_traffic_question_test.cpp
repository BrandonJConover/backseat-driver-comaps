// Loop B165 (core half of BI-142): "are there tolls?" and "is there traffic ahead?" are heard as
// TollsAhead / TrafficAhead and answered from what the navigator actually knows: the route's toll
// stretch (on one / in X / none / can't tell) and its own crawl judgment (jam / moving / can't tell yet).
// Stories about past drives ("I paid the toll yesterday") are not questions. No prices, no claims about
// traffic miles ahead.
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"
#include "backseat/units.hpp"
#include "backseat/voice_command.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
bool HeardOnly(std::string const & said, AppAction action)
{
  auto const c = ParseAppCommands(said);
  return c.size() == 1 && c[0].action == action;
}

bool HeardAny(std::string const & said, AppAction action)
{
  for (auto const & c : ParseAppCommands(said))
    if (c.action == action)
      return true;
  return false;
}

/// Whether `reply` is one of `key`'s lines (any tier), with {distance} filled by `distance`.
bool FromPool(std::string const & reply, std::string const & key, std::string const & distance = {})
{
  for (int tier = 0; tier < 3; ++tier)
    for (std::string line : LinesFor(key, tier))
    {
      for (size_t at; (at = line.find("{distance}")) != std::string::npos;)
        line.replace(at, 10, distance);
      if (line == reply)
        return true;
    }
  return false;
}

EngineConfig Config()
{
  EngineConfig c;
  c.rng = SeededRng(5);
  return c;
}

RouteSnapshot Route(std::optional<double> tollAheadM, bool tollsKnown = true)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Right;
  r.distToTurnM = 20000;
  r.nextStreet = "Turnpike Road";
  r.destination = "the city";
  r.etaSeconds = 3600;
  r.plannedTurns = {TurnKind::Right, TurnKind::Destination};
  r.destinationPoint = {40.5, -105};
  r.tollAheadM = tollAheadM;
  r.tollsKnown = tollsKnown;
  return r;
}

struct Drive
{
  Navigator nav{Config()};
  double lat = 39.7;
  int t = 0;

  void Run(int seconds, double mps, RouteSnapshot const & route, std::optional<double> limitMps = {})
  {
    for (int end = t + seconds; t < end; ++t)
    {
      Fix f;
      f.tMs = t * 1000.0;
      f.lat = lat;
      f.lon = -105;
      f.speedMps = mps;
      f.speedLimitMps = limitMps;
      nav.OnFix(f, route);
      lat += mps / 111000.0;
      (void)nav.TakeLines();
    }
  }
};
}  // namespace

TEST(TollQuestionsAreHeard)
{
  for (char const * said : {"are there tolls", "Are there any tolls on this route?", "any tolls ahead?",
                            "is this a toll road", "Is that a toll road?", "are we on a toll road",
                            "do we have to pay a toll", "will there be tolls", "hey, any toll roads?"})
  {
    bool const ok = HeardOnly(said, AppAction::TollsAhead);
    if (!ok)
      std::printf("    not heard as tolls: %s\n", said);
    CHECK(ok);
    CHECK(WordsSupportTool(AppAction::TollsAhead, "", said));
  }
  // Still the orders they always were.
  CHECK(HeardOnly("avoid tolls", AppAction::AvoidTolls));
  CHECK(HeardOnly("tolls are fine", AppAction::AllowTolls));
  CHECK(!WordsSupportTool(AppAction::TollsAhead, "", "is there traffic"));
}

TEST(TrafficQuestionsAreHeard)
{
  for (char const * said : {"is there traffic ahead", "Is there any traffic?", "how's the traffic", "how is traffic",
                            "why is it so slow", "Why aren't we moving?", "what's the holdup", "are we stuck in traffic"})
  {
    bool const ok = HeardOnly(said, AppAction::TrafficAhead);
    if (!ok)
      std::printf("    not heard as traffic: %s\n", said);
    CHECK(ok);
    CHECK(WordsSupportTool(AppAction::TrafficAhead, "", said));
  }
  CHECK(!WordsSupportTool(AppAction::TrafficAhead, "", "are there tolls"));
}

TEST(PastTollAndTrafficTalkIsNotAQuestion)
{
  for (char const * said : {"I paid the toll yesterday", "traffic was bad last week", "was there any traffic yesterday",
                            "there were tolls last time", "we got stuck in traffic this morning",
                            "did we pay any tolls on the way here", "is this a toll road? no wait, that was the old one"})
  {
    bool const heard = HeardAny(said, AppAction::TollsAhead) || HeardAny(said, AppAction::TrafficAhead);
    // The last one asks first, then talks about the past in its own clause: still a question.
    bool const expect = std::string(said).rfind("is this a toll road?", 0) == 0;
    if (heard != expect)
      std::printf("    wrong: %s\n", said);
    CHECK_EQUAL(heard, expect);
  }
  // A story, then a question in its own clause: the question counts.
  CHECK(HeardAny("I paid one yesterday. Are there tolls today?", AppAction::TollsAhead));
  CHECK(HeardAny("traffic was awful last week, how's the traffic now?", AppAction::TrafficAhead));
  // Negated: not asked.
  CHECK(!HeardAny("I don't care if there's traffic ahead", AppAction::TrafficAhead));
}

TEST(TollQuestionsComeBeforeGames)
{
  CHECK(DriveComesFirst("are there tolls"));
  CHECK(DriveComesFirst("is there traffic ahead"));
  CHECK(DriveComesFirst("why is it so slow"));
}

TEST(NewToolsKeepEnumOrder)
{
  CHECK(ToolFor(AppAction::TollsAhead).action == AppAction::TollsAhead);
  CHECK(ToolFor(AppAction::TrafficAhead).action == AppAction::TrafficAhead);
  CHECK(AppActionNamed("tolls_ahead") == AppAction::TollsAhead);
  CHECK(AppActionNamed("traffic_ahead") == AppAction::TrafficAhead);
}

TEST(TollReplyFollowsTheRoute)
{
  {
    Navigator idle{Config()};
    CHECK(FromPool(idle.TollReply(), "cmd.tolls.none"));
  }
  {
    // The host didn't read toll data: "can't tell", never a made-up "no tolls".
    Drive d;
    d.Run(5, 20, Route(std::nullopt, false));
    CHECK(FromPool(d.nav.TollReply(), "cmd.tolls.unknown"));
  }
  {
    Drive d;
    d.Run(5, 20, Route(std::nullopt));
    CHECK(FromPool(d.nav.TollReply(), "cmd.tolls.free"));
  }
  {
    Drive d;
    d.Run(5, 20, Route(0));
    CHECK(FromPool(d.nav.TollReply(), "cmd.tolls.on"));
    d.Run(1, 20, Route(30));
    CHECK(FromPool(d.nav.TollReply(), "cmd.tolls.on"));
  }
  {
    Drive d;
    d.Run(5, 20, Route(8047));
    auto const reply = d.nav.TollReply();
    auto const distance = FormatDistance(8047, d.nav.GetUnits());
    bool const ok = FromPool(reply, "cmd.tolls.ahead", distance);
    if (!ok)
      std::printf("    toll ahead reply: %s (%s)\n", reply.c_str(), distance.c_str());
    CHECK(ok);
  }
}

TEST(TollLinesNeverQuoteAPrice)
{
  for (char const * key : {"cmd.tolls.ahead", "cmd.tolls.on", "cmd.tolls.free", "cmd.tolls.unknown", "cmd.tolls.none"})
    for (int tier = 0; tier < 3; ++tier)
    {
      CHECK(!LinesFor(key, tier).empty());
      for (auto const & line : LinesFor(key, tier))
        CHECK(line.find('$') == std::string::npos && line.find("dollar") == std::string::npos &&
              line.find("cents") == std::string::npos);
    }
}

TEST(TrafficReplyFollowsTheCrawl)
{
  {
    Navigator idle{Config()};
    CHECK(FromPool(idle.TrafficReply(), "cmd.traffic.none"));
  }
  {
    // A few seconds in: not enough driving to judge.
    Drive d;
    d.Run(10, 25, Route(std::nullopt), 29);
    CHECK(FromPool(d.nav.TrafficReply(), "cmd.traffic.unknown"));
    // No speed limits known: still can't judge, however long.
    Drive n;
    n.Run(200, 25, Route(std::nullopt));
    CHECK(FromPool(n.nav.TrafficReply(), "cmd.traffic.unknown"));
  }
  {
    Drive d;
    d.Run(120, 27, Route(std::nullopt), 29);
    CHECK(FromPool(d.nav.TrafficReply(), "cmd.traffic.clear"));
    // Crawling at 3 m/s in a 29 m/s zone: a jam.
    d.Run(120, 3, Route(std::nullopt), 29);
    CHECK(FromPool(d.nav.TrafficReply(), "cmd.traffic.jam"));
  }
}
