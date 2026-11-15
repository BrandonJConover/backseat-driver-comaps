// A driver who rides alone hears no lines that talk to a passenger (loop B166, BI-129). Setup asks
// "Do you usually have someone riding with you?"; before this, "just me" still got "Or ask your
// passenger. They know where the mic is." and "Excuse me, passengers?".
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/navigator.hpp"
#include "backseat/voice_command.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
struct AloneReset
{
  ~AloneReset() { SetLineFilters({}); }
};

bool Has(std::vector<std::string> const & pool, std::string const & part)
{
  return std::any_of(pool.begin(), pool.end(), [&](auto const & l) { return l.find(part) != std::string::npos; });
}

Navigator Fresh(unsigned seed)
{
  EngineConfig c;
  c.rng = SeededRng(seed);
  return Navigator(c);
}
}  // namespace

TEST(RidingAloneSpotsPassengerLines)
{
  CHECK(!LineFitsRidingAlone("Wake me when we're parked. Or ask your passenger. They know where the mic is."));
  CHECK(!LineFitsRidingAlone("Excuse me, passengers? If any of you can drive, now's a great time to say so."));
  CHECK(!LineFitsRidingAlone("Whoever's in the back, please don't sing. Your driver is concentrating."));
  CHECK(!LineFitsRidingAlone("I'D CHEER, BUT YOUR PASSENGERS ARE DOING ENOUGH OF THAT!"));
  CHECK(!LineFitsRidingAlone("Your car has airbags. Your passengers have regrets."));
  CHECK(!LineFitsRidingAlone("Quiet down back there! I'm talking!"));
  // Fine with nobody else in the car.
  CHECK(LineFitsRidingAlone("Somebody back there is sliding across the back seat. It's probably a water bottle."));
  CHECK(LineFitsRidingAlone("One more time, for the people in the back: left."));
  CHECK(LineFitsRidingAlone("This road can't make up its mind. You two have a lot in common."));
  CHECK(LineFitsRidingAlone("Is there anyone else here? A passenger? A backseat driver? Oh wait, that's me."));
  CHECK(LineFitsRidingAlone("Right is the side with the passenger seat. Usually. You turned toward yourself."));
  CHECK(LineFitsRidingAlone("Route cancelled. You and your instincts. Good luck to both of you."));
  CHECK(LineFitsRidingAlone("Turn left onto Elm Street."));
}

TEST(SoloDriverPoolsLeaveOutPassengerLines)
{
  AloneReset reset;
  CHECK(Has(LinesFor("cmd.stopListening", 0), "ask your passenger"));
  CHECK(Has(LinesFor("anyoneElse", 0), "Excuse me, passengers?"));
  size_t const withCompany = LinesFor("cmd.stopListening", 0).size();

  SetLineRidesAlone(true);
  auto const & stop = LinesFor("cmd.stopListening", 0);
  CHECK(!Has(stop, "ask your passenger"));
  CHECK_EQUAL(stop.size(), withCompany - 1);
  CHECK(!Has(LinesFor("anyoneElse", 0), "Excuse me, passengers?"));
  CHECK(Has(LinesFor("anyoneElse", 1), "Is there anyone else here?"));
  // The setup script is said word for word.
  CHECK(Has(LinesFor("setup.askPassenger", 0), "Or is it just you?"));

  SetLineRidesAlone(false);
  CHECK(Has(LinesFor("cmd.stopListening", 0), "ask your passenger"));
}

TEST(SoloDriverBankHasNoPassengerLineLeft)
{
  AloneReset reset;
  SetLineRidesAlone(true);
  for (auto const & [key, tiers] : LineData())
  {
    if (key.rfind("setup.", 0) == 0 || key.rfind("onboard.", 0) == 0 || key.rfind("speeding.story", 0) == 0)
      continue;
    for (int tier = 0; tier < static_cast<int>(tiers.size()); ++tier)
    {
      auto const & pool = LinesFor(key, tier);
      auto const unfit = std::count_if(pool.begin(), pool.end(), [](auto const & l) { return !LineFitsRidingAlone(l); });
      // Either nothing unfit, or the whole pool is about the passengers (kept: the caller decides).
      if (unfit != 0 && static_cast<size_t>(unfit) != pool.size())
        std::fprintf(stderr, "passenger line left in %s#%d\n", key.c_str(), tier);
      CHECK(unfit == 0 || static_cast<size_t>(unfit) == pool.size());
    }
  }
}

TEST(SoloDriverWorksWithOtherFilters)
{
  AloneReset reset;
  SetLineRidesAlone(true);
  SetLineHousing(Housing::Streets);
  SetLineMetric(true);
  CHECK(!Has(LinesFor("cmd.stopListening", 0), "ask your passenger"));
  CHECK_EQUAL(DescribeLineFilters(CurrentLineFilters()), std::string("drive/streets/metric/right/alone"));
  SetLineTravelMode(TravelMode::Walk);
  for (auto const & line : LinesFor("anyoneElse", 0))
    CHECK(LineFitsRidingAlone(line));
}

TEST(NavigatorRidesAloneFromSetup)
{
  AloneReset reset;
  {
    Navigator nav = Fresh(41);
    CHECK(!nav.RidesAlone());
    // "No, just me" parses to a passenger with no word: the host passes that on.
    auto const answer = ParsePassengerAnswer("no, just me");
    CHECK(answer.has_value());
    nav.SetRidesAlone(answer->word.empty());
    CHECK(nav.RidesAlone());
    CHECK(CurrentLineFilters().alone);
    CHECK(!Has(LinesFor("cmd.stopListening", 0), "ask your passenger"));
    // Naming a partner later means someone rides along after all.
    nav.SetPartner("wife", Pronouns::She);
    CHECK(!nav.RidesAlone());
    CHECK(!CurrentLineFilters().alone);
    nav.SetRidesAlone(true);
    nav.SetPartner("", Pronouns::They);
    CHECK(nav.RidesAlone());
  }
  // A new navigator starts from "maybe not alone".
  Navigator const next = Fresh(42);
  CHECK(!CurrentLineFilters().alone);
  CHECK(Has(LinesFor("cmd.stopListening", 0), "ask your passenger"));
}
