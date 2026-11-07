// Hostile talk mid-game: the plate game, 20 Questions and the pop quiz keep state between
// utterances, so the stateless soak (hostile_text_test.cpp) can't reach their later branches
// (chatter windows, negation spans, correction cues, nudges, wrap-ups). Feed each live game a few
// hundred garbage utterances and check it never throws, never speaks a raw {placeholder} and
// keeps every line short. Loop fallback:security.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/navigator.hpp"
#include "backseat/plate_game.hpp"
#include "backseat/twenty_questions.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
struct GameRng
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

// Game words, negations, corrections, nav questions and broken bytes, glued at random.
std::vector<std::string> const kTalk = {
  " ", "  ", ",", ".", "?", "!", "'", "’", "-", "\"", "{", "}", "{thing}", "{count}", "\t", "\n",
  "is it", "is it not", "isn't it", "does it", "can you", "a ", "an ", "the ", "alive", "bigger than",
  "a car", "made of", "small enough", "I give up", "give me a hint", "just tell me", "what is it",
  "I'm done", "game over", "stop playing", "no more plates", "enough", "Texas", "Ohio", "no Texas",
  "haven't seen", "never seen", "saw a", "plate", "license", "Ontario", "Quebec", "D.C.", "a", "b",
  "c", "A", "B", "C", "not B", "no wait", "I mean", "actually", "scratch that", "option ", "1", "2",
  "3", "the first one", "I don't know", "yes", "no", "maybe", "what's the next turn", "cancel the route",
  "take me home", "police", "thanks", "let's play twenty questions", "quiz me", "Omnipotent Overseer",
  "\xC3", "\xE2\x80", "\xF0\x9F", "\x80", "\xFF", "\xED\xA0\x80", "😀", "улица", "ありがとう",
};

std::string Utterance(GameRng & r)
{
  std::string s;
  size_t const n = 1 + r.Below(10);
  for (size_t i = 0; i < n; ++i)
  {
    if (r.Below(10) == 0)
      s.push_back(static_cast<char>(r.Below(256)));
    else
      s += kTalk[r.Below(kTalk.size())];
    if (r.Below(3) == 0)
      s += ' ';
  }
  return s;
}

// What the GPS says back must be a finished sentence of a sane length.
bool SaneLine(std::string const & text)
{
  return text.find('{') == std::string::npos && text.find('}') == std::string::npos && text.size() < 600;
}

void CheckSane(std::string const & text, char const * where, std::string const & said)
{
  if (!SaneLine(text))
    std::fprintf(stderr, "  %s said \"%s\" after \"%s\"\n", where, text.c_str(), said.c_str());
  CHECK(SaneLine(text));
}

double constexpr kDayMs = 1793156400000.0 + 16 * 3600000.0;

RouteSnapshot LongRoute(double dist)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 7;
  r.turn = TurnKind::Right;
  r.nextStreet = "Elm Street";
  r.distToTurnM = dist;
  r.destination = "Grandma's";
  r.etaSeconds = 3600;
  r.plannedTurns = {TurnKind::Right, TurnKind::Destination};
  return r;
}

Fix FixAt(double tMs, double speedMps)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.74;
  f.lon = -104.99;
  f.speedMps = speedMps;
  f.bearingDeg = 90;
  return f;
}
}  // namespace

TEST(HostileTalkMidPlateGame)
{
  for (uint32_t seed = 1; seed <= 4; ++seed)
  {
    PlateGame game(SeededRng(seed));
    game.NewTrip("home->work");
    CheckSane(game.Offer(0).text, "plate offer", "");
    (void)game.OnDriverSaid("sure", 0, false, 0.0);
    GameRng r{0xC0FFEEULL * seed};
    double t = 0;
    for (int i = 0; i < 300; ++i)
    {
      t += 1000.0 * static_cast<double>(1 + r.Below(40));
      std::string const said = Utterance(r);
      std::optional<PlateGame::Reply> reply;
      try
      {
        // Half the time without a clock: the "every unmatched utterance" path.
        reply = r.Below(2) ? game.OnDriverSaid(said, static_cast<int>(r.Below(4)), r.Below(5) == 0, t)
                           : game.OnDriverSaid(said, static_cast<int>(r.Below(4)), false);
      }
      catch (...)
      {
        CHECK(!"the plate game threw on hostile talk");
      }
      if (reply)
        CheckSane(reply->text, "plate game", said);
    }
    if (auto const tally = game.WrapUp(0))
      CheckSane(tally->text, "plate tally", "");
  }
}

TEST(HostileTalkMid20Questions)
{
  for (uint32_t seed = 1; seed <= 6; ++seed)
  {
    TwentyQuestions game(SeededRng(seed));
    double t = 0;
    CheckSane(game.Start({}, seed % 2 == 0, t, 0).text, "20Q start", "");
    GameRng r{0xBADC0DEULL + seed};
    for (int i = 0; i < 300; ++i)
    {
      t += 1000.0 * static_cast<double>(1 + r.Below(90));
      std::string const said = Utterance(r);
      try
      {
        if (auto const reply = game.OnDriverSaid(said, t, static_cast<int>(r.Below(4))))
          CheckSane(reply->text, "20Q", said);
        if (auto const nudge = game.Tick(t, 0))
          CheckSane(nudge->text, "20Q tick", said);
      }
      catch (...)
      {
        CHECK(!"20 Questions threw on hostile talk");
      }
      // A game that ended (won, lost, quit, nudged out) starts over, so later branches get hit too.
      if (!game.Active())
        CheckSane(game.Start({}, false, t, 0).text, "20Q restart", said);
      CHECK(game.QuestionsLeft() >= 0);
    }
  }
}

TEST(HostileTalkThroughTheNavigatorGames)
{
  // The live path: the host asks QuizTakes/TwentyTakes, then hands the words to OnDriverSaid while
  // the car keeps moving. Games get started by name along the way.
  for (uint32_t seed = 1; seed <= 3; ++seed)
  {
    EngineConfig c;
    c.rng = SeededRng(seed);
    Navigator nav(c);
    nav.SetDriverName("Brandon");
    nav.SetPopQuizOn(true);
    double t = kDayMs;
    double dist = 200000;
    for (int i = 0; i < 30; ++i, t += 1000, dist -= 25)
      nav.OnFix(FixAt(t, 25), LongRoute(dist));
    (void)nav.TakeLines();
    GameRng r{0x5EEDULL * seed};
    int twentyLines = 0;
    int quizLines = 0;
    for (int i = 0; i < 400; ++i)
    {
      std::string said = Utterance(r);
      if (i % 60 == 0)
        said = i % 120 == 0 ? "let's play twenty questions" : "quiz me";
      try
      {
        (void)nav.QuizTakes(said);
        (void)nav.TwentyTakes(said);
        nav.OnDriverSaid(t, said);
        for (int k = 0; k < 3; ++k, t += 1000, dist -= 25)
          nav.OnFix(FixAt(t, r.Below(8) == 0 ? 0 : 25), LongRoute(std::max(dist, 3000.0)));
        for (auto const & line : nav.TakeLines())
        {
          CheckSane(line.text, "navigator", said);
          twentyLines += line.category == Category::TwentyQuestions;
          quizLines += line.category == Category::PopQuiz;
        }
      }
      catch (...)
      {
        CHECK(!"the navigator threw on hostile talk");
      }
    }
    // Both games really ran, so the garbage reached their mid-game branches.
    CHECK(twentyLines > 5);
    CHECK(quizLines > 2);
  }
}
