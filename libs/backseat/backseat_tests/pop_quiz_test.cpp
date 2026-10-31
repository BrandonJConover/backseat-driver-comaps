#include "check.hpp"

#include "backseat/achievements.hpp"
#include "backseat/engine.hpp"
#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"
#include "backseat/pop_quiz.hpp"

#include <algorithm>

using namespace backseat;

namespace
{
QuizQuestion const & PqQuestion(char const * id)
{
  auto const * q = FindQuizQuestion(id);
  if (!q)
    throw std::runtime_error(std::string("no question ") + id);
  return *q;
}

int PqPick(char const * id, std::string const & said)
{
  auto const heard = ParseQuizAnswer(PqQuestion(id), said);
  return heard.kind == QuizHeard::Kind::Choice ? heard.choice : -1;
}

// 2026-10-28 19:00 UTC: 1 pm in Denver.
double constexpr kPqDayMs = 1793156400000.0 + 16 * 3600000.0;

RouteSnapshot PqRoute(uint32_t turnId, double dist)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = turnId;
  r.turn = TurnKind::Right;
  r.nextStreet = "Elm Street";
  r.distToTurnM = dist;
  r.destination = "Grandma's";
  r.etaSeconds = 3600;
  r.plannedTurns = {TurnKind::Right, TurnKind::Destination};
  return r;
}

Fix PqFix(double tMs, double speed)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.74;
  f.lon = -104.99;
  f.speedMps = speed;
  f.bearingDeg = 90;
  return f;
}

/// A navigator whose quiet-stretch bits have all had their first time, with `said` already in its log.
Navigator PqNavigator(std::string const & said = {}, uint32_t seed = 11)
{
  EngineConfig c;
  c.rng = SeededRng(seed);
  Navigator nav(c);
  nav.SetDriverName("Brandon");
  nav.SetDriverPronouns(Pronouns::He);
  nav.SetSaidLog(SaidLog::Parse(said), {});
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

/// One fix a second along a long straight toward maneuver 1, `speed` m/s.
std::vector<Commentary> PqDrive(Navigator & nav, double & t, int seconds, double & dist, double speed = 25)
{
  std::vector<Commentary> said;
  for (int i = 0; i < seconds; ++i, t += 1000, dist -= speed)
  {
    nav.OnFix(PqFix(t, speed), PqRoute(1, dist));
    auto lines = nav.TakeLines();
    said.insert(said.end(), lines.begin(), lines.end());
  }
  return said;
}

std::vector<Commentary> PqQuiz(std::vector<Commentary> const & lines)
{
  std::vector<Commentary> out;
  for (auto const & l : lines)
    if (l.category == Category::PopQuiz)
      out.push_back(l);
  return out;
}

/// Drives until the quiz question has been read. Throws if none comes in `maxSeconds`.
std::vector<Commentary> PqUntilAsked(Navigator & nav, double & t, double & dist, int maxSeconds = 1800,
                                     double speed = 25)
{
  for (int i = 0; i < maxSeconds; ++i)
  {
    auto const quiz = PqQuiz(PqDrive(nav, t, 1, dist, speed));
    if (!quiz.empty())
      return quiz;
  }
  throw std::runtime_error("no pop quiz");
}

bool PqHas(std::string const & text, char const * part) { return text.find(part) != std::string::npos; }
}  // namespace

TEST(PopQuizParsesAnswers)
{
  CHECK_EQUAL(PqPick("stopSides", "eight"), 1);
  CHECK_EQUAL(PqPick("stopSides", "8"), 1);
  CHECK_EQUAL(PqPick("stopSides", "it's an octagon"), 1);
  CHECK_EQUAL(PqPick("stopSides", "B"), 1);
  CHECK_EQUAL(PqPick("stopSides", "C."), 2);
  CHECK_EQUAL(PqPick("stopSides", "the first one"), 0);
  CHECK_EQUAL(PqPick("yellowLight", "stop if you can"), 1);
  CHECK_EQUAL(PqPick("yellowLight", "floor it obviously"), 0);
  CHECK_EQUAL(PqPick("yellowLight", "I'll go with b"), 1);
  CHECK_EQUAL(PqPick("yellowLight", "option c"), 2);
  CHECK_EQUAL(PqPick("yellowLight", "bee"), 1);
  CHECK_EQUAL(PqPick("yellowLight", "see"), 2);
  CHECK_EQUAL(PqPick("followDistance", "three"), 1);
  CHECK_EQUAL(PqPick("followDistance", "three seconds"), 1);
  CHECK_EQUAL(PqPick("followDistance", "the second one"), 1);
  CHECK_EQUAL(PqPick("canberra", "Canberra"), 2);
  CHECK_EQUAL(PqPick("canberra", "I think it's Sydney"), 0);
  CHECK_EQUAL(PqPick("bac", "point oh eight"), 1);
  CHECK_EQUAL(PqPick("bac", "0.08"), 1);
  CHECK_EQUAL(PqPick("rhodeIsland", "Rhode Island"), 1);
  CHECK_EQUAL(PqPick("fourWay", "the one on the right"), 0);
  CHECK_EQUAL(PqPick("leftSide", "england"), 2);
  // Nothing that picks one: chatter, or two choices at once.
  CHECK_EQUAL(PqPick("canberra", "turn the radio up"), -1);
  CHECK_EQUAL(PqPick("canberra", "Sydney or Melbourne"), -1);
  CHECK_EQUAL(PqPick("canberra", "a or b"), -1);
  CHECK(ParseQuizAnswer(PqQuestion("canberra"), "I don't know").kind == QuizHeard::Kind::GiveUp);
  CHECK(ParseQuizAnswer(PqQuestion("canberra"), "no idea").kind == QuizHeard::Kind::GiveUp);
  CHECK(ParseQuizAnswer(PqQuestion("canberra"), "").kind == QuizHeard::Kind::Unclear);
}

TEST(PopQuizEveryQuestionAnswersToItsOwnWords)
{
  std::set<std::string> ids;
  for (auto const & q : QuizQuestions())
  {
    CHECK(ids.insert(q.id).second);
    CHECK(q.answer >= 0 && q.answer <= 2);
    CHECK(std::string(q.question).back() == '?');
    for (int i = 0; i < 3; ++i)
    {
      auto const heard = ParseQuizAnswer(q, QuizChoiceText(q.choices[i]));
      if (heard.choice != i)
        std::cerr << "  " << q.id << " choice " << i << " heard as " << heard.choice << "\n";
      CHECK_EQUAL(heard.choice, i);
      CHECK_EQUAL(ParseQuizAnswer(q, std::string(1, static_cast<char>('A' + i))).choice, i);
    }
  }
  CHECK(QuizQuestions().size() >= 60);
  CHECK(std::count_if(QuizQuestions().begin(), QuizQuestions().end(), [](auto const & q) { return q.road; }) >= 25);
}

TEST(PopQuizLinesAreFilled)
{
  Vars const vars = {{"question", "Q?"}, {"a", "x"}, {"b", "y"}, {"c", "z"}, {"answer", "x"},
                     {"brownies", "2 brownie points"}, {"name", "Brandon"}};
  for (char const * key : {"quiz.open", "quiz.asked", "quiz.aloud", "quiz.aloud.walk", "quiz.ask", "quiz.right.first",
                           "quiz.right", "quiz.wrong", "quiz.wrong.road", "quiz.timeout", "quiz.giveup", "quiz.unclear",
                           "badge.teacherspet"})
    for (int tier = 0; tier < 3; ++tier)
    {
      auto const & lines = LinesFor(key, tier);
      CHECK(!lines.empty());
      for (auto const & l : lines)
        CHECK(Fill(l, vars).find('{') == std::string::npos);
    }
  // Brandon's words for a right answer.
  for (auto const & l : LinesFor("quiz.right", 0))
    CHECK(PqHas(l, "brown") || PqHas(l, "teacher's pet") || PqHas(l, "Teacher's pet") || PqHas(l, "sucking up"));
  CHECK(PqHas(LinesFor("quiz.aloud", 0)[0], "tried to touch your phone screen"));
  CHECK(FindBadge("teacherspet") != nullptr);
}

TEST(PopQuizIsOffUntilTheHostListens)
{
  Navigator nav = PqNavigator();
  double t = kPqDayMs;
  double dist = 80000;
  CHECK(PqQuiz(PqDrive(nav, t, 1500, dist)).empty());
  CHECK(!nav.PopQuizCard());
}

TEST(PopQuizRightAnswerEarnsABrowniePoint)
{
  Navigator nav = PqNavigator();
  nav.SetPopQuizOn(true);
  double t = kPqDayMs;
  double dist = 80000;
  auto const asked = PqUntilAsked(nav, t, dist);
  // Pop quiz, the out-loud rule, then the question; the host listens after the last one.
  CHECK_EQUAL(asked.size(), size_t{3});
  CHECK_EQUAL(asked[0].key, std::string("quiz.open"));
  CHECK_EQUAL(asked[1].key, std::string("quiz.aloud"));
  CHECK_EQUAL(asked[2].key, std::string("quiz.ask"));
  // The first quiz ever is the yellow light.
  CHECK(PqHas(asked[2].text, "yellow light"));
  CHECK(PqHas(asked[2].text, "Stop if you safely can"));
  CHECK(t - kPqDayMs >= 5 * 60000);
  CHECK(nav.InGame());
  auto card = nav.PopQuizCard();
  CHECK(card.has_value());
  CHECK_EQUAL(card->correct, -1);
  CHECK(!card->tappable);
  CHECK_EQUAL(card->choices[1], std::string("Stop if you safely can"));
  // Taps don't count on a drive.
  nav.QuizTapped(t, 1);
  CHECK(nav.InGame());

  nav.OnDriverSaid(t, "B");
  auto const reply = PqQuiz(nav.TakeLines());
  CHECK_EQUAL(reply.size(), size_t{1});
  CHECK_EQUAL(reply[0].key, std::string("quiz.right.first"));
  CHECK(PqHas(reply[0].text, "brown noser") || PqHas(reply[0].text, "teacher's pet") ||
        PqHas(reply[0].text, "Brown noser"));
  CHECK_EQUAL(nav.BrowniePoints(), 1);
  CHECK(!nav.InGame());
  card = nav.PopQuizCard();
  CHECK(card.has_value());
  CHECK_EQUAL(card->correct, 1);
  CHECK_EQUAL(card->picked, 1);
  CHECK_EQUAL(card->brownies, 1);
  // The result stays up a moment, then the card goes.
  PqDrive(nav, t, 10, dist);
  CHECK(!nav.PopQuizCard());
}

TEST(PopQuizWrongRoadAnswerGetsMocked)
{
  Navigator nav = PqNavigator();
  nav.SetPopQuizOn(true);
  double t = kPqDayMs;
  double dist = 80000;
  PqUntilAsked(nav, t, dist);
  nav.OnDriverSaid(t, "honk at it");
  auto const reply = PqQuiz(nav.TakeLines());
  CHECK_EQUAL(reply.size(), size_t{1});
  CHECK_EQUAL(reply[0].key, std::string("quiz.wrong.road"));
  CHECK(PqHas(reply[0].text, "Stop if you safely can"));
  CHECK_EQUAL(nav.BrowniePoints(), 0);
}

TEST(PopQuizChatterGetsOneNudgeThenTimesOut)
{
  Navigator nav = PqNavigator();
  nav.SetPopQuizOn(true);
  double t = kPqDayMs;
  double dist = 80000;
  PqUntilAsked(nav, t, dist);
  nav.OnDriverSaid(t, "turn the radio up");
  auto nudge = PqQuiz(nav.TakeLines());
  CHECK_EQUAL(nudge.size(), size_t{1});
  CHECK_EQUAL(nudge[0].key, std::string("quiz.unclear"));
  nav.OnDriverSaid(t, "what");
  CHECK(PqQuiz(nav.TakeLines()).empty());
  auto const later = PqQuiz(PqDrive(nav, t, 60, dist));
  CHECK_EQUAL(later.size(), size_t{1});
  CHECK_EQUAL(later[0].key, std::string("quiz.timeout"));
  CHECK(!nav.InGame());
}

TEST(PopQuizAtMostTwoATrip)
{
  Navigator nav = PqNavigator();
  nav.SetPopQuizOn(true);
  double t = kPqDayMs;
  double dist = 200000;
  int asked = 0;
  for (auto const & l : PqQuiz(PqDrive(nav, t, 7200, dist)))
    asked += l.key == "quiz.ask";
  CHECK(asked >= 1);
  CHECK(asked <= 2);
}

TEST(PopQuizOnAWalkIsTriviaAndTappable)
{
  // Lines follow the travel mode for the whole process; back to driving for the other tests.
  struct DriveAgain
  {
    ~DriveAgain() { SetLineTravelMode(TravelMode::Drive); }
  } driveAgain;
  Navigator nav = PqNavigator();
  nav.SetPopQuizOn(true);
  nav.SetTravelMode(TravelMode::Walk);
  double t = kPqDayMs;
  double dist = 30000;
  auto const asked = PqUntilAsked(nav, t, dist, 3600, 1.4);
  CHECK_EQUAL(asked[1].key, std::string("quiz.aloud.walk"));
  auto const card = nav.PopQuizCard();
  CHECK(card.has_value());
  CHECK(card->tappable);
  // No road rules on foot: the first walk quiz is the first trivia question.
  CHECK(!PqHas(asked[2].text, "yellow light"));
  nav.QuizTapped(t, 0);
  auto const reply = PqQuiz(nav.TakeLines());
  CHECK_EQUAL(reply.size(), size_t{1});
  CHECK(!nav.InGame());
}

TEST(PopQuizWhenAsked)
{
  Navigator nav = PqNavigator();
  nav.SetPopQuizOn(true);
  double t = kPqDayMs;
  double dist = 80000;
  PqDrive(nav, t, 30, dist);
  nav.OnDriverSaid(t, "give me a pop quiz");
  auto const asked = PqQuiz(nav.TakeLines());
  CHECK_EQUAL(asked.size(), size_t{3});
  CHECK_EQUAL(asked[0].key, std::string("quiz.asked"));
  CHECK(nav.InGame());
}

TEST(PopQuizTenBrowniesMakeATeachersPet)
{
  Navigator nav = PqNavigator("_quiz.brownies\t9\n");
  nav.SetPopQuizOn(true);
  CHECK_EQUAL(nav.BrowniePoints(), 9);
  double t = kPqDayMs;
  double dist = 80000;
  PqDrive(nav, t, 30, dist);
  nav.OnDriverSaid(t, "quiz me");
  nav.TakeLines();
  auto const card = nav.PopQuizCard();
  CHECK(card.has_value());
  auto const & all = QuizQuestions();
  auto const q = std::find_if(all.begin(), all.end(), [&](auto const & q) { return card->question == q.question; });
  CHECK(q != all.end());
  nav.OnDriverSaid(t, std::string(1, static_cast<char>('A' + q->answer)));
  auto const reply = PqQuiz(nav.TakeLines());
  CHECK_EQUAL(nav.BrowniePoints(), 10);
  CHECK_EQUAL(reply.size(), size_t{2});
  CHECK_EQUAL(reply[0].key, std::string("quiz.right"));
  CHECK(PqHas(reply[0].text, "10") || !PqHas(reply[0].text, "brownie points"));
  CHECK_EQUAL(reply[1].key, std::string("badge.teacherspet"));
  CHECK(nav.GetAchievements().Has("teacherspet"));
}
