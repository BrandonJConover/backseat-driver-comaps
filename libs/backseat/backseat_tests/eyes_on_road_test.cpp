// Replies to map and settings commands don't say look at the screen or pick something on it while the
// car is moving (loop B92, idea BI-079); parked, the whole pool still comes up.
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"

#include <cstdio>
#include <set>
#include <string>

using namespace backseat;

namespace
{
char const * const kKeys[] = {"cmd.overview", "cmd.zoomIn", "cmd.zoomOut", "cmd.voice.pick", "cmd.settings",
                              "cmd.recenter"};

RouteSnapshot Leg()
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Left;
  r.distToTurnM = 4000;
  r.destination = "Work";
  r.etaSeconds = 600;
  r.plannedTurns = {TurnKind::Left, TurnKind::Destination};
  return r;
}

Fix At(double tMs, double mps)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.7;
  f.lon = -105;
  f.speedMps = mps;
  f.accuracyM = 5;
  return f;
}
}  // namespace

TEST(LooksOrPicksDetector)
{
  CHECK(LooksOrPicks("THE WHOLE ROUTE. Look at it. LOOK AT IT."));
  CHECK(LooksOrPicks("ZOOMED IN! LOOK! THAT'S YOU! BEING WRONG!"));
  CHECK(LooksOrPicks("ZOOMED IN. Look closely. That's where you went wrong."));
  CHECK(LooksOrPicks("Overview. Look how straightforward I made it. You're welcome."));
  CHECK(LooksOrPicks("Here are my voices. Pick one. They all think the same things about you."));
  CHECK(LooksOrPicks("Pick a voice. Any voice. It's still me in there."));
  CHECK(LooksOrPicks("Opening settings. Change anything you like. Except me. I'm not adjustable."));
  CHECK(LooksOrPicks("Settings. Feel free to look for a 'nicer GPS' option. There isn't one."));
  CHECK(LooksOrPicks("Zooming out. Look at all the roads you haven't gotten lost on yet."));
  CHECK(LooksOrPicks("VOICES! CHOOSE! THEY ALL THINK THE SAME THING ABOUT YOU!"));
  // Negatives.
  CHECK(!LooksOrPicks("Don't look at the screen."));
  CHECK(!LooksOrPicks("Look, I tried."));
  CHECK(!LooksOrPicks("Never look at the map while driving."));
  CHECK(!LooksOrPicks("Zooming in. Don't stare at the map. Stare at the road."));
  CHECK(!LooksOrPicks("I found 3. A, B, C. Which one? Pick one. Say first, second, or third."));
  CHECK(!LooksOrPicks("The whole route. Pick a turn to miss. Just one, please."));
  CHECK(!LooksOrPicks("Looking for a way to make me nicer? Good luck."));
  CHECK(!LooksOrPicks("Overlook it. I won't."));
  CHECK(!LooksOrPicks(""));
}

TEST(EyesOnRoadLinesEveryTier)
{
  // Each command keeps at least three lines a moving driver can hear, in every tier.
  bool thin = false;
  for (char const * key : kKeys)
    for (int tier = 0; tier < 3; ++tier)
    {
      int ok = 0;
      for (auto const & l : LinesFor(key, tier))
        ok += LooksOrPicks(l) ? 0 : 1;
      if (ok < 3)
        std::printf("  %s tier %d: only %d eyes-up lines\n", key, tier, ok);
      thin = thin || ok < 3;
    }
  CHECK(!thin);
}

TEST(RepliesAtSpeedNeverSayLook)
{
  for (char const * key : kKeys)
  {
    EngineConfig c;
    c.rng = SeededRng(5);
    Navigator nav(c);
    nav.OnFix(At(0, 20), Leg());
    nav.TakeLines();
    for (int i = 0; i < 200; ++i)
    {
      auto const reply = nav.Reply(key);
      CHECK(!reply.empty());
      if (LooksOrPicks(reply))
        std::printf("  %s at speed: %s\n", key, reply.c_str());
      CHECK(!LooksOrPicks(reply));
    }
  }
}

TEST(RepliesParkedUseWholePool)
{
  int flagged = 0;
  for (char const * key : kKeys)
  {
    EngineConfig c;
    c.rng = SeededRng(9);
    Navigator nav(c);
    nav.OnFix(At(0, 0), Leg());
    nav.TakeLines();
    std::set<std::string> said;
    for (int i = 0; i < 200; ++i)
    {
      auto const reply = nav.Reply(key);
      said.insert(reply);
      flagged += LooksOrPicks(reply) ? 1 : 0;
    }
    CHECK(said.size() >= 3);
  }
  CHECK(flagged > 0);
  // No fix yet (speed unknown): the whole pool too.
  EngineConfig c;
  c.rng = SeededRng(2);
  Navigator idle(c);
  CHECK(!idle.Reply("cmd.overview").empty());
}
