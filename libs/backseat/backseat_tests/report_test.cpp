// End-of-trip report: grades, quip, heat map and the spoken grade (report.cpp had no direct tests).
#include "check.hpp"

#include "backseat/report.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
Incident At(IncidentKind k, double lat, double lon, std::optional<int> overLimit = std::nullopt)
{
  Incident i;
  i.kind = k;
  i.lat = lat;
  i.lon = lon;
  i.overLimit = overLimit;
  return i;
}

Incident Nowhere(IncidentKind k)
{
  Incident i;
  i.kind = k;
  return i;
}

TripStats Minutes(double m)
{
  TripStats s;
  s.startedAt = 1000;
  s.endedAt = 1000 + m * 60000;
  return s;
}
}  // namespace

TEST(ReportCleanTripIsAnA)
{
  auto const r = BuildReport({}, Minutes(10));
  CHECK_EQUAL(r.overall.score, 100);
  CHECK_EQUAL(r.overall.letter, std::string("A"));
  CHECK_EQUAL(r.instructions.score, 100);
  CHECK_EQUAL(r.driving.score, 100);
  CHECK_EQUAL(r.quip, std::string("An A. I checked twice. I'm as surprised as you are."));
  CHECK(r.heatmap.empty());
}

TEST(ReportInstructionsGrade)
{
  // 8 on time, 2 late (0.6 each), 1 missed, 1 wrong way: 9.2 / 12. Four reroutes, two explained by the
  // botched turns, so two extra at 5 points each: 76.67 - 10 = 66.67.
  TripStats s = Minutes(20);
  s.turnsCompleted = 10;
  s.lateTurns = 2;
  s.missedTurns = 1;
  s.wrongTurns = 1;
  s.reroutes = 4;
  auto const r = BuildReport({}, s);
  CHECK_EQUAL(r.onTime, 8);
  CHECK_EQUAL(r.late, 2);
  CHECK_EQUAL(r.missed, 1);
  CHECK_EQUAL(r.wrongWay, 1);
  CHECK_EQUAL(r.reroutes, 4);
  CHECK_EQUAL(r.instructions.score, 67);
  CHECK_EQUAL(r.instructions.letter, std::string("D"));
  CHECK_EQUAL(r.driving.score, 100);
  // 0.65 * 66.67 + 0.35 * 100 = 78.33
  CHECK_EQUAL(r.overall.score, 78);
  CHECK_EQUAL(r.overall.letter, std::string("C+"));
  CHECK_EQUAL(r.quip, std::string("Average, which in your case is a personal best."));
}

TEST(ReportRerouteWithoutBotchedTurnsCosts)
{
  TripStats s = Minutes(5);
  s.turnsCompleted = 4;
  s.reroutes = 1;
  CHECK_EQUAL(BuildReport({}, s).instructions.score, 95);
}

TEST(ReportDrivingGrade)
{
  // 10 minutes, 60 s speeding (10% of the trip, 6 points), 10 over at worst (15), two hard brakes (12),
  // one hard launch (4), one crawl (2): 100 - 39 = 61.
  TripStats s = Minutes(10);
  s.speedingSeconds = 60;
  s.maxOverLimit = 10;
  s.hardBrakes = 2;
  s.hardAccels = 1;
  std::vector<Incident> const incidents = {Nowhere(IncidentKind::HardBraking), Nowhere(IncidentKind::HardBraking),
                                           Nowhere(IncidentKind::HardAccel), Nowhere(IncidentKind::TooSlow)};
  auto const r = BuildReport(incidents, s);
  CHECK_EQUAL(r.driving.score, 61);
  CHECK_EQUAL(r.driving.letter, std::string("D"));
  CHECK_EQUAL(r.speedingSeconds, 60);
  CHECK_EQUAL(r.maxOverLimit, 10);
  CHECK_EQUAL(r.hardBrakes, 2);
  CHECK_EQUAL(r.hardAccels, 1);
  // No maneuvers: instructions 100. 65 + 0.35 * 61 = 86.35.
  CHECK_EQUAL(r.instructions.score, 100);
  CHECK_EQUAL(r.overall.score, 86);
  CHECK_EQUAL(r.overall.letter, std::string("B"));
  // Incidents without a position count toward the grade but not the heat map.
  CHECK(r.heatmap.empty());
}

TEST(ReportScoresClampAtZero)
{
  TripStats s = Minutes(1);
  s.missedTurns = 3;
  s.reroutes = 9;
  s.maxOverLimit = 100;
  auto const r = BuildReport({}, s);
  CHECK_EQUAL(r.instructions.score, 0);
  CHECK_EQUAL(r.driving.score, 0);
  CHECK_EQUAL(r.overall.score, 0);
  CHECK_EQUAL(r.overall.letter, std::string("F"));
  CHECK_EQUAL(r.quip, std::string("An F. I'd write more, but the heat map says it all."));
}

TEST(ReportShortOrUnfinishedTripCountsAsAMinute)
{
  // No end time (or an end before the start) must not divide by zero: the trip counts as one minute,
  // so 30 s of speeding is half of it (30 points).
  TripStats s;
  s.startedAt = 5000;
  s.speedingSeconds = 30;
  CHECK_EQUAL(BuildReport({}, s).driving.score, 70);
  s.endedAt = 1000;
  CHECK_EQUAL(BuildReport({}, s).driving.score, 70);
}

TEST(ReportLetterMatchesTheScoreShown)
{
  // 2 on time + 1 late of 3 = 86.67, shown as 87: the letter must be the 87 one (B+), not B.
  TripStats s = Minutes(10);
  s.turnsCompleted = 3;
  s.lateTurns = 1;
  auto r = BuildReport({}, s);
  CHECK_EQUAL(r.instructions.score, 87);
  CHECK_EQUAL(r.instructions.letter, std::string("B+"));

  // 5 over at worst: 92.5, shown as 93, so A rather than A-.
  TripStats d = Minutes(10);
  d.maxOverLimit = 5;
  r = BuildReport({}, d);
  CHECK_EQUAL(r.driving.score, 93);
  CHECK_EQUAL(r.driving.letter, std::string("A"));
}

TEST(ReportLetterBands)
{
  // Driving score = 100 - 1.5 * maxOverLimit, so even limits give whole scores to walk the bands.
  struct Band
  {
    int over;
    int score;
    char const * letter;
  };
  for (auto const & b : {Band{0, 100, "A"}, Band{4, 94, "A"}, Band{6, 91, "A-"}, Band{8, 88, "B+"}, Band{10, 85, "B"},
                         Band{12, 82, "B-"}, Band{14, 79, "C+"}, Band{16, 76, "C"}, Band{20, 70, "C-"},
                         Band{26, 61, "D"}, Band{28, 58, "F"}})
  {
    TripStats s = Minutes(10);
    s.maxOverLimit = b.over;
    auto const r = BuildReport({}, s);
    CHECK_EQUAL(r.driving.score, b.score);
    CHECK_EQUAL(r.driving.letter, std::string(b.letter));
  }
}

TEST(IncidentWeights)
{
  CHECK_EQUAL(IncidentWeight(Nowhere(IncidentKind::MissedTurn)), 5.0);
  CHECK_EQUAL(IncidentWeight(Nowhere(IncidentKind::WrongTurn)), 5.0);
  CHECK_EQUAL(IncidentWeight(Nowhere(IncidentKind::HardBraking)), 3.0);
  CHECK_EQUAL(IncidentWeight(Nowhere(IncidentKind::HardAccel)), 2.0);
  CHECK_EQUAL(IncidentWeight(Nowhere(IncidentKind::LateTurn)), 2.0);
  CHECK_EQUAL(IncidentWeight(Nowhere(IncidentKind::TooSlow)), 0.5);
  CHECK_EQUAL(IncidentWeight(Nowhere(IncidentKind::LongStop)), 0.5);
  // Speeding scales with how far over: a third of the overage, never under 1.
  CHECK_EQUAL(IncidentWeight(Nowhere(IncidentKind::Speeding)), 1.0);
  CHECK_EQUAL(IncidentWeight(At(IncidentKind::Speeding, 0, 0, 2)), 1.0);
  CHECK_EQUAL(IncidentWeight(At(IncidentKind::Speeding, 0, 0, 12)), 4.0);
}

TEST(HeatmapBinsAndSortsWorstFirst)
{
  double const lat = 30.2672, lon = -97.7431;
  std::vector<Incident> const incidents = {
      At(IncidentKind::Speeding, lat + 0.05, lon, 9),       // its own cell, weight 3
      At(IncidentKind::MissedTurn, lat, lon),               // same cell as the next: 5 + 3
      At(IncidentKind::HardBraking, lat, lon),
      Nowhere(IncidentKind::WrongTurn),                     // no position: left off the map
  };
  auto const cells = BuildHeatmap(incidents);
  CHECK_EQUAL(cells.size(), 2u);
  CHECK_EQUAL(cells[0].weight, 8.0);
  CHECK_EQUAL(cells[0].count, 2);
  CHECK_EQUAL(cells[0].kinds.at(IncidentKind::MissedTurn), 1);
  CHECK_EQUAL(cells[0].kinds.at(IncidentKind::HardBraking), 1);
  CHECK_EQUAL(cells[1].weight, 3.0);
  // The cell center sits within half a cell of the incident.
  double const latStep = 75.0 / 111320;
  double const lonStep = 75.0 / (111320 * std::cos(lat * M_PI / 180));
  CHECK(std::abs(cells[0].lat - lat) <= latStep / 2 + 1e-6);
  CHECK(std::abs(cells[0].lon - lon) <= lonStep / 2 + 1e-6);
}

TEST(HeatmapTiesKeepFirstSeenOrder)
{
  std::vector<Incident> const incidents = {At(IncidentKind::UnplannedStop, 40.0, -105.0),
                                           At(IncidentKind::Rerouted, 40.1, -105.0),
                                           At(IncidentKind::UnplannedStop, 40.2, -105.0)};
  auto const cells = BuildHeatmap(incidents);
  CHECK_EQUAL(cells.size(), 3u);
  CHECK(cells[0].kinds.count(IncidentKind::UnplannedStop) == 1 && cells[0].lat < 40.05);
  CHECK(cells[1].kinds.count(IncidentKind::Rerouted) == 1);
  CHECK(cells[2].lat > 40.15);
}

TEST(HeatmapCellSizeIsAParameter)
{
  // Two incidents ~110 m apart: separate 75 m cells, one 1 km cell.
  std::vector<Incident> const incidents = {At(IncidentKind::LateTurn, 40.0001, -105.0),
                                           At(IncidentKind::LateTurn, 40.0011, -105.0)};
  CHECK_EQUAL(BuildHeatmap(incidents).size(), 2u);
  auto const big = BuildHeatmap(incidents, 1000);
  CHECK_EQUAL(big.size(), 1u);
  CHECK_EQUAL(big[0].weight, 4.0);
  CHECK_EQUAL(BuildReport(incidents, Minutes(5), 1000).heatmap.size(), 1u);
}

TEST(SpokenGradeSaysPlusAndMinus)
{
  CHECK_EQUAL(SpokenGrade("B+"), std::string("B plus"));
  CHECK_EQUAL(SpokenGrade("A-"), std::string("A minus"));
  CHECK_EQUAL(SpokenGrade("F"), std::string("F"));
  CHECK_EQUAL(SpokenGrade(""), std::string(""));
}
