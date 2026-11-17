// Personal asks wait for a calm moment (loop B174, BI-087): no "for your file" question, game offer or
// "rate my directions" at highway speed with the exit under a minute away, or in rain, snow, ice or fog.
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/navigator.hpp"

#include <algorithm>
#include <cmath>

using namespace backseat;

namespace
{
double constexpr kAskStartMs = 1793156400000.0 + 16 * 3600000.0;  // 1 pm in Denver
double constexpr kAskLat = 39.74;

RouteSnapshot AskRoute(double dist)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Right;
  r.nextStreet = "Elm Street";
  r.distToTurnM = dist;
  r.destination = "Grandma's";
  r.etaSeconds = 3600;
  r.plannedTurns = {TurnKind::Right, TurnKind::Destination};
  return r;
}

/// Everything before the trap in the quiet-bit order was said already, so the trap is next.
Navigator TrapNextNavigator()
{
  std::string text;
  for (auto const & k : {"call.script", "ispy.day", "fact.frame", "twenty.offer", "quiet.aside"})
    text += std::string(k) + "\tsomething\n";
  EngineConfig c;
  c.rng = SeededRng(5);
  Navigator nav(c);
  nav.SetDriverName("Brandon");
  nav.SetSaidLog(SaidLog::Parse(text), {});
  nav.SetLocalTime([](double) {
    std::tm tm{};
    tm.tm_year = 126;
    tm.tm_mon = 9;
    tm.tm_mday = 28;
    tm.tm_hour = 13;
    return tm;
  });
  return nav;
}

/// `seconds` of driving at `mps`; `holdDist` keeps the maneuver the same distance ahead the whole time.
std::vector<Commentary> Drive(Navigator & nav, double & t, int seconds, double mps, double & dist, bool holdDist = false)
{
  std::vector<Commentary> said;
  for (int i = 0; i < seconds; ++i, t += 1000)
  {
    if (!holdDist)
      dist -= mps;
    Fix f;
    f.tMs = t;
    f.lat = kAskLat;
    f.lon = -104.99 + (t - kAskStartMs) / 1000.0 * mps / 85000.0;
    f.speedMps = mps;
    f.bearingDeg = 90;
    nav.OnFix(f, AskRoute(dist));
    auto lines = nav.TakeLines();
    said.insert(said.end(), lines.begin(), lines.end());
  }
  return said;
}

bool Asked(std::vector<Commentary> const & lines)
{
  return std::any_of(lines.begin(), lines.end(), [](Commentary const & c) { return InvitesAnswer(c.key); });
}

Weather Rain()
{
  Weather w;
  w.rain = true;
  return w;
}

Weather Clear()
{
  Weather w;
  w.clear = true;
  return w;
}
}  // namespace

TEST(AskWaitsAtHighwaySpeedNearAnExitOrInBadWeather)
{
  CHECK(AskMustWait(29, 45, false));    // 65 mph, exit in 45 s
  CHECK(AskMustWait(22, 59, false));
  CHECK(!AskMustWait(29, 61, false));   // a minute plus to go
  CHECK(!AskMustWait(29, 1e9, false));  // no maneuver
  CHECK(!AskMustWait(13, 30, false));   // 30 mph in town: the existing turn holds are enough
  CHECK(AskMustWait(13, 600, true));    // rain, snow, ice or fog
  CHECK(AskMustWait(0, 1e9, true));
}

TEST(AnswerInvitingKeysAreInTheBankAndCoverEveryAsk)
{
  for (auto const & key : AnswerInvitingKeys())
  {
    if (LinesFor(key, 0).empty())
      std::cerr << "  no lines for " << key << "\n";
    CHECK(!LinesFor(key, 0).empty());
  }
  // Audit: a file question, a ".ask" or a mileage ask added to the bank later has to join the list
  // (or be a parked/in-game ask or a reply that keeps its own timing).
  auto const starts = [](std::string const & s, char const * p) { return s.rfind(p, 0) == 0; };
  for (auto const & [key, tiers] : LineData())
  {
    bool const fileAsk = starts(key, "file.") &&
                         std::any_of(tiers[0].begin(), tiers[0].end(), [](std::string const & l) { return l.find('?') != std::string::npos; });
    bool const ask = (key.size() > 4 && key.compare(key.size() - 4, 4, ".ask") == 0) || starts(key, "mileage.ask") ||
                     key == "mileage.unknownAsk";
    // Replies to an answer ("No answer? I'll make something up.") are said when the driver answers.
    bool const ownTiming = starts(key, "setup.") || starts(key, "mode.ask") || key == "quiz.ask" ||
                           key == "file.declined" || key == "file.saved" || starts(key, "file.snackLie") ||
                           starts(key, "file.snackSame");
    if ((fileAsk || ask) && !ownTiming && !InvitesAnswer(key))
      std::cerr << "  not in AnswerInvitingKeys: " << key << "\n";
    CHECK(!(fileAsk || ask) || ownTiming || InvitesAnswer(key));
  }
  CHECK(InvitesAnswer("file.color"));
  CHECK(!InvitesAnswer("turn.right"));
  CHECK(!InvitesAnswer("quiz.ask"));  // mid-game: the quiz has its own holds
}

TEST(HighwayExitWithinAMinuteHoldsTheTrapUntilAfter)
{
  Navigator nav = TrapNextNavigator();
  double t = kAskStartMs;
  // 65 mph (29 m/s) with the exit 50 s away the whole time: over the 45 s quiet-bit floor, under a minute.
  double dist = 1450;
  auto const near = Drive(nav, t, 600, 29, dist, true);
  CHECK(!Asked(near));
  CHECK(nav.CalmForAsk() == false);
  // Past it, on a long stretch: the same ask comes.
  dist = 60000;
  auto const later = Drive(nav, t, 600, 29, dist);
  CHECK(Asked(later));
  CHECK(nav.CalmForAsk());
}

TEST(CitySpeedSameSecondsToTurnStillAsks)
{
  Navigator nav = TrapNextNavigator();
  double t = kAskStartMs;
  // 25 mph, turn 50 s away: unchanged from before B174.
  double dist = 560;
  auto const lines = Drive(nav, t, 600, 11, dist, true);
  CHECK(Asked(lines));
}

TEST(RainHoldsTheTrapUntilItClears)
{
  Navigator nav = TrapNextNavigator();
  nav.OnWeather(kAskStartMs, Rain());
  double t = kAskStartMs;
  double dist = 60000;
  auto const wet = Drive(nav, t, 600, 25, dist);
  CHECK(!Asked(wet));
  CHECK(!nav.CalmForAsk());
  nav.OnWeather(t, Clear());
  auto const dry = Drive(nav, t, 600, 25, dist);
  CHECK(Asked(dry));
}

TEST(NoTripIsAlwaysCalm)
{
  EngineConfig c;
  Navigator nav(c);
  nav.OnWeather(kAskStartMs, Rain());
  CHECK(nav.CalmForAsk());
}
