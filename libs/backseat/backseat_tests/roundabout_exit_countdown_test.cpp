// Roundabout countdowns name the exit too (loop B99, BI-084). B98 put "take the third exit" in the
// call, but the far/mention nags in between were jokes about the roundabout with no exit in them. Now
// the joke comes first and the exit follows as its own short sentence, so it is never buried.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"

#include <regex>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
CritiqueEngine FirstLines()
{
  EngineConfig c;
  c.rng = [] { return 0.0; };
  return CritiqueEngine(c, 0);
}

std::vector<std::string> Countdown(std::string const & text)
{
  auto e = FirstLines();
  e.OnNav(NavEvent::Instruction(-10000, text, 400));
  std::vector<std::string> said;
  double const ds[] = {300, 140, 70, 40, 3, 0, 0, 0};
  for (size_t i = 0; i < std::size(ds); ++i)
    for (auto const & c : e.OnNav(NavEvent::Progress(1000.0 * (i + 1) + (i >= 2 ? 10000 : 0), ds[i])))
      said.push_back(c.text);
  for (auto const & c : e.OnNav(NavEvent::TurnCompleted(20000, true)))
    said.push_back(c.text);
  return said;
}

bool EndsWith(std::string const & s, std::string const & tail)
{
  return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}
}  // namespace

TEST(RoundaboutExitOfReadsTheCall)
{
  CHECK_EQUAL(RoundaboutExitOf("At the roundabout, take the third exit onto Oak Street"), std::string("third"));
  CHECK_EQUAL(RoundaboutExitOf("At the roundabout, take the First exit"), std::string("first"));
  CHECK_EQUAL(RoundaboutExitOf("At the roundabout, take exit 12 onto Oak Street"), std::string("12"));
  CHECK_EQUAL(RoundaboutExitOf("Take the roundabout onto Oak Street"), std::string());
  CHECK_EQUAL(RoundaboutExitOf("Exit the roundabout onto Oak Street"), std::string());
  // A highway exit is not a roundabout exit; nor is a roundabout that only comes after "then".
  CHECK_EQUAL(RoundaboutExitOf("Take exit 12 onto I-25"), std::string());
  CHECK_EQUAL(RoundaboutExitOf("Turn left onto Elm Street, then at the roundabout take the third exit"),
              std::string());
}

TEST(ExitCallIsOneShortSentence)
{
  CHECK_EQUAL(ExitCallFor({{"exit", "third"}}, 0), std::string("Third exit."));
  CHECK_EQUAL(ExitCallFor({{"exit", "second"}}, 1), std::string("Second exit."));
  CHECK_EQUAL(ExitCallFor({{"exit", "third"}}, 2), std::string("THIRD EXIT!"));
  CHECK_EQUAL(ExitCallFor({{"exit", "12"}}, 0), std::string("Exit 12."));
  CHECK_EQUAL(ExitCallFor({{"exit", "12"}}, 2), std::string("EXIT 12!"));
  CHECK_EQUAL(ExitCallFor({{"exit", ""}}, 0), std::string());
  CHECK_EQUAL(ExitCallFor({{"dir", "left"}}, 2), std::string());
}

TEST(RoundaboutCountdownEndsWithTheExit)
{
  auto const said = Countdown("At the roundabout, take the third exit onto Oak Street");
  std::vector<std::string> const want = {
      "In 500 feet you are going to take the roundabout. Third exit.",
      "By the way, did I mention you should take the roundabout. Third exit.",
      "Hey, you should be taking the roundabout. Third exit.",
      "You should still be going around the roundabout.",
      "Please stop going around. You're out of the roundabout."};
  CHECK(said == want);
}

TEST(RoundaboutCountdownWithoutAnExitIsUnchanged)
{
  std::vector<std::string> const want = {
      "In 500 feet you are going to take the roundabout.",
      "By the way, did I mention you should take the roundabout.", "Hey, you should be taking the roundabout.",
      "You should still be going around the roundabout.", "Please stop going around. You're out of the roundabout."};
  CHECK(Countdown("Take the roundabout onto Oak Street") == want);
  CHECK(Countdown("Exit the roundabout onto Oak Street").front().find("exit.") == std::string::npos);
  // A plain turn chained to a roundabout keeps its own countdown.
  for (auto const & line : Countdown("Turn left onto Elm Street, then at the roundabout take the third exit"))
    CHECK(!EndsWith(line, "Third exit."));
}

TEST(NoRoundaboutCountdownLineNamesAnExitAlready)
{
  // The appended sentence must be the only exit number in the line, in every tier and drive side.
  std::regex const ordinal(R"(\b(first|second|third|fourth|fifth|sixth|seventh|eighth)\s+exit\b|\bexit\s+\d)",
                           std::regex::icase);
  for (char const * stage : {"far", "mention", "near", "now"})
  {
    std::string const key = std::string("countdown.roundabout.") + stage;
    CHECK(LineData().count(key) == 1);
    for (int tier = 0; tier < 3; ++tier)
      for (auto const & line : LinesFor(key, tier))
      {
        std::string const said = line + " " + ExitCallFor({{"exit", "second"}}, tier);
        CHECK(std::distance(std::sregex_iterator(said.begin(), said.end(), ordinal), std::sregex_iterator()) == 1);
      }
  }
}
