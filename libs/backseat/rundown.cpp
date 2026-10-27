#include "backseat/rundown.hpp"

#include "backseat/report.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <cmath>
#include <regex>

namespace backseat
{
namespace
{
/// Past this many turns it gives up listing them, which is its own insult.
size_t constexpr kMaxListed = 7;
/// The first-trip speed run lists more before giving up.
size_t constexpr kMaxRattled = 12;

std::string const kFirstTripDone = "See? Easy. Even you can't mess that up.";
/// Brandon's line, closing the first-trip rundown.
std::string const kPutOnMusic = "Now that we got the directions out of the way, why don't we put on some music.";
std::vector<std::string> const kSkips = {
    "You know what? No rundown today. Just listen when I talk. For once.",
    "I'm not going over the whole route again. You've heard me do it. You didn't listen then either.",
};

/// Rundown styles for returning drivers, taken in turn so two trips in a row never sound alike.
enum class Style
{
  Classic,
  FirstOnly,
  Count,
  Skip,
  Tally,
  Quiz
};
// The full turn list (Classic) comes last: it sounds too much like the first trip's speed run
// (Brandon, 2026-10-27: the same bit every time falls flat).
Style constexpr kStyles[] = {Style::Quiz, Style::FirstOnly, Style::Count, Style::Skip, Style::Tally, Style::Classic};

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
/// Taken in turn, like the rundown styles, so the trip start sounds different every time.
std::vector<std::string> const kReturnGreetings = {
    "Oh. It's you again. I still didn't ask to be in this car.",
    "Back for more pointers? You clearly need them.",
    "Trip number {n} together. I've stopped hoping it gets better.",
    "Hello again. I've been sitting in your pocket thinking about your last trip. Grade: {grade}. Let's not repeat it.",
    "We meet again. I was having a lovely time doing nothing.",
    "Seat belt on? Mirrors? Dignity? Two out of three, let's go.",
    "Ah, the driver. My favorite person to sit next to. That was sarcasm. I'm required to tell you.",
    "Let's get this over with. Last trip earned you a {grade}. Today's a fresh start. For me, mostly.",
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

using Kind = RouteStep::Kind;

std::string TurnName(Kind k) { return k == Kind::UTurn ? "U-turn" : k == Kind::Left ? "left" : "right"; }

std::string Plural(size_t n, std::string const & word) { return std::to_string(n) + " " + word + (n == 1 ? "" : "s"); }

std::string CountLine(size_t n)
{
  return n == 1 ? "One turn today. One. I believe in you. Barely."
                : std::to_string(n) + " turns today. I'll call out every one of them. Twice. Maybe three times.";
}

/// First trip ever: every turn in rapid succession, then Brandon's music line.
std::string RattleOff(std::vector<Kind> const & turns)
{
  if (turns.empty())
    return "Okay, real quick. It's a straight line. No turns. " + kFirstTripDone + " " + kPutOnMusic;
  std::vector<std::string> said;
  for (size_t i = 0; i < std::min(turns.size(), kMaxRattled); ++i)
    said.push_back(TurnName(turns[i]));
  said[0][0] = static_cast<char>(std::toupper(static_cast<unsigned char>(said[0][0])));
  std::string const more =
      turns.size() > kMaxRattled ? " And " + std::to_string(turns.size() - kMaxRattled) + " more after that." : "";
  return "Okay, real quick. " + Join(said, ", ") + "." + more + " " + kFirstTripDone + " " + kPutOnMusic;
}
}  // namespace

std::string RouteRundown(std::vector<RouteStep> const & steps, std::vector<TripRecord> const & history, Rng const & rng,
                         std::optional<size_t> tripsBefore)
{
  size_t const before = tripsBefore.value_or(history.size());
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

  if (before == 0)
    return RattleOff(turns);

  if (turns.empty())
    return "Good news: it's a straight line. No turns. If you get lost on this one, I'm uninstalling myself.";

  auto const lefts = static_cast<size_t>(std::count(turns.begin(), turns.end(), Kind::Left));
  auto const rights = static_cast<size_t>(std::count(turns.begin(), turns.end(), Kind::Right));
  Kind const first = turns[0];
  switch (kStyles[(before - 1) % std::size(kStyles)])
  {
  case Style::Classic: break;
  case Style::FirstOnly:
    return "First turn's a " + TurnName(first) +
           ". The rest I'll spoon-feed you as we go, since apparently that's how this works.";
  case Style::Count: return CountLine(turns.size());
  case Style::Skip: return PickFrom(kSkips, rng);
  case Style::Tally:
    if (lefts == 0 || rights == 0)
      return CountLine(turns.size());
    return Plural(lefts, "left") + " and " + Plural(rights, "right") +
           " today. Try to do them in the right order. That's the hard part, apparently.";
  case Style::Quiz:
    if (first == Kind::UTurn)
      return "Pop quiz: what's the first turn? ... It's a U-turn. You weren't going to say that. Nobody ever does.";
    return "Pop quiz: what's the first turn? ... It's a " + TurnName(first) + ". You were going to say " +
           (first == Kind::Left ? "right" : "left") + ". I know you were.";
  }

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
    // "I've met you" only lands once it actually has.
    std::string const why = before >= 3 ? "because I've met you" : "because something tells me I'll need to";
    list = "First you're going to " + Join(phrases, ", then ") + ", and then " + std::to_string(turns.size() - kMaxListed) +
           " more turns that I'll explain as we go, " + why + ".";
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

std::string TripGreeting(std::vector<TripRecord> const & history, Rng const &, std::optional<size_t> tripsBefore)
{
  size_t const before = tripsBefore.value_or(history.size());
  if (before == 0)
    return kFirstGreeting;
  size_t i = (before - 1) % kReturnGreetings.size();
  // No finished trip to grade yet: skip the greetings that need one.
  while (history.empty() && kReturnGreetings[i].find("{grade}") != std::string::npos)
    i = (i + 1) % kReturnGreetings.size();
  std::string line = kReturnGreetings[i];
  ReplaceFirst(line, "{n}", std::to_string(before + 1));
  if (!history.empty())
    ReplaceFirst(line, "{grade}", SpokenGrade(history.back().grade));
  return line;
}
}  // namespace backseat
