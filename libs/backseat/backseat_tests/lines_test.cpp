#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/pronunciation.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

using namespace backseat;
using namespace backseat::pronunciation;

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

TEST(MetricWordingSaysMetersAndLeavesFeetAlone)
{
  CHECK_EQUAL(MetricWording("Ten feet of glory, and we're done again. Savor it."),
              std::string("Three meters of glory, and we're done again. Savor it."));
  CHECK_EQUAL(MetricWording("You saw that coming a quarter mile ago. I know, because I pointed at it."),
              std::string("You saw that coming a few hundred meters ago. I know, because I pointed at it."));
  CHECK_EQUAL(MetricWording("the last one for a hundred miles."), std::string("the last one for a hundred and fifty kilometers."));
  // Body parts and facts stay.
  CHECK_EQUAL(MetricWording("One foot in front of the other."), std::string("One foot in front of the other."));
  CHECK_EQUAL(MetricWording("arrested for speeding at twelve miles an hour"),
              std::string("arrested for speeding at twelve miles an hour"));

  // Every written-in distance in the table has a metric version, so a metric driver never hears one.
  // Facts and the mileage log (which is in miles on purpose) are the exceptions.
  SetLineMetric(true);
  for (auto const & [key, tiers] : LineData())
  {
    if (key == "fact" || key.rfind("mileage.", 0) == 0)
      continue;
    for (int tier = 0; tier < 3; ++tier)
      for (auto const & line : LinesFor(key, tier))
        for (char const * word : {" miles", " mile ", " mile.", " feet of", "moved four feet", "moved three feet"})
          if (line.find(word) != std::string::npos)
          {
            std::fprintf(stderr, "imperial in %s: %s\n", key.c_str(), line.c_str());
            CHECK(false);
          }
  }
  SetLineMetric(false);
}


namespace
{
std::string Lower(std::string s)
{
  for (char & c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool StartsWith(std::string const & s, std::string const & prefix) { return s.rfind(prefix, 0) == 0; }
}  // namespace

TEST(ForeignPhraseLinesAreIdleBanterNeverAtATurn)
{
  // Brandon's template, "X means 'turn left' in Y.", lives in the idle banter: idle is never
  // said within 600 m of a maneuver, so a "means turn left" line can't land near a real right turn.
  // It also lives where the car is stopped or the trip is over (traffic.standstill, longStop,
  // arrived). A translated "turn left" only goes in idle or arrived: a stop can sit right before
  // a real turn, so a stopped quip never names a direction, even in another language.
  size_t foreignLines = 0;
  size_t idleLines = 0;
  std::set<std::string> turnLeftPhrases;
  for (std::string const key : {"idle", "traffic.standstill", "longStop", "arrived"})
    for (auto const & tier : LineData().at(key))
      for (auto const & line : tier)
      {
        auto const at = line.find(" means '");
        if (at == std::string::npos)
          continue;
        ++foreignLines;
        idleLines += key == "idle" ? 1 : 0;
        std::string const lowerLine = Lower(line);
        bool const direction = lowerLine.find("'turn left'") != std::string::npos ||
                               lowerLine.find("'turn right'") != std::string::npos ||
                               lowerLine.find("'please turn left'") != std::string::npos;
        CHECK(!direction || key == "idle" || key == "arrived");
        if (lowerLine.find("'turn left'") != std::string::npos || lowerLine.find("'please turn left'") != std::string::npos)
          turnLeftPhrases.insert(Lower(line.substr(0, at)));
        std::string const phrase = Lower(line.substr(0, at));
        bool known = false;
        for (auto const & entry : ForeignPhrases())
          known = known || Lower(entry.first) == phrase;
        CHECK(known);
        // The voice says the phrase in its own sounds, never the raw spelling.
        std::string const said = ForSpeech(SpeakableText(line));
        CHECK(StartsWith(said, "fx"));
        // A translated direction is followed at once by a word that it is not an instruction.
        std::string const lower = Lower(line);
        if (lower.find("'turn left'") != std::string::npos || lower.find("'turn right'") != std::string::npos)
          CHECK(lower.find("not now") != std::string::npos || lower.find("not yet") != std::string::npos ||
                lower.find("don't") != std::string::npos);
      }
  CHECK(idleLines >= 12);
  CHECK(foreignLines >= idleLines + 16);
  // Brandon's "turn left" table (gps-comments.md) is used, not just stored: Spanish, French and
  // Latin in idle; Portuguese, Dutch, Swedish, Polish, Russian, Korean, Mandarin, Finnish at arrival.
  CHECK(turnLeftPhrases.size() >= 11);

  // No line said at a turn, lane, exit, U-turn or reroute, or as part of an instruction, has a
  // foreign phrase in it.
  char const * const maneuverKeys[] = {"countdown", "lane.", "instruction.", "uturn", "straight", "rerouted",
                                       "curve.", "sketch.", "cmd.", "search.", "fear.curve", "road.highway"};
  size_t checked = 0;
  for (auto const & [key, tiers] : LineData())
  {
    bool maneuver = false;
    for (char const * k : maneuverKeys)
      maneuver = maneuver || StartsWith(key, k);
    if (!maneuver)
      continue;
    for (auto const & tier : tiers)
      for (auto const & line : tier)
      {
        ++checked;
        CHECK_EQUAL(Foreign(line), line);
        CHECK(line.find(" means '") == std::string::npos);
      }
  }
  CHECK(checked > 200);
}

namespace
{
bool EndsWith(std::string const & s, std::string const & suffix)
{
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string Trimmed(std::string s)
{
  size_t const b = s.find_first_not_of(" \"'");
  if (b == std::string::npos)
    return {};
  size_t const e = s.find_last_not_of(" \"'");
  return s.substr(b, e - b + 1);
}

/// The clauses of a line, lower case: split at sentence and clause punctuation, the "||" pause,
/// and "and"/"or"/"then", with leading fillers ("just", "please", "now", "so") dropped.
std::vector<std::string> Clauses(std::string const & line)
{
  std::string text = Lower(line);
  for (char & c : text)
    if (c == '.' || c == '!' || c == '?' || c == ',' || c == ';' || c == ':' || c == '|')
      c = '\n';
  for (char const * joiner : {" and ", " or ", " then "})
    for (size_t at = text.find(joiner); at != std::string::npos; at = text.find(joiner, at + 1))
      text[at] = '\n';
  std::vector<std::string> clauses;
  size_t start = 0;
  while (start <= text.size())
  {
    size_t end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    std::string clause = Trimmed(text.substr(start, end - start));
    for (bool dropped = true; dropped;)
    {
      dropped = false;
      for (char const * filler : {"just ", "please ", "now ", "so ", "okay ", "go ", "and ", "or ", "then "})
        if (StartsWith(clause, filler))
        {
          clause = Trimmed(clause.substr(std::string(filler).size()));
          dropped = true;
        }
    }
    if (!clause.empty())
      clauses.push_back(clause);
    start = end + 1;
  }
  return clauses;
}

bool Mentions(std::string const & clause, std::initializer_list<char const *> words)
{
  for (char const * w : words)
    if (clause.find(w) != std::string::npos)
      return true;
  return false;
}

/// Whether `line` tells the listener to touch, tap, type on or look at the phone (or to tap the
/// brakes). Negated advice ("Don't touch the screen") doesn't start its clause with the verb, so
/// it passes.
bool TellsDriverToTouchOrLook(std::string const & line)
{
  std::string previous;
  for (auto const & clause : Clauses(line))
  {
    // "DON'T. TOUCH. MY. SLIDER." is still a don't.
    bool const negated = previous == "don't" || previous == "do not" || previous == "never" || previous == "no";
    previous = clause;
    if (negated)
      continue;
    std::string const first = clause.substr(0, clause.find(' '));
    for (char const * verb : {"tap", "touch", "type", "swipe", "click", "scroll"})
      if (first == verb)
        return true;
    std::initializer_list<char const *> const screen = {"screen", "phone", "map", "mic", "button", "app", "slider"};
    if (StartsWith(clause, "press ") && Mentions(clause, screen))
      return true;
    for (char const * look : {"look at ", "look down", "glance at ", "check your ", "check the "})
      if (StartsWith(clause, look) && Mentions(clause, {"screen", "phone", "map", "app"}))
        return true;
  }
  return false;
}

/// Whether `text` holds `phrase` with no negation ("don't", "not", "never", "no") before it in the
/// same clause: "Do not take a photo while driving" passes, "Let's take a picture" doesn't.
bool SaysUnnegated(std::string const & clause, char const * phrase)
{
  size_t const at = clause.find(phrase);
  if (at == std::string::npos)
    return false;
  std::string const before = " " + clause.substr(0, at);
  for (char const * no : {"n't ", " not ", " never ", " no ", " without "})
    if (before.find(no) != std::string::npos)
      return false;
  return true;
}

/// Whether `line` tells the driver to take their hands off the wheel (hold up / raise their hands,
/// let go of the wheel), take their eyes off the road (close your eyes), or take a picture (loop
/// B41). "Keep both hands on the wheel", "Hands off the phone" and "Don't look at the water" pass.
bool TellsDriverToLetGoOrLookAway(std::string const & line)
{
  std::string previous;
  for (auto const & clause : Clauses(line))
  {
    bool const negated = previous == "don't" || previous == "do not" || previous == "never" || previous == "no";
    previous = clause;
    if (negated)
      continue;
    std::initializer_list<char const *> const hands = {"hand", "arm", "finger"};
    if ((StartsWith(clause, "hold up ") || StartsWith(clause, "raise ") || StartsWith(clause, "put up ") ||
         StartsWith(clause, "wave ")) &&
        Mentions(clause, hands))
      return true;
    for (char const * phrase :
         {"hands up", "hands in the air", "let go of the wheel", "hands off the wheel", "hands off the steering",
          "close your eyes", "shut your eyes", "eyes closed", "eyes off the road", "take a picture", "take a photo",
          "take a pic", "take a selfie", "snap a photo", "snap a picture", "grab a photo", "get a photo",
          "get a picture", "selfie time"})
      if (SaysUnnegated(clause, phrase))
        return true;
  }
  return false;
}
}  // namespace

TEST(SafetyNoTouchPromptsWhileDriving)
{
  // The detector itself: imperatives are caught, negated advice and unrelated words are not.
  CHECK(TellsDriverToTouchOrLook("Not listening anymore. Tap the mic if you need me. You will."));
  CHECK(TellsDriverToTouchOrLook("If you're being held against your will, tap the brakes twice. Gently."));
  CHECK(TellsDriverToTouchOrLook("Fine. Type. I'll be over here."));
  CHECK(TellsDriverToTouchOrLook("Just look at the screen for a second."));
  CHECK(TellsDriverToTouchOrLook("Say it or tap it."));
  CHECK(!TellsDriverToTouchOrLook("Out loud. Don't touch the phone."));
  CHECK(!TellsDriverToTouchOrLook("PRESS THE PEDAL! THE ONE ON THE RIGHT!"));
  CHECK(!TellsDriverToTouchOrLook("I spy a car in front of us. || Try not to touch it."));
  CHECK(!TellsDriverToTouchOrLook("Stop the presses."));
  CHECK(!TellsDriverToTouchOrLook("DON'T. TOUCH. MY. SLIDER."));
  CHECK(!TellsDriverToTouchOrLook("Scrolling the map while driving. Bold."));
  CHECK(!TellsDriverToTouchOrLook("Oh, you remembered me. How touching."));

  // Hands off the wheel, eyes off the road, cameras out (loop B41).
  CHECK(TellsDriverToLetGoOrLookAway("Hold up both hands. The one that makes an L is left."));
  CHECK(TellsDriverToLetGoOrLookAway("Raise your hands if you're lost."));
  CHECK(TellsDriverToLetGoOrLookAway("Let go of the wheel. Feel the road."));
  CHECK(TellsDriverToLetGoOrLookAway("Close your eyes and picture the exit."));
  CHECK(TellsDriverToLetGoOrLookAway("Mall time. Let's take a picture of the parking spot."));
  CHECK(TellsDriverToLetGoOrLookAway("Selfie time!"));
  CHECK(TellsDriverToLetGoOrLookAway("Hands up! This is a stick-up."));
  CHECK(!TellsDriverToLetGoOrLookAway("Keep both hands on the wheel."));
  CHECK(!TellsDriverToLetGoOrLookAway("Both hands on the wheel. Ten and two."));
  CHECK(!TellsDriverToLetGoOrLookAway("I'm going to need a photo. Do not take a photo while driving. Logged."));
  CHECK(!TellsDriverToLetGoOrLookAway("Hands off the phone."));
  CHECK(!TellsDriverToLetGoOrLookAway("Don't look at the water."));
  CHECK(!TellsDriverToLetGoOrLookAway("Don't. Close your eyes. Ever."));
  CHECK(!TellsDriverToLetGoOrLookAway("Never take your eyes off the road."));
  CHECK(!TellsDriverToLetGoOrLookAway("Raise the volume. I have things to say."));

  // Every line the GPS can say in a car. Onboarding and setup happen parked; the walking question
  // ("click the driving button") is asked as a trip starts on a walking route; walk/bike/hike
  // lines never play in a car.
  size_t checked = 0;
  for (TravelMode mode : {TravelMode::Drive, TravelMode::OffRoad})
  {
    SetLineTravelMode(mode);
    for (auto const & entry : LineData())
    {
      std::string const & key = entry.first;
      if (StartsWith(key, "onboard.") || StartsWith(key, "setup.") || StartsWith(key, "mode.ask.") ||
          EndsWith(key, ".walk") || EndsWith(key, ".bike") || EndsWith(key, ".hike") || EndsWith(key, ".offroad"))
        continue;
      for (int tier = 0; tier < 3; ++tier)
        for (auto const & line : LinesFor(key, tier))
        {
          ++checked;
          if (TellsDriverToTouchOrLook(line))
            std::printf("    touch prompt while driving: %s [%d]: %s\n", key.c_str(), tier, line.c_str());
          CHECK(!TellsDriverToTouchOrLook(line));
          // nightOut.tube only plays parked (TrackNightOut's parked branch): the sobriety joke stays.
          if (key == "nightOut.tube")
            continue;
          if (TellsDriverToLetGoOrLookAway(line))
            std::printf("    hands/eyes/photo prompt while driving: %s [%d]: %s\n", key.c_str(), tier, line.c_str());
          CHECK(!TellsDriverToLetGoOrLookAway(line));
        }
    }
  }
  CHECK(checked > 5000);
  // The "no, not walking" reply for a car that's already moving (BI-014) must not ask for a tap.
  SetLineTravelMode(TravelMode::Walk);
  for (int tier = 0; tier < 3; ++tier)
  {
    CHECK(!LinesFor("mode.ask.walk.no.moving", tier).empty());
    for (auto const & line : LinesFor("mode.ask.walk.no.moving", tier))
      CHECK(!TellsDriverToTouchOrLook(line));
  }
  SetLineTravelMode(TravelMode::Drive);

  // On foot the mic is fair game: the walking lines still say how to get the GPS back.
  SetLineTravelMode(TravelMode::Walk);
  bool tapTheMic = false;
  for (auto const & line : LinesFor("cmd.stopListening", 0))
    tapTheMic = tapTheMic || Lower(line).find("tap the mic") != std::string::npos;
  CHECK(tapTheMic);
  SetLineTravelMode(TravelMode::Drive);
}

TEST(ThinPoolsHaveVariety)
{
  // Situations that used to have the same two lines in every tier (loop B8): each needs enough
  // lines that a long trip doesn't repeat itself, and the angry tier must actually sound different.
  SetLineTravelMode(TravelMode::Drive);
  for (char const * key :
       {"signal.lost", "signal.back", "night.late", "night.sunset", "road.farm", "road.gravel", "road.nature",
        "road.smallTown", "gas.none", "rest.passed", "rest.back", "rest.quick", "coffee.far", "driveThru.in",
        "driveThru.long"})
  {
    std::vector<std::string> unique;
    for (int tier = 0; tier < 3; ++tier)
      for (auto const & line : LinesFor(key, tier))
      {
        bool seen = false;
        for (auto const & u : unique)
          seen = seen || u == line;
        if (!seen)
          unique.push_back(line);
      }
    if (unique.size() < 6)
      std::printf("    thin pool: %s has %zu unique lines\n", key, unique.size());
    CHECK(unique.size() >= 6);
    CHECK(LinesFor(key, 2) != LinesFor(key, 0));
    CHECK(LinesFor(key, 1) != LinesFor(key, 0));
  }
}

namespace
{
// A flat-tier key after its escalation batch: >= 6 unique lines, all three tiers differ, and the original first
// line is still in every tier (new lines go beside the originals, never instead of them). Read straight from the
// table so walk/bike keys count too.
void CheckEscalated(char const * key)
{
  auto const & data = LineData();
  auto const it = data.find(key);
  CHECK(it != data.end());
  if (it == data.end())
    return;
  auto const & tiers = it->second;
  std::vector<std::string> unique;
  for (auto const & tier : tiers)
    for (auto const & line : tier)
    {
      bool seen = false;
      for (auto const & u : unique)
        seen = seen || u == line;
      if (!seen)
        unique.push_back(line);
    }
  if (unique.size() < 6)
    std::printf("    thin pool: %s has %zu unique lines\n", key, unique.size());
  CHECK(unique.size() >= 6);
  CHECK(tiers[1] != tiers[0]);
  CHECK(tiers[2] != tiers[0]);
  CHECK(tiers[2] != tiers[1]);
  for (auto const & tier : tiers)
  {
    bool found = false;
    for (auto const & l : tier)
      found = found || l == tiers[0][0];
    CHECK(found);
  }
}
}  // namespace

TEST(FlatTierBatchTwoEscalates)
{
  // Loop BI-012 batch 1: situations whose three tiers were identical, so a driver at full frustration
  // heard the calm wording.
  for (char const * key :
       {"driveThru.leaving", "hardAccel.first", "night.dark", "rest.ahead", "road.highway", "viewpoint.ahead",
        "viewpoint.passed", "signal.lost.walk", "signal.lost.bike", "signal.lost.offroad", "partner.aside",
        "quiet.aside", "start.slow.walk"})
    CheckEscalated(key);
}

TEST(FlatTierBatchThreeEscalates)
{
  // Loop BI-012 batch 2: the clean-streak beats, the clean arrival, yesterday's route and the designated driver.
  for (char const * key : {"clean.1", "clean.2", "clean.3", "arrived.clean", "routine.yesterday", "nightOut.dd"})
    CheckEscalated(key);
}

TEST(FlatTierKeysOnlyShrink)
{
  // Ratchet: keys whose three tiers are identical. Scripted onboarding, stories, facts and lists are flat on
  // purpose; reactive situations should escalate. Lower this number as batches land, never raise it.
  int flat = 0;
  for (auto const & [key, tiers] : LineData())
    if (tiers[0] == tiers[1] && tiers[1] == tiers[2])
      ++flat;
  if (flat > 66)
    std::printf("    %d keys have identical tiers (limit 66)\n", flat);
  CHECK(flat <= 66);
}

TEST(PickNeverCrashesOnAnEmptyPoolOrABadRandom)
{
  SaidLog log;
  // Nothing to pick from: "" back, nothing remembered, nothing to save.
  CHECK_EQUAL(log.Pick("no.lines", {}, 0.5), std::string());
  CHECK(!log.Said("no.lines"));
  CHECK(!log.Dirty());

  std::vector<std::string> const pool = {"one", "two", "three"};
  auto const inPool = [&](std::string const & s) { return std::find(pool.begin(), pool.end(), s) != pool.end(); };
  // A random source that hands back NaN, a negative, exactly 1 or more still picks a real line.
  for (double const r : {std::nan(""), -1.0, -0.0001, 1.0, 2.0, 1e300, -1e300, 0.9999999})
    for (int i = 0; i < 4; ++i)
      CHECK(inPool(log.Pick("pool", pool, r)));
  // The first-ever pick is still the first option, whatever r says.
  SaidLog fresh;
  CHECK_EQUAL(fresh.Pick("k", pool, std::nan("")), std::string("one"));
  // r >= 1 picks the last fresh line, never past the end.
  SaidLog edge;
  edge.Pick("k", pool, 0);
  CHECK_EQUAL(edge.Pick("k", pool, 1.0), std::string("three"));
}

TEST(UnknownKeysAndTiersGiveAnEmptyPoolInsteadOfThrowing)
{
  CHECK(LinesFor("no.such.key", 0).empty());
  CHECK(LinesFor("", 1).empty());
  auto const & firstKey = LineData().begin()->first;
  CHECK(!LinesFor(firstKey, 0).empty());
  CHECK(LinesFor(firstKey, -1).empty());
  CHECK(LinesFor(firstKey, 3).empty());
  CHECK(LinesFor(firstKey, 5).empty());
  // Also off the cached path (not driving, not a house).
  SetLineTravelMode(TravelMode::Walk);
  SetLineHousing(Housing::Streets);
  SetLineMetric(true);
  CHECK(LinesFor("no.such.key", 0).empty());
  CHECK(LinesFor(firstKey, 7).empty());
  SetLineTravelMode(TravelMode::Drive);
  SetLineHousing(Housing::Unknown);
  SetLineMetric(false);
}

TEST(EveryKeyHasLinesForEveryModeHousingUnitAndTier)
{
  int empty = 0, checked = 0;
  std::string firstEmpty;
  for (TravelMode const mode :
       {TravelMode::Drive, TravelMode::Walk, TravelMode::Bike, TravelMode::OffRoad, TravelMode::Hike})
    for (Housing const housing : {Housing::Unknown, Housing::House, Housing::Apartment, Housing::Streets})
      for (bool const metric : {false, true})
      {
        SetLineTravelMode(mode);
        SetLineHousing(housing);
        SetLineMetric(metric);
        for (auto const & [key, tiers] : LineData())
          for (int tier = 0; tier < 3; ++tier)
          {
            ++checked;
            if (LinesFor(key, tier).empty())
            {
              if (firstEmpty.empty())
                firstEmpty = key + " tier " + std::to_string(tier) + " " + TravelModeName(mode) + " " +
                             HousingName(housing) + (metric ? " metric" : "");
              ++empty;
            }
          }
      }
  SetLineTravelMode(TravelMode::Drive);
  SetLineHousing(Housing::Unknown);
  SetLineMetric(false);
  if (empty)
    std::printf("first empty pool: %s\n", firstEmpty.c_str());
  CHECK_EQUAL(empty, 0);
  CHECK(checked > 1000);
}
