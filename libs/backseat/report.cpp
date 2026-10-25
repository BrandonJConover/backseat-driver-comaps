#include "backseat/report.hpp"

#include "backseat/units.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace backseat
{
namespace
{
double HeatWeight(IncidentKind k)
{
  switch (k)
  {
  case IncidentKind::Speeding: return 1;
  case IncidentKind::TooSlow: return 0.5;
  case IncidentKind::Traffic: return 0.5;
  case IncidentKind::HardBraking: return 3;
  case IncidentKind::HardAccel: return 2;
  case IncidentKind::LongStop: return 0.5;
  case IncidentKind::MissedTurn: return 5;
  case IncidentKind::WrongTurn: return 5;
  case IncidentKind::UnplannedStop: return 1;
  case IncidentKind::LateTurn: return 2;
  case IncidentKind::Rerouted: return 1;
  }
  return 1;
}

double ClampScore(double n, double lo, double hi) { return std::min(hi, std::max(lo, n)); }

std::string Letter(double score)
{
  if (score >= 93) return "A";
  if (score >= 90) return "A-";
  if (score >= 87) return "B+";
  if (score >= 83) return "B";
  if (score >= 80) return "B-";
  if (score >= 77) return "C+";
  if (score >= 73) return "C";
  if (score >= 70) return "C-";
  if (score >= 60) return "D";
  return "F";
}

std::string Quip(double score)
{
  if (score >= 93) return "An A. I checked twice. I'm as surprised as you are.";
  if (score >= 80) return "Fine. Adequate. Don't make it weird.";
  if (score >= 70) return "Average, which in your case is a personal best.";
  if (score >= 60) return "A D. The D stands for 'Did you even listen'.";
  return "An F. I'd write more, but the heat map says it all.";
}
}  // namespace

double IncidentWeight(Incident const & i)
{
  if (i.kind == IncidentKind::Speeding)
    return HeatWeight(i.kind) * std::max(1.0, static_cast<double>(i.overLimit.value_or(1)) / 3);
  return HeatWeight(i.kind);
}

std::vector<HeatCell> BuildHeatmap(std::vector<Incident> const & incidents, double cellMeters)
{
  // Insertion order matters for ties in the final sort, so keep it like a JS Map.
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

TripReport BuildReport(std::vector<Incident> const & incidents, TripStats const & stats, double cellMeters)
{
  auto const count = [&](IncidentKind k)
  { return static_cast<double>(std::count_if(incidents.begin(), incidents.end(), [k](Incident const & i) { return i.kind == k; })); };

  TripReport r;
  r.onTime = stats.turnsCompleted - stats.lateTurns;
  r.late = stats.lateTurns;
  r.missed = stats.missedTurns;
  r.wrongWay = stats.wrongTurns;
  r.reroutes = stats.reroutes;

  // Each maneuver is worth 1 if on time, 0.6 if late, 0 if missed or turned the wrong way;
  // extra reroutes (beyond those a botched turn already explains) cost a bit more.
  int const maneuvers = r.onTime + r.late + r.missed + r.wrongWay;
  int const extraReroutes = std::max(0, r.reroutes - r.missed - r.wrongWay);
  double const instrScore =
      maneuvers == 0 ? 100 : ClampScore(100 * (r.onTime + 0.6 * r.late) / maneuvers - extraReroutes * 5, 0, 100);

  double const minutes = std::max(1.0, (stats.endedAt.value_or(stats.startedAt) - stats.startedAt) / 60000);
  double const drivingScore = ClampScore(100 - (stats.speedingSeconds / (minutes * 60)) * 60  // share of the trip spent speeding
                                        - stats.maxOverLimit * 1.5 - count(IncidentKind::HardBraking) * 6 -
                                        count(IncidentKind::HardAccel) * 4 - count(IncidentKind::TooSlow) * 2,
                                    0, 100);

  double const overallScore = JsRound(0.65 * instrScore + 0.35 * drivingScore);
  r.overall = {static_cast<int>(overallScore), Letter(overallScore)};
  r.quip = Quip(overallScore);
  r.instructions = {static_cast<int>(JsRound(instrScore)), Letter(instrScore)};
  r.driving = {static_cast<int>(JsRound(drivingScore)), Letter(drivingScore)};
  r.speedingSeconds = static_cast<int>(JsRound(stats.speedingSeconds));
  r.maxOverLimit = stats.maxOverLimit;
  r.hardBrakes = stats.hardBrakes;
  r.hardAccels = stats.hardAccels;
  r.heatmap = BuildHeatmap(incidents, cellMeters);
  return r;
}

std::string SpokenGrade(std::string letter)
{
  // Same as letter.replace("+", " plus").replace("-", " minus"): first occurrence only.
  if (auto p = letter.find('+'); p != std::string::npos)
    letter.replace(p, 1, " plus");
  if (auto p = letter.find('-'); p != std::string::npos)
    letter.replace(p, 1, " minus");
  return letter;
}
}  // namespace backseat
