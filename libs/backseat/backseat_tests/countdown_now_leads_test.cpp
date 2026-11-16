// The final "now" call leads with the maneuver (loop B172, BI-153). At the junction the driver has
// about four seconds: the turn, exit, U-turn or roundabout has to be in the first few words, with the
// joke after it, and a roundabout's exit has to come right after the maneuver, not after the joke.
#include "check.hpp"

#include "backseat/engine.hpp"
#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"

#include <cctype>
#include <cstdio>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
char const * const kNowKeys[] = {"countdown.now", "countdown.exit.now", "countdown.uturn.now",
                                 "countdown.roundabout.now", "countdown.now.hike"};

/// True when a maneuver word or the direction is within the first four words.
bool LeadsWithManeuver(std::string const & line)
{
  static std::regex const maneuver(
      R"(^(\{dir\}|turn|turning|exit|ramp|u-turn|roundabout|around|trail|fork)$)", std::regex::icase);
  std::istringstream words(line);
  std::string w;
  for (int i = 0; i < 4 && words >> w; ++i)
  {
    while (!w.empty() && !std::isalnum(static_cast<unsigned char>(w.front())) && w.front() != '{')
      w.erase(w.begin());
    while (!w.empty() && !std::isalnum(static_cast<unsigned char>(w.back())) && w.back() != '}' &&
           w.back() != '-')
      w.pop_back();
    if (std::regex_match(w, maneuver))
      return true;
  }
  return false;
}

std::vector<std::string> NowCalls(std::string const & instruction)
{
  EngineConfig c;
  c.rng = [] { return 0.0; };
  CritiqueEngine e(c, 0);
  e.OnNav(NavEvent::Instruction(-10000, instruction, 400));
  std::vector<std::string> said;
  double const ds[] = {300, 140, 70, 40, 3};
  for (size_t i = 0; i < std::size(ds); ++i)
    for (auto const & call : e.OnNav(NavEvent::Progress(1000.0 * (i + 1) + (i >= 2 ? 10000 : 0), ds[i])))
      said.push_back(call.text);
  return said;
}
}  // namespace

TEST(EveryNowCallLeadsWithTheManeuver)
{
  LineFilters const saved = CurrentLineFilters();
  for (bool metric : {false, true})
    for (bool leftHand : {false, true})
    {
      LineFilters f;
      f.metric = metric;
      f.leftHand = leftHand;
      SetLineFilters(f);
      for (char const * key : kNowKeys)
      {
        CHECK(LineData().count(key) == 1);
        for (int tier = 0; tier < 3; ++tier)
          for (auto const & line : LinesFor(key, tier))
          {
            if (!LeadsWithManeuver(line))
              std::printf("  late maneuver: %s t%d: %s\n", key, tier, line.c_str());
            CHECK(LeadsWithManeuver(line));
          }
      }
    }
  SetLineFilters(saved);
}

TEST(LeadsWithManeuverCheckSeesLateCalls)
{
  CHECK(LeadsWithManeuver("Turn {dir}. Now."));
  CHECK(LeadsWithManeuver("{DIR}! NOW!"));
  CHECK(LeadsWithManeuver("U-TURN! WHEN IT'S CLEAR!"));
  CHECK(!LeadsWithManeuver("Hey, you should be turning {dir}."));
  CHECK(!LeadsWithManeuver("HEY! YOU SHOULD BE TAKING THE ROUNDABOUT!"));
}

TEST(ExitCallGoesRightAfterTheManeuver)
{
  CHECK_EQUAL(WithExitCallAfterManeuver("Roundabout. Now. Smoothly. Like a carousel, but with insurance.",
                                        "Third exit."),
              std::string("Roundabout. Third exit. Now. Smoothly. Like a carousel, but with insurance."));
  CHECK_EQUAL(WithExitCallAfterManeuver("Now. Into the roundabout. When there's a gap.", "Second exit."),
              std::string("Now. Into the roundabout. Second exit. When there's a gap."));
  CHECK_EQUAL(WithExitCallAfterManeuver("IN! AROUND! NOW!", "THIRD EXIT!"),
              std::string("IN! AROUND! THIRD EXIT! NOW!"));
  CHECK_EQUAL(WithExitCallAfterManeuver("Here's the roundabout.", "Exit 12."),
              std::string("Here's the roundabout. Exit 12."));
  CHECK_EQUAL(WithExitCallAfterManeuver("Go now.", "Exit 12."), std::string("Go now. Exit 12."));
  CHECK_EQUAL(WithExitCallAfterManeuver("Roundabout. Now.", ""), std::string("Roundabout. Now."));
}

TEST(EveryRoundaboutNowLineSaysTheExitBeforeTheJoke)
{
  // Whatever line is picked, the exit lands within the first two sentences of the call.
  for (int tier = 0; tier < 3; ++tier)
    for (auto const & line : LinesFor("countdown.roundabout.now", tier))
    {
      std::string const call = ExitCallFor({{"exit", "third"}}, tier);
      std::string const said = WithExitCallAfterManeuver(line, call);
      size_t const at = said.find(call);
      CHECK(at != std::string::npos);
      size_t sentences = 0;
      for (size_t i = 0; i < at; ++i)
        if ((said[i] == '.' || said[i] == '!' || said[i] == '?') && (i + 1 == at || said[i + 1] == ' '))
          ++sentences;
      if (sentences > 2)
        std::printf("  exit late: t%d: %s\n", tier, said.c_str());
      CHECK(sentences <= 2);
    }
}

TEST(RoundaboutNowCallSaysTheExitFirst)
{
  auto const said = NowCalls("At the roundabout, take the third exit onto Oak Street");
  CHECK(!said.empty());
  CHECK_EQUAL(said.back(), std::string("Roundabout. Third exit. You should be taking it now."));
  // The earlier nags keep the joke first and the exit last (loop B99).
  CHECK_EQUAL(said.front(), std::string("In 500 feet you are going to take the roundabout. Third exit."));
}
