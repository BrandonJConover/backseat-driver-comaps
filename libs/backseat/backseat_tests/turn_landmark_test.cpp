// Directions the way people give them: "At the second light, turn left at the Walgreens onto Elm
// Street." Google (landmark navigation) and Apple ("at the second traffic light") both name what the
// driver can see; a distance alone ("in 1,500 feet") makes them count blocks. The light count comes
// from the map's signals along the route, the landmark from a named place on the turn's corner.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"
#include "backseat/running_bits.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
RouteSnapshot Turn(TurnKind kind = TurnKind::Left, std::string street = "Elm Street", std::string landmark = {})
{
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = kind;
  r.distToTurnM = 400;
  r.nextStreet = std::move(street);
  r.turnLandmark = std::move(landmark);
  return r;
}
}  // namespace

TEST(LightNumberCountsJunctionsNotSignals)
{
  // Two signals at the first junction (one per approach), one at the turn: the second light.
  CHECK_EQUAL(LightNumberAt({120, 135, 400}, 400), 2);
  // Only the turn's light ahead.
  CHECK_EQUAL(LightNumberAt({395, 410}, 400), 1);
  // Three junctions before the turn's: too many to count out loud.
  CHECK_EQUAL(LightNumberAt({100, 200, 300, 380, 400}, 400), 0);
  CHECK_EQUAL(LightNumberAt({100, 250, 400}, 400), 3);
  // No light at the turn itself: nothing to count to.
  CHECK_EQUAL(LightNumberAt({100, 250}, 400), 0);
  // Sitting at a light right now: "the next light" is ambiguous, so no count.
  CHECK_EQUAL(LightNumberAt({5, 400}, 400), 0);
  // Lights past the turn don't count, in any order.
  CHECK_EQUAL(LightNumberAt({900, 400, 150}, 400), 2);
  CHECK_EQUAL(LightNumberAt({}, 400), 0);
  CHECK_EQUAL(LightNumberAt({100, 400}, NAN), 0);
}

TEST(DirectionSaysWhichLight)
{
  CHECK_EQUAL(DirectionText("Turn left onto Elm Street", 150, Units::Mph, Control::TrafficLight, 2),
              std::string("In 500 feet, at the second light, turn left onto Elm Street."));
  CHECK_EQUAL(DirectionText("Turn left onto Elm Street", 150, Units::Mph, Control::TrafficLight, 3),
              std::string("In 500 feet, at the third light, turn left onto Elm Street."));
  // The next light, or unknown, stays "at the light".
  CHECK_EQUAL(DirectionText("Turn left onto Elm Street", 150, Units::Mph, Control::TrafficLight, 1),
              std::string("In 500 feet, at the light, turn left onto Elm Street."));
  CHECK_EQUAL(DirectionText("Turn left onto Elm Street", 150, Units::Mph, Control::TrafficLight),
              std::string("In 500 feet, at the light, turn left onto Elm Street."));
  // No light at the turn: a count means nothing.
  CHECK_EQUAL(DirectionText("Turn left onto Elm Street", 150, Units::Mph, Control::None, 2),
              std::string("In 500 feet, turn left onto Elm Street."));
  CHECK_EQUAL(NavEvent::Instruction(0, "Turn left", 100, Maneuver::Left, Control::StopSign, 2).lightNumber, 0);
}

TEST(LandmarkPhraseNamesTheCorner)
{
  CHECK_EQUAL(LandmarkPhrase("Walgreens", "store", true), std::string("the Walgreens"));
  CHECK_EQUAL(LandmarkPhrase("Shell", "gas", true), std::string("the Shell station"));
  CHECK_EQUAL(LandmarkPhrase("Shell Gas", "gas", true), std::string("the Shell Gas"));
  CHECK_EQUAL(LandmarkPhrase("The Home Depot", "store", true), std::string("the Home Depot"));
  CHECK_EQUAL(LandmarkPhrase("First Baptist Church", "worship", false), std::string("First Baptist Church"));
  // Names that would muddle the direction, or take too long to say.
  CHECK_EQUAL(LandmarkPhrase("Right Way Auto", "store", false), std::string());
  CHECK_EQUAL(LandmarkPhrase("Left Bank Cafe", "coffee", false), std::string());
  CHECK_EQUAL(LandmarkPhrase("Exit 9 Diner", "food", false), std::string());
  CHECK_EQUAL(LandmarkPhrase("Roundabout Pizza", "food", false), std::string());
  CHECK_EQUAL(LandmarkPhrase("North Side Family Restaurant", "food", false), std::string());
  CHECK_EQUAL(LandmarkPhrase("The Church of Jesus Christ of Latter-day Saints", "worship", false), std::string());
  CHECK_EQUAL(LandmarkPhrase("", "food", true), std::string());
}

TEST(TurnCallNamesTheLandmark)
{
  CHECK_EQUAL(InstructionText(Turn(TurnKind::Left, "Elm Street", "the Walgreens")),
              std::string("Turn left at the Walgreens onto Elm Street"));
  CHECK_EQUAL(InstructionText(Turn(TurnKind::Right, "", "the Shell station")),
              std::string("Turn right at the Shell station"));
  CHECK_EQUAL(InstructionText(Turn(TurnKind::SharpLeft, "Oak Lane", "First Baptist Church")),
              std::string("Make a sharp left at First Baptist Church onto Oak Lane"));
  // No landmark: unchanged.
  CHECK_EQUAL(InstructionText(Turn(TurnKind::Left, "Elm Street")), std::string("Turn left onto Elm Street"));
  // Exits, roundabouts and arrivals keep their own calls.
  auto exit = Turn(TurnKind::ExitRight, "I 25 North", "the Shell station");
  CHECK_EQUAL(InstructionText(exit), std::string("Take the exit on the right for I 25 North"));
  auto numbered = Turn(TurnKind::SlightRight, "I 25 North", "the Shell station");
  numbered.nextExitRef = "42B";
  CHECK_EQUAL(InstructionText(numbered), std::string("Take exit 42B on the right for I 25 North"));
  auto arrive = Turn(TurnKind::Destination, "", "the Walgreens");
  arrive.destination = "Joe's Diner";
  CHECK_EQUAL(InstructionText(arrive), std::string("Arrive at Joe's Diner"));
}

TEST(LandmarkKeepsTheTurnAndItsChain)
{
  auto r = Turn(TurnKind::Left, "Elm Street", "the Walgreens");
  r.thenTurn = TurnKind::Right;
  r.thenStreet = "Pine Lane";
  r.thenGapM = 100;
  std::string const text = InstructionText(r);
  CHECK_EQUAL(text, std::string("Turn left at the Walgreens onto Elm Street, then right onto Pine Lane"));
  CHECK(TurnShapeOf(FirstManeuver(text)) == TurnShape::Turn);
  CHECK(TurnDirectionOf(FirstManeuver(text)) == Maneuver::Left);
  CHECK_EQUAL(StreetOf(text), std::string("Elm Street"));
}

TEST(NavigatorSaysTheLightAndLandmark)
{
  EngineConfig c;
  c.rng = SeededRng(3);
  Navigator nav(c);
  auto r = Turn(TurnKind::Left, "Elm Street", "the Walgreens");
  r.etaSeconds = 600;
  r.plannedTurns = {TurnKind::Left, TurnKind::Destination};
  r.control = Control::TrafficLight;
  r.lightNumber = 2;
  std::string heard;
  double t = 0;
  for (double d = 600; d > 200 && heard.empty(); d -= 12, t += 1000)
  {
    r.distToTurnM = d;
    Fix f;
    f.tMs = t;
    f.lat = 39.7;
    f.lon = -105;
    f.speedMps = 12;
    nav.OnFix(f, r);
    for (auto const & l : nav.TakeLines())
      if (l.text.find("Walgreens") != std::string::npos)
        heard = l.text;
  }
  CHECK(heard.find("at the second light, turn left at the Walgreens onto Elm Street") != std::string::npos);
  // A junk count from the host is dropped, not read out.
  r.lightNumber = 9;
  CHECK_EQUAL(CleanRoute(r, 0).lightNumber, 0);
  r.lightNumber = 2;
  r.control = Control::StopSign;
  CHECK_EQUAL(CleanRoute(r, 0).lightNumber, 0);
}
