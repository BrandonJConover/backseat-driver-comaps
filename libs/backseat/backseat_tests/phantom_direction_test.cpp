// Phantom directions (loop B143, idea BI-126, driving-safety gate): a joke, roast or reply that is not a
// direction must never shout a bare maneuver ("TURN LEFT!", "Turn around.") the driver could obey by
// reflex. Only the direction pools below may say one. Quoted words ('turn left' in Brandon's "X means
// 'turn left' in Y" lines) and reported or noun uses ("I told you to turn left", "one turn right") pass.
#include "check.hpp"

#include "backseat/lines_data.hpp"

#include <cctype>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
// Keys whose job is to give or discuss a real instruction.
char const * const kDirectionPools[] = {"countdown.", "lane.",     "wrongWay",    "missedTurn",  "rerouted",
                                        "uturn",      "instruction.", "hazard.",  "curve.",      "arrive.side.",
                                        "cmd.repeat", "cmd.nextTurn", "cmd.back"};

bool IsDirectionPool(std::string const & key)
{
  for (char const * p : kDirectionPools)
    if (key.rfind(p, 0) == 0)
      return true;
  return false;
}

/// Lowercased words, with single-quoted spans ('turn left') dropped. A quote opens only at the start of a
/// word, so apostrophes inside words (it's, driver's) stay put.
std::vector<std::string> Words(std::string const & text)
{
  std::vector<std::string> words;
  std::string cur;
  bool quoted = false;
  auto flush = [&] {
    if (!cur.empty() && !quoted)
      words.push_back(cur);
    cur.clear();
  };
  for (size_t i = 0; i < text.size(); ++i)
  {
    char const c = text[i];
    if (c == '\'')
    {
      bool const atWordStart = cur.empty() && (i == 0 || !std::isalnum(static_cast<unsigned char>(text[i - 1])));
      bool const atWordEnd = !cur.empty() && (i + 1 == text.size() || !std::isalnum(static_cast<unsigned char>(text[i + 1])));
      if (!quoted && atWordStart)
      {
        quoted = true;
        continue;
      }
      if (quoted && atWordEnd)
      {
        cur.clear();
        quoted = false;
        continue;
      }
      if (!cur.empty())
        cur += c;
      continue;
    }
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-')
      cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    else
      flush();
  }
  flush();
  return words;
}

bool Is(std::vector<std::string> const & w, size_t i, char const * s) { return i < w.size() && w[i] == s; }

/// True when the text says a maneuver as a command: "turn/go/bear/keep/veer left|right", "turn around",
/// "make a u-turn", "take the exit". A word before the verb that makes it reported speech or a noun
/// ("to turn left", "one turn right", "the wrong turn left") lets it pass.
bool PhantomDirection(std::string const & text)
{
  static std::set<std::string> const kVerbs = {"turn", "go", "bear", "keep", "veer"};
  // "to" counts as reported speech only after someone was told or asked ("told you to turn left");
  // "something to go right" still sounds like a command at speaking speed.
  static std::set<std::string> const kToldWho = {"you", "me", "him", "her", "them", "us", "told", "asked", "said"};
  static std::set<std::string> const kNotCommand = {"one", "a",    "no",   "the",  "every",
                                                    "wrong", "your", "that", "didn't", "won't", "don't", "can't",
                                                    "never", "not",  "can",  "could", "should", "would"};
  auto const w = Words(text);
  for (size_t i = 0; i < w.size(); ++i)
  {
    bool const reported = i > 0 && (kNotCommand.count(w[i - 1]) > 0 ||
                                    (w[i - 1] == "to" && i > 1 && kToldWho.count(w[i - 2]) > 0));
    if (reported)
      continue;
    if (kVerbs.count(w[i]) && (Is(w, i + 1, "left") || Is(w, i + 1, "right")))
      return true;
    if (Is(w, i, "turn") && Is(w, i + 1, "around"))
      return true;
    if (Is(w, i, "make") && Is(w, i + 1, "a") &&
        (Is(w, i + 2, "u-turn") || (Is(w, i + 2, "u") && Is(w, i + 3, "turn"))))
      return true;
    if (Is(w, i, "take") && Is(w, i + 1, "the") && Is(w, i + 2, "exit"))
      return true;
  }
  return false;
}
}  // namespace

TEST(PhantomDirectionDetector)
{
  CHECK(PhantomDirection("KNOCK KNOCK! TURN LEFT!"));
  CHECK(PhantomDirection("{PERCENT}! TELL MY FIRMWARE I LOVED IT! TURN LEFT IN... in..."));
  CHECK(PhantomDirection("AND TURN LEFT! OR RIGHT! WHATEVER IT IS!"));
  CHECK(PhantomDirection("POP QUIZ! I NEED SOMETHING TO GO RIGHT TODAY!"));
  CHECK(PhantomDirection("It's Saturday. Turn around. Go home."));
  CHECK(PhantomDirection("Make a U-turn. Kidding."));
  CHECK(PhantomDirection("make a u turn, I dare you"));
  CHECK(PhantomDirection("Take the exit. Any exit."));
  CHECK(PhantomDirection("Bear right, said no bear ever."));
  // Negatives.
  CHECK(!PhantomDirection("Gire a la izquierda means 'turn left' in Spanish. || Not now."));
  CHECK(!PhantomDirection("Vire à esquerda means 'turn left' in Portuguese. Don't."));
  CHECK(!PhantomDirection("I know I just told you to turn left at a stop sign. Consider this payback."));
  CHECK(!PhantomDirection("Food? You haven't even made one turn right today."));
  CHECK(!PhantomDirection("NO! We stay right here until somebody sober shows up!"));
  CHECK(!PhantomDirection("You didn't take the exit. Of course you didn't."));
  CHECK(!PhantomDirection("I asked you to keep right. Twice."));
  CHECK(!PhantomDirection("Left, right, it's all the same to you."));
  CHECK(!PhantomDirection("It's the driver's fault. It's always the driver's fault."));
  CHECK(!PhantomDirection(""));
}

TEST(NoPhantomDirectionsOutsideDirectionPools)
{
  int offenders = 0;
  for (auto const & [key, tiers] : LineData())
  {
    if (IsDirectionPool(key))
      continue;
    for (int tier = 0; tier < 3; ++tier)
      for (auto const & line : tiers[tier])
        if (PhantomDirection(line))
        {
          std::printf("  %s tier %d: %s\n", key.c_str(), tier, line.c_str());
          ++offenders;
        }
  }
  CHECK(offenders == 0);
}

TEST(DirectionPoolsStillGiveDirections)
{
  // The scan must keep seeing real maneuvers where they belong, or it is checking nothing.
  int found = 0;
  for (auto const & [key, tiers] : LineData())
    if (IsDirectionPool(key))
      for (auto const & tier : tiers)
        for (auto const & line : tier)
          found += PhantomDirection(line) ? 1 : 0;
  CHECK(found > 0);
}
