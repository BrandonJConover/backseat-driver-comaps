// Direct tests for history.cpp: trip predictions, routines, past places and the driver's memory
// (loop BI-030). The parity trace and voice_command_test cover the happy paths; these pin the
// edges: weights, thresholds, ties, damaged records and hand-edited memory files.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/history.hpp"
#include "backseat/lines.hpp"

#include <cmath>
#include <ctime>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
std::tm HistUtc(double ms)
{
  std::time_t const t = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  gmtime_r(&t, &tm);
  return tm;
}

Rng Always(double r)
{
  return [r] { return r; };
}

bool Has(std::string const & text, std::string const & part)
{
  return text.find(part) != std::string::npos;
}

LatLon const kHome{39.70, -104.90};
LatLon const kWork{39.75, -104.99};
LatLon const kGym{39.72, -104.95};
double constexpr kDay = 86400000.0;
double const kMonday = 1791763200000.0;  // 2026-10-12 00:00 UTC, a Monday

TripRecord Trip(LatLon from, LatLon to, double expected, double actual, std::string name = "Work")
{
  TripRecord r;
  r.routeKey = RouteKey(from, to);
  r.destination = std::move(name);
  r.to = to;
  r.endedAt = 1000;
  r.expectedMinutes = expected;
  r.actualMinutes = actual;
  return r;
}

TripPlan Plan(LatLon from, LatLon to, double expected, std::string name = "Work")
{
  return {from, to, std::move(name), expected};
}

/// A trip on route from>to that started at day/hour/minute (UTC) and took 20 minutes.
TripRecord Started(LatLon from, LatLon to, int day, int hour, int minute, std::string name = "Work")
{
  auto r = Trip(from, to, 20, 20, std::move(name));
  r.endedAt = kMonday + day * kDay + (hour * 60 + minute + 20) * 60000.0;
  return r;
}

std::tm At(int wday, int hour, int minute)
{
  std::tm tm{};
  tm.tm_wday = wday;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  return tm;
}

Routine Commute(int startMinute, std::array<bool, 7> usualOn, int trips = 10, LatLon to = kWork)
{
  Routine r;
  r.routeKey = RouteKey(kHome, to);
  r.startMinute = startMinute;
  r.usualOn = usualOn;
  r.trips = trips;
  return r;
}

std::array<bool, 7> const kWeekdays = {false, true, true, true, true, true, false};
}  // namespace

TEST(RouteKeySnapsToAQuarterKilometre)
{
  CHECK_EQUAL(RouteKey({39.7391, -104.9902}, {39.761, -105.0123}), std::string("39.7400,-104.9900>39.7600,-105.0125"));
  // ~100 m apart: the same route; ~500 m apart: a different one.
  CHECK_EQUAL(RouteKey({39.7391, -104.9902}, kWork), RouteKey({39.7399, -104.9907}, kWork));
  CHECK(RouteKey(kHome, kWork) != RouteKey({39.705, -104.90}, kWork));
  CHECK(RouteKey(kHome, kWork) != RouteKey(kWork, kHome));
}

TEST(PredictTripWithNoHistoryAdmitsIt)
{
  auto const p = PredictTrip({}, Plan(kHome, kWork, 20), Always(0));
  CHECK(p.habit == Habit::Unknown);
  CHECK_EQUAL(p.predictedMinutes, 20.0);
  CHECK_EQUAL(p.timesOnRoute, 0);
  CHECK_EQUAL(p.text, std::string("New route. I have no data on you here. Yet."));
  // Trips without both an estimate and a time teach nothing.
  std::vector<TripRecord> const untimed = {Trip(kHome, kWork, 0, 20), Trip(kHome, kWork, 20, 0)};
  CHECK(PredictTrip(untimed, Plan(kHome, kWork, 20), Always(0)).habit == Habit::Unknown);
}

TEST(PredictTripReadsTheHabitFromTheMedian)
{
  std::vector<TripRecord> fast = {Trip(kHome, kGym, 10, 8), Trip(kGym, kHome, 10, 8), Trip(kHome, kGym, 10, 8)};
  auto const lead = PredictTrip(fast, Plan(kHome, kWork, 20), Always(0));
  CHECK(lead.habit == Habit::LeadFoot);
  CHECK_EQUAL(lead.predictedMinutes, 16.0);
  CHECK(Has(lead.text, "take 20 minutes") && Has(lead.text, "take 16 with your lead foot"));
  CHECK_EQUAL(lead.timesOnRoute, 0);

  std::vector<TripRecord> const slow = {Trip(kHome, kGym, 10, 12), Trip(kGym, kHome, 10, 13)};
  CHECK(PredictTrip(slow, Plan(kHome, kWork, 20), Always(0)).habit == Habit::Slowpoke);
  std::vector<TripRecord> const steady = {Trip(kHome, kGym, 10, 9.5), Trip(kGym, kHome, 10, 10.4)};
  CHECK(PredictTrip(steady, Plan(kHome, kWork, 20), Always(0)).habit == Habit::OnTime);

  // One road-trip disaster doesn't make a slowpoke.
  fast.push_back(Trip(kHome, {35.0, -100.0}, 60, 300));
  CHECK(PredictTrip(fast, Plan(kHome, kWork, 20), Always(0)).habit == Habit::LeadFoot);
  // Never predicts less than a minute.
  CHECK_EQUAL(PredictTrip(fast, Plan(kHome, kWork, 0.4), Always(0)).predictedMinutes, 1.0);
}

TEST(PredictTripWeighsThisRouteTriple)
{
  // Elsewhere the driver is slow (1.5x), on this route fast (0.8x): 3 + 2 votes, median 0.8.
  std::vector<TripRecord> const history = {Trip(kHome, kGym, 10, 15), Trip(kGym, kHome, 10, 15),
                                           Trip(kHome, kWork, 20, 16)};
  auto const p = PredictTrip(history, Plan(kHome, kWork, 20), Always(0));
  CHECK(p.habit == Habit::LeadFoot);
  CHECK_EQUAL(p.timesOnRoute, 1);
  // The same history, planning a different route: 1.5 x2 + 0.8 x1, median 1.5.
  CHECK(PredictTrip(history, Plan(kHome, {39.80, -105.0}, 20), Always(0)).habit == Habit::Slowpoke);
}

TEST(PredictTripRemembersTheLastTripOnThisRoute)
{
  auto plan = Plan(kHome, kWork, 20);
  auto last = Trip(kHome, kWork, 20, 20);
  last.missedTurns = 1;
  auto const one = PredictTrip({last}, plan, Always(0));
  CHECK(Has(one.text, "missed 1 turn on this exact route"));
  last.missedTurns = 3;
  last.hardBrakes = 4;  // missed turns win over brakes
  CHECK(Has(PredictTrip({last}, plan, Always(0)).text, "missed 3 turns on"));
  last.missedTurns = 0;
  CHECK(Has(PredictTrip({last}, plan, Always(0)).text, "brake checked 4 times"));
  last.hardBrakes = 1;  // one hard brake is just driving
  auto const quiet = PredictTrip({last}, plan, Always(0));
  CHECK(!Has(quiet.text, "brake") && !Has(quiet.text, "trip to"));

  // Only the latest trip counts: an old missed turn is forgiven.
  auto old = last;
  old.missedTurns = 2;
  auto const repeat = PredictTrip({old, last}, plan, Always(0));
  CHECK(!Has(repeat.text, "missed"));
  CHECK(Has(repeat.text, "This is your third trip to Work."));
  CHECK_EQUAL(repeat.timesOnRoute, 2);
}

TEST(PredictTripCountsTripsInWords)
{
  auto const nth = [](size_t trips)
  {
    std::vector<TripRecord> const history(trips, Trip(kHome, kWork, 20, 20));
    return PredictTrip(history, Plan(kHome, kWork, 20, "the gym"), Always(0)).text;
  };
  CHECK(Has(nth(9), "your tenth trip to the gym"));
  CHECK(Has(nth(10), "your 11th trip"));
  CHECK(Has(nth(11), "your 12th trip"));
  CHECK(Has(nth(12), "your 13th trip"));
  CHECK(Has(nth(20), "your 21st trip"));
  CHECK(Has(nth(21), "your 22nd trip"));
  CHECK(Has(nth(22), "your 23rd trip"));
  CHECK(Has(nth(110), "your 111th trip"));
  CHECK(Has(nth(111), "your 112th trip"));
}

TEST(PredictTripWithASaidLogDoesntRepeatItself)
{
  std::vector<TripRecord> const steady = {Trip(kHome, kGym, 10, 10)};
  SaidLog said;
  auto rng = SeededRng(7);
  std::set<std::string> heard;
  for (int i = 0; i < 6; ++i)  // the on-time pool has 6 lines with the log
    heard.insert(PredictTrip(steady, Plan(kHome, kWork, 20), rng, &said).text);
  CHECK_EQUAL(heard.size(), size_t(6));
  CHECK(said.Said("_predict.onTime"));
}

TEST(PredictTripSurvivesBadRandomValuesAndDamagedRecords)
{
  double const nan = std::numeric_limits<double>::quiet_NaN();
  double const inf = std::numeric_limits<double>::infinity();
  // Without a log, r = 1 used to index one past the pool.
  CHECK_EQUAL(PredictTrip({}, Plan(kHome, kWork, 20), Always(1.0)).text,
              std::string("Never driven this one before. Neither of us knows how bad this will be. That's exciting. "
                          "For me."));
  CHECK_EQUAL(PredictTrip({}, Plan(kHome, kWork, 20), Always(nan)).text,
              std::string("New route. I have no data on you here. Yet."));
  CHECK(!PredictTrip({}, Plan(kHome, kWork, 20), Always(-3)).text.empty());
  CHECK(!PredictTrip({Trip(kHome, kWork, 20, 20)}, Plan(kHome, kWork, 20), Always(7)).text.empty());

  // NaN, infinite or negative minutes from a damaged file teach nothing.
  std::vector<TripRecord> const junk = {Trip(kHome, kWork, 20, nan), Trip(kHome, kWork, inf, 20),
                                        Trip(kHome, kWork, 20, inf), Trip(kHome, kWork, -5, 20)};
  auto const p = PredictTrip(junk, Plan(kHome, kWork, 20), Always(0));
  CHECK(p.habit == Habit::Unknown);
  CHECK_EQUAL(p.predictedMinutes, 20.0);
}

TEST(RecordTripKeepsTheDestinationWhenKnown)
{
  EngineConfig c;
  c.rng = SeededRng(5);
  CritiqueEngine e(c, 1000);
  e.OnNav(NavEvent::Arrived(1000 + 12.5 * 60000));
  auto const r = RecordTrip(e, Plan(kHome, kWork, 14, "Acme"));
  CHECK_EQUAL(r.routeKey, RouteKey(kHome, kWork));
  CHECK_EQUAL(r.destination, std::string("Acme"));
  CHECK_EQUAL(r.expectedMinutes, 14.0);
  CHECK_EQUAL(r.actualMinutes, 12.5);
  CHECK_EQUAL(r.endedAt, 1000 + 12.5 * 60000);
  CHECK(r.to && r.to->lat == kWork.lat && r.to->lon == kWork.lon);
  CHECK(!r.grade.empty());
  // No destination point (0, 0): nothing exact to remember.
  CHECK(!RecordTrip(e, Plan(kHome, {0, 0}, 14)).to);
}

TEST(LearnRoutinesNeedsFourTripsAndTwoOnADay)
{
  std::vector<TripRecord> history;
  for (int d = 0; d < 3; ++d)
    history.push_back(Started(kHome, kWork, d, 8, 0));
  CHECK(LearnRoutines(history, HistUtc).empty());  // 3 trips
  history.push_back(Started(kHome, kWork, 3, 8, 0));
  CHECK(LearnRoutines(history, HistUtc).empty());  // 4 trips, but no day twice
  history.push_back(Started(kHome, kWork, 7, 8, 10));  // the next Monday
  auto const routines = LearnRoutines(history, HistUtc);
  CHECK_EQUAL(routines.size(), size_t(1));
  CHECK_EQUAL(routines[0].trips, 5);
  CHECK_EQUAL(routines[0].startMinute, 8 * 60 + 2);
  CHECK_EQUAL(routines[0].tripsByDay[1], 2);
  CHECK(routines[0].usualOn[1] && !routines[0].usualOn[2]);
}

TEST(LearnRoutinesSplitsMorningAndEveningAndKeepsTheLatestName)
{
  std::vector<TripRecord> history;
  for (int week = 0; week < 2; ++week)
    for (int d = 0; d < 3; ++d)
    {
      history.push_back(Started(kHome, kWork, week * 7 + d, 7, 50 + d * 5, week ? "Acme Corp" : "Acme"));
      history.push_back(Started(kHome, kWork, week * 7 + d, 18, 0));
    }
  history.push_back(Started(kHome, kGym, 1, 8, 0));  // once: not a routine
  TripRecord unfinished = Started(kHome, kWork, 2, 8, 0);
  unfinished.endedAt = 0;
  history.push_back(unfinished);
  TripRecord damaged = Started(kHome, kWork, 2, 8, 0);
  damaged.endedAt = std::numeric_limits<double>::quiet_NaN();
  history.push_back(damaged);
  TripRecord noRoute = Started(kHome, kWork, 2, 8, 0);
  noRoute.routeKey.clear();
  history.push_back(noRoute);

  auto const routines = LearnRoutines(history, HistUtc);
  CHECK_EQUAL(routines.size(), size_t(2));
  CHECK_EQUAL(routines[0].startMinute, 7 * 60 + 55);
  CHECK_EQUAL(routines[0].trips, 6);
  CHECK_EQUAL(routines[0].destination, std::string("Acme Corp"));
  CHECK_EQUAL(routines[0].usualMinutes, 20);
  CHECK_EQUAL(routines[1].startMinute, 18 * 60);
  // A clock that hands back a nonsense weekday doesn't index past the week.
  auto const broken = [](double) { std::tm tm{}; tm.tm_wday = 9; return tm; };
  CHECK(LearnRoutines(history, broken).empty());
}

TEST(UsualTripPicksTheBusiestRoutineNearNow)
{
  auto const work = Commute(8 * 60, kWeekdays, 10, kWork);
  auto const gym = Commute(8 * 60 + 10, kWeekdays, 4, kGym);
  auto const best = UsualTrip({gym, work}, kHome, At(2, 8, 5));
  CHECK(best && best->routeKey == work.routeKey);
  CHECK(UsualTrip({work}, kHome, At(2, 8, 45)));
  CHECK(!UsualTrip({work}, kHome, At(2, 8, 46)));
  CHECK(UsualTrip({work}, kHome, At(2, 7, 15)));
  CHECK(!UsualTrip({work}, kHome, At(6, 8, 0)));  // Saturday
  CHECK(!UsualTrip({}, kHome, At(2, 8, 0)));
}

TEST(CheckRoutineThresholds)
{
  auto const work = Commute(8 * 60, kWeekdays);
  auto const key = work.routeKey;
  // Late: from 45 minutes to 4 hours after the usual start.
  CHECK(!CheckRoutine({work}, key, At(2, 8, 44)));
  auto const late = CheckRoutine({work}, key, At(2, 8, 46));
  CHECK(late && late->kind == RoutineBreak::Kind::Late && late->minutesLate == 46);
  CHECK(CheckRoutine({work}, key, At(2, 12, 0)));
  CHECK(!CheckRoutine({work}, key, At(2, 12, 1)));
  // Off day: within two hours of the usual time; a 2-day routine (weekend brunch) never counts.
  CHECK(CheckRoutine({work}, key, At(0, 10, 0))->kind == RoutineBreak::Kind::OffDay);
  CHECK(!CheckRoutine({work}, key, At(0, 10, 1)));
  auto const brunch = Commute(10 * 60, {true, false, false, false, false, false, true});
  CHECK(!CheckRoutine({brunch}, brunch.routeKey, At(3, 10, 0)));
  // Elsewhere: within half an hour, and the usual trip on time beats it.
  auto const gymKey = RouteKey(kHome, kGym);
  CHECK(CheckRoutine({work}, gymKey, At(2, 8, 30))->kind == RoutineBreak::Kind::Elsewhere);
  CHECK(!CheckRoutine({work}, gymKey, At(2, 8, 31)));
  CHECK(!CheckRoutine({work, Commute(8 * 60, kWeekdays, 5, kGym)}, gymKey, At(2, 8, 0)));
  // From somewhere else entirely: nothing to compare.
  CHECK(!CheckRoutine({work}, RouteKey(kGym, kWork), At(2, 8, 0)));
}

TEST(PastPlacesEdges)
{
  std::vector<TripRecord> history;
  auto a = Trip(kHome, kWork, 20, 20, "Acme");
  a.endedAt = 1000;
  auto b = Trip(kHome, kGym, 10, 10, "Gym");
  b.endedAt = 2000;
  b.to.reset();  // older records have no exact point: the route key's cell is used
  history = {a, b};
  auto places = PastPlaces(history, 10);
  CHECK_EQUAL(places.size(), size_t(2));
  CHECK_EQUAL(places[0].destination, std::string("Gym"));  // tie on trips: most recent first
  CHECK_EQUAL(places[0].to.lat, 39.72);
  CHECK_EQUAL(places[0].to.lon, -104.95);
  CHECK(PastPlaces(history, 0).empty());

  auto noArrow = a;
  noArrow.routeKey = "garbage";
  auto badCell = b;
  badCell.routeKey = "39.7,-104.9>not,a,place";
  auto nanEnd = a;
  nanEnd.endedAt = std::numeric_limits<double>::quiet_NaN();
  places = PastPlaces({noArrow, badCell, nanEnd}, 10);
  CHECK(places.empty());
}

TEST(DriverMemoryTopicsReplaceInPlaceAndNotesPileUp)
{
  DriverMemory m;
  CHECK(!m.Recall("snack"));
  CHECK(m.Remember("name", "Brandon", 1));
  CHECK(m.Remember("snack", "beef jerky", 2));
  CHECK(m.Remember(" name\n", "Brendon", 3));  // trimmed topic, same memory
  CHECK_EQUAL(m.All().size(), size_t(2));
  CHECK_EQUAL(m.All()[0].fact, std::string("Brendon"));
  CHECK_EQUAL(m.All()[0].learnedAt, 3.0);
  CHECK(!m.Remember("", "x", 4));
  CHECK(!m.Forget("partner"));
  CHECK(m.Forget("snack"));
  CHECK(!m.Recall("snack"));
}

TEST(DriverMemoryForModelSkipsWhatDoesntFit)
{
  DriverMemory m;
  m.Remember("name", "Brandon", 1);
  m.Remember("partner", std::string(300, 'x'), 2);
  m.Remember("note", "likes jazz", 3);
  CHECK_EQUAL(m.ForModel(0), std::string());
  // The long fact used to stop the list, so the note never made it in.
  CHECK_EQUAL(m.ForModel(100), std::string("name: Brandon\nnote: likes jazz\n"));
}

TEST(DriverMemoryParseSkipsJunkLines)
{
  std::string const text = "100\tname\tBrandon\r\n"
                           "no tabs at all\n"
                           "200\tonly one tab\n"
                           "\n"
                           "300\t\tno topic\n"
                           "400\tsnack\t\n"
                           "later\tnote\thates traffic\n";
  auto const m = DriverMemory::Parse(text);
  CHECK_EQUAL(m.All().size(), size_t(2));
  CHECK_EQUAL(*m.Recall("name"), std::string("Brandon"));
  CHECK_EQUAL(m.All()[1].learnedAt, 0.0);  // unreadable time
  CHECK_EQUAL(DriverMemory::Parse(m.Serialize()).Serialize(), m.Serialize());
  CHECK(DriverMemory::Parse("").All().empty());
}
