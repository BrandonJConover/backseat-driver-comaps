// End-of-trip report: the grade (mostly about following instructions) and the heat map of
// where the driver went wrong. Built purely from the incident log. Port of engine/src/report.ts.
#pragma once

#include "backseat/types.hpp"

#include <map>
#include <string>
#include <vector>

namespace backseat
{
struct GradePart
{
  int score = 0;  // 0..100
  std::string letter;
};

struct HeatCell
{
  double lat = 0;  // cell center
  double lon = 0;
  double weight = 0;
  int count = 0;
  std::map<IncidentKind, int> kinds;
};

struct TripReport
{
  GradePart overall;
  std::string quip;

  /// The headline: did you do what the GPS told you?
  GradePart instructions;
  int onTime = 0;
  int late = 0;
  int missed = 0;
  int wrongWay = 0;
  int reroutes = 0;

  GradePart driving;
  int speedingSeconds = 0;
  int maxOverLimit = 0;
  int hardBrakes = 0;
  int hardAccels = 0;

  /// Incidents binned into ~75 m squares, worst first: the "hall of shame".
  std::vector<HeatCell> heatmap;
};

double IncidentWeight(Incident const & i);
std::vector<HeatCell> BuildHeatmap(std::vector<Incident> const & incidents, double cellMeters = 75);
TripReport BuildReport(std::vector<Incident> const & incidents, TripStats const & stats, double cellMeters = 75);

/// "B+" -> "B plus", for speaking.
std::string SpokenGrade(std::string letter);
}  // namespace backseat
