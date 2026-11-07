#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/navigator.hpp"
#include "backseat/persona_lines_data.hpp"
#include "backseat/rewriter.hpp"

#include <cctype>
#include <cstdio>
#include <set>
#include <string>

using namespace backseat;

namespace
{
std::set<std::string> Placeholders(std::string const & line)
{
  std::set<std::string> out;
  for (size_t at = line.find('{'); at != std::string::npos; at = line.find('{', at + 1))
  {
    auto const end = line.find('}', at);
    if (end == std::string::npos)
      break;
    std::string name = line.substr(at + 1, end - at - 1);
    for (auto & c : name)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    out.insert(name);
  }
  return out;
}

/// Restores the Critic however a test ends.
struct CriticAfter
{
  ~CriticAfter() { SetLinePersona(Persona::Critic); }
};
}  // namespace

TEST(PersonaLinesOnlyUseWhatTheCriticsLinesAreGiven)
{
  for (auto const * table : {&MelancholyLineData(), &ProudLineData(), &SantaLineData()})
    for (auto const & [key, tiers] : *table)
    {
      CHECK(!tiers[0].empty());
      if (key == "persona.leave")
        continue;
      auto const critic = LineData().find(key);
      if (critic == LineData().end())
      {
        std::fprintf(stderr, "persona key the Critic doesn't have: %s\n", key.c_str());
        CHECK(false);
        continue;
      }
      std::set<std::string> known;
      for (auto const & lines : critic->second)
        for (auto const & line : lines)
          for (auto const & p : Placeholders(line))
            known.insert(p);
      for (auto const & lines : tiers)
        for (auto const & line : lines)
          for (auto const & p : Placeholders(line))
            if (!known.count(p))
            {
              std::fprintf(stderr, "%s: {%s} in %s\n", key.c_str(), p.c_str(), line.c_str());
              CHECK(false);
            }
    }
}

TEST(EveryTurnByTurnKeyHasItsOwnLinesInEachPersona)
{
  // The lines a driver hears every trip never fall back to the Critic.
  for (char const * key : {"countdown.far", "countdown.near", "countdown.now", "countdown.still", "countdown.mention",
                           "countdown.done", "straight.please", "straight.still", "straight.serious", "missedTurn",
                           "rerouted", "turnCompleted.onTime", "speeding.barely", "arrived", "instruction.prefix",
                           "instruction.suffix", "cmd.voice", "cmd.voice.audition", "persona.leave"})
  {
    CHECK(PersonaHasLines(Persona::Melancholy, key));
    CHECK(PersonaHasLines(Persona::Proud, key));
    CHECK(PersonaHasLines(Persona::Santa, key));
  }
  CHECK(PersonaHasLines(Persona::Proud, "straight.serious+"));
  CHECK(!PersonaHasLines(Persona::Critic, "missedTurn"));
}

TEST(VoicesComeWithTheirPersonality)
{
  CHECK(PersonaForVoice("George") == Persona::Melancholy);
  CHECK(PersonaForVoice("bm_george") == Persona::Melancholy);
  CHECK(PersonaForVoice("Heart (American female)") == Persona::Proud);
  CHECK(PersonaForVoice("HEART") == Persona::Proud);
  CHECK(PersonaForVoice("am_santa") == Persona::Santa);
  CHECK(PersonaForVoice("Echo") == Persona::Critic);
  CHECK(PersonaForVoice("") == Persona::Critic);
  for (Persona const p : {Persona::Critic, Persona::Melancholy, Persona::Proud, Persona::Santa})
    CHECK(PersonaFrom(PersonaName(p)) == p);
  CHECK_EQUAL(std::string(PersonaTitle(Persona::Proud)), std::string("Proud Parent"));
}

TEST(ProudParentDebutsWithBrandonsMissedTurnLine)
{
  CriticAfter after;
  SaidLog said;
  // The Critic has missed-turn history...
  said.Pick("missedTurn", LinesFor("missedTurn", 0), 0.7);
  SetLinePersona(Persona::Proud);
  // ...but the Proud Parent's first one is still Brandon's.
  CHECK_EQUAL(said.Pick("missedTurn", LinesFor("missedTurn", 0), 0.7),
              std::string("You are doing so good! We can just take the next exit. I must have forgotten to tell you "
                          "to turn. My little driver would never miss a turn."));
  CHECK(said.Said("missedTurn"));
  // And a whole pool goes by before a line comes back.
  auto const & pool = LinesFor("missedTurn", 0);
  std::set<std::string> heard;
  for (size_t i = 0; i < pool.size(); ++i)
    heard.insert(said.Pick("missedTurn", pool, 0.3));
  CHECK_EQUAL(heard.size(), pool.size());
}

TEST(PersonaWithoutItsOwnLinesSpeaksLikeTheCritic)
{
  CriticAfter after;
  auto const critic = LinesFor("plateGame.offer", 0);
  SetLinePersona(Persona::Melancholy);
  CHECK(LinesFor("plateGame.offer", 0) == critic);
  CHECK(LinesFor("countdown.now", 0) != std::vector<std::string>(LineData().at("countdown.now")[0]));
}

TEST(PersonaLinesFollowTheTravelMode)
{
  CriticAfter after;
  SetLinePersona(Persona::Proud);
  SetLineTravelMode(TravelMode::Walk);
  for (int tier = 0; tier < 3; ++tier)
    for (auto const & line : LinesFor("hardBraking", tier))
      CHECK(LineFitsMode(line, TravelMode::Walk));
  // Its own turn lines fit a walk, so they stay its own.
  CHECK(!LinesFor("countdown.now", 0).empty());
  CHECK(LinesFor("countdown.now", 0)[0] == ProudLineData().at("countdown.now")[0][0]);
  SetLineTravelMode(TravelMode::Drive);
}

TEST(NavigatorPersonaFollowsTheVoice)
{
  CriticAfter after;
  EngineConfig config;
  config.rng = [] { return 0.0; };
  Navigator nav(config);
  nav.SetGpsName("George");
  CHECK(nav.GetPersona() == Persona::Melancholy);
  CHECK(LinePersona() == Persona::Melancholy);
  // A new voice says hello in its own manner; the GPS stays who it is.
  std::string const hello = nav.ReplyAs(Persona::Proud, "cmd.voice", {{"voice", "Heart"}});
  CHECK(hello.find("Heart") != std::string::npos);
  CHECK(LinePersona() == Persona::Melancholy);
  nav.SetGpsName("Echo");
  CHECK(nav.GetPersona() == Persona::Critic);
}

TEST(RewritesSoundLikeThePersona)
{
  CriticAfter after;
  std::string const critic = RewriteInstructions();
  SetLinePersona(Persona::Melancholy);
  CHECK(RewriteInstructions().find("melancholy") != std::string::npos);
  SetLinePersona(Persona::Proud);
  CHECK(RewriteInstructions().find("proud parent") != std::string::npos);
  CHECK(RewritePrompt({"Nice turn.", "turnCompleted", 90, {}}).find("tearfully proud") != std::string::npos);
  SetLinePersona(Persona::Critic);
  CHECK_EQUAL(RewriteInstructions(), critic);
}

TEST(DirtySantaDebutsWithBrandonsCarLine)
{
  CriticAfter after;
  SaidLog said;
  SetLinePersona(Persona::Santa);
  CHECK_EQUAL(said.Pick("idle", LinesFor("idle", 0), 0.9),
              std::string("Do you like the car you are driving? You should be grateful for it, because I gave it to "
                          "you. The elves worked especially hard to make the belt squeak at that exact pitch. You "
                          "should be grateful."));
  CHECK(RewriteInstructions().find("dirty Santa") != std::string::npos);
}
