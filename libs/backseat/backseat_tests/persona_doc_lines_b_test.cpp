// Persona-doc lines, batch B (loop B115, BI-097): bullets from /mnt/project-files/gps-comments/gps-comments.md
// sections "Weather / heat / battery", "Arrival / parking", "Back seat / passengers / food" and
// "Ads / sponsored" sit in the bank in the key whose situation makes them true.
//
// Adapted (meaning kept, made true for the key): the critical battery line says {percent} instead of "Ten"
// (battery.critical fires at <= 10%); arrival lines end "Grade: {grade}." like every
// arrived line; the fries line opens "A drive-thru." instead of "There's a drive-thru ahead" (driveThru.in fires
// inside it); the restaurant ad uses tour.food's {side} and {sight} instead of "On your right is a restaurant".
//
// Held back, with the gate or reason that blocked them:
// - "It's raining. Please slow down. Not because of the road...": waves off the real reason to slow down
//   (driving-safety gate: no joking about real safety advice).
// - "It's snowing. I'd say 'drive carefully,' but...": mocks safety advice (same gate).
// - "My battery is at 15 percent...": any new battery.low line shifts the TypeScript parity trace (trace line
//   3406 picks battery.low); the fries line sits in driveThru.in tier 0 only (a tier-1 copy is a cross-tier repeat).
// - "It's foggy. I can see through it...": already in weather.fog tier 0.
// - "Destination on your left. Or right...": makes the real arrival side ambiguous (driving-safety gate).
// - "We made it... you'd still be on the highway": false on a trip with no highway.
// - "I won't say anything. I'll just quietly add twenty minutes to the arrival time...": a false ETA claim.
// - "You think you know a shortcut", "I'm not offended you went the other way", "Oh, we're going your way now?",
//   "Fine. Your way...": rerouted is the only key they fit, and a new line in its tier 0 or 1 shifts the
//   TypeScript parity trace (lines 6275 / 6692); tier 2 is the shouting tier. Waiting for a parity-trace refresh.
// - "Remember the last time you didn't trust me?": false on a first reroute.
// - "Can everyone use their inside voices? Some of us are trying to say 'turn left.'": "turn left" heard with
//   no turn coming is a real instruction out of nowhere (same reason B114 held back "Turn. Left.").
// - Kicking the seat, "are we there yet" kids, "Passenger, if you'd like to navigate", lunch, snacks "again",
//   spilled drink: need passenger, food or drink info the core doesn't have.
// - Car wash "coming up", "six coffee shops", "the mattress store": no key knows those places or counts.
// - "It's getting dark. Headlights...", "Is that a deer?", "Your passenger looks like...": deer/headlights and
//   passenger-looks lines are excluded by the backlog row (safety and looks gates).
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/pronunciation.hpp"

#include <cctype>
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

std::vector<DocLine> const & BatchB()
{
  static std::vector<DocLine> const lines = {
    {"weather.clear", 0, "It's very sunny. I'd wear sunglasses, but I don't have a face. Or ears. You see my problem."},
    {"battery.critical", 0, "{percent}. If I start saying my final goodbyes, just let me have this."},
    {"battery.pluggedIn", 0, "I'm on the charger. Thank you. I feel like a new app."},
    {"arrived", 0, "You have arrived. Somehow. I'm as surprised as you are. Grade: {grade}."},
    {"arrived", 0, "You've arrived. Please park within the lines. Both of them. Ideally. Grade: {grade}."},
    {"arrived", 0, "You're here. Don't forget me in the car. Last time it got very hot and lonely. Grade: {grade}."},
    {"driveThru.in", 0, "A drive-thru. I'd like a large fries. I won't eat them. I just want to feel included."},
    {"idle", 0, "This route was brought to you by... nobody. Nobody sponsors me. I just like saying that."},
    {"idle", 0, "Sponsored message: drink water. Nobody paid for that. I'm just worried about you."},
    {"tour.food", 0,
     "On your {side} is {sight}. I've been told to say it's 'delicious.' I don't eat. I've never eaten. I'll never "
     "know."},
  };
  return lines;
}

bool Has(std::string const & s, std::string const & part) { return s.find(part) != std::string::npos; }

std::string Filled(std::string s)
{
  auto const put = [&s](std::string const & var, std::string const & value)
  {
    for (size_t at = s.find(var); at != std::string::npos; at = s.find(var))
      s.replace(at, var.size(), value);
  };
  put("{percent}", "15 percent");
  put("{grade}", "B");
  put("{side}", "right");
  put("{sight}", "Rosa's Diner");
  return s;
}
}  // namespace

TEST(PersonaDocBatchBLinesSitInTheirKeyAndTier)
{
  for (auto const & d : BatchB())
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

TEST(PersonaDocBatchBLinesPassTheSafetyDetectors)
{
  for (auto const & d : BatchB())
  {
    CHECK(!AsksToHandlePhone(d.line));
    CHECK(!LooksOrPicks(d.line));
    // No directions hidden in a joke, in any case: never "turn left/right" or a "U-turn".
    std::string lower = d.line;
    for (auto & c : lower)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    CHECK(!Has(lower, "turn left") && !Has(lower, "turn right") && !Has(lower, "u-turn"));
    CHECK(!Has(d.line, " feet") && !Has(d.line, " miles") && !Has(d.line, " meters"));
    // Never waves off real safety advice.
    CHECK(!Has(lower, "drive carefully") && !Has(lower, "not because of the road"));
  }
}

TEST(PersonaDocBatchBLinesUseOnlyTheirKeysVars)
{
  for (auto const & d : BatchB())
  {
    bool const battery = d.key == "battery.critical";
    CHECK_EQUAL(Has(d.line, "{percent}"), battery);
    CHECK_EQUAL(Has(d.line, "{grade}"), d.key == "arrived");
    CHECK_EQUAL(Has(d.line, "{sight}"), d.key == "tour.food");
    CHECK(!Has(Filled(d.line), "{"));
  }
}

TEST(PersonaDocBatchBLinesReadCleanly)
{
  for (auto const & d : BatchB())
  {
    auto const said = ForSpeech(Filled(d.line));
    CHECK(!said.empty());
    CHECK(!Has(said, "{") && !Has(said, "}"));
  }
  CHECK(Has(ForSpeech(Filled("My battery is at {percent}.")), "fifteen percent"));
}
