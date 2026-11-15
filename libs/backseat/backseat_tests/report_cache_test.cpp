// Arrival report built once (loop B163, BI-145). CritiqueEngine::Report() is asked for 3-4 times at
// arrival (the grade line, the trip record, the trace, the host callback, the arrival bit); it now
// builds once and is reused until an incident or a stat changes. RoundTo got a print-free fast path
// that must return exactly the doubles strtod(ToFixed(x, d)) gave.
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/engine.hpp"
#include "backseat/navigator.hpp"
#include "backseat/report.hpp"
#include "backseat/units.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

using namespace backseat;
using namespace backseat_tests;

namespace
{
double OldRoundTo(double x, int digits)
{
  if (!std::isfinite(x) || std::fabs(x) >= 1e21)
    return x;
  return std::strtod(ToFixed(x, digits).c_str(), nullptr);
}

// Same value and same sign of zero (or both NaN).
bool Identical(double a, double b)
{
  if (std::isnan(a) || std::isnan(b))
    return std::isnan(a) && std::isnan(b);
  return a == b && std::signbit(a) == std::signbit(b);
}

void CheckSameReport(TripReport const & a, TripReport const & b)
{
  CHECK_EQUAL(a.overall.score, b.overall.score);
  CHECK_EQUAL(a.overall.letter, b.overall.letter);
  CHECK_EQUAL(a.instructions.score, b.instructions.score);
  CHECK_EQUAL(a.driving.score, b.driving.score);
  CHECK_EQUAL(a.quip, b.quip);
  CHECK_EQUAL(a.speedingSeconds, b.speedingSeconds);
  CHECK_EQUAL(a.hardBrakes, b.hardBrakes);
  CHECK_EQUAL(a.missed, b.missed);
  CHECK_EQUAL(a.heatmap.size(), b.heatmap.size());
  for (size_t i = 0; i < std::min(a.heatmap.size(), b.heatmap.size()); ++i)
  {
    CHECK(Identical(a.heatmap[i].weight, b.heatmap[i].weight));
    CHECK(Identical(a.heatmap[i].lat, b.heatmap[i].lat));
    CHECK_EQUAL(a.heatmap[i].count, b.heatmap[i].count);
  }
}

TelemetrySample Sample(double t, double mph, double limitMph, double lat)
{
  TelemetrySample s;
  s.t = t;
  s.speedMps = mph * 0.44704;
  s.speedLimitMps = limitMph * 0.44704;
  s.lat = lat;
  s.lon = -94.2;
  return s;
}
}  // namespace

TEST(RoundToFastPathMatchesTheOldPrintOnRandomValues)
{
  std::mt19937_64 rng(163);
  std::uniform_real_distribution<double> unit(0, 1);
  std::uniform_int_distribution<int> exp10(-12, 22);
  std::uniform_int_distribution<uint64_t> bits;
  int mismatches = 0;
  for (int n = 0; n < 120000; ++n)
  {
    double x;
    switch (n % 4)
    {
    case 0: x = (unit(rng) - 0.5) * std::pow(10.0, exp10(rng)); break;          // across magnitudes
    case 1: x = (unit(rng) - 0.5) * 360; break;                                  // lat/lon-ish
    case 2: x = std::floor(unit(rng) * 100000) / 1000 + 0.0005; break;           // x.xx5 near-halves
    default:
    {
      uint64_t b = bits(rng);  // any bit pattern: subnormals, huge, NaN, inf
      std::memcpy(&x, &b, sizeof x);
    }
    }
    for (int d : {0, 1, 2, 3, 6})
      if (!Identical(RoundTo(x, d), OldRoundTo(x, d)))
        ++mismatches;
  }
  CHECK_EQUAL(mismatches, 0);
}

TEST(RoundToFastPathMatchesTheOldPrintAtTheEdges)
{
  double const nan = std::numeric_limits<double>::quiet_NaN();
  double const inf = std::numeric_limits<double>::infinity();
  std::vector<double> const xs{0.0,    -0.0,    0.5,     1.5,     2.5,     -0.5,    -2.5,     1.005,  1.015,
                               1.025,  2.675,   1.0005,  -1.005,  -0.001,  -0.004,  -0.005,   0.004,  0.125,
                               0.375,  -0.125,  1e20,    -1e20,   9.99e20, 1e21,    -1e21,    5e-324, -5e-324,
                               2.2250738585072014e-308,  4503599627370495.5,         4503599627370496.0,
                               9007199254740993.0,       0.1 + 0.2,                  1.0 / 3,         nan,
                               -nan,   inf,     -inf,    123456.785,       36.3725005, -94.2088005};
  int mismatches = 0;
  for (double x : xs)
    for (int d = -2; d <= 22; ++d)
      if (!Identical(RoundTo(x, d), OldRoundTo(x, d)))
        ++mismatches;
  CHECK_EQUAL(mismatches, 0);
  // Exact binary ties round half up like JavaScript, not half to even like printf.
  CHECK_EQUAL(RoundTo(2.5, 0), 3.0);
  CHECK_EQUAL(RoundTo(0.125, 2), 0.13);
  CHECK(std::signbit(RoundTo(-0.001, 2)));
  CHECK(!std::signbit(RoundTo(-0.0, 2)));
}

TEST(EngineReportIsBuiltOnceUntilSomethingChanges)
{
  EngineConfig c;
  c.rng = SeededRng(7);
  CritiqueEngine engine(c, 0);
  engine.OnTelemetry(Sample(0, 50, 50, 36.37));
  engine.OnTelemetry(Sample(1000, 62, 50, 36.371));
  engine.OnTelemetry(Sample(2000, 63, 50, 36.372));
  auto const first = engine.Report();
  CHECK_EQUAL(engine.ReportBuilds(), 1);
  CheckSameReport(engine.Report(), first);
  CheckSameReport(engine.Report(), first);
  CHECK_EQUAL(engine.ReportBuilds(), 1);
  CheckSameReport(first, BuildReport(engine.Incidents(), engine.Stats()));

  // A stat moves (more speeding time, distance): rebuilt, and matches a fresh build.
  engine.OnTelemetry(Sample(3000, 64, 50, 36.373));
  auto const second = engine.Report();
  CHECK_EQUAL(engine.ReportBuilds(), 2);
  CheckSameReport(second, BuildReport(engine.Incidents(), engine.Stats()));

  // An incident lands (a missed turn): rebuilt again.
  size_t const before = engine.Incidents().size();
  engine.OnNav(NavEvent::MissedTurn(4000));
  CHECK(engine.Incidents().size() > before || engine.Stats().missedTurns > 0);
  auto const third = engine.Report();
  CHECK_EQUAL(engine.ReportBuilds(), 3);
  CheckSameReport(third, BuildReport(engine.Incidents(), engine.Stats()));
  CHECK_EQUAL(third.missed, 1);

  // Arrival stamps endedAt and asks for the grade; asking again after builds nothing.
  engine.OnNav(NavEvent::Arrived(60000));
  int const atArrival = engine.ReportBuilds();
  CHECK_EQUAL(atArrival, 4);
  engine.Report();
  engine.Report();
  CHECK_EQUAL(engine.ReportBuilds(), atArrival);
  CheckSameReport(engine.Report(), BuildReport(engine.Incidents(), engine.Stats()));
}

TEST(ArrivalBuildsTheTripReportOnce)
{
  for (uint32_t seed : {3u, 7u, 11u})
  {
    EngineConfig c;
    c.rng = SeededRng(seed);
    Navigator * self = nullptr;
    int buildsAtCallback = -1;
    int buildsAfterArrival = -1;
    TripReport handed;
    Navigator nav(c, {},
                  [&](TripRecord const &, TripReport const & r)
                  {
                    handed = r;
                    buildsAtCallback = self->Engine()->ReportBuilds();
                  });
    self = &nav;
    SimOptions o;
    o.shape = TripShape::Town;
    o.seed = seed;
    o.afterFix = [&](Navigator & n, double, double)
    {
      if (n.Engine() && buildsAtCallback >= 0 && buildsAfterArrival < 0)
        buildsAfterArrival = n.Engine()->ReportBuilds();
    };
    auto const r = SimulateTrip(nav, o);
    CHECK(r.arrived);
    // The grade line, the trip record, the trace and the callback all shared one build.
    CHECK_EQUAL(buildsAtCallback, 1);
    // The arrival bit and anything after reused it.
    CHECK(buildsAfterArrival == -1 || buildsAfterArrival == 1);
    CHECK(!handed.overall.letter.empty());
  }
}
