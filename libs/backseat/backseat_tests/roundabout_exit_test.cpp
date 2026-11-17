// Roundabout calls say which exit (loop B98, BI-084). "Take the roundabout onto Oak Street" left the
// driver counting exits on a guess; Google and Apple say "take the second exit". When the host doesn't
// know the exit the old call stays, and it never says "the  exit" or "0th exit" (a known Waze bug).
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/navigator.hpp"
#include "backseat/running_bits.hpp"

#include <string>

using namespace backseat;

namespace
{
RouteSnapshot Round(int exit, std::string street = "Oak Street", RoundaboutLeg leg = RoundaboutLeg::Enter)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Roundabout;
  r.distToTurnM = 300;
  r.nextStreet = std::move(street);
  r.roundaboutExit = exit;
  r.roundaboutLeg = leg;
  return r;
}
}  // namespace

TEST(RoundaboutExitWordsSayOrdinalsThenNumbers)
{
  CHECK_EQUAL(RoundaboutExitWords(1), std::string("the first exit"));
  CHECK_EQUAL(RoundaboutExitWords(3), std::string("the third exit"));
  CHECK_EQUAL(RoundaboutExitWords(8), std::string("the eighth exit"));
  CHECK_EQUAL(RoundaboutExitWords(9), std::string("exit 9"));
  CHECK_EQUAL(RoundaboutExitWords(0), std::string());
  CHECK_EQUAL(RoundaboutExitWords(-2), std::string());
}

TEST(RoundaboutCallNamesTheExit)
{
  CHECK_EQUAL(InstructionText(Round(2)), std::string("At the roundabout, take the second exit onto Oak Street"));
  CHECK_EQUAL(InstructionText(Round(1, "")), std::string("At the roundabout, take the first exit"));
  CHECK_EQUAL(InstructionText(Round(12)), std::string("At the roundabout, take exit 12 onto Oak Street"));
}

TEST(UnknownRoundaboutExitKeepsTheOldCall)
{
  CHECK_EQUAL(InstructionText(Round(0)), std::string("Take the roundabout onto Oak Street"));
  CHECK_EQUAL(InstructionText(Round(0, "")), std::string("Take the roundabout"));
  for (int exit : {0, -1})
  {
    std::string const text = InstructionText(Round(exit));
    CHECK(text.find("the  exit") == std::string::npos);
    CHECK(text.find("0th") == std::string::npos);
    CHECK(text.find("exit") == std::string::npos);
  }
}

TEST(LeavingTheRoundaboutDoesNotSayTakeItAgain)
{
  CHECK_EQUAL(InstructionText(Round(2, "Oak Street", RoundaboutLeg::Leave)),
              std::string("Exit the roundabout onto Oak Street"));
  CHECK_EQUAL(InstructionText(Round(0, "", RoundaboutLeg::Leave)), std::string("Exit the roundabout"));
  CHECK_EQUAL(InstructionText(Round(0, "Oak Street", RoundaboutLeg::Stay)), std::string("Stay on the roundabout"));
  // Still a roundabout to the countdown (not a highway exit with a side).
  CHECK(TurnShapeOf(InstructionText(Round(2, "Oak Street", RoundaboutLeg::Leave))) == TurnShape::Roundabout);
}

TEST(ExitCallStaysARoundaboutAndKeepsItsStreet)
{
  std::string const text = InstructionText(Round(3));
  CHECK(TurnShapeOf(text) == TurnShape::Roundabout);
  CHECK_EQUAL(StreetOf(text), std::string("Oak Street"));
  CHECK_EQUAL(DirectionText(text, 150, Units::Mph),
              std::string("In 500 feet, at the roundabout, take the third exit onto Oak Street."));
}

TEST(ThenChainNamesTheRoundaboutExit)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Left;
  r.nextStreet = "Elm Street";
  r.thenTurn = TurnKind::Roundabout;
  r.thenStreet = "Main Street";
  r.thenGapM = 100;
  r.thenRoundaboutExit = 3;
  CHECK_EQUAL(InstructionText(r),
              std::string("Turn left onto Elm Street, then at the roundabout take the third exit onto Main Street"));
  r.thenRoundaboutExit = 0;
  CHECK_EQUAL(InstructionText(r), std::string("Turn left onto Elm Street, then take the roundabout onto Main Street"));
  // A roundabout with a known exit chains its own next turn the same way.
  auto round = Round(2);
  round.thenTurn = TurnKind::Right;
  round.thenStreet = "Pine Lane";
  round.thenGapM = 80;
  CHECK_EQUAL(InstructionText(round),
              std::string("At the roundabout, take the second exit onto Oak Street, then right onto Pine Lane"));
}
