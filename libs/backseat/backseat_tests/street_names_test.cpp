// Spoken street names and highway exit numbers (BI street-names). The router's screen text for an exit
// is "42B: [I 35] > Dallas"; reading it lost the exit number whenever the sign also named a town, and
// an exit with only towns came out "Take the exit on the right onto toward Dallas". Now the adapter
// passes the sign's parts (RoadSign) and the call says "Take exit 42B on the right for I 35 North
// toward Dallas", with "Now, take exit 42B on the right." as the last call.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/navigator.hpp"
#include "backseat/pronunciation.hpp"

#include <string>

using namespace backseat;

namespace
{
RoadSign Exit(std::string junction, std::string destRef, std::string dest)
{
  RoadSign s;
  s.junctionRef = std::move(junction);
  s.destinationRef = std::move(destRef);
  s.destination = std::move(dest);
  s.isLink = true;
  return s;
}
}  // namespace

TEST(SpeakRoadPlainStreetIsItsName)
{
  RoadSign s;
  s.name = "Winesap Drive";
  s.ref = "CR 12";
  CHECK_EQUAL(SpeakRoad(s).street, std::string("Winesap Drive"));
  CHECK_EQUAL(SpeakRoad(s).exitRef, std::string());
  s.name.clear();
  CHECK_EQUAL(SpeakRoad(s).street, std::string("CR 12"));
  s.ref = "US 290;TX 71;TX 130";
  CHECK_EQUAL(SpeakRoad(s).street, std::string("US 290 and TX 71"));
}

TEST(SpeakRoadExitKeepsNumberRoadAndTowns)
{
  auto const r = SpeakRoad(Exit("42B", "I 35 North", "Dallas;Fort Worth;Waco"));
  CHECK_EQUAL(r.exitRef, std::string("42B"));
  CHECK_EQUAL(r.street, std::string("I 35 North toward Dallas and Fort Worth"));
  CHECK_EQUAL(SpeakRoad(Exit("42A;42B", "", "Dallas")).exitRef, std::string("42A"));
  CHECK_EQUAL(SpeakRoad(Exit("42 B", "", "")).exitRef, std::string("42B"));
  CHECK_EQUAL(SpeakRoad(Exit("", "", " Dallas ; Dallas ")).street, std::string("toward Dallas"));
}

TEST(SpeakRoadRampWithoutSignUsesRoadRefThenName)
{
  RoadSign s;
  s.isLink = true;
  s.ref = "I 25";
  s.name = "Valley Highway";
  CHECK_EQUAL(SpeakRoad(s).street, std::string("I 25"));
  s.ref.clear();
  CHECK_EQUAL(SpeakRoad(s).street, std::string("Valley Highway"));
}

TEST(InstructionSaysExitNumber)
{
  CHECK_EQUAL(InstructionText(TurnKind::ExitRight, "I 35 North toward Dallas", "", "42B"),
              std::string("Take exit 42B on the right for I 35 North toward Dallas"));
  CHECK_EQUAL(InstructionText(TurnKind::ExitLeft, "", "", "7"), std::string("Take exit 7 on the left"));
  CHECK_EQUAL(InstructionText(TurnKind::SlightRight, "US 287 South", "", "238B"),
              std::string("Take exit 238B on the right for US 287 South"));
  // No number: same wording as before, and "toward" never follows "onto".
  CHECK_EQUAL(InstructionText(TurnKind::ExitRight, "toward Dallas", ""), std::string("Take the exit on the right toward Dallas"));
  CHECK_EQUAL(InstructionText(TurnKind::ExitRight, "I 25", ""), std::string("Take the exit on the right for I 25"));
  CHECK_EQUAL(InstructionText(TurnKind::SlightLeft, "I 25 South", ""), std::string("Bear left onto I 25 South"));
  // A number on a plain turn is a tagging slip; the turn stays a turn.
  CHECK_EQUAL(InstructionText(TurnKind::Left, "Main Street", "", "3"), std::string("Turn left onto Main Street"));
}

TEST(SnapshotCarriesExitNumberIntoChains)
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::ExitRight;
  r.nextStreet = "I 35 North toward Dallas";
  r.nextExitRef = "42B";
  r.thenTurn = TurnKind::SlightLeft;
  r.thenStreet = "toward Dallas";
  r.thenGapM = 100;
  CHECK_EQUAL(InstructionText(r, 25),
              std::string("Take exit 42B on the right for I 35 North toward Dallas, then bear left toward Dallas"));
}

TEST(FinalCallStopsAtTheExitNumber)
{
  CHECK_EQUAL(FinalCallText("Take exit 42B on the right for I 35 North toward Dallas"),
              std::string("Take exit 42B on the right"));
  CHECK_EQUAL(FinalCallText("Take the exit on the left for I 25, then bear right onto Colfax Avenue"),
              std::string("Take the exit on the left, then bear right onto Colfax Avenue"));
  CHECK_EQUAL(FinalCallText("Turn left onto Elm Street"), std::string("Turn left onto Elm Street"));
  CHECK(TurnShapeOf("Take exit 42B on the right for I 35") == TurnShape::Exit);
  CHECK_EQUAL(RoundaboutExitOf("Take exit 12 on the right for I 35"), std::string());
}

TEST(KokoroReadsStreetsAndExits)
{
  using pronunciation::ForSpeech;
  CHECK_EQUAL(ForSpeech("In half a mile, take exit 42B on the right for I 35 North toward Dallas."),
              std::string("In half a mile, take exit forty two B on the right for I thirty five North toward Dallas."));
  CHECK_EQUAL(ForSpeech("Take exit 238B on the left for US 287 S."),
              std::string("Take exit two thirty eight B on the left for yooess two eighty seven South."));
  CHECK_EQUAL(ForSpeech("Take exit 3 on the right for FM 1431 toward Cedar Park."),
              std::string("Take exit three on the right for F M fourteen thirty one toward Cedar Park."));
  CHECK_EQUAL(ForSpeech("Turn right onto N Colorado Blvd, then left onto St. Louis Ave."),
              std::string("Turn right onto North Colorado Boulevard, then left onto Saint Louis Avenue."));
  CHECK_EQUAL(ForSpeech("Turn left onto CR 12."), std::string("Turn left onto County Road twelve."));
}
