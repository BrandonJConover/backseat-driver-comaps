// Pop quizzes (Brandon, 2026-10-31). Now and then on a quiet stretch the GPS springs a multiple
// choice question: driver's ed and road rules (it's a driving app, judging your driving) or
// general trivia. The question and its three choices show on screen, but on a drive the answer has
// to be said out loud: "We can't wreck because you tried to
// touch your phone screen." A right answer
// earns a brownie point and gets the driver called a brown noser or a teacher's pet; a wrong one
// gets mocked. All offline: the questions are hardcoded here.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace backseat
{
struct QuizQuestion
{
  /// Stable id, for "not again so soon" across trips.
  char const * id;
  /// Road rules and driver's ed: only asked on car trips, and missing one is a bigger deal.
  bool road;
  /// As read out and shown, ending in a question mark.
  char const * question;
  /// Each choice as shown, then other ways to say it, split by '|': "8|eight".
  std::array<char const *, 3> choices;
  /// Index of the right choice.
  int answer;
};

/// Every question. The first is the first one ever asked.
std::vector<QuizQuestion> const & QuizQuestions();
QuizQuestion const * FindQuizQuestion(std::string const & id);
/// A choice as shown and said, without its other spellings.
std::string QuizChoiceText(char const * choice);

/// What the driver said, as the quiz reads it.
struct QuizHeard
{
  enum class Kind
  {
    Choice,
    /// "I don't know", "pass", "no idea".
    GiveUp,
    /// Nothing that picks one choice: the radio, a sneeze, "uh".
    Unclear
  };
  Kind kind = Kind::Unclear;
  int choice = -1;
};
/// The choice named by its words ("octagon"), its letter ("B", "bee", "option c") or its place
/// ("the second one"). Words win: "three" answers "3 seconds" before it means "C".
QuizHeard ParseQuizAnswer(QuizQuestion const & q, std::string const & spoken);

/// The driver asked for one: "give me a pop quiz", "quiz me".
bool AsksForPopQuiz(std::string const & spoken);

/// One quiz: asked, then answered (or not) once.
class PopQuiz
{
public:
  enum class Outcome
  {
    Right,
    Wrong,
    GaveUp,
    TimedOut
  };

  /// `answerBy`: when no answer counts as not knowing (the question still has to be read first).
  /// `tappable`: the card takes taps (not on a drive).
  void Start(QuizQuestion const & q, double t, double answerBy, bool tappable);
  /// Forgets the quiz, card and all.
  void End() { m_question = nullptr; }

  /// A question is waiting for its answer.
  bool Asking() const { return m_question && !m_outcome; }
  /// Anything to show: the question while it's asked, and the result for a little while after.
  bool Showing(double t) const { return m_question && (!m_outcome || t - m_answeredT < kResultShownMs); }
  QuizQuestion const * Question() const { return m_question; }
  bool Tappable() const { return m_tappable; }
  /// The choice the driver went with, -1 for none (yet).
  int Picked() const { return m_picked; }
  std::optional<Outcome> GetOutcome() const { return m_outcome; }
  double StartedT() const { return m_startedT; }

  /// The driver answered with `choice`. Empty if no question is waiting.
  std::optional<Outcome> Answer(int choice, double t);
  /// "I don't know."
  std::optional<Outcome> GiveUp(double t);
  /// Out of time. Empty until then.
  std::optional<Outcome> Tick(double t);

  /// After an answer the card shows the result this long.
  static double constexpr kResultShownMs = 8000;

private:
  std::optional<Outcome> Finish(Outcome outcome, double t);

  QuizQuestion const * m_question = nullptr;
  double m_startedT = 0;
  double m_answerBy = 0;
  double m_answeredT = 0;
  bool m_tappable = false;
  int m_picked = -1;
  std::optional<Outcome> m_outcome;
};
}  // namespace backseat
