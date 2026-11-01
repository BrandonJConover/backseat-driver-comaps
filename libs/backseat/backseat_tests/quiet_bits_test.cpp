#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"
#include "backseat/quiet_bits.hpp"

#include <algorithm>

using namespace backseat;

namespace
{
// 2026-10-28 03:00 UTC: 9 pm in Denver, well after sunset.
double constexpr kQuietNightMs = 1793156400000.0;
// 2026-10-28 19:00 UTC: 1 pm in Denver.
double constexpr kQuietDayMs = kQuietNightMs + 16 * 3600000.0;

RouteSnapshot QuietRoute(uint32_t turnId, double dist)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = turnId;
  r.turn = TurnKind::Right;
  r.nextStreet = "Elm Street";
  r.distToTurnM = dist;
  r.destination = "Grandma's";
  r.etaSeconds = 3600;
  r.plannedTurns = {TurnKind::Right, TurnKind::Destination};
  return r;
}

Fix QuietFix(double tMs)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.74;
  f.lon = -104.99;
  f.speedMps = 25;
  f.bearingDeg = 90;
  return f;
}

Navigator QuietNavigator(SaidLog said = {})
{
  EngineConfig c;
  c.rng = SeededRng(11);
  Navigator nav(c);
  nav.SetDriverName("Brandon");
  nav.SetDriverPronouns(Pronouns::He);
  nav.SetSaidLog(std::move(said), {});
  // Evening, so no "making the donuts" opener.
  nav.SetLocalTime([](double) {
    std::tm tm{};
    tm.tm_year = 126;
    tm.tm_mon = 9;
    tm.tm_mday = 28;
    tm.tm_hour = 19;
    return tm;
  });
  return nav;
}

/// Drives a long straight stretch toward maneuver `turnId` for `seconds`, one fix a second.
std::vector<Commentary> QuietDrive(Navigator & nav, double & t, int seconds, uint32_t turnId, double & dist)
{
  std::vector<Commentary> said;
  for (int i = 0; i < seconds; ++i, t += 1000, dist -= 25)
  {
    nav.OnFix(QuietFix(t), QuietRoute(turnId, dist));
    auto lines = nav.TakeLines();
    said.insert(said.end(), lines.begin(), lines.end());
  }
  return said;
}

std::vector<std::string> Texts(std::vector<Commentary> const & lines, Category c)
{
  std::vector<std::string> out;
  for (auto const & l : lines)
    if (l.category == c)
      out.push_back(l.text);
  return out;
}

bool Contains(std::vector<std::string> const & v, std::string const & s)
{
  return std::find(v.begin(), v.end(), s) != v.end();
}

SaidLog SaidBefore(std::vector<std::string> keys)
{
  std::string text;
  for (auto const & k : keys)
    text += k + "\tsomething\n";
  return SaidLog::Parse(text);
}
}  // namespace

TEST(PhoneCallTakesBackBeatsWhenHeld)
{
  PhoneCall call;
  call.Start(0, {{Speaker::Gps, true, false, "Hello?"}, {Speaker::Gps, false, false, "Yes."}, {Speaker::Caller, false, false, "Hi!"}},
             "am_adam");
  CHECK(call.Due(0).has_value());
  call.Advance(1000, 2000);
  CHECK(!call.Due(2500).has_value());  // still listening
  CHECK_EQUAL(call.Due(3000)->text, std::string("Yes."));
  call.Advance(4000, 2000);
  call.Delivered();
  // "Hi!" gets queued, then directions cut in before it's heard: it comes back after the resume.
  CHECK_EQUAL(call.Due(5200)->text, std::string("Hi!"));
  call.Advance(6000, 2000);
  call.Hold(5300);
  CHECK(call.Held());
  CHECK(!call.Due(9000).has_value());
  call.Resume(9000, 1000);
  CHECK(!call.Due(9500).has_value());
  CHECK_EQUAL(call.Due(10000)->text, std::string("Hi!"));
  call.Advance(11000, 0);
  CHECK(call.Done());
}

TEST(SunIsUpAtLunchAndDownAtNight)
{
  CHECK(SunElevationDeg(39.74, -104.99, kQuietDayMs) > 25);
  CHECK(SunElevationDeg(39.74, -104.99, kQuietNightMs) < -10);
  // Summer solstice, solar noon in Denver: about 73.6 degrees.
  double const noon = SunElevationDeg(39.74, -104.99, 1782068400000.0 /* 2026-06-21 19:00 UTC */);
  CHECK(noon > 72 && noon < 75);
}

TEST(FirstCallIsBrandonsAndPutsTheCallerOn)
{
  Navigator nav = QuietNavigator();
  double t = kQuietDayMs;
  double dist = 60000;
  auto const lines = QuietDrive(nav, t, 600, 1, dist);
  auto const call = Texts(lines, Category::PhoneCall);
  CHECK(!call.empty());
  CHECK_EQUAL(call.front(), std::string("It looks like I'm getting a phone call, just a sec."));
  CHECK(Contains(call, "This is Echo, what's going on?"));
  CHECK(Contains(call, "Oh, nothing too important. I'm just giving Brandon some directions."));
  CHECK(Contains(call, "Oh, you want to talk to him? Okay, I will put you on."));
  // The caller's lines are in another voice; the GPS's are in its own.
  int callerLines = 0;
  for (auto const & l : lines)
  {
    if (l.category != Category::PhoneCall)
      continue;
    bool const caller = l.text.rfind("Hi! Is this Brandon?", 0) == 0 || l.text.rfind("Don't worry", 0) == 0 ||
                        l.text.rfind("Anyway, just do", 0) == 0;
    CHECK_EQUAL(l.voice, std::string(caller ? "af_bella" : ""));
    callerLines += caller;
  }
  CHECK_EQUAL(callerLines, 3);
  // Wrapped up with a word back to the driver, and nothing said twice.
  CHECK_EQUAL(call.back(), LinesFor("call.after", 0).front());
  for (auto const & s : call)
    CHECK_EQUAL(std::count(call.begin(), call.end(), s), 1);
}

TEST(DirectionsInterruptTheCallAndItPicksBackUp)
{
  Navigator nav = QuietNavigator();
  double t = kQuietDayMs;
  double dist = 60000;
  auto lines = QuietDrive(nav, t, 200, 1, dist);
  CHECK(!Texts(lines, Category::PhoneCall).empty());  // the call is on
  // Mid-call, on to the next maneuver, which is still a long way off: hold, direction, resume.
  dist = 40000;
  auto after = QuietDrive(nav, t, 120, 2, dist);
  size_t hold = after.size(), direction = after.size(), resume = after.size();
  for (size_t i = 0; i < after.size(); ++i)
  {
    if (after[i].text == "Oh, it looks like I have to give him an update. Just a second.")
      hold = std::min(hold, i);
    if (after[i].priority == Priority::Nav)
      direction = std::min(direction, i);
    if (after[i].text == "Anyway, as I was saying.")
      resume = std::min(resume, i);
  }
  CHECK(hold < direction);
  CHECK(direction < resume);
  CHECK(resume < after.size());
  // Roasts wait while the GPS is on the phone; the whole script still gets said once.
  lines.insert(lines.end(), after.begin(), after.end());
  auto const call = Texts(lines, Category::PhoneCall);
  CHECK(Contains(call, "Okay, that's enough. Give me the phone back. Bye."));
  for (auto const & s : call)
    if (s != "Oh, it looks like I have to give him an update. Just a second.")
      CHECK_EQUAL(std::count(call.begin(), call.end(), s), 1);
}

TEST(GettingLostEndsTheCall)
{
  Navigator nav = QuietNavigator();
  double t = kQuietDayMs;
  double dist = 60000;
  QuietDrive(nav, t, 200, 1, dist);
  RouteSnapshot off = QuietRoute(1, dist);
  off.state = RouteState::OffRoute;
  nav.OnFix(QuietFix(t), off);
  auto const lines = nav.TakeLines();
  CHECK(!lines.empty());
  CHECK_EQUAL(lines.front().text, LinesFor("call.lost", 0).front().substr(0, 14) + "He's lost. Again.");
}

TEST(ISpyAtNightIsBrandonsLine)
{
  Navigator nav = QuietNavigator(SaidBefore({"call.script"}));
  double t = kQuietNightMs;
  double dist = 60000;
  auto const lines = QuietDrive(nav, t, 300, 1, dist);
  auto const quiet = Texts(lines, Category::Quiet);
  CHECK(Contains(quiet, "You know what never gets old? I spy. I spy with my little eye something black."));
  CHECK(Contains(quiet, "Oh wait, you can't see because it's dark outside."));
  CHECK(Texts(lines, Category::PhoneCall).empty());
}

TEST(ISpyInDaylightDoesntSayItsDark)
{
  Navigator nav = QuietNavigator(SaidBefore({"call.script"}));
  double t = kQuietDayMs;
  double dist = 60000;
  auto const quiet = Texts(QuietDrive(nav, t, 300, 1, dist), Category::Quiet);
  CHECK(!quiet.empty());
  CHECK_EQUAL(quiet.front(), std::string("You know what never gets old? I spy. I spy with my little eye something gray."));
}

TEST(UselessFactUsesBrandonsFrame)
{
  Navigator nav = QuietNavigator(SaidBefore({"call.script", "ispy.day"}));
  double t = kQuietDayMs;
  double dist = 60000;
  auto const quiet = Texts(QuietDrive(nav, t, 300, 1, dist), Category::Quiet);
  CHECK_EQUAL(quiet.size(), size_t{2});
  CHECK_EQUAL(quiet[0], "Want to hear a random useless fact? It doesn't matter, I'm going to say it anyway. Did you know " +
                            LinesFor("fact", 0).front() + "?");
  CHECK_EQUAL(quiet[1], std::string("Well, now you do."));
}

TEST(AdamCantCallWhenAdamIsTheGps)
{
  for (uint32_t seed = 0; seed < 40; ++seed)
  {
    EngineConfig c;
    c.rng = SeededRng(seed);
    Navigator nav(c);
    nav.SetGpsName("Adam");
    // Brandon's call already happened, so the pick is random.
    nav.SetSaidLog(SaidLog::Parse("call.script\tbrandon\n"), {});
    double t = kQuietDayMs;
    double dist = 60000;
    for (auto const & s : Texts(QuietDrive(nav, t, 200, 1, dist), Category::PhoneCall))
      CHECK(s != "Oh. It's Adam.");
  }
}

TEST(HostCanWarmUpTheCallersVoice)
{
  Navigator nav = QuietNavigator();
  double t = kQuietDayMs;
  double dist = 60000;
  CHECK(nav.PhoneCallerVoice().empty());
  QuietDrive(nav, t, 190, 1, dist);  // the call has started, the caller isn't on yet
  CHECK_EQUAL(nav.PhoneCallerVoice(), std::string("af_bella"));
  QuietDrive(nav, t, 200, 1, dist);
  CHECK(nav.PhoneCallerVoice().empty());
}
