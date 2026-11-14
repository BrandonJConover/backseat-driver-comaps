// Turns that went by while the GPS was out (a tunnel, a parking garage) weren't seen, so they
// neither extend nor break the "N in a row" streak and get no verdict (loop B49, idea BI-045).
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/navigator.hpp"
#include "backseat/running_bits.hpp"

using namespace backseat;
using namespace backseat_tests;

namespace
{
RouteSnapshot Leg(uint32_t turnId, double dist)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = turnId;
  r.turn = turnId % 2 ? TurnKind::Left : TurnKind::Right;
  r.distToTurnM = dist;
  r.nextStreet = "Elm Street";
  r.destination = "Grandma's";
  r.etaSeconds = 600;
  r.plannedTurns = {TurnKind::Left, TurnKind::Right, TurnKind::Destination};
  return r;
}

Fix At(double tMs)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.7;
  f.lon = -105;
  f.speedMps = 10;
  f.accuracyM = 5;
  return f;
}

EngineConfig Config()
{
  EngineConfig c;
  c.rng = SeededRng(5);
  return c;
}

/// Fixes every 2 s from `fromMs` to `toMs` on leg `turnId`.
void Drive(Navigator & nav, double fromMs, double toMs, uint32_t turnId)
{
  for (double t = fromMs; t <= toMs; t += 2000)
    nav.OnFix(At(t), Leg(turnId, 400 - (t - fromMs) / 200));
  nav.TakeLines();
}

bool HasVerdict(std::vector<Commentary> const & lines)
{
  for (auto const & l : lines)
    if (l.key.rfind("turnCompleted.", 0) == 0)
      return true;
  return false;
}
}  // namespace

TEST(UnseenTurnNeitherExtendsNorBreaksTheStreak)
{
  RunningBits b;
  b.NewTrip(false, 2);
  TripStats st;
  // Done in the dark: counted by the engine, not by the streak, and no verdict.
  b.TurnUnseen();
  ++st.turnsCompleted;
  CHECK(!b.OnTimeTurn(st, {}, 0.0).has_value());
  CHECK_EQUAL(b.Streak(), 2);
  // Blown in the dark: no reset, no "made up for it" owed.
  b.TurnUnseen();
  ++st.missedTurns;
  b.SyncTurns(st, {});
  CHECK_EQUAL(b.Streak(), 2);
  // The next turn it sees is the third in a row.
  ++st.turnsCompleted;
  auto const v = b.OnTimeTurn(st, {}, 0.7);
  CHECK(v.has_value());
  CHECK_EQUAL(v->key, std::string("turnCompleted.streak3"));
  CHECK_EQUAL(b.Streak(), 3);
}

TEST(UnseenTurnIsSpentOnTheNextCountOnly)
{
  RunningBits b;
  b.NewTrip(false);
  TripStats st;
  b.TurnUnseen();
  st.turnsCompleted = 3;  // three caught up at once: one of them unseen
  b.SyncTurns(st, {});
  CHECK_EQUAL(b.Streak(), 2);
  ++st.turnsCompleted;
  b.SyncTurns(st, {});
  CHECK_EQUAL(b.Streak(), 3);
}

TEST(NavigatorSkipsTurnsPassedDuringAGpsOutage)
{
  Navigator nav(Config());
  Drive(nav, 0, 60000, 1);
  Drive(nav, 62000, 90000, 2);  // a turn it saw
  CHECK_EQUAL(nav.Engine()->Stats().turnsCompleted, 1);
  CHECK_EQUAL(nav.TurnStreak(), 1);
  // GPS out for two minutes; the first fix back is two turns on.
  nav.OnFix(At(210000), Leg(4, 300));
  auto const back = nav.TakeLines();
  CHECK_EQUAL(nav.Engine()->Stats().turnsCompleted, 2);
  CHECK_EQUAL(nav.TurnStreak(), 1);
  CHECK(!HasVerdict(back));
  // Seen again: counting picks up where it left off.
  Drive(nav, 212000, 240000, 4);
  Drive(nav, 242000, 270000, 5);
  CHECK_EQUAL(nav.TurnStreak(), 2);
}

TEST(NavigatorStillCountsTurnsAfterAShortGap)
{
  Navigator nav(Config());
  Drive(nav, 0, 60000, 1);
  // A 20 s hiccup between fixes is a short leg, not an outage.
  nav.OnFix(At(80000), Leg(2, 300));
  nav.TakeLines();
  CHECK_EQUAL(nav.TurnStreak(), 1);
}

TEST(NavigatorDoesNotBlameAMissNoticedAfterAnOutage)
{
  Navigator nav(Config());
  Drive(nav, 0, 60000, 1);
  Drive(nav, 62000, 90000, 2);
  CHECK_EQUAL(nav.TurnStreak(), 1);
  nav.OnFix(At(92000), Leg(2, 20));
  nav.TakeLines();
  // Out for a minute right at the turn; back off the route.
  RouteSnapshot off = Leg(2, 20);
  off.state = RouteState::OffRoute;
  nav.OnFix(At(152000), off);
  nav.OnFix(At(155500), off);  // still off once the miss is sure
  nav.TakeLines();
  CHECK_EQUAL(nav.Engine()->Stats().missedTurns, 1);
  CHECK_EQUAL(nav.TurnStreak(), 1);
}
