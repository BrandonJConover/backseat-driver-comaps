// Jam lines blame the driver's route ("I told you to take I-25") only when the driver went their own
// way first: a missed turn, an off-route reroute, or a slower route they drew. A driver who followed
// the directions into a jam gets traffic.start.onRoute / traffic.onRoute, which blame the traffic.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/lines.hpp"
#include "backseat/units.hpp"

using namespace backseat;

namespace
{
double constexpr kJamBlameMin = 60000;

CritiqueEngine JamBlameEngine(bool gate = true)
{
  EngineConfig c;
  c.rng = SeededRng(7);
  c.trafficBlameNeedsDetour = gate;
  return CritiqueEngine(c, 0);
}

/// The key of the line said when a jam is called at `t`.
std::string JamStartKey(CritiqueEngine & e, double t)
{
  for (auto const & c : e.OnNav(NavEvent::Traffic(t, true, std::string("I-25"))))
    if (c.category == Category::Traffic)
      return c.key;
  return "";
}

/// Sit still in the jam for a few minutes; the keys of the jam lines said.
std::vector<std::string> SitInJam(CritiqueEngine & e, double from)
{
  std::vector<std::string> keys;
  for (double t = from; t < from + 4 * kJamBlameMin; t += 1000)
  {
    TelemetrySample s;
    s.t = t;
    s.speedMps = 0;
    s.speedLimitMps = FromDisplay(65, Units::Mph);
    for (auto const & c : e.OnTelemetry(s))
      if (c.category == Category::Traffic)
        keys.push_back(c.key);
  }
  return keys;
}
}  // namespace

TEST(JamOnTheGivenRouteDoesNotBlameTheDriver)
{
  auto e = JamBlameEngine();
  CHECK_EQUAL(JamStartKey(e, 10 * kJamBlameMin), std::string("traffic.start.onRoute"));
  CHECK(!e.JamBlamesRoute());
  auto const keys = SitInJam(e, 10 * kJamBlameMin + 1000);
  CHECK(!keys.empty());
  for (auto const & k : keys)
    CHECK_EQUAL(k, std::string("traffic.onRoute"));
}

TEST(JamAfterAMissedTurnBlamesTheRoute)
{
  auto e = JamBlameEngine();
  e.OnNav(NavEvent::MissedTurn(2 * kJamBlameMin));
  CHECK_EQUAL(JamStartKey(e, 10 * kJamBlameMin), std::string("traffic.start"));
  auto const keys = SitInJam(e, 10 * kJamBlameMin + 1000);
  CHECK(!keys.empty());
  for (auto const & k : keys)
    CHECK_EQUAL(k, std::string("traffic"));
}

TEST(JamAfterAQuietRerouteBlamesTheRoute)
{
  // Back on a new route before the reroute joke was due: the Navigator still notes the detour.
  auto e = JamBlameEngine();
  e.NoteDetour(5 * kJamBlameMin);
  e.OnNav(NavEvent::Rerouted(5 * kJamBlameMin, true));
  CHECK_EQUAL(JamStartKey(e, 10 * kJamBlameMin), std::string("traffic.start"));
}

TEST(SilentRerouteAloneIsNotADetour)
{
  // A reroute the driver asked for ("find gas") or one the router made around a jam isn't going
  // against the directions.
  auto e = JamBlameEngine();
  e.OnNav(NavEvent::Rerouted(5 * kJamBlameMin, true));
  CHECK_EQUAL(JamStartKey(e, 10 * kJamBlameMin), std::string("traffic.start.onRoute"));
}

TEST(OldDetourDoesNotBlameALaterJam)
{
  // Missed a turn early, got back on track, and hit a jam much later on the route as given.
  auto e = JamBlameEngine();
  e.OnNav(NavEvent::MissedTurn(kJamBlameMin));
  double const later = kJamBlameMin + CritiqueEngine::kDetourBlamesJamMs + kJamBlameMin;
  CHECK_EQUAL(JamStartKey(e, later), std::string("traffic.start.onRoute"));
}

TEST(DrawnSlowerRouteBlamesEveryJam)
{
  auto e = JamBlameEngine();
  e.NoteDetour(0, true /* wholeTrip */);
  CHECK_EQUAL(JamStartKey(e, 90 * kJamBlameMin), std::string("traffic.start"));
}

TEST(LeavingTheRouteMidJamTurnsTheBlameOn)
{
  auto e = JamBlameEngine();
  CHECK_EQUAL(JamStartKey(e, 10 * kJamBlameMin), std::string("traffic.start.onRoute"));
  e.OnNav(NavEvent::MissedTurn(11 * kJamBlameMin));
  CHECK(e.JamBlamesRoute());
}

TEST(GateOffKeepsTheOldBlame)
{
  // The TS engine (parity trace) always blames the route.
  auto e = JamBlameEngine(false);
  CHECK_EQUAL(JamStartKey(e, 10 * kJamBlameMin), std::string("traffic.start"));
}

TEST(OnRouteJamLinesNeverBlameTheRoute)
{
  SetLineTravelMode(TravelMode::Drive);
  for (char const * key : {"traffic.start.onRoute", "traffic.onRoute"})
    for (int tier = 0; tier < 3; ++tier)
    {
      CHECK(LinesFor(key, tier).size() >= 4);
      for (auto const & l : LinesFor(key, tier))
        CHECK(!LineBlamesRoute(l));
    }
}

TEST(StandstillBlameFilterKeepsBrandonsLines)
{
  // The two "I told you so" standstill lines are held back after an on-route jam; Brandon's verbatim
  // standstill lines are not.
  CHECK(LineBlamesRoute("Je te l'avais dit means 'I told you so' in French. I'm not saying I told you to take "
                        "the other road. I'm just saying it in French."));
  CHECK(LineBlamesRoute("HAB ICH DIR DOCH GESAGT means 'I TOLD YOU SO' in German! WE'RE NOT MOVING! I HAVE TIME FOR "
                        "ALL OF THEM!"));
  for (char const * l :
       {"What does their bumper sticker say? You can't read it? Well back up a bit and give them some room.",
        "It seems we have stopped for a while. You might want to get out and stretch your legs. Get some fresh air.",
        "You should ask the person behind you if they have seen any good movies recently. I bet they would really "
        "like that. Actually, don't do that. They should probably come to you instead."})
    CHECK(!LineBlamesRoute(l));
}
