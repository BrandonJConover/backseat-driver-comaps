// Trip report speed (loop B105). The heat map keys cells by floored row/col bits instead of printed
// JsNumber strings, and ToFixed (RoundTo) screens out non-ties before its 1,100-digit exact print. Both
// must give exactly what the old code gave: same cells, order and weights; same ToFixed strings.
#include "check.hpp"

#include "backseat/report.hpp"
#include "backseat/units.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <cstdio>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using namespace backseat;

namespace
{
// The pre-B105 BuildHeatmap, verbatim apart from the name: the reference the new keys must match.
std::vector<HeatCell> StringKeyedHeatmap(std::vector<Incident> const & incidents, double cellMeters)
{
  std::vector<HeatCell> cells;
  std::unordered_map<std::string, size_t> index;
  for (auto const & i : incidents)
  {
    if (!i.lat || !i.lon)
      continue;
    double const latStep = cellMeters / 111320;
    double const lonStep = cellMeters / (111320 * std::cos(*i.lat * M_PI / 180));
    double const row = std::floor(*i.lat / latStep);
    double const col = std::floor(*i.lon / lonStep);
    std::string const key = JsNumber(row) + ":" + JsNumber(col);
    auto [it, inserted] = index.emplace(key, cells.size());
    if (inserted)
    {
      HeatCell c;
      c.lat = (row + 0.5) * latStep;
      c.lon = (col + 0.5) * lonStep;
      cells.push_back(c);
    }
    auto & cell = cells[it->second];
    cell.weight += IncidentWeight(i);
    cell.count++;
    cell.kinds[i.kind]++;
  }
  for (auto & c : cells)
  {
    c.weight = RoundTo(c.weight, 2);
    c.lat = RoundTo(c.lat, 6);
    c.lon = RoundTo(c.lon, 6);
  }
  std::stable_sort(cells.begin(), cells.end(), [](HeatCell const & a, HeatCell const & b) { return a.weight > b.weight; });
  return cells;
}

// The pre-B105 ToFixed, verbatim apart from the name.
std::string ExactToFixed(double x, int digits)
{
  digits = digits < 0 ? 0 : (digits > 100 ? 100 : digits);
  if (!std::isfinite(x) || std::fabs(x) >= 1e21)
    return JsNumber(x);
  if (x == 0)
    x = 0;
  bool const negative = x < 0;
  double const a = std::fabs(x);
  std::vector<char> exact(1200);
  std::snprintf(exact.data(), exact.size(), "%.1100f", a);
  std::string const e = exact.data();
  std::size_t const dot = e.find('.');
  std::string out;
  if (dot != std::string::npos && e[dot + 1 + digits] == '5' &&
      e.find_first_not_of('0', dot + 2 + digits) == std::string::npos)
  {
    out = e.substr(0, dot + 1 + digits);
    if (!out.empty() && out.back() == '.')
      out.pop_back();
    int i = static_cast<int>(out.size()) - 1;
    for (; i >= 0; --i)
    {
      if (out[i] == '.')
        continue;
      if (out[i] != '9')
      {
        ++out[i];
        break;
      }
      out[i] = '0';
    }
    if (i < 0)
      out.insert(out.begin(), '1');
  }
  else
  {
    std::vector<char> buf(160);
    std::snprintf(buf.data(), buf.size(), "%.*f", digits, a);
    out = buf.data();
  }
  return negative ? "-" + out : out;
}

Incident At(IncidentKind k, double lat, double lon)
{
  Incident i;
  i.kind = k;
  i.lat = lat;
  i.lon = lon;
  i.overLimit = 9;
  return i;
}

bool SameDouble(double a, double b) { return (std::isnan(a) && std::isnan(b)) || a == b; }

void CheckSameCells(std::vector<HeatCell> const & got, std::vector<HeatCell> const & want)
{
  CHECK_EQUAL(got.size(), want.size());
  for (size_t n = 0; n < std::min(got.size(), want.size()); ++n)
  {
    CHECK(SameDouble(got[n].lat, want[n].lat));
    CHECK(SameDouble(got[n].lon, want[n].lon));
    CHECK_EQUAL(got[n].weight, want[n].weight);
    CHECK_EQUAL(got[n].count, want[n].count);
    CHECK(got[n].kinds == want[n].kinds);
  }
}

// A commute's worth of incidents: a random walk with clusters, mixed kinds, and some with no position.
std::vector<Incident> Drive(unsigned seed, size_t n)
{
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> step(-0.0008, 0.0008);
  std::uniform_int_distribution<int> kind(0, 10);
  double lat = 30.27, lon = -97.74;
  std::vector<Incident> out;
  for (size_t i = 0; i < n; ++i)
  {
    lat += step(rng);
    lon += step(rng);
    if (i % 17 == 0)
    {
      Incident none;
      none.kind = IncidentKind::Traffic;
      out.push_back(none);
      continue;
    }
    out.push_back(At(static_cast<IncidentKind>(kind(rng)), lat, lon));
  }
  return out;
}
}  // namespace

TEST(HeatmapKeysMatchTheStringKeysOnRandomDrives)
{
  for (unsigned seed = 1; seed <= 20; ++seed)
  {
    auto const drive = Drive(seed, 400);
    CheckSameCells(BuildHeatmap(drive, 75), StringKeyedHeatmap(drive, 75));
    CheckSameCells(BuildHeatmap(drive, 300), StringKeyedHeatmap(drive, 300));
  }
}

TEST(HeatmapKeysMatchTheStringKeysAtTheEdges)
{
  double const nan = std::numeric_limits<double>::quiet_NaN();
  double const inf = std::numeric_limits<double>::infinity();
  // -0 and 0 share a cell; every NaN shares one; the equator, both sides of the date line, the poles
  // (lonStep blows up), and garbage positions a broken fix could carry.
  std::vector<Incident> const edges{
      At(IncidentKind::Speeding, 0.0, 0.0),        At(IncidentKind::HardBraking, -0.0, -0.0),
      At(IncidentKind::Speeding, 0.0001, -0.0001), At(IncidentKind::LateTurn, -0.0001, 0.0001),
      At(IncidentKind::Speeding, 89.9999, 179.9999), At(IncidentKind::Speeding, 90, 10),
      At(IncidentKind::Speeding, -90, -10),        At(IncidentKind::Speeding, 10, -180),
      At(IncidentKind::Speeding, 10, 180),         At(IncidentKind::MissedTurn, nan, 3),
      At(IncidentKind::WrongTurn, 3, nan),         At(IncidentKind::Traffic, nan, nan),
      At(IncidentKind::Traffic, std::nan("7"), std::nan("9")), At(IncidentKind::HardAccel, inf, 1),
      At(IncidentKind::HardAccel, -inf, 1),        At(IncidentKind::TooSlow, 1, inf),
      At(IncidentKind::LongStop, 1e300, -1e300),   At(IncidentKind::LongStop, 5e-324, -5e-324)};
  for (double cell : {75.0, 1.0, 0.0, -75.0, nan})
    CheckSameCells(BuildHeatmap(edges, cell), StringKeyedHeatmap(edges, cell));
}

TEST(HeatmapOfALongTripIsFast)
{
  // A long trip logging an incident every second: 20,000 incidents, ~4,400 cells. The old string keys
  // plus exact rounding took ~190 ms here (RelWithDebInfo); now a few ms. Loose bound for slow laptops.
  auto const drive = Drive(42, 20000);
  auto const start = std::chrono::steady_clock::now();
  auto const cells = BuildHeatmap(drive, 75);
  double const seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  CHECK(!cells.empty());
  CHECK(seconds < 0.25);
}

TEST(ToFixedScreenMatchesTheExactPrint)
{
  // Ties (k + 0.5) / 10^d that are exact in binary, near-ties that only look like ties, and random values
  // across magnitudes, at every digit count the app uses and a few it doesn't.
  std::vector<double> xs{0.5, 1.5, 2.5, -2.5, 0.125, 1.125, 2.675, 1.005, 1.0049999999999999, 0.045, 1e20 + 0.5,
                         999999.5, 9.5, 99.95, 0.0000005, 5e-324, 1e-7, 123456789.125, 0.30000000000000004,
                         4.35, 1.45, 8.345, -0.0, 0.0, 1e21 - 65536, 9007199254740993.0};
  for (int k = 0; k < 400; ++k)
    for (double p : {2.0, 4.0, 8.0, 16.0, 1024.0})
      xs.push_back((k + 0.5) / p);
  std::mt19937_64 rng(7);
  std::uniform_real_distribution<double> mant(1, 10);
  std::uniform_int_distribution<int> exp10(-12, 20);
  for (int k = 0; k < 3000; ++k)
  {
    double const x = mant(rng) * std::pow(10.0, exp10(rng));
    xs.push_back(k % 2 ? -x : x);
    xs.push_back(std::round(x * 1000) / 1000);  // the 3-digit values the report rounds most
  }
  int mismatches = 0;
  for (double x : xs)
    for (int d : {0, 1, 2, 3, 6, 10, 20})
      if (ToFixed(x, d) != ExactToFixed(x, d))
      {
        if (++mismatches <= 5)
          std::printf("  ToFixed(%.17g, %d) = %s, exact %s\n", x, d, ToFixed(x, d).c_str(), ExactToFixed(x, d).c_str());
      }
  CHECK_EQUAL(mismatches, 0);
}

TEST(RoundingAThousandHeatCellsIsFast)
{
  // RoundTo runs three times per heat cell; the exact print made a 4,000-cell trip take ~80 ms here.
  std::vector<double> xs;
  for (int k = 0; k < 3000; ++k)
    xs.push_back(30.27 + k * 0.000673);
  auto const start = std::chrono::steady_clock::now();
  double sum = 0;
  for (double x : xs)
    sum += RoundTo(x, 6);
  double const seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  CHECK(sum > 0);
  CHECK(seconds < 0.05);
}
