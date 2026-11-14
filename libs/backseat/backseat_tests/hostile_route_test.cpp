// Hostile route snapshots: the host fills RouteSnapshot from the router every fix, and CleanFix only
// guards the GPS half. NaN/inf/negative/huge distances, ETAs, bearings, grades, gaps, exit numbers and
// a NaN destination mark must never be spoken as "nan", "inf" or a ten-digit number, never throw and
// never trip ASan/UBSan (the newer B116-B119 paths read most of these). Loop fallback:security.
#include "check.hpp"

#include "backseat/navigator.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
struct RouteRng
{
  uint64_t s;
  uint32_t Next()
  {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return static_cast<uint32_t>(s >> 11);
  }
  size_t Below(size_t n) { return n == 0 ? 0 : Next() % n; }
};

double constexpr kNan = std::numeric_limits<double>::quiet_NaN();
double constexpr kInf = std::numeric_limits<double>::infinity();
double constexpr kStartMs = 1793156400000.0 + 15 * 3600000.0;

// A plausible value most of the time, junk the rest.
double Junk(RouteRng & r, double good)
{
  switch (r.Below(12))
  {
  case 0: return kNan;
  case 1: return kInf;
  case 2: return -kInf;
  case 3: return -1e12;
  case 4: return 1e15;
  case 5: return -0.0;
  case 6: return std::numeric_limits<double>::denorm_min();
  case 7: return std::numeric_limits<double>::max();
  default: return good;
  }
}

int JunkInt(RouteRng & r, int good)
{
  switch (r.Below(10))
  {
  case 0: return std::numeric_limits<int>::min();
  case 1: return std::numeric_limits<int>::max();
  case 2: return -7;
  case 3: return 99;
  default: return good;
  }
}

TurnKind AnyTurn(RouteRng & r)
{
  TurnKind constexpr kTurns[] = {TurnKind::None,        TurnKind::Left,       TurnKind::Right,
                                 TurnKind::SlightLeft,  TurnKind::SlightRight, TurnKind::SharpLeft,
                                 TurnKind::SharpRight,  TurnKind::UTurn,      TurnKind::Roundabout,
                                 TurnKind::ExitLeft,    TurnKind::ExitRight,  TurnKind::Destination};
  return kTurns[r.Below(std::size(kTurns))];
}

RouteSnapshot HostileSnapshot(RouteRng & r, double distToTurnM, uint32_t turnId)
{
  RouteSnapshot s;
  s.state = r.Below(20) == 0 ? RouteState::OffRoute : RouteState::OnRoute;
  s.turnId = r.Below(15) == 0 ? std::optional<uint32_t>{} : std::optional<uint32_t>{turnId};
  s.turn = r.Below(3) == 0 ? AnyTurn(r) : TurnKind::Destination;
  s.nextStreet = r.Below(4) == 0 ? "" : "Elm Street";
  s.distToTurnM = Junk(r, distToTurnM);
  s.laneSide = r.Below(6) == 0 ? LaneSide::Left : LaneSide::None;
  s.thenTurn = r.Below(4) == 0 ? AnyTurn(r) : TurnKind::None;
  s.thenStreet = "Oak Avenue";
  s.thenGapM = Junk(r, 120);
  s.roundaboutExit = JunkInt(r, 2);
  s.thenRoundaboutExit = JunkInt(r, 0);
  s.etaSeconds = Junk(r, distToTurnM / 20);
  s.routeLengthM = Junk(r, 30000);
  s.destination = "Joe's Diner";
  s.plannedTurns = {TurnKind::Right, TurnKind::Left, TurnKind::Destination};
  s.origin = {Junk(r, 39.70), Junk(r, -105.0)};
  s.destinationPoint = {Junk(r, 39.98), Junk(r, -105.0)};
  if (r.Below(2))
    s.destinationMark = LatLon{Junk(r, 39.98), Junk(r, -104.9998)};
  s.routeBearingDeg = Junk(r, 0);
  s.sketched = r.Below(8) == 0;
  s.fastestSeconds = Junk(r, 0);
  s.curveAheadM = Junk(r, -1);
  if (r.Below(6) == 0)
    s.passedExitNextM = Junk(r, 900);
  s.exitsBeforeTurn = JunkInt(r, -1);
  s.exitBeforeGapM = Junk(r, -1);
  s.onDirt = r.Below(10) == 0;
  s.dirtShare = Junk(r, 0);
  if (r.Below(4) == 0)
    s.gradeAhead = Junk(r, 0.06);
  return s;
}

std::string Lower(std::string s)
{
  for (char & c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool HasWord(std::string const & text, std::string const & word)
{
  for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1))
  {
    bool const startOk = at == 0 || !std::isalpha(static_cast<unsigned char>(text[at - 1]));
    size_t const end = at + word.size();
    bool const endOk = end >= text.size() || !std::isalpha(static_cast<unsigned char>(text[end]));
    if (startOk && endOk)
      return true;
  }
  return false;
}

size_t LongestDigitRun(std::string const & text)
{
  size_t best = 0, run = 0;
  for (char c : text)
  {
    run = std::isdigit(static_cast<unsigned char>(c)) ? run + 1 : 0;
    best = std::max(best, run);
  }
  return best;
}

// A spoken line built from junk numbers: "nan", "inf", "-2147483648 exits" or "1e+15 miles".
bool SaneSpoken(std::string const & raw)
{
  std::string const text = Lower(raw);
  return !HasWord(text, "nan") && !HasWord(text, "inf") && !HasWord(text, "infinity") &&
         text.find("e+") == std::string::npos && LongestDigitRun(text) < 7 &&
         text.find('{') == std::string::npos && text.size() < 600;
}

void CheckLines(Navigator & nav, uint32_t seed, int step)
{
  for (auto const & line : nav.TakeLines())
  {
    if (!SaneSpoken(line.text))
      std::fprintf(stderr, "  seed %u step %d said \"%s\"\n", seed, step, line.text.c_str());
    CHECK(SaneSpoken(line.text));
    CHECK(std::isfinite(line.voiceSpeed));
  }
}
}  // namespace

// The car drives a straight road north toward a destination mark just off to the east; every fix comes
// with a snapshot whose numbers are randomly junk.
TEST(HostileRouteSnapshotsNeverSpeakJunk)
{
  for (uint32_t seed = 1; seed <= 12; ++seed)
  {
    RouteRng r{0xBADF00DULL * seed + 17};
    Navigator nav;
    double lat = 39.70;
    double dist = 30000;
    uint32_t turnId = 1;
    for (int i = 0; i < 400; ++i)
    {
      Fix f;
      f.tMs = kStartMs + i * 1000.0;
      f.lat = lat += 20.0 / 111320;
      f.lon = -105.0;
      f.speedMps = r.Below(10) == 0 ? -1 : 20;
      f.bearingDeg = 0;
      f.accuracyM = 5;
      if (r.Below(5) == 0)
        f.speedLimitMps = Junk(r, 25);
      dist = std::max(0.0, dist - 75);
      if (r.Below(40) == 0)
        ++turnId;
      try
      {
        nav.OnFix(f, HostileSnapshot(r, dist, turnId));
        if (r.Below(4) == 0)
        {
          // A second snapshot for the same fix half a second later (routers update between fixes).
          Fix again = f;
          again.tMs += 500;
          nav.OnFix(again, HostileSnapshot(r, dist, turnId));
        }
      }
      catch (...)
      {
        CHECK(!"the navigator threw on a hostile route snapshot");
      }
      CheckLines(nav, seed, i);
    }
    // And the trip ends cleanly from a junk snapshot.
    RouteSnapshot done = HostileSnapshot(r, 0, turnId);
    done.state = RouteState::Finished;
    Fix last;
    last.tMs = kStartMs + 401000.0;
    last.lat = lat;
    last.lon = -105.0;
    last.speedMps = 0;
    last.accuracyM = 5;
    nav.OnFix(last, done);
    CheckLines(nav, seed, 401);
  }
}

// The arrival side reads the bearing and the mark straight from the host: junk in either says nothing
// about the side, and never "on your nan".
TEST(HostileDestinationMarkSaysNoSide)
{
  for (double const bad : {kNan, kInf, -kInf, 1e15, -1e12})
  {
    for (int which = 0; which < 3; ++which)
    {
      Navigator nav;
      RouteSnapshot s;
      s.state = RouteState::OnRoute;
      s.turnId = 4;
      s.turn = TurnKind::Destination;
      s.destination = "Joe's Diner";
      s.distToTurnM = 300;
      s.routeBearingDeg = which == 0 ? bad : 0;
      s.destinationMark = LatLon{which == 1 ? bad : 39.70 + 300.0 / 111320, which == 2 ? bad : -104.9997};
      for (int i = 0; i < 12; ++i)
      {
        Fix f;
        f.tMs = kStartMs + i * 1000.0;
        f.lat = 39.70 + i * 1.0 / 111320;
        f.lon = -105.0;
        f.speedMps = 1;
        f.bearingDeg = 0;
        f.accuracyM = 5;
        nav.OnFix(f, s);
      }
      for (auto const & line : nav.TakeLines())
      {
        CHECK(SaneSpoken(line.text));
        CHECK(line.text.find("on your") == std::string::npos);
      }
    }
  }
}

// A finite but absurd speed or posted limit is a garbage reading, not a number to round to whole mph
// (DBL_MAX overflowed int in the speeding check). Real fast highway readings are kept.
TEST(CleanFixDropsAbsurdSpeeds)
{
  Fix good;
  good.tMs = 100000;
  good.lat = 36.37;
  good.lon = -94.2;
  good.speedMps = 40;
  good.speedLimitMps = 36;
  auto const kept = CleanFix(good, 0);
  CHECK(kept && kept->speedMps == 40 && kept->speedLimitMps && *kept->speedLimitMps == 36);
  for (double const absurd : {151.0, 1e15, std::numeric_limits<double>::max()})
  {
    Fix f = good;
    f.speedMps = absurd;
    f.speedLimitMps = absurd;
    auto const c = CleanFix(f, 0);
    CHECK(c.has_value());
    CHECK_EQUAL(c->speedMps, -1.0);
    CHECK(!c->speedLimitMps);
  }
}
