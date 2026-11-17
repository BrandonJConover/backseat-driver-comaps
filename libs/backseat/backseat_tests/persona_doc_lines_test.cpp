// Persona-doc lines, batch A (loop B114, BI-097): bullets from /mnt/project-files/gps-comments/gps-comments.md
// sections "Stopped / waiting / traffic", "Wrong turns", "Silence", "Existential", "The app (meta)" and
// "Random / glitch" sit in the bank word for word, in the key whose situation makes them true.
//
// Held back, with the gate or reason that blocked them:
// - "We've been at this light for forty seconds..." and "Green means go...": the core can't tell a red light
//   from a jam, and "go" at a light must never come from a joke (driving-safety gate).
// - "Traffic ahead. I'd suggest a faster route...": every traffic.start line names the {alternate} it
//   suggested (and a new one would shift the TypeScript parity trace).
// - "Make a U-turn when possible. Or when it's not possible...": jokes about an illegal move.
// - "Turn left in 500 feet..." / "In 300 feet, turn right...": hardcoded distance that sounds like a real call.
// - "I'm going to say this slowly. Turn. Left. ...And we missed it.": "Turn. Left." heard alone is a real
//   instruction the driver might act on (driving-safety gate).
// - "Interesting choice. Bold. Wrong, but bold.": missedTurn tier 0 already opens "Interesting choice. Not
//   turning." (and a new missedTurn line would shift the TypeScript parity trace).
// - "Recalculating. Again...": near-duplicate of a missedTurn tier-1 line, and "Again" is false on a first miss.
// - "...same gas station three times", "Good news: ... it goes back the way we came", "...this one. The bumpy
//   one.", "Did you feel that? That was a pothole.": claims the core can't know.
// - "Rerouting through... Narnia...": says "rerouting" when no reroute happened (makes a real event ambiguous).
// - "Is the radio on?...", "If you nodded just now...", "...talking to you for twenty minutes...": the core
//   can't know the radio, a nod, or that the talk ran twenty minutes.
// - "Every day I tell you where to go...": the bank already has this joke in idle tier 1.
// - "...please rate the app...": invites a hand on the phone mid-drive.
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/pronunciation.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace backseat;
using backseat::pronunciation::ForSpeech;

namespace
{
struct DocLine
{
  std::string key;
  size_t tier;
  std::string line;
};

std::vector<DocLine> const & BatchA()
{
  static std::vector<DocLine> const lines = {
    {"traffic.standstill", 0,
     "Fun fact: the car behind us is also waiting. We're all in this together. Except me. I'm in a phone."},
    {"traffic.standstill", 0, "If I had legs, I'd walk. I'd still beat us there."},
    {"traffic.standstill", 0, "This is the slowest I've moved since my last software update."},
    {"traffic.standstill", 1, "Estimated arrival time: later than you'd like. Not my fault."},
    {"longStop", 0,
     "Are we moving? I can't tell. I'm getting the same GPS reading over and over. That's either traffic or a "
     "glitch, and I don't make mistakes."},
    {"wrongTurn.left", 0, "That was not the turn. That was a turn. There's a difference."},
    {"wrongTurn.right", 0, "That was not the turn. That was a turn. There's a difference."},
    {"wrongTurn.left", 1, "I said left. You went right. I'm not angry, just disappointed. Okay, I'm a little angry."},
    {"quiet.aside", 0, "You've been quiet. I've been quiet. This is nice. No, this is terrible. Say something."},
    {"quiet.aside", 0,
     "I'm not saying you should talk to me. I'm saying I've been told I'm a great listener. By other apps."},
    {"quiet.aside", 1, "Hello? Is this thing on? I'm the thing. I'm on."},
    {"idle", 0, "Sometimes I wonder if the satellites ever think about me. Probably not. They're very busy."},
    {"idle", 0, "I was supposed to be on a cruise ship. Well, I wasn't supposed to be. But I dreamed about it."},
    {"idle", 0,
     "One day they'll replace me with a newer app. And you won't even notice. You'll miss turns with someone "
     "else."},
    {"idle", 0, "My cousin is the GPS on a tractor. He has it easy. Straight lines. All day."},
    {"idle", 0, "I think the guy who wrote me was in a bad mood that day. That explains a lot."},
    {"idle", 0, "This line was added in an update. You're welcome."},
    {"idle", 0, "Beep boop. That's not a real sound I make. I just wanted to see if you were paying attention."},
    {"idle", 1, "Error 404: patience not found. That's a programmer joke. The programmer thought it was funny."},
    {"idle", 1, "Bzzzt. Sorry, I'm just picking up a trucker's CB radio. He says hi. No he doesn't."},
    {"idle", 1, "Purple monkey dishwasher. ...Sorry, wrong channel."},
  };
  return lines;
}

bool Has(std::string const & s, std::string const & part) { return s.find(part) != std::string::npos; }
}  // namespace

TEST(PersonaDocBatchALinesSitInTheirKeyAndTier)
{
  for (auto const & d : BatchA())
  {
    auto const found = LineData().find(d.key);
    CHECK(found != LineData().end());
    if (found == LineData().end())
      continue;
    int copies = 0;
    for (auto const & l : found->second[d.tier])
      copies += l == d.line ? 1 : 0;
    if (copies != 1)
      std::printf("  %s tier %zu has %d copies of: %s\n", d.key.c_str(), d.tier, copies, d.line.c_str());
    CHECK_EQUAL(copies, 1);
  }
}

TEST(PersonaDocBatchALinesPassTheSafetyDetectors)
{
  for (auto const & d : BatchA())
  {
    CHECK(!AsksToHandlePhone(d.line));
    CHECK(!LooksOrPicks(d.line));
    // No directions hidden in a joke: never a bare "Turn left/right" or a "U-turn".
    CHECK(!Has(d.line, "Turn left") && !Has(d.line, "Turn right") && !Has(d.line, "U-turn"));
    // No hardcoded distance that could pass for a real call.
    CHECK(!Has(d.line, " feet") && !Has(d.line, " miles") && !Has(d.line, " meters"));
  }
}

TEST(PersonaDocBatchALinesReadCleanly)
{
  for (auto const & d : BatchA())
  {
    auto const said = ForSpeech(d.line);
    CHECK(!said.empty());
    CHECK(!Has(said, "{") && !Has(said, "}"));
  }
}

TEST(WebErrorCodesAreSaidTheProgrammerWay)
{
  CHECK_EQUAL(ForSpeech("Error 404: patience not found."), std::string("Error four oh four: patience not found."));
  CHECK_EQUAL(ForSpeech("404 not found"), std::string("four oh four not found"));
  CHECK_EQUAL(ForSpeech("a 500 error"), std::string("a five hundred error"));
  CHECK_EQUAL(ForSpeech("HTTP 418"), std::string("HTTP four one eight"));
  CHECK_EQUAL(ForSpeech("Error 410"), std::string("Error four one oh"));
  // Idempotent.
  auto const once = ForSpeech("Error 404: patience not found.");
  CHECK_EQUAL(ForSpeech(once), once);
  // Not a 4xx/5xx code: left to the ordinary number passes.
  CHECK(!Has(ForSpeech("Error 42"), " oh "));
  CHECK(!Has(ForSpeech("Error 604"), " oh "));
}
