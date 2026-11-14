// Pop quiz road rules and the plate game follow the country being driven (loop B95, BI-082).
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"
#include "backseat/pop_quiz.hpp"

#include <cstdio>
#include <map>
#include <set>
#include <string>

using namespace backseat;

namespace
{
// 2026-10-28 19:00 UTC.
double constexpr kQcDayMs = 1793156400000.0 + 16 * 3600000.0;

RouteSnapshot QcRoute(double dist)
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

Fix QcFix(double tMs)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.74;
  f.lon = -104.99;
  f.speedMps = 25;
  f.bearingDeg = 90;
  return f;
}

/// The question the GPS asks when a driver in `country` says "give me a pop quiz". A trivia question
/// is already in the log so the pick is random (a key's first ever pick is always its first option).
QuizQuestion const * QcAsk(std::string const & country, bool metric, uint32_t seed)
{
  EngineConfig c;
  c.rng = SeededRng(seed);
  Navigator nav(c);
  nav.SetSaidLog(SaidLog::Parse("quiz.question\tmodelT\n"), {});
  nav.SetCountry(country);
  if (metric)
    nav.SetUnits(Units::Kmh);
  nav.SetPopQuizOn(true);
  double t = kQcDayMs;
  double dist = 80000;
  for (int i = 0; i < 30; ++i, t += 1000, dist -= 25)
    nav.OnFix(QcFix(t), QcRoute(dist));
  nav.TakeLines();
  nav.OnDriverSaid(t, "give me a pop quiz");
  auto const card = nav.PopQuizCard();
  if (!card)
    return nullptr;
  for (auto const & q : QuizQuestions())
    if (card->question == q.question && card->choices[0] == QuizChoiceText(q.choices[0]))
      return &q;
  return nullptr;
}

std::set<std::string> QcAskMany(std::string const & country, bool metric, int n)
{
  std::set<std::string> ids;
  for (int i = 0; i < n; ++i)
  {
    auto const * q = QcAsk(country, metric, 1000 + static_cast<uint32_t>(i));
    CHECK(q != nullptr);
    if (q)
      ids.insert(q->id);
  }
  // The line filters are process-wide; leave them as the other tests expect.
  SetLineDriveSide(false);
  SetLineMetric(false);
  return ids;
}

std::set<std::string> const kNotInTheUk = {"leftLane",  "siren",     "fourWay",   "rightOnRed",
                                            "solidYellow", "orangeSign", "brownSign", "blueSign",
                                            "bac",       "schoolBus"};
}  // namespace

TEST(QuizCountryNamesMatchTheMapFiles)
{
  CHECK(InNorthAmerica(""));
  CHECK(InNorthAmerica("US"));
  CHECK(InNorthAmerica("US_Colorado_Denver"));
  CHECK(InNorthAmerica("Canada_Ontario"));
  CHECK(InNorthAmerica("United States"));
  CHECK(!InNorthAmerica("UK_England"));
  CHECK(!InNorthAmerica("Germany"));
  CHECK(!InNorthAmerica("Mexico"));
  CHECK(!InNorthAmerica("Australia_Queensland"));
  CHECK(PlatesNameAState(""));
  CHECK(PlatesNameAState("US_Texas"));
  CHECK(PlatesNameAState("Canada"));
  CHECK(PlatesNameAState("Mexico"));
  CHECK(!PlatesNameAState("UK"));
  CHECK(!PlatesNameAState("Germany_Bavaria"));
}

TEST(QuizFitsTheCountryAndUnits)
{
  auto const & fourWay = *FindQuizQuestion("fourWay");
  auto const & leftLane = *FindQuizQuestion("leftLane");
  auto const & feet = *FindQuizQuestion("turnSignal");
  auto const & metres = *FindQuizQuestion("turnSignalMetric");
  auto const & fog = *FindQuizQuestion("fog");
  CHECK(QuizFits(fourWay, "", false));
  CHECK(QuizFits(fourWay, "Canada", true));
  CHECK(!QuizFits(fourWay, "Germany", true));
  CHECK(QuizFits(leftLane, "Germany", true));  // keep right, pass on the left
  CHECK(!QuizFits(leftLane, "UK", false));
  CHECK(!QuizFits(leftLane, "Australia", true));
  CHECK(QuizFits(feet, "US", false));
  CHECK(!QuizFits(feet, "US", true));
  CHECK(QuizFits(metres, "Canada", true));
  CHECK(!QuizFits(metres, "UK", false));  // the UK signs in miles and yards
  CHECK(QuizFits(fog, "UK", false));
  CHECK(QuizFits(fog, "Japan", true));
  // Both signal questions read their own distance.
  CHECK_EQUAL(QuizChoiceText(metres.choices[0]), std::string("About 30 metres"));
  CHECK_EQUAL(QuizChoiceText(feet.choices[0]), std::string("About 100 feet"));
}

TEST(QuizInTheUkSkipsUsRoadRules)
{
  auto const asked = QcAskMany("UK_England", false, 500);
  for (auto const & id : asked)
  {
    if (kNotInTheUk.count(id))
      std::printf("  UK asked %s\n", id.c_str());
    CHECK(kNotInTheUk.count(id) == 0);
    CHECK(id != "turnSignalMetric");
  }
  // Still road rules, not just trivia.
  int road = 0;
  for (auto const & id : asked)
    road += FindQuizQuestion(id)->road ? 1 : 0;
  CHECK(road >= 5);
}

TEST(QuizInTheUsStillAsksUsRoadRules)
{
  auto const asked = QcAskMany("US", false, 300);
  int us = 0;
  for (auto const & id : asked)
    us += kNotInTheUk.count(id) ? 1 : 0;
  CHECK(us >= 5);
  CHECK(asked.count("turnSignalMetric") == 0);
  // A metric driver in Canada signals in metres.
  auto const canada = QcAskMany("Canada", true, 300);
  CHECK(canada.count("turnSignal") == 0);
  CHECK(canada.count("rightOnRed") == 1);
}

namespace
{
LatLon const kQcHome{39.74, -104.99};
LatLon const kQcWork{39.75, -104.94};

/// A commute driven three times before, past the opening minute: the plate game's usual moment.
bool QcPlatesOffered(std::string const & country)
{
  std::vector<TripRecord> trips(3);
  for (auto & tr : trips)
    tr.routeKey = RouteKey(kQcHome, kQcWork);
  EngineConfig c;
  c.rng = SeededRng(4);
  Navigator nav(c, trips);
  nav.SetCountry(country);
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Left;
  r.destination = "Work";
  r.etaSeconds = 900;
  r.origin = kQcHome;
  r.destinationPoint = kQcWork;
  Fix f;
  f.lat = 39.745;
  f.lon = -104.96;
  f.speedMps = 13;
  bool offered = false;
  for (int i = 0; i < 30; ++i)
  {
    f.tMs = i * 10000.0;
    r.distToTurnM = 5000 - i * 10;
    nav.OnFix(f, r);
    for (auto const & l : nav.TakeLines())
      offered = offered || l.category == Category::PlateGame;
  }
  SetLineDriveSide(false);
  return offered;
}
}  // namespace

TEST(PlateGameOnlyWherePlatesNameAState)
{
  CHECK(QcPlatesOffered(""));
  CHECK(QcPlatesOffered("US"));
  CHECK(QcPlatesOffered("Canada_Ontario"));
  CHECK(QcPlatesOffered("Mexico"));
  CHECK(!QcPlatesOffered("UK_England"));
  CHECK(!QcPlatesOffered("Germany"));
}
