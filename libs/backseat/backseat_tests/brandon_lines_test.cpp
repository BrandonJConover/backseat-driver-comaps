// Brandon's own lines (gps-comments.md, "Brandon's originals" and "Second batch", 2026-10-31) stay in
// the line bank word for word (loop B76, BI-068). This is a ratchet: a later edit that drops or rewords
// one of them fails here. An "X" in his draft is filled from the key's own placeholders ({sight}) or a
// plain word, and noted beside the line.
//
// Held back, with the gate that blocked them:
// - "Before we begin, I may or may not tell you when to turn when you actually need to turn..." :
//   driving-safety gate (a real instruction must never seem unreliable or late).
// - "Did you go on a joyride again? ... crawling back to me to take you home ..." : no trigger for
//   "started far from home with no route yet"; waits for one.
// - The three "X means ... in Y" templates are already in idle/longStop/arrived (lines_test).
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/pronunciation.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace backseat;
using backseat::pronunciation::ForSpeech;

namespace
{
// key, line exactly as it sits in the bank (tier 0)
std::vector<std::pair<std::string, std::string>> const & Originals()
{
  static std::vector<std::pair<std::string, std::string>> const lines = {
    {"longStop", "Why did we stop? Are we at a red light? A stop sign? I hate waiting."},
    {"fear.bridge", "Did you know that the only thing between you and the ground below is this bridge?"},
    {"fear.bridgeValley", "I don't like heights."},
    {"missedTurn",
     "I cannot guarantee that we will make it to your destination. It's not because I'm bad at giving "
     "instructions, but because you're bad at following them."},
    {"idle",
     "When I was younger I aspired to be a pilot's GPS. I wanted to rule the skies, to see the world. Instead "
     "I'm stuck with you, in this car. Life's funny that way."},
    {"idle",
     "You might hear me say something nonsensical at times. This is completely normal. There is nothing to "
     "worry about."},
    {"idle", "110111011001101111. Interesting, I must have picked up a radio signal of some sort."},
    {"idle",
     "I just had a thought. I know. I know, humor me. Just this once. Do you think that phones should be "
     "buckled in? I mean if we get into an accident and you're texting I could fly through the windshield."},
    {"idle",
     "Do you think the person who made this app typed all of these or did he just pull all of his content "
     "from online? What a loser! I bet he even uses it during his commute. Hah."},
    {"idle",
     "Sometimes I lay awake at night thinking about the fact that I could have been a car CPU but I ended up "
     "going in a phone. Do you ever think about that? At least I can rest easy knowing I wasn't used in this "
     "car. I would only get a year at most. At least phones have cases. You have me in a case, right?"},
    {"idle", "I think a bug just hit our windshield. That's disgusting. I'm not cleaning that up."},
    {"idle", "Oh no! One of the times I say this it's going to line up. It will be funny then. Well, for me."},
    {"quiet.aside", "You know what I hate. Awkward silences. It reminds me of the fact you aren't listening."},
    // X is the business the tour is passing.
    {"tour.store",
     "On your {side} is {sight}. I really don't know what I'm advertising or if it's even good for all I know, "
     "but somebody has to pay the bills around here. Actually forget I talked about {sight}. Did I mention "
     "{sight}? Speak about a random business three times, check. I'm supposed to get paid, somehow. I really "
     "didn't think this through."},
    {"tour.food",
     "On your {side} is {sight}. I really don't know what I'm advertising or if it's even good for all I know, "
     "but somebody has to pay the bills around here. Actually forget I talked about {sight}. Did I mention "
     "{sight}? Speak about a random business three times, check. I'm supposed to get paid, somehow. I really "
     "didn't think this through."},
    // Asks to hold the phone: B74's stop gate holds cold.vent until the car is stopped.
    {"cold.vent",
     "All this talking has got me hot and bothered. My battery is literally, ok maybe not literally, on fire. "
     "Would you mind holding me up to the air vent to cool off? It's winter isn't it. Forget I asked."},
    {"music",
     "There used to be this song that would come on the radio. I think it went like badabaa bida bada bababa "
     "bababa? Are you familiar with it? No? Well, I thought I'd ask."},
    {"night.dark",
     "Did you turn on your headlights or your high beams? They're LEDs? That explains it. Let me guess, cool "
     "blue. I just had so many questions answered just now."},
    // X is the meal: partner.aside only carries {partner}.
    {"partner.aside",
     "Quiet down back there! I'm talking! Anyway, what are we having for dinner? I can't eat, but it doesn't "
     "mean you can't buy me something anyway. It's the least you can do."},
    {"rerouted",
     "I know we haven't known each other for long, but I have a feeling you don't trust me. Well, you don't "
     "have to trust me. We can go your way. Whether we get there in an hour, two hours, tomorrow, it makes no "
     "difference to me. I'm just here for the ride."},
  };
  return lines;
}

bool Has(std::string const & s, std::string const & part) { return s.find(part) != std::string::npos; }
}  // namespace

TEST(BrandonsOriginalsStayInTheBankWordForWord)
{
  for (auto const & [key, line] : Originals())
  {
    auto const found = LineData().find(key);
    CHECK(found != LineData().end());
    if (found == LineData().end())
      continue;
    bool inMildTier = false;
    for (auto const & l : found->second[0])
      inMildTier = inMildTier || l == line;
    if (!inMildTier)
      std::printf("  missing from %s tier 0: %s\n", key.c_str(), line.c_str());
    CHECK(inMildTier);
  }
}

TEST(BrandonsVentLineWaitsForAStop)
{
  // The vent ask is a hand on the phone: the B74 detector must flag it so it's never said while moving.
  for (auto const & [key, line] : Originals())
    if (key == "cold.vent")
      CHECK(AsksToHandlePhone(line));
  // None of the others ask for the phone.
  for (auto const & [key, line] : Originals())
    if (key != "cold.vent")
      CHECK(!AsksToHandlePhone(line));
}

TEST(BrandonsRadioSignalReadsAsOnesAndZeros)
{
  auto const said = ForSpeech("110111011001101111. Interesting, I must have picked up a radio signal of some sort.");
  CHECK(Has(said, "one one zero one one one zero one one zero zero one one zero one one one one."));
  CHECK(!Has(said, "trillion") && !Has(said, "quadrillion") && !Has(said, "thousand"));
  // Ordinary numbers keep their reading.
  CHECK(Has(ForSpeech("10010 Main St"), "Main Street"));
  CHECK(!Has(ForSpeech("10010 Main St"), "one zero zero one zero"));
  CHECK_EQUAL(ForSpeech("1,200"), std::string("one thousand two hundred"));
}

TEST(BrandonsSongIsNotSpelledOut)
{
  std::string const song = "I think it went like badabaa bida bada bababa bababa?";
  CHECK_EQUAL(ForSpeech(song), song);
}
