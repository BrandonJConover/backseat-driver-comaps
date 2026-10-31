#include "check.hpp"

#include "backseat/lines.hpp"

using namespace backseat;

TEST(ShoutedLinesAreSpokenInNormalCase)
{
  CHECK_EQUAL(SpeakableText("WHO TAUGHT YOU THE GAS PEDAL HAS ONLY TWO SETTINGS?"),
              std::string("Who taught you the gas pedal has only two settings?"));
  CHECK_EQUAL(SpeakableText("YOU DID IT! YOU MADE THE TURN! I'M NOT CRYING, I'M A PHONE!"),
              std::string("You did it! You made the turn! I'm not crying, I'm a phone!"));
  CHECK_EQUAL(SpeakableText("57. IN A 50. I CAN SEE THE NUMBERS."), std::string("57. In a 50. I can see the numbers."));
  // Abbreviations and ordinary text are left alone.
  CHECK_EQUAL(SpeakableText("The GPS says NASCAR. OK?"), std::string("The GPS says NASCAR. OK?"));
  CHECK_EQUAL(SpeakableText("Turn left onto Elm Street."), std::string("Turn left onto Elm Street."));
}
