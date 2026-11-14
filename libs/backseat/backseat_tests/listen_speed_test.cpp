// Listening speed guard (loop B137). Everything the driver says runs through the voice readers below,
// on the phone, while the GPS waits to answer. Today a whole pass takes about 0.3 ms per utterance;
// the 20 ms cap is loose enough that an unoptimised or sanitized build never flakes, but it catches a
// regex (or a big table) rebuilt on every call, which costs tens of milliseconds per utterance.
#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/plate_game.hpp"
#include "backseat/pop_quiz.hpp"
#include "backseat/twenty_questions.hpp"
#include "backseat/voice_command.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
std::vector<std::string> const & Utterances()
{
  static std::vector<std::string> const said = {
      // App commands.
      "what's the next turn",
      "mute the voice for a while",
      "find a gas station",
      "shut up and drive",
      // Yes / no.
      "yes",
      "nope, not now",
      "yeah sure why not",
      // 20 Questions: questions, guesses, giving up.
      "is it bigger than a car",
      "can you use it on foot",
      "is it a deer",
      "is it a giraffe",
      "I give up",
      // Plate game.
      "I just saw a Texas plate",
      "no Ohio plates yet",
      // Quiz answers.
      "the octagon",
      "option b",
      "the second one",
      "I don't know",
      // Chatter and noise.
      "turn the radio up a bit, this song is great and I want to hear the chorus again before we get there",
      "uh",
  };
  return said;
}

size_t RunAll(QuizQuestion const & q, std::string const & s)
{
  size_t n = ParseAppCommands(s).size();
  n += static_cast<size_t>(ParseYesNo(s));
  n += static_cast<size_t>(ParseTwentyHeard(s).kind);
  n += MatchTwentyThing(s).value_or(0);
  n += MatchPlateRegion(s).value_or(std::string()).size();
  n += DriveComesFirst(s) ? 1 : 0;
  n += static_cast<size_t>(ParseQuizAnswer(q, s).kind);
  return n;
}
}  // namespace

TEST(ListeningEveryUtteranceIsFast)
{
  CHECK(!QuizQuestions().empty());
  QuizQuestion const & q = QuizQuestions().front();
  auto const & said = Utterances();
  CHECK(said.size() >= 20);

  // Warm-up: first calls build tables and lexicons once; that cost belongs to Warm(), not to listening.
  volatile size_t sink = 0;
  for (auto const & s : said)
    sink = sink + RunAll(q, s);

  double worstMs = 0;
  std::string worst;
  for (auto const & s : said)
  {
    // Best of three, so a scheduler hiccup on a busy test machine isn't read as slow code.
    double bestMs = 1e9;
    for (int rep = 0; rep < 3; ++rep)
    {
      auto const start = std::chrono::steady_clock::now();
      sink = sink + RunAll(q, s);
      double const ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      bestMs = ms < bestMs ? ms : bestMs;
    }
    if (bestMs > worstMs)
    {
      worstMs = bestMs;
      worst = s;
    }
  }
  std::printf("    listen speed: worst %.3f ms (\"%s\")\n", worstMs, worst.c_str());
  if (worstMs >= 20.0)
    throw ::backseat_tests::Failure{"listening took " + std::to_string(worstMs) + " ms for \"" + worst + "\""};
  (void)sink;
}

TEST(ListeningSpeedUtterancesStillReadRight)
{
  // The guard only means something if these readers still understand the words it times.
  CHECK(ParseYesNo("yes") == YesNo::Yes);
  CHECK(ParseYesNo("nope, not now") == YesNo::No);
  CHECK(ParseTwentyHeard("I give up").kind == TwentyHeard::Kind::GiveUp);
  CHECK(ParseTwentyHeard("is it a deer").kind == TwentyHeard::Kind::Guess);
  CHECK(MatchPlateRegion("I just saw a Texas plate").has_value());
  CHECK(ParseQuizAnswer(QuizQuestions().front(), "I don't know").kind == QuizHeard::Kind::GiveUp);
}
