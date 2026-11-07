// "Repeat that" near a turn says the direction first and the joke after; far from a turn the whole
// pool still rotates (loop B58, idea BI-053).
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>

using namespace backseat;
using namespace backseat_tests;

namespace
{
std::string const kInstruction = "Turn left onto Elm Street";

RouteSnapshot Leg(double dist)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Left;
  r.distToTurnM = dist;
  r.nextStreet = "Elm Street";
  r.destination = "Grandma's";
  r.etaSeconds = 600;
  r.plannedTurns = {TurnKind::Left, TurnKind::Destination};
  return r;
}

Fix At(double tMs, double mps)
{
  Fix f;
  f.tMs = tMs;
  f.lat = 39.7;
  f.lon = -105;
  f.speedMps = mps;
  f.accuracyM = 5;
  return f;
}

std::string Lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

/// Words spoken before the instruction in a filled reply; -1 if it's missing.
int WordsBefore(std::string const & reply)
{
  auto const at = Lower(reply).find(Lower(kInstruction));
  if (at == std::string::npos)
    return -1;
  int words = 0;
  bool inWord = false;
  for (size_t i = 0; i < at; ++i)
  {
    bool const space = reply[i] == ' ';
    if (!space && !inWord)
      ++words;
    inWord = !space;
  }
  return words;
}
}  // namespace

TEST(RepeatLinesHaveInstructionFirstChoices)
{
  for (int tier = 0; tier < 3; ++tier)
  {
    int first = 0;
    for (auto const & l : LinesFor("cmd.repeat", tier))
      first += Navigator::InstructionComesFirst(l) ? 1 : 0;
    CHECK(first >= 2);
  }
  CHECK(Navigator::InstructionComesFirst("{instruction} Again."));
  CHECK(Navigator::InstructionComesFirst("AGAIN?! OKAY! {INSTRUCTION}"));
  CHECK(Navigator::InstructionComesFirst("Ah, the replay. {instruction}"));
  CHECK(!Navigator::InstructionComesFirst("Okay, slower this time. {instruction}"));
  CHECK(!Navigator::InstructionComesFirst("No placeholder at all."));
}

TEST(RepeatNearTurnSaysDirectionFirst)
{
  EngineConfig c;
  c.rng = SeededRng(7);
  Navigator nav(c);
  nav.OnFix(At(0, 13), Leg(350));
  nav.TakeLines();
  std::set<std::string> said;
  for (int i = 0; i < 200; ++i)
  {
    auto const reply = nav.Reply("cmd.repeat", {{"instruction", kInstruction}});
    int const w = WordsBefore(reply);
    CHECK(w >= 0 && w <= Navigator::kRepeatFirstMaxWords);
    said.insert(reply);
  }
  // Still rotates among the short ones.
  CHECK(said.size() >= 2);
}

TEST(RepeatNearTurnScalesWithSpeed)
{
  // 30 m/s (highway): 20 s is 600 m, so 550 m out is near.
  EngineConfig c;
  c.rng = SeededRng(3);
  Navigator nav(c);
  nav.OnFix(At(0, 30), Leg(550));
  nav.TakeLines();
  for (int i = 0; i < 50; ++i)
  {
    int const w = WordsBefore(nav.Reply("cmd.repeat", {{"instruction", kInstruction}}));
    CHECK(w >= 0 && w <= Navigator::kRepeatFirstMaxWords);
  }
}

TEST(RepeatFarFromTurnUsesWholePool)
{
  EngineConfig c;
  c.rng = SeededRng(11);
  Navigator nav(c);
  nav.OnFix(At(0, 13), Leg(2500));
  nav.TakeLines();
  int longLead = 0;
  for (int i = 0; i < 200; ++i)
  {
    int const w = WordsBefore(nav.Reply("cmd.repeat", {{"instruction", kInstruction}}));
    CHECK(w >= 0);
    longLead += w > Navigator::kRepeatFirstMaxWords ? 1 : 0;
  }
  CHECK(longLead > 0);
  // No trip at all: whole pool too.
  Navigator idle(c);
  CHECK(!idle.Reply("cmd.repeat", {{"instruction", kInstruction}}).empty());
}
