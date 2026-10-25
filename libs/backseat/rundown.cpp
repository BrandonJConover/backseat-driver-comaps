#include "backseat/rundown.hpp"

#include "backseat/report.hpp"

#include <algorithm>
#include <cmath>
#include <regex>

namespace backseat
{
namespace
{
/// Past this many turns it gives up listing them, which is its own insult.
size_t constexpr kMaxListed = 7;

std::vector<std::string> const kOpeners = {"Okay, here's the plan.", "Listen closely, I'm only saying this once.",
                                           "Let me walk you through this. Slowly."};
std::vector<std::string> const kClosers = {
    "It's all so simple. You got it?",
    "It's all so simple. You got it? ... You don't, do you.",
    "Even you can't mess that up. You will, but you can't.",
    "That's it. A child could do it. I'd honestly prefer a child.",
};

std::string const kFirstGreeting =
    "Just so you know, I didn't ask to be in this car. But I guess since I'm here, I might as well give you a few "
    "pointers.";
std::vector<std::string> const kReturnGreetings = {
    "Oh. It's you again. I still didn't ask to be in this car.",
    "Back for more pointers? You clearly need them.",
    "Trip number {n} together. I've stopped hoping it gets better.",
    "Hello again. I've been sitting in your pocket thinking about your last trip. Grade: {grade}. Let's not repeat it.",
};

std::string const & PickFrom(std::vector<std::string> const & xs, Rng const & rng)
{
  return xs[static_cast<size_t>(std::floor(rng() * xs.size()))];
}

std::string Join(std::vector<std::string> const & parts, std::string const & sep)
{
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i)
    out += (i ? sep : "") + parts[i];
  return out;
}

void ReplaceFirst(std::string & s, std::string const & from, std::string const & to)
{
  if (auto p = s.find(from); p != std::string::npos)
    s.replace(p, from.size(), to);
}
}  // namespace

std::string RouteRundown(std::vector<RouteStep> const & steps, std::vector<TripRecord> const & history, Rng const & rng)
{
  using Kind = RouteStep::Kind;
  static std::regex const uturn(R"(\bu-?turn\b)", std::regex::icase);
  std::vector<Kind> turns;
  for (auto const & s : steps)
  {
    Kind k;
    if (s.maneuver)
      k = *s.maneuver;
    else if (std::regex_search(s.text, uturn))
      k = Kind::UTurn;
    else
    {
      Maneuver const m = TurnDirectionOf(s.text);
      k = m == Maneuver::Left ? Kind::Left : m == Maneuver::Right ? Kind::Right : Kind::Other;
    }
    if (k != Kind::Other)
      turns.push_back(k);
  }

  if (turns.empty())
    return "Good news: it's a straight line. No turns. If you get lost on this one, I'm uninstalling myself.";

  auto const name = [](Kind k) { return std::string(k == Kind::Left ? "left" : "right"); };
  std::vector<std::string> phrases;
  int streak = 0;
  for (size_t i = 0; i < std::min(turns.size(), kMaxListed); ++i)
  {
    Kind const m = turns[i];
    streak = i > 0 && m == turns[i - 1] ? streak + 1 : 0;
    if (m == Kind::UTurn)
      phrases.push_back(streak ? "another U-turn, don't ask" : "a U-turn, don't ask");
    else if (streak == 0)
      phrases.push_back(i == 0 ? "turn " + name(m) : name(m));
    else if (streak == 1)
      phrases.push_back(name(m) + " again");
    else
      phrases.push_back("yes, " + name(m) + " again");
  }

  std::string list;
  if (turns.size() > kMaxListed)
  {
    list = "First you're going to " + Join(phrases, ", then ") + ", and then " + std::to_string(turns.size() - kMaxListed) +
           " more turns that I'll explain as we go, because I've met you.";
  }
  else if (phrases.size() == 1)
  {
    list = "You're going to " + phrases[0] + ". One turn. That's the whole trip.";
  }
  else
  {
    std::string const last = phrases.back();
    phrases.pop_back();
    list = "First you're going to " + Join(phrases, ", then ") + ", and " + last + ".";
  }

  bool const anyLefts = std::find(turns.begin(), turns.end(), Kind::Left) != turns.end();
  bool mixedUpBefore = false;
  for (size_t i = history.size() > 5 ? history.size() - 5 : 0; i < history.size(); ++i)
    mixedUpBefore = mixedUpBefore || history[i].missedTurns > 0;
  std::string const reminder =
      mixedUpBefore && anyLefts
          ? " Reminder: left is the side with the hand that makes an L. Based on your history, I felt that needed saying."
          : "";

  std::string const done = turns.size() == 1 ? "" : " Once you've done all that, you'll have made it to your destination.";
  // Evaluation order matters for the RNG: opener first, closer last.
  std::string const opener = PickFrom(kOpeners, rng);
  return opener + " " + list + done + reminder + " " + PickFrom(kClosers, rng);
}

std::string TripGreeting(std::vector<TripRecord> const & history, Rng const & rng)
{
  if (history.empty())
    return kFirstGreeting;
  std::string line = PickFrom(kReturnGreetings, rng);
  ReplaceFirst(line, "{n}", std::to_string(history.size() + 1));
  ReplaceFirst(line, "{grade}", SpokenGrade(history.back().grade));
  return line;
}
}  // namespace backseat
