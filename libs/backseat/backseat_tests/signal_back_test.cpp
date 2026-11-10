// GPS dropouts (loop B116, BI-098): after a tunnel, a garage ramp or a downtown canyon at road speed, the first
// fix with a speed again gets one "gps.back" line, from the persona doc's Tunnel bullets. Never at a crawl,
// never for a short blip, never near a maneuver, in a game, after arrival or more than once per 10 minutes.
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/navigator.hpp"
#include "backseat/pronunciation.hpp"

#include <cctype>
#include <ctime>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
double constexpr kT0 = 1.7e12;

EngineConfig SbSeeded()
{
  EngineConfig c;
  c.rng = SeededRng(7);
  return c;
}

enum class Gap
{
  NoFixes,
  UnknownSpeed
};

struct SbDrive
{
  Navigator nav{SbSeeded()};
  double t = kT0;
  /// Distance to the next maneuver (a far-off exit by default).
  double toTurnM = 60000;
  RouteState state = RouteState::OnRoute;
  std::vector<Commentary> said;

  RouteSnapshot Route() const
  {
    RouteSnapshot r;
    r.state = state;
    r.turnId = 1;
    r.turn = TurnKind::ExitRight;
    r.nextStreet = "Highway 412";
    r.distToTurnM = toTurnM;
    r.destination = "Home";
    r.etaSeconds = 3600;
    r.plannedTurns = {TurnKind::ExitRight, TurnKind::Destination};
    r.origin = {36.1, -94.1};
    r.destinationPoint = {36.6, -94.1};
    return r;
  }

  void Fixes(double seconds, double speedMps, bool known = true)
  {
    for (double end = t + seconds * 1000; t < end; t += 1000)
    {
      Fix f;
      f.tMs = t;
      f.lat = 36.1 + (t - kT0) / 1e8;
      f.lon = -94.1;
      f.speedMps = known ? speedMps : -1;
      f.bearingDeg = 0;
      f.speedLimitMps = 31;
      nav.OnFix(f, Route());
      toTurnM -= speedMps;
      Take();
    }
  }

  /// The GPS goes quiet for `seconds` while the car keeps going at `speedMps`; the app still polls.
  void Dropout(double seconds, double speedMps, Gap gap)
  {
    if (gap == Gap::UnknownSpeed)
    {
      Fixes(seconds, speedMps, false);
      return;
    }
    for (double end = t + seconds * 1000; t < end; t += 1000)
    {
      toTurnM -= speedMps;
      nav.Poll(Route());
      Take();
    }
  }

  void Take()
  {
    for (auto & l : nav.TakeLines())
      said.push_back(std::move(l));
  }

  int Count(double fromT = 0) const
  {
    int n = 0;
    for (auto const & l : said)
      n += l.key == "gps.back" && l.t >= fromT;
    return n;
  }
};

/// Two minutes at highway speed, a dropout, then 40 s more; how many "gps.back" lines came after the dropout.
int AfterDropout(double gapS, double speedMps, Gap gap, double toTurnM = 60000)
{
  SbDrive d;
  d.Fixes(120, speedMps);
  d.toTurnM = toTurnM;
  double const gapEnd = d.t + gapS * 1000;
  d.Dropout(gapS, speedMps, gap);
  int const before = d.Count();
  d.Fixes(40, speedMps);
  CHECK_EQUAL(before, 0);
  return d.Count(gapEnd);
}
}  // namespace

TEST(SignalBackAfterAHighwayDropout)
{
  CHECK_EQUAL(AfterDropout(12, 30, Gap::NoFixes), 1);
  CHECK_EQUAL(AfterDropout(12, 30, Gap::UnknownSpeed), 1);
  // A long tunnel too.
  CHECK_EQUAL(AfterDropout(90, 25, Gap::NoFixes), 1);
}

TEST(SignalBackSaidOnTheFirstFixBack)
{
  SbDrive d;
  d.Fixes(120, 30);
  d.Dropout(12, 30, Gap::NoFixes);
  double const back = d.t;
  d.Fixes(1, 30);
  CHECK_EQUAL(d.Count(), 1);
  for (auto const & l : d.said)
    if (l.key == "gps.back")
    {
      CHECK(l.t == back);
      CHECK(l.priority != Priority::Nav);
    }
}

TEST(SignalBackNotForAShortBlipOrACrawl)
{
  // Five seconds is a bridge, not lost satellites.
  CHECK_EQUAL(AfterDropout(5, 30, Gap::NoFixes), 0);
  CHECK_EQUAL(AfterDropout(5, 30, Gap::UnknownSpeed), 0);
  // Creeping down a garage ramp.
  CHECK_EQUAL(AfterDropout(12, 5, Gap::NoFixes), 0);
  CHECK_EQUAL(AfterDropout(12, 5, Gap::UnknownSpeed), 0);
  // Hours later is a phone left off, not a tunnel.
  CHECK_EQUAL(AfterDropout(15 * 60, 30, Gap::NoFixes), 0);
}

TEST(SignalBackNeverNearAManeuver)
{
  // Within 400 m of the exit when the GPS comes back.
  CHECK_EQUAL(AfterDropout(12, 15, Gap::NoFixes, 300 + 12 * 15), 0);
  // 800 m out at 30 m/s is under 30 s of driving.
  CHECK_EQUAL(AfterDropout(12, 30, Gap::NoFixes, 800 + 12 * 30), 0);
  // Well clear of it: said.
  CHECK_EQUAL(AfterDropout(12, 30, Gap::NoFixes, 3000 + 12 * 30), 1);
}

TEST(SignalBackAtMostOncePerTenMinutes)
{
  SbDrive d;
  d.Fixes(120, 30);
  d.Dropout(12, 30, Gap::NoFixes);
  d.Fixes(180, 30);
  d.Dropout(12, 30, Gap::UnknownSpeed);
  d.Fixes(40, 30);
  CHECK_EQUAL(d.Count(), 1);
  // Past ten minutes since the last one, the next tunnel gets its line.
  d.Fixes(420, 30);
  double const third = d.t;
  d.Dropout(12, 30, Gap::NoFixes);
  d.Fixes(40, 30);
  CHECK_EQUAL(d.Count(third), 1);
  CHECK_EQUAL(d.Count(), 2);
}

TEST(SignalBackNotInAGameOrAfterArrivalOrDirectionsOnly)
{
  {
    // A pop quiz question waiting for its answer.
    SbDrive d;
    d.nav.SetPopQuizOn(true);
    d.nav.SetLocalTime([](double) {
      std::tm tm{};
      tm.tm_year = 126;
      tm.tm_mon = 9;
      tm.tm_mday = 28;
      tm.tm_hour = 13;
      return tm;
    });
    for (int i = 0; i < 1800 && !d.nav.InGame(); ++i)
      d.Fixes(1, 25);
    CHECK(d.nav.InGame());
    d.Dropout(12, 30, Gap::NoFixes);
    d.Fixes(40, 30);
    CHECK_EQUAL(d.Count(), 0);
  }
  {
    SbDrive d;
    d.Fixes(120, 30);
    d.state = RouteState::Finished;
    d.Fixes(5, 30);
    d.Dropout(12, 30, Gap::NoFixes);
    d.Fixes(40, 30);
    CHECK_EQUAL(d.Count(), 0);
  }
  {
    SbDrive d;
    d.nav.SetTalkLevel(TalkLevel::DirectionsOnly);
    d.Fixes(120, 30);
    d.Dropout(12, 30, Gap::NoFixes);
    d.Fixes(40, 30);
    CHECK_EQUAL(d.Count(), 0);
  }
}

TEST(SignalBackLinesAreSafeAndRead)
{
  auto const found = LineData().find("gps.back");
  CHECK(found != LineData().end());
  if (found == LineData().end())
    return;
  // Brandon's tunnel line, made true for after the dropout, leads tier 0.
  CHECK_EQUAL(found->second[0].front(),
              std::string("I'm back. I lost the satellites. If I hadn't come back, I wanted you to tell the other "
                          "apps I loved them. Except the calculator."));
  for (int tier = 0; tier < 3; ++tier)
  {
    CHECK(!found->second[tier].empty());
    for (auto const & line : found->second[tier])
    {
      CHECK(!AsksToHandlePhone(line));
      CHECK(!LooksOrPicks(line));
      std::string lower = line;
      for (auto & c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      CHECK(lower.find("turn") == std::string::npos && lower.find("exit") == std::string::npos);
      CHECK(lower.find("screen") == std::string::npos && lower.find("look at") == std::string::npos);
      CHECK(lower.find('{') == std::string::npos);
      CHECK(!pronunciation::ForSpeech(line).empty());
    }
  }
}
