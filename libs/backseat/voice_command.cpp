#include "backseat/voice_command.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <sstream>

namespace backseat
{
namespace
{
/// Lowercase words separated by single spaces, padded with a space at each end.
std::string CommandWords(std::string const & text)
{
  std::string out = " ";
  for (unsigned char const ch : text)
  {
    char const c = std::isalnum(ch) || ch == '\'' ? static_cast<char>(std::tolower(ch)) : ' ';
    if (c != ' ' || out.back() != ' ')
      out += c;
  }
  if (out.back() != ' ')
    out += ' ';
  return out;
}

std::optional<LatLon> RouteKeyLatLon(std::string const & s)
{
  auto const comma = s.find(',');
  if (comma == std::string::npos)
    return {};
  char * end = nullptr;
  double const lat = std::strtod(s.c_str(), &end);
  if (end != s.c_str() + comma)
    return {};
  double const lon = std::strtod(s.c_str() + comma + 1, &end);
  if (end != s.c_str() + s.size())
    return {};
  return LatLon{lat, lon};
}
/// What comes after "take me to", ending in a space: "the starbucks please " -> "starbucks".
std::string ParseTarget(std::string const & rest)
{
  std::string target = " " + rest;
  for (bool trimmed = true; trimmed;)
  {
    trimmed = false;
    for (std::string const tail : {" please ", " thanks ", " thank you ", " now ", " asap "})
      if (target.size() >= tail.size() && target.compare(target.size() - tail.size(), tail.size(), tail) == 0)
      {
        target.erase(target.size() - tail.size() + 1);
        trimmed = true;
      }
  }
  target = target.size() < 2 ? "" : target.substr(1, target.size() - 2);
  for (std::string const lead : {"the ", "my "})
    if (target.rfind(lead, 0) == 0)
      target.erase(0, lead.size());
  return target;
}
}  // namespace

std::optional<DestinationCommand> ParseDestinationCommand(std::string const & spoken)
{
  std::string const words = CommandWords(spoken);
  std::string target;
  for (char const * cue : {" take me to ", " take us to ", " navigate to ", " drive me to ", " drive us to ",
                           " directions to ", " get me to ", " get us to ", " route to ", " route me to ",
                           " let's go to ", " lets go to ", " head to ", " go to ", " bring me to ", " take me back to ",
                           " take us back to ", " head back to "})
  {
    auto const at = words.find(cue);
    if (at != std::string::npos)
    {
      target = ParseTarget(words.substr(at + std::string(cue).size()));
      break;
    }
  }
  // "Take me back to where we started" isn't a place; TakeMeBack has it.
  if (target.rfind("where", 0) == 0 || target == "start" || target == "beginning")
    target.clear();
  // "Take me home" needs no "to".
  for (char const * cue : {" take me home ", " take us home ", " go home ", " drive me home ", " drive us home ",
                           " navigate home ", " head home ", " bring me home ", " get me home ", " get us home "})
    if (target.empty() && words.find(cue) != std::string::npos)
      target = "home";
  if (target.empty())
    return {};

  return DestinationCommand{target, SaidPolitely(spoken)};
}

bool SaidPolitely(std::string const & spoken)
{
  std::string const words = CommandWords(spoken);
  for (char const * nice : {" please ", " pls ", " thanks ", " thank you ", " hi ", " hello ", " hey ", " good morning ",
                            " good afternoon ", " good evening ", " morning ", " could you ", " would you ",
                            " can you ", " kindly ", " if you don't mind ", " when you get a chance "})
    if (words.find(nice) != std::string::npos)
      return true;
  return false;
}

std::vector<AppTool> const & AppTools()
{
  static std::vector<AppTool> const tools = {
      {AppAction::NavigateTo, "navigate_to", "Start directions to a place.", "Where to, as the driver said it: home, work, a name or an address."},
      {AppAction::CancelRoute, "cancel_route", "Stop navigating and clear the route.", nullptr},
      {AppAction::Mute, "mute", "Stop speaking directions and comments out loud.", nullptr},
      {AppAction::Unmute, "unmute", "Speak out loud again after being muted.", nullptr},
      {AppAction::RepeatInstruction, "repeat_instruction", "Say the last direction again.", nullptr},
      {AppAction::TimeLeft, "time_left", "Say how long until we get there.", nullptr},
      {AppAction::ReportPolice, "report_police", "The driver sees police or is being pulled over.", nullptr},
      {AppAction::PlayPlateGame, "play_plate_game", "Start the license plate game.", nullptr},
      {AppAction::AvoidTolls, "avoid_tolls", "Reroute without toll roads.", nullptr},
      {AppAction::AllowTolls, "allow_tolls", "Allow toll roads again.", nullptr},
      {AppAction::AvoidHighways, "avoid_highways", "Reroute without highways.", nullptr},
      {AppAction::AllowHighways, "allow_highways", "Allow highways again.", nullptr},
      {AppAction::ChangeVoice, "change_voice", "Change the speaking voice.", "Voice name, like Echo or Heart; empty to show the list."},
      {AppAction::ListVoices, "list_voices", "Have every voice introduce itself, with the phrase that picks it.", nullptr},
      {AppAction::ZoomIn, "zoom_in", "Zoom the map in.", nullptr},
      {AppAction::ZoomOut, "zoom_out", "Zoom the map out.", nullptr},
      {AppAction::Recenter, "recenter", "Center the map on the car.", nullptr},
      {AppAction::RouteOverview, "route_overview", "Show the whole route on the map.", nullptr},
      {AppAction::OpenSettings, "open_settings", "Open the settings screen.", nullptr},
      {AppAction::StopListening, "stop_listening", "Turn off the hands-free microphone.", nullptr},
      {AppAction::ListCommands, "list_commands", "Explain what the driver can say, and show the list.", nullptr},
      {AppAction::FindAlongRoute, "find_along_route", "Find a place to stop and add it to the route, keeping the destination.", "What to find: gas, food, coffee, a restroom, or a name."},
      {AppAction::SkipStop, "skip_stop", "Remove the next stop from the route.", nullptr},
      {AppAction::NextStop, "next_stop", "Say what the next stop on the route is.", nullptr},
      {AppAction::SpeedLimit, "speed_limit", "Say the speed limit here.", nullptr},
      {AppAction::CurrentSpeed, "current_speed", "Say how fast the car is going.", nullptr},
      {AppAction::CurrentRoad, "current_road", "Say what road the car is on.", nullptr},
      {AppAction::NextTurn, "next_turn", "Say what the next turn is.", nullptr},
      {AppAction::ArrivalTime, "arrival_time", "Say what time on the clock we'll get there.", nullptr},
      {AppAction::DifferentRoute, "different_route", "Try another way to the destination.", nullptr},
      {AppAction::TakeMeBack, "take_me_back", "Drive back to where this trip started.", nullptr},
      {AppAction::BeNicer, "be_nicer", "The driver wants the GPS to be nicer.", nullptr},
      {AppAction::BeMeaner, "be_meaner", "The driver wants the GPS to be meaner.", nullptr},
      {AppAction::GradeSoFar, "grade_so_far", "Give a report card for the trip so far.", nullptr},
      {AppAction::RoastMe, "roast_me", "Insult the driver, because they asked.", nullptr},
      {AppAction::TellJoke, "tell_joke", "Tell a joke.", nullptr},
      {AppAction::BlameGps, "blame_gps", "The driver says the GPS was wrong or at fault.", nullptr},
      {AppAction::WhatDoYouKnow, "what_do_you_know", "Read out what's saved about the driver.", nullptr},
      {AppAction::ForgetThat, "forget_that", "Delete something saved about the driver.", "What to forget; empty for the last thing read out."},
      {AppAction::SetTravelMode, "set_travel_mode", "Change how the trip is being made: walking, biking, driving or off-roading.", "walk, bike, drive or offroad."},
  };
  return tools;
}

AppTool const & ToolFor(AppAction action) { return AppTools()[static_cast<size_t>(action)]; }

std::optional<AppAction> AppActionNamed(std::string const & name)
{
  for (auto const & tool : AppTools())
    if (name == tool.name)
      return tool.action;
  return {};
}

namespace
{
/// The voice in "switch to the echo voice", "change your voice to heart", "use george".
std::string VoiceArgument(std::string const & words)
{
  for (char const * cue : {" voice to ", " sound like ", " talk like ", " switch to ", " change to ", " use "})
  {
    auto const at = words.find(cue);
    if (at == std::string::npos)
      continue;
    std::string rest = words.substr(at + std::string(cue).size());
    for (char const * lead : {"the ", "a ", "an "})
      if (rest.rfind(lead, 0) == 0)
        rest.erase(0, std::string(lead).size());
    auto const end = rest.find(' ');
    std::string name = rest.substr(0, end);
    if (!name.empty() && name != "voice" && name != "a" && name != "different" && name != "another" && name != "new")
      return name;
  }
  return {};
}
}  // namespace

namespace
{
/// Where in `words` a stop was asked for, and what for: "find gas", "I need a bathroom", "add a stop at Walmart".
std::pair<size_t, std::string> StopSearch(std::string const & words)
{
  static std::pair<char const *, char const *> const kWants[] = {
      {"gas", "gas station"},          {"gas station", "gas station"}, {"a gas station", "gas station"},
      {"fuel", "gas station"},         {"food", "restaurant"},         {"something to eat", "restaurant"},
      {"a restaurant", "restaurant"},  {"coffee", "coffee"},           {"a coffee shop", "coffee"},
      {"a restroom", "restroom"},      {"a bathroom", "restroom"},     {"restroom", "restroom"},
      {"bathroom", "restroom"}};
  for (char const * cue : {" find ", " need ", " stop for ", " add a stop for ", " find me ", " need to find "})
    for (auto const & want : kWants)
    {
      auto const at = words.find(cue + std::string(want.first) + " ");
      if (at != std::string::npos)
        return {at, want.second};
    }
  for (char const * cue : {" add a stop at ", " add a stop for "})
  {
    auto const at = words.find(cue);
    if (at != std::string::npos)
      return {at, ParseTarget(words.substr(at + std::string(cue).size()))};
  }
  return {0, {}};
}
}  // namespace

std::vector<AppCommand> ParseAppCommands(std::string const & spoken)
{
  std::string const words = CommandWords(spoken);
  struct Cues
  {
    AppAction action;
    std::vector<char const *> phrases;
  };
  // Phrases specific enough for a microphone that's always on: plain chatter shouldn't match.
  static std::vector<Cues> const cues = {
      {AppAction::CancelRoute, {" cancel the route ", " cancel route ", " cancel navigation ", " cancel the navigation ",
                                " stop navigation ", " stop navigating ", " end the route ", " end route ",
                                " end navigation ", " cancel the trip ", " end the trip ", " forget the route ",
                                " cancel directions ", " stop the directions ", " stop directions "}},
      {AppAction::Unmute, {" unmute ", " un mute ", " you can talk again ", " talk to me again ", " start talking again ",
                           " sound back on ", " turn the sound back on ", " you can speak again "}},
      {AppAction::Mute, {" mute ", " mute yourself ", " be quiet ", " shut up ", " stop talking ", " quiet please ",
                         " go quiet ", " turn off the sound ", " turn the sound off ", " hush "}},
      {AppAction::RepeatInstruction, {" repeat that ", " say that again ", " what did you say ", " come again ",
                                      " repeat the direction ", " repeat the directions ", " repeat the last ",
                                      " what was that ", " say again ", " repeat yourself "}},
      {AppAction::NextTurn, {" what's the next turn ", " whats the next turn ", " what is the next turn ",
                             " where do i turn ", " when do i turn ", " when's my next turn ", " whens my next turn ",
                             " what's my next turn ", " whats my next turn "}},
      {AppAction::BeNicer, {" be nicer ", " be nice ", " nicer to me ", " stop being mean ", " stop being so mean ", " be kinder "}},
      {AppAction::BeMeaner, {" be meaner ", " be mean ", " meaner to me ", " be more mean "}},
      {AppAction::GradeSoFar, {" grade me ", " grade so far ", " what's my grade ", " whats my grade ", " how am i doing ",
                               " how am i driving ", " my report card ", " give me a report card "}},
      {AppAction::RoastMe, {" roast me ", " insult me ", " make fun of me "}},
      {AppAction::TellJoke, {" tell me a joke ", " tell a joke ", " tell us a joke ", " say something funny ", " got any jokes "}},
      {AppAction::BlameGps, {" your fault ", " you're wrong ", " youre wrong ", " you were wrong ", " that's on you ",
                             " thats on you ", " you messed up ", " you got it wrong ", " blame you "}},
      {AppAction::WhatDoYouKnow, {" what do you know about me ", " what do you remember ", " what's in my file ",
                                  " whats in my file ", " read my file ", " what have you saved "}},
      {AppAction::SkipStop, {" skip this stop ", " skip the stop ", " skip the next stop ", " remove the stop ",
                             " remove this stop ", " cancel the stop ", " cancel this stop ", " no more stops "}},
      {AppAction::NextStop, {" what's the next stop ", " whats the next stop ", " what is the next stop ",
                             " where's the next stop ", " wheres the next stop ", " where is the next stop ",
                             " how far is the next stop ", " how far is the stop "}},
      {AppAction::SpeedLimit, {" what's the speed limit ", " whats the speed limit ", " what is the speed limit ",
                               " how fast can i go "}},
      {AppAction::CurrentSpeed, {" how fast am i ", " how fast are we ", " what's my speed ", " whats my speed ",
                                 " what is my speed ", " what speed am i "}},
      {AppAction::CurrentRoad, {" what road ", " what street ", " which road ", " which street ", " what highway ",
                                " road is this ", " street is this "}},
      {AppAction::ArrivalTime, {" what time will we ", " what time will i ", " what time do we ", " what time do i ",
                                " when will we arrive ", " when will i arrive ", " arrival time ", " what's our arrival ",
                                " whats our arrival "}},
      {AppAction::DifferentRoute, {" different route ", " another route ", " other route ", " better route ",
                                   " alternate route ", " alternative route ", " another way ", " different way "}},
      {AppAction::TakeMeBack, {" take me back ", " take us back ", " go back to where ", " head back ",
                               " back where we started ", " back to the start "}},
      {AppAction::TimeLeft, {" how long ", " how much longer ", " are we there yet ", " time left ", " when will we get ",
                             " when do we get ", " when will we be ", " what's the eta ", " whats the eta ", " eta ",
                             " how far ", " how much further ", " how much farther "}},
      {AppAction::AvoidTolls, {" avoid tolls ", " avoid toll ", " no tolls ", " no toll roads ", " avoid the tolls ",
                               " without tolls ", " skip the tolls ", " no toll "}},
      {AppAction::AllowTolls, {" allow tolls ", " use tolls ", " use toll roads ", " tolls are fine ", " tolls are okay ",
                               " tolls are ok ", " toll roads are fine ", " take the toll road ", " take toll roads "}},
      {AppAction::AvoidHighways, {" avoid highways ", " avoid the highway ", " avoid highway ", " no highways ",
                                  " avoid freeways ", " avoid the freeway ", " no freeways ", " take back roads ",
                                  " take the back roads ", " stay off the highway "}},
      {AppAction::AllowHighways, {" allow highways ", " use highways ", " use the highway ", " take the highway ",
                                  " highways are fine ", " highways are okay ", " take the freeway ", " use the freeway "}},
      {AppAction::ChangeVoice, {" change your voice ", " change the voice ", " change voice ", " switch voice ",
                                " switch voices ", " different voice ", " another voice ", " new voice ", " voice to ",
                                " use the voice ", " voice please "}},
      {AppAction::ListVoices, {" what voice options ", " what voices ", " which voices ", " voice options ",
                               " list the voices ", " list voices ", " what other voices ", " show me the voices ",
                               " other voices ", " voices do you have ", " voice choices "}},
      {AppAction::ZoomIn, {" zoom in "}},
      {AppAction::ZoomOut, {" zoom out "}},
      {AppAction::Recenter, {" recenter ", " re center ", " center the map ", " center on me ", " where am i ",
                             " show me where i am "}},
      {AppAction::RouteOverview, {" show the route ", " show me the route ", " route overview ", " show the whole route ",
                                  " show me the whole route ", " overview "}},
      {AppAction::OpenSettings, {" open settings ", " open the settings ", " show settings ", " show me the settings ",
                                 " settings menu ", " go to settings ", " open preferences "}},
      {AppAction::StopListening, {" stop listening ", " quit listening ", " stop eavesdropping ", " mic off ",
                                  " microphone off ", " turn off the mic ", " turn off the microphone ",
                                  " turn the mic off ", " turn the microphone off "}},
      {AppAction::ListCommands, {" what can you do ", " what can i say ", " what commands ", " list commands ",
                                 " list the commands ", " what are your commands ", " how do i use you ",
                                 " how does this work ", " what do you do ", " help me out ", " i need help "}},
  };
  std::vector<std::pair<size_t, AppCommand>> found;
  bool const voiceNamed = words.find(" voice ") != std::string::npos;
  for (auto const & c : cues)
  {
    for (char const * phrase : c.phrases)
    {
      auto const at = words.find(phrase);
      if (at == std::string::npos)
        continue;
      AppCommand command{c.action, {}};
      if (c.action == AppAction::ChangeVoice)
        command.argument = VoiceArgument(words);
      found.push_back({at, std::move(command)});
      break;
    }
  }
  // "Use the echo voice" / "switch to heart voice": a voice named without the cue words above.
  if (voiceNamed && std::none_of(found.begin(), found.end(), [](auto const & f) { return f.second.action == AppAction::ChangeVoice; }))
    if (auto name = VoiceArgument(words); !name.empty())
      found.push_back({words.find(" voice "), AppCommand{AppAction::ChangeVoice, std::move(name)}});
  // "How far is the next stop?" is about the stop, not the trip, and "take me back to work" is a place.
  auto const drop = [&found](AppAction action)
  {
    found.erase(std::remove_if(found.begin(), found.end(), [action](auto const & f) { return f.second.action == action; }),
                found.end());
  };
  auto const heardAction = [&found](AppAction action)
  { return std::any_of(found.begin(), found.end(), [action](auto const & f) { return f.second.action == action; }); };
  if (heardAction(AppAction::NextStop))
    drop(AppAction::TimeLeft);
  if (words.find(" back to ") != std::string::npos && words.find(" back to where ") == std::string::npos &&
      words.find(" back to the start ") == std::string::npos)
    drop(AppAction::TakeMeBack);
  // "Forget my favorite color", "forget that". Not "I always forget my keys": an order has no "I" first.
  auto const anOrder = [&words](size_t at) { return at != std::string::npos && words.find(" i ") > at; };
  for (char const * cue : {" forget that ", " forget about that ", " delete that "})
    if (auto const at = words.find(cue); anOrder(at) && !heardAction(AppAction::ForgetThat))
      found.push_back({at, AppCommand{AppAction::ForgetThat, {}}});
  for (char const * cue : {" forget my ", " forget what i said about ", " delete my "})
    if (auto const at = words.find(cue); anOrder(at) && !heardAction(AppAction::ForgetThat))
      found.push_back({at, AppCommand{AppAction::ForgetThat, ParseTarget(words.substr(at + std::string(cue).size()))}});
  for (auto const & mode : {std::pair<char const *, char const *>{" off roading ", "offroad"}, {" offroading ", "offroad"},
                            {" off road mode ", "offroad"}, {" going off road ", "offroad"}, {" we're off road ", "offroad"},
                            {" back on the road ", "drive"}, {" walking directions ", "walk"}, {" i'm walking ", "walk"},
                            {" im walking ", "walk"}, {" on foot ", "walk"}, {" switch to walking ", "walk"},
                            {" going for a hike ", "walk"}, {" i'm hiking ", "walk"}, {" im hiking ", "walk"},
                            {" biking directions ", "bike"}, {" bike directions ", "bike"}, {" i'm biking ", "bike"},
                            {" im biking ", "bike"}, {" switch to biking ", "bike"}, {" on my bike ", "bike"},
                            {" driving directions ", "drive"}, {" i'm driving ", "drive"}, {" im driving ", "drive"},
                            {" switch to driving ", "drive"}})
    if (auto const at = words.find(mode.first); at != std::string::npos && !heardAction(AppAction::SetTravelMode))
      found.push_back({at, AppCommand{AppAction::SetTravelMode, mode.second}});
  auto const stop = StopSearch(words);
  if (!stop.second.empty())
    found.push_back({stop.first, AppCommand{AppAction::FindAlongRoute, stop.second}});
  // "What other voices do you have?" isn't asking for a particular one.
  if (std::any_of(found.begin(), found.end(), [](auto const & f) { return f.second.action == AppAction::ListVoices; }))
    found.erase(std::remove_if(found.begin(), found.end(), [](auto const & f)
    { return f.second.action == AppAction::ChangeVoice && f.second.argument.empty(); }), found.end());
  // "Unmute" also contains "mute", and "turn the sound back on" isn't muting.
  bool const unmuted = std::any_of(found.begin(), found.end(), [](auto const & f) { return f.second.action == AppAction::Unmute; });
  if (unmuted)
    found.erase(std::remove_if(found.begin(), found.end(), [](auto const & f) { return f.second.action == AppAction::Mute; }), found.end());
  if (auto const destination = ParseDestinationCommand(spoken))
  {
    // "Go to settings" and "switch to echo" aren't places.
    std::string const & t = destination->target;
    bool const notAPlace = t.find("settings") != std::string::npos || t.find("voice") != std::string::npos ||
                           std::any_of(found.begin(), found.end(), [](auto const & f)
    { return f.second.action == AppAction::ChangeVoice || f.second.action == AppAction::OpenSettings; });
    if (!notAPlace)
    {
      size_t at = std::string::npos;
      for (char const * cue : {" take ", " navigate ", " drive ", " directions ", " get ", " route ", " let's ",
                               " lets ", " head ", " go ", " bring "})
        at = std::min(at, words.find(cue));
      found.push_back({at, AppCommand{AppAction::NavigateTo, t}});
    }
  }
  std::stable_sort(found.begin(), found.end(), [](auto const & a, auto const & b) { return a.first < b.first; });
  std::vector<AppCommand> out;
  for (auto & f : found)
    out.push_back(std::move(f.second));
  return out;
}

namespace
{
std::vector<std::string> WordsOf(std::string const & text)
{
  std::istringstream in(CommandWords(text));
  std::vector<std::string> words;
  std::string word;
  while (in >> word)
    words.push_back(word);
  return words;
}

bool IsSmallWord(std::string const & word)
{
  static char const * const kSmall[] = {"a", "an", "the", "to", "of", "in", "on", "at", "me", "my", "i", "you", "your",
                                        "it", "it's", "is", "are", "and", "or", "we", "us", "be", "that", "this", "for"};
  return std::find_if(std::begin(kSmall), std::end(kSmall), [&word](char const * s) { return word == s; }) != std::end(kSmall);
}

bool Contains(std::vector<std::string> const & words, std::string const & word)
{
  return std::find(words.begin(), words.end(), word) != words.end();
}
}  // namespace

bool IsEchoOf(std::string const & heard, std::string const & line)
{
  auto const said = WordsOf(heard);
  auto const spoken = WordsOf(line);
  size_t meaningful = 0, matched = 0;
  for (auto const & word : said)
  {
    if (IsSmallWord(word))
      continue;
    ++meaningful;
    matched += Contains(spoken, word) ? 1 : 0;
  }
  // "Take me to the store" shouldn't be an echo of a line with "take me to the" in it.
  if (meaningful < 2)
    return false;
  if (static_cast<double>(matched) / meaningful >= 0.6)
    return true;
  size_t constexpr kRun = 3;
  std::string const paddedLine = CommandWords(line);
  for (size_t i = 0; i + kRun <= said.size(); ++i)
  {
    size_t bigWords = 0;
    std::string run = " ";
    for (size_t j = i; j < i + kRun; ++j)
    {
      run += said[j] + " ";
      bigWords += IsSmallWord(said[j]) ? 0 : 1;
    }
    if (bigWords >= 2 && paddedLine.find(run) != std::string::npos)
      return true;
  }
  return false;
}

bool PlaceMatches(std::string const & asked, std::string const & name)
{
  std::string squashed;
  for (auto const & word : WordsOf(name))
    squashed += word;
  size_t big = 0, found = 0;
  for (auto const & word : WordsOf(asked))
  {
    if (IsSmallWord(word))
      continue;
    ++big;
    // Squashed so "walmart" finds "Wal-Mart".
    found += squashed.find(word) != std::string::npos ? 1 : 0;
  }
  return big == 0 || found * 2 >= big;
}

bool IsJunkAnswer(std::string const & topic, std::string const & fact)
{
  std::string const words = CommandWords(fact);
  if (IsFiller(fact))
    return true;
  for (char const * dodge : {" let's see ", " lets see ", " let me think ", " let me see ", " hold on ", " hang on ",
                             " what was the question ", " i don't know ", " i dont know ", " good question ", " i forget "})
    if (words.find(dodge) != std::string::npos)
      return true;
  // The question said back.
  std::string const asked = CommandWords(topic);
  return asked != " " && words.find(asked) != std::string::npos;
}

std::optional<std::string> MemoryTopicNamed(DriverMemory const & memory, std::string const & named)
{
  std::string const wanted = CommandWords(named);
  if (wanted == " ")
    return {};
  for (auto const & m : memory.All())
    if (CommandWords(m.topic).find(wanted) != std::string::npos || wanted.find(CommandWords(m.topic)) != std::string::npos)
      return m.topic;
  for (auto const & m : memory.All())
    if (CommandWords(m.fact).find(wanted) != std::string::npos)
      return m.topic;
  return {};
}

void SpokenLines::Add(double tMs, std::string line)
{
  m_lines.push_back({tMs, std::move(line)});
}

std::optional<std::string> SpokenLines::EchoOf(double tMs, std::string const & heard)
{
  m_lines.erase(std::remove_if(m_lines.begin(), m_lines.end(), [tMs](Spoken const & s) { return tMs - s.tMs > kWindowMs; }),
                m_lines.end());
  for (auto it = m_lines.rbegin(); it != m_lines.rend(); ++it)
    if (IsEchoOf(heard, it->line))
      return it->line;
  return {};
}

bool IsFiller(std::string const & spoken)
{
  static char const * const kFiller[] = {"yeah", "yes", "yep", "yup", "ok", "okay", "uh", "um", "umm", "mm", "mmm", "hmm",
                                         "huh", "oh", "ah", "no", "nope", "nah", "sure", "right", "so", "well"};
  auto const words = WordsOf(spoken);
  return std::all_of(words.begin(), words.end(), [](std::string const & word)
  { return std::find_if(std::begin(kFiller), std::end(kFiller), [&word](char const * f) { return word == f; }) != std::end(kFiller); });
}

bool WordsSupportTool(AppAction action, std::string const & argument, std::string const & spoken)
{
  auto const said = WordsOf(spoken);
  auto const saidAny = [&said](std::initializer_list<char const *> words)
  { return std::any_of(words.begin(), words.end(), [&said](char const * w) { return Contains(said, w); }); };
  auto const argumentSaid = [&]
  {
    auto const wanted = WordsOf(argument);
    size_t big = 0, found = 0;
    for (auto const & word : wanted)
    {
      if (IsSmallWord(word))
        continue;
      ++big;
      found += Contains(said, word) ? 1 : 0;
    }
    return big > 0 && found * 2 >= big;
  };
  switch (action)
  {
  case AppAction::NavigateTo:
    return argumentSaid() || ParseDestinationCommand(spoken) ||
           saidAny({"take", "navigate", "drive", "directions", "route", "head", "bring", "go"});
  case AppAction::CancelRoute: return saidAny({"cancel", "stop", "end", "forget", "quit"});
  case AppAction::Mute: return saidAny({"mute", "quiet", "shut", "hush", "silence", "stop", "sound"});
  case AppAction::Unmute: return saidAny({"unmute", "un", "talk", "speak", "sound", "back"});
  case AppAction::RepeatInstruction: return saidAny({"repeat", "again", "what", "what's", "whats", "next", "where"});
  case AppAction::TimeLeft:
    return saidAny({"long", "far", "eta", "time", "when", "there", "minutes", "left", "longer", "further", "farther"});
  case AppAction::ReportPolice: return saidAny({"police", "cop", "cops", "pulled", "pulling", "trooper", "sheriff", "officer"});
  case AppAction::PlayPlateGame: return saidAny({"plate", "plates", "license", "game"});
  case AppAction::AvoidTolls:
  case AppAction::AllowTolls: return saidAny({"toll", "tolls"});
  case AppAction::AvoidHighways:
  case AppAction::AllowHighways: return saidAny({"highway", "highways", "freeway", "freeways", "interstate", "back"});
  case AppAction::ChangeVoice:
    return argumentSaid() || saidAny({"voice", "voices", "switch", "change", "sound"});
  case AppAction::ListVoices: return saidAny({"voice", "voices"});
  case AppAction::ZoomIn:
  case AppAction::ZoomOut: return saidAny({"zoom", "closer", "farther"});
  case AppAction::Recenter: return saidAny({"center", "recenter", "re", "where"});
  case AppAction::RouteOverview: return saidAny({"route", "overview", "map", "whole"});
  case AppAction::OpenSettings: return saidAny({"settings", "preferences", "options"});
  case AppAction::StopListening: return saidAny({"listening", "mic", "microphone", "eavesdropping"});
  case AppAction::ListCommands: return saidAny({"commands", "command", "help", "can", "how", "do"});
  case AppAction::FindAlongRoute:
    return argumentSaid() || saidAny({"find", "need", "stop", "gas", "food", "coffee", "restroom", "bathroom", "fuel", "eat"});
  case AppAction::SkipStop: return saidAny({"stop", "stops"}) && saidAny({"skip", "remove", "cancel", "no"});
  case AppAction::NextStop: return saidAny({"stop"});
  case AppAction::SpeedLimit: return saidAny({"limit"}) || saidAny({"fast"});
  case AppAction::CurrentSpeed: return saidAny({"fast", "speed"});
  case AppAction::CurrentRoad: return saidAny({"road", "street", "highway"});
  case AppAction::NextTurn: return saidAny({"turn"});
  case AppAction::ArrivalTime: return saidAny({"time", "arrive", "arrival"});
  case AppAction::DifferentRoute: return saidAny({"route", "way"}) && saidAny({"different", "another", "other", "better", "alternate", "alternative"});
  case AppAction::TakeMeBack: return saidAny({"back", "start"});
  case AppAction::BeNicer: return saidAny({"nice", "nicer", "kinder", "mean"});
  case AppAction::BeMeaner: return saidAny({"mean", "meaner"});
  case AppAction::GradeSoFar: return saidAny({"grade", "doing", "driving", "card"});
  case AppAction::RoastMe: return saidAny({"roast", "insult", "fun"});
  case AppAction::TellJoke: return saidAny({"joke", "jokes", "funny"});
  case AppAction::BlameGps: return saidAny({"fault", "wrong", "blame", "messed"}) || saidAny({"on"}) && saidAny({"you"});
  case AppAction::WhatDoYouKnow: return saidAny({"know", "remember", "file", "saved"});
  case AppAction::ForgetThat: return saidAny({"forget", "delete"});
  case AppAction::SetTravelMode:
    return saidAny({"walk", "walking", "foot", "hike", "hiking", "bike", "biking", "cycling", "drive", "driving", "off", "offroading", "offroad", "road"});
  case AppAction::Count: break;
  }
  return false;
}

namespace
{
/// "Peace Lutheran" and "Peace Lutheran and Rogers" are the same place being asked for.
bool SameDestination(std::string const & a, std::string const & b)
{
  std::string const wordsA = CommandWords(a), wordsB = CommandWords(b);
  return wordsA.find(wordsB) != std::string::npos || wordsB.find(wordsA) != std::string::npos;
}
}  // namespace

bool CommandRepeats::IsRepeat(AppCommand const & command, double tMs)
{
  // Asking twice is the point of these.
  if (command.action == AppAction::RepeatInstruction || command.action == AppAction::TimeLeft ||
      command.action == AppAction::ZoomIn || command.action == AppAction::ZoomOut)
    return false;
  m_ran.erase(std::remove_if(m_ran.begin(), m_ran.end(), [tMs](Ran const & r) { return tMs - r.tMs >= kRepeatMs; }), m_ran.end());
  std::string const key = std::to_string(static_cast<int>(command.action)) + CommandWords(command.argument);
  if (command.action == AppAction::NavigateTo)
  {
    bool const building = m_buildingSince && tMs - *m_buildingSince < kBuildingMaxMs;
    if ((building || m_lastNavigateAt && tMs - *m_lastNavigateAt < kRepeatMs) && SameDestination(m_buildingTo, command.argument))
      return true;
    m_buildingSince = tMs;
    m_lastNavigateAt = tMs;
    m_buildingTo = command.argument;
    return false;
  }
  if (std::any_of(m_ran.begin(), m_ran.end(), [&key](Ran const & r) { return r.key == key; }))
    return true;
  m_ran.push_back({tMs, key});
  return false;
}

bool SaidYes(std::string const & spoken)
{
  std::string const words = CommandWords(spoken);
  for (char const * no : {" no ", " nope ", " not ", " nah "})
    if (words.find(no) != std::string::npos)
      return false;
  for (char const * yes : {" yes ", " yeah ", " yep ", " yup ", " sure ", " correct ", " right ", " you got it ",
                           " uh huh ", " ok ", " okay ", " affirmative ", " exactly ", " you know it ", " of course "})
    if (words.find(yes) != std::string::npos)
      return true;
  return false;
}

bool SaidNo(std::string const & spoken)
{
  std::string const words = CommandWords(spoken);
  for (char const * no : {" no ", " nope ", " nah ", " not ", " negative ", " nuh uh ", " no way "})
    if (words.find(no) != std::string::npos)
      return true;
  return false;
}

namespace
{
char const * const kPartnerWords[] = {"wife", "husband", "partner", "girlfriend", "boyfriend", "fiance", "fiancee"};
}  // namespace

std::optional<std::string> ParseBusinessOwner(std::string const & spoken)
{
  std::string words = CommandWords(spoken);
  // "wife's" and "wife’s" (which comes through as "wife s") both lose the s.
  for (std::string const tail : {"'s ", " s "})
    for (auto at = words.find(tail); at != std::string::npos; at = words.find(tail))
      words.replace(at, tail.size(), " ");
  for (char const * partner : kPartnerWords)
    if (words.find(" " + std::string(partner) + " ") != std::string::npos)
      return std::string(partner);
  for (char const * mine : {" me ", " mine ", " myself ", " i do ", " my own ", " my business ", " i own "})
    if (words.find(mine) != std::string::npos)
      return std::string();
  std::string name = ParseNameAnswer(words);
  if (name.empty())
    return {};
  return name;
}

std::string BusinessOwnerPossessive(std::string const & owner)
{
  if (owner.empty())
    return "your";
  for (char const * partner : kPartnerWords)
    if (owner == partner)
      return "your " + owner + "'s";
  return owner + "'s";
}

std::string ParseFavorite(std::string const & spoken, std::vector<std::string> const & drop)
{
  std::string words = CommandWords(spoken);
  auto const erase = [&words](std::string const & phrase)
  {
    std::string const padded = " " + phrase + " ";
    for (auto at = words.find(padded); at != std::string::npos; at = words.find(padded))
      words.replace(at, padded.size() - 1, "");
  };
  // Longest first, so "i would say" goes before "i".
  for (char const * filler : {"my favorite is", "my favorite", "favorite", "i would say", "i'd say", "i guess",
                              "i think", "i like", "i love", "probably", "definitely", "obviously", "it's", "its",
                              "it is", "um", "uh", "hmm", "well", "oh", "the", "flavor", "please", "for sure"})
    erase(filler);
  for (auto const & d : drop)
    erase(d);
  std::vector<std::string> kept;
  size_t start = 0;
  while (kept.size() < 4)
  {
    auto const b = words.find_first_not_of(' ', start);
    if (b == std::string::npos)
      break;
    auto const e = words.find(' ', b);
    kept.push_back(words.substr(b, e - b));
    start = e;
  }
  std::string out;
  for (auto const & w : kept)
    out += (out.empty() ? "" : " ") + w;
  return out;
}

std::string ParseBedSide(std::string const & spoken)
{
  std::string const words = CommandWords(spoken);
  for (char const * side : {"left", "right", "middle"})
    if (words.find(std::string(" ") + side + " ") != std::string::npos)
      return side;
  return {};
}

bool DeclinedToAnswer(std::string const & spoken)
{
  std::string const words = CommandWords(spoken);
  for (char const * no : {" none ", " pass ", " skip ", " don't know ", " dont know ", " no idea ", " not telling ",
                          " none of your business ", " no comment ", " rather not ", " nothing ", " i don't have one ",
                          " no thanks ", " nope "})
    if (words.find(no) != std::string::npos)
      return true;
  return words.find_first_not_of(' ') == std::string::npos;
}

int ParseRating(std::string const & spoken)
{
  std::string const words = CommandWords(spoken);
  char const * const names[] = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"};
  for (int n = 10; n >= 1; --n)
  {
    if (words.find(" " + std::to_string(n) + " ") != std::string::npos ||
        words.find(std::string(" ") + names[n - 1] + " ") != std::string::npos)
      return n;
  }
  return 0;
}

std::string ParseNameAnswer(std::string const & spoken)
{
  std::string words = CommandWords(spoken);
  std::string rest;
  for (char const * cue : {" my name is ", " my name's ", " call me ", " name is ", " i am ", " i'm ", " im ", " it's ",
                           " its ", " this is "})
  {
    auto const at = words.find(cue);
    if (at != std::string::npos)
    {
      rest = words.substr(at + std::string(cue).size());
      break;
    }
  }
  if (rest.empty())
    rest = words.substr(1);
  // Up to two words of name, minus filler.
  std::string name;
  int taken = 0;
  size_t pos = 0;
  while (taken < 2 && pos < rest.size())
  {
    auto const end = rest.find(' ', pos);
    std::string word = rest.substr(pos, end - pos);
    pos = end == std::string::npos ? rest.size() : end + 1;
    if (word.empty())
      continue;
    static char const * const kFiller[] = {"um", "uh", "well", "hi", "hey", "hello", "oh", "so", "and", "please", "thanks",
                                          "just", "it", "yeah", "ok", "okay"};
    if (taken == 0 && std::find(std::begin(kFiller), std::end(kFiller), word) != std::end(kFiller))
      continue;
    if (taken == 1 && std::find(std::begin(kFiller), std::end(kFiller), word) != std::end(kFiller))
      break;
    word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
    name += (name.empty() ? "" : " ") + word;
    ++taken;
  }
  return name;
}

std::string MisheardName(std::string const & name, unsigned variant)
{
  if (name.empty())
    return "Gary";
  std::string const first = name.substr(0, name.find(' '));
  auto const isVowel = [](char c) { return std::string("aeiouAEIOU").find(c) != std::string::npos; };
  // Ways to mishear it, tried in turn from `variant` until one changes something.
  auto const shiftVowel = [&](std::string n)
  {
    // The first vowel after the first letter: Brandon -> Brendon, Sarah -> Serah, Mike -> Moke.
    static std::string const from = "aeiou", to = "eiauo";
    for (size_t i = 1; i < n.size(); ++i)
      if (auto const at = from.find(static_cast<char>(std::tolower(static_cast<unsigned char>(n[i]))));
          at != std::string::npos)
      {
        n[i] = to[at];
        return n;
      }
    return n;
  };
  auto const shiftEnding = [&](std::string n)
  {
    // The last consonant: Brandon -> Brandom, David -> Davit, Jess -> Jezz.
    static std::string const from = "ndtsmkgbpl", to = "mtdznmkpbr";
    for (size_t i = n.size(); i-- > 1;)
    {
      char const c = static_cast<char>(std::tolower(static_cast<unsigned char>(n[i])));
      if (auto const at = from.find(c); at != std::string::npos)
      {
        n[i] = to[at];
        return n;
      }
      if (!isVowel(n[i]))
        break;
    }
    return n;
  };
  auto const addY = [&](std::string n)
  {
    // Brandon -> Brandy, Kate -> Katy: the first syllable and the consonants after it, plus a y.
    size_t i = 1;
    while (i < n.size() && !isVowel(n[i]))
      ++i;
    while (i < n.size() && isVowel(n[i]))
      ++i;
    while (i < n.size() && !isVowel(n[i]))
      ++i;
    return n.substr(0, i) + "y";
  };
  for (unsigned k = 0; k < 3; ++k)
  {
    std::string heard;
    switch ((variant + k) % 3)
    {
    case 0: heard = shiftVowel(first); break;
    case 1: heard = shiftEnding(first); break;
    default: heard = addY(first); break;
    }
    std::string lowerHeard, lowerFirst;
    for (char const c : heard)
      lowerHeard += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char const c : first)
      lowerFirst += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lowerHeard != lowerFirst)
      return heard;
  }
  return first + "o";
}

std::optional<std::string> PronounsForName(std::string const & name)
{
  static char const * const kHe[] = {
      "james", "john", "robert", "michael", "mike", "william", "will", "david", "dave", "richard", "rick", "joseph", "joe",
      "thomas", "tom", "charles", "chris", "christopher", "daniel", "dan", "matthew", "matt", "anthony", "tony", "mark",
      "donald", "steven", "steve", "paul", "andrew", "andy", "joshua", "josh", "kenneth", "ken", "kevin", "brian",
      "george", "timothy", "tim", "ronald", "edward", "ed", "jason", "jeffrey", "jeff", "ryan", "jacob", "jake", "gary",
      "nicholas", "nick", "eric", "jonathan", "stephen", "larry", "justin", "scott", "brandon", "benjamin", "ben",
      "samuel", "gregory", "greg", "frank", "alexander", "raymond", "patrick", "jack", "dennis", "jerry", "tyler",
      "aaron", "jose", "adam", "henry", "nathan", "douglas", "doug", "zachary", "zach", "peter", "kyle", "walter",
      "ethan", "jeremy", "harold", "keith", "christian", "roger", "noah", "gerald", "carl", "terry", "sean", "austin",
      "arthur", "lawrence", "jesse", "dylan", "bryan", "joe", "jordan", "billy", "bruce", "albert", "willie", "gabriel",
      "logan", "alan", "juan", "wayne", "roy", "ralph", "randy", "eugene", "vincent", "russell", "elijah", "louis",
      "bobby", "philip", "phil", "johnny", "liam", "mason", "lucas", "oliver", "carlos", "luis", "derek", "travis",
      "chad", "todd", "brad", "bradley", "cody", "shawn", "marcus", "luke", "evan", "ian", "connor", "caleb", "hunter"};
  static char const * const kShe[] = {
      "mary", "patricia", "jennifer", "jen", "linda", "elizabeth", "liz", "barbara", "susan", "sue", "jessica", "jess",
      "sarah", "karen", "lisa", "nancy", "betty", "margaret", "sandra", "ashley", "kimberly", "kim", "emily", "donna",
      "michelle", "carol", "amanda", "dorothy", "melissa", "deborah", "debbie", "stephanie", "rebecca", "becky",
      "sharon", "laura", "cynthia", "kathleen", "kathy", "amy", "angela", "shirley", "anna", "brenda", "pamela", "pam",
      "emma", "nicole", "helen", "samantha", "katherine", "kate", "katie", "christine", "debra", "rachel", "carolyn",
      "janet", "catherine", "maria", "heather", "diane", "ruth", "julie", "olivia", "joyce", "virginia", "victoria",
      "kelly", "lauren", "christina", "joan", "evelyn", "judith", "megan", "andrea", "cheryl", "hannah", "jacqueline",
      "martha", "gloria", "teresa", "ann", "sara", "madison", "frances", "kathryn", "janice", "jean", "abigail",
      "alice", "judy", "sophia", "grace", "denise", "amber", "doris", "marilyn", "danielle", "beverly", "isabella",
      "theresa", "diana", "natalie", "brittany", "charlotte", "marie", "kayla", "alexis", "lori", "ava", "mia", "chloe",
      "ella", "lily", "zoe", "tiffany", "crystal", "erin", "tina", "tracy", "wendy", "vanessa", "molly", "allison",
      "courtney", "jasmine", "holly", "monica", "erica", "april", "dawn", "kristen", "lindsay", "leah", "paige"};
  std::string first = CommandWords(name).substr(1);
  first = first.substr(0, first.find(' '));
  for (char const * n : kHe)
    if (first == n)
      return "he";
  for (char const * n : kShe)
    if (first == n)
      return "she";
  return {};
}

std::optional<std::string> PronounCorrection(std::string const & spoken)
{
  std::string const words = CommandWords(spoken);
  for (char const * cue : {" my pronouns are ", " my pronoun is ", " use the pronoun ", " use the pronouns ",
                           " refer to me as ", " call me "})
    if (auto const at = words.find(cue); at != std::string::npos)
      if (auto const p = ParsePronounAnswer(words.substr(at + std::string(cue).size() - 1)))
        return p;
  for (char const * self : {" i'm a ", " im a ", " i am a ", " i'm ", " im ", " i am "})
  {
    auto const at = words.find(self);
    if (at == std::string::npos)
      continue;
    std::string const rest = words.substr(at + std::string(self).size() - 1);
    for (char const * she : {" woman ", " girl ", " lady ", " female ", " gal ", " chick "})
      if (rest.rfind(she, 0) == 0)
        return "she";
    for (char const * he : {" man ", " guy ", " dude ", " boy ", " male ", " dad "})
      if (rest.rfind(he, 0) == 0)
        return "he";
    for (char const * they : {" nonbinary ", " non binary ", " enby "})
      if (rest.rfind(they, 0) == 0)
        return "they";
  }
  return {};
}

std::optional<std::string> ParsePronounAnswer(std::string const & spoken)
{
  std::string const words = CommandWords(spoken);
  auto const has = [&words](std::initializer_list<char const *> cues)
  { return std::any_of(cues.begin(), cues.end(), [&](char const * c) { return words.find(c) != std::string::npos; }); };
  if (has({" they ", " them ", " they them ", " neither ", " nonbinary ", " non binary ", " enby "}))
    return "they";
  if (has({" she ", " her ", " female ", " woman ", " girl ", " lady ", " gal "}))
    return "she";
  if (has({" he ", " him ", " male ", " man ", " guy ", " dude ", " boy "}))
    return "he";
  return {};
}

namespace
{
struct PartnerWord
{
  char const * word;
  char const * pronouns;
};
PartnerWord const kPartners[] = {
    {"wife", "she"},       {"husband", "he"},  {"girlfriend", "she"}, {"boyfriend", "he"}, {"fiancee", "she"},
    {"fiance", "they"},    {"partner", "they"}, {"spouse", "they"},   {"hubby", "he"},     {"wifey", "she"},
    {"significant other", "they"}};
}  // namespace

std::optional<Passenger> MentionedPartner(std::string const & spoken)
{
  std::string const words = CommandWords(spoken);
  for (auto const & p : kPartners)
    if (words.find(std::string(" my ") + p.word + " ") != std::string::npos)
      return Passenger{p.word, p.pronouns};
  return {};
}

std::optional<Passenger> ParsePassengerAnswer(std::string const & spoken)
{
  if (auto const partner = MentionedPartner(spoken))
    return partner;
  std::string const words = CommandWords(spoken);
  for (auto const & p : kPartners)
    if (words.find(std::string(" ") + p.word + " ") != std::string::npos)
      return Passenger{p.word, p.pronouns};
  for (char const * alone : {" no ", " nope ", " nobody ", " no one ", " noone ", " just me ", " alone ", " only me ",
                             " myself ", " single "})
    if (words.find(alone) != std::string::npos)
      return Passenger{};
  return {};
}

std::optional<size_t> ParseChoice(std::string const & spoken, std::vector<std::string> const & names)
{
  if (names.empty())
    return {};
  std::string const words = CommandWords(spoken);
  auto const has = [&words](char const * w) { return words.find(w) != std::string::npos; };
  static std::vector<std::vector<char const *>> const kOrdinals = {
      {" first ", " number one ", " one ", " 1 ", " 1st ", " top "},
      {" second ", " number two ", " two ", " 2 ", " 2nd "},
      {" third ", " number three ", " three ", " 3 ", " 3rd "},
      {" fourth ", " number four ", " four ", " 4 ", " 4th "},
      {" fifth ", " number five ", " five ", " 5 ", " 5th "}};
  if (has(" closest ") || has(" nearest ") || has(" close one "))
    return 0;
  if (has(" last ") || has(" farthest ") || has(" furthest "))
    return names.size() - 1;
  // A word in just one result's name: "the one on Colfax".
  static char const * const kCommon[] = {"the", "on", "at", "and", "of", "a", "in", "st", "street", "ave", "avenue", "rd", "road"};
  std::optional<size_t> byName;
  int matches = 0;
  for (size_t i = 0; i < names.size(); ++i)
  {
    std::string const name = CommandWords(names[i]);
    for (size_t pos = 1; pos < name.size();)
    {
      auto const end = name.find(' ', pos);
      std::string const w = name.substr(pos, end - pos);
      pos = end + 1;
      if (w.size() < 3 || std::find(std::begin(kCommon), std::end(kCommon), w) != std::end(kCommon))
        continue;
      bool const shared = std::any_of(names.begin(), names.end(), [&](std::string const & other)
      { return &other != &names[i] && CommandWords(other).find(" " + w + " ") != std::string::npos; });
      if (!shared && words.find(" " + w + " ") != std::string::npos)
      {
        if (byName != i)
          ++matches;
        byName = i;
        break;
      }
    }
  }
  if (byName && matches == 1)
    return byName;
  for (size_t i = 0; i < kOrdinals.size() && i < names.size(); ++i)
    for (char const * o : kOrdinals[i])
      if (has(o))
        return i;
  return {};
}

int CountVoiceName(std::string const & spoken, std::string const & name)
{
  std::string const words = CommandWords(spoken);
  std::string const wanted = CommandWords(name);
  int count = 0;
  if (wanted.find(' ', 1) != wanted.size() - 1)
  {
    // Several words: the whole phrase.
    for (size_t at = words.find(wanted); at != std::string::npos; at = words.find(wanted, at + 1))
      ++count;
    return count;
  }
  std::string const target = wanted.substr(1, wanted.size() - 2);
  auto const close = [&target](std::string const & word)
  {
    if (word == target)
      return true;
    if (target.size() < 4 || word.size() < 3 || word[0] != target[0])
      return false;
    // A cut-off word, while the recognizer is still working: "jess" for Jessica.
    if (word.size() >= 4 && target.rfind(word, 0) == 0)
      return true;
    // One letter off.
    size_t const a = word.size(), b = target.size();
    if ((a > b ? a - b : b - a) > 1)
      return false;
    size_t i = 0;
    while (i < a && i < b && word[i] == target[i])
      ++i;
    size_t j = a, k = b;
    while (j > i && k > i && word[j - 1] == target[k - 1])
    {
      --j;
      --k;
    }
    return j - i <= 1 && k - i <= 1;
  };
  std::istringstream in(words);
  std::string word;
  while (in >> word)
    if (close(word))
      ++count;
  return count;
}

std::vector<std::string> VoicePhrases(std::vector<VoiceOption> const & voices)
{
  static char const * const kPool[] = {
      "banana phone",   "disco potato",   "spicy pickle",    "turbo llama",    "soggy waffle",
      "ninja toaster",  "wobbly penguin", "cosmic burrito",  "grumpy cactus",  "velvet hamster",
      "rubber chicken", "sneaky noodle",  "laser walrus",    "polka dot moose", "tofu tornado",
      "captain meatball", "moon pudding", "sassy lobster",   "pancake wizard", "jelly bean volcano",
      "funky giraffe",  "pickle parade",  "taco submarine",  "hamster disco",  "bubblegum thunder",
      "noodle kingdom", "llama drama",    "sock puppet",     "waffle iron",    "marshmallow tank"};
  auto const brandons = [](std::string const & name) -> char const *
  {
    std::string lower;
    for (unsigned char const ch : name)
      lower += static_cast<char>(std::tolower(ch));
    if (lower == "echo")
      return "purple monkey";
    if (lower == "adam")
      return "elephant shoe";
    return nullptr;
  };
  std::vector<std::string> out;
  size_t next = 0;
  for (auto const & voice : voices)
  {
    if (char const * phrase = brandons(voice.name))
      out.push_back(phrase);
    else
    {
      size_t const n = std::size(kPool);
      out.push_back(kPool[next % n] + (next >= n ? " " + std::to_string(next / n + 1) : std::string()));
      ++next;
    }
  }
  return out;
}

std::optional<size_t> VoiceByPhrase(std::vector<VoiceOption> const & voices, std::string const & spoken)
{
  std::string const words = CommandWords(spoken);
  auto const phrases = VoicePhrases(voices);
  for (size_t i = 0; i < phrases.size(); ++i)
    if (words.find(" " + phrases[i] + " ") != std::string::npos)
      return i;
  return {};
}

std::optional<size_t> VoiceNamed(std::vector<VoiceOption> const & voices, std::string const & said)
{
  std::string const target = CommandWords(said);
  if (target == " ")
    return {};
  for (size_t i = 0; i < voices.size(); ++i)
    if (CommandWords(voices[i].name) == target || CommandWords(voices[i].id) == target)
      return i;
  // "echo" for "am_echo".
  for (size_t i = 0; i < voices.size(); ++i)
  {
    auto const underscore = voices[i].id.rfind('_');
    if (underscore != std::string::npos && CommandWords(voices[i].id.substr(underscore + 1)) == target)
      return i;
  }
  return {};
}

std::optional<KnownPlace> KnownPlaceOf(std::string const & target)
{
  for (char const * home : {"home", "my house", "house", "my place", "my home"})
    if (target == home)
      return KnownPlace::Home;
  for (char const * work : {"work", "my work", "the office", "office", "my office", "my job", "job"})
    if (target == work)
      return KnownPlace::Work;
  return {};
}

std::optional<LatLon> GuessPlace(std::vector<TripRecord> const & history, KnownPlace place)
{
  // Places by the route key's grid cell (~250 m): how often trips start or end there, and the
  // exact destination points seen for it.
  struct Seen
  {
    int visits = 0;
    int arrivals = 0;
    LatLon sum{0, 0};
    int exact = 0;
    LatLon cell{0, 0};
  };
  std::map<std::string, Seen> seen;
  for (auto const & trip : history)
  {
    auto const arrow = trip.routeKey.find('>');
    if (arrow == std::string::npos)
      continue;
    std::string const from = trip.routeKey.substr(0, arrow);
    std::string const to = trip.routeKey.substr(arrow + 1);
    if (auto const p = RouteKeyLatLon(from))
    {
      seen[from].visits++;
      seen[from].cell = *p;
    }
    if (auto const p = RouteKeyLatLon(to))
    {
      auto & s = seen[to];
      s.visits++;
      s.arrivals++;
      s.cell = *p;
      if (trip.to)
      {
        s.sum.lat += trip.to->lat;
        s.sum.lon += trip.to->lon;
        s.exact++;
      }
    }
  }
  auto const best = [&](std::string const & skip, bool byArrivals) -> std::map<std::string, Seen>::const_iterator
  {
    auto top = seen.cend();
    for (auto it = seen.cbegin(); it != seen.cend(); ++it)
    {
      int const n = byArrivals ? it->second.arrivals : it->second.visits;
      if (it->first != skip && n >= 2 && (top == seen.cend() || n > (byArrivals ? top->second.arrivals : top->second.visits)))
        top = it;
    }
    return top;
  };
  auto const home = best("", false);
  auto const it = place == KnownPlace::Home ? home : best(home == seen.cend() ? "" : home->first, true);
  if (it == seen.cend())
    return {};
  auto const & s = it->second;
  if (s.exact > 0)
    return LatLon{s.sum.lat / s.exact, s.sum.lon / s.exact};
  return s.cell;
}
namespace
{
/// What the GPS calls someone with these pronouns when it's being polite through its teeth.
std::string Title(std::string const & pronouns)
{
  if (pronouns == "he")
    return "sir";
  if (pronouns == "she")
    return "ma'am";
  return "my friend";
}
}  // namespace

std::optional<ProfileChange> ProfileChanged(DriverProfile const & before, DriverProfile const & after)
{
  auto const changed = [](std::string const & was, std::string const & now)
  { return !was.empty() && !now.empty() && CommandWords(was) != CommandWords(now); };
  if (changed(before.name, after.name))
    return ProfileChange{"profile.name", {{"old", before.name}, {"new", after.name}}};
  // Pronouns default to "they" when never set, so only a set-to-set change counts.
  if (changed(before.pronouns, after.pronouns) && Title(before.pronouns) != Title(after.pronouns))
    return ProfileChange{"profile.title", {{"old", Title(before.pronouns)}, {"new", Title(after.pronouns)}}};
  if (changed(before.age, after.age))
  {
    int was = 0, now = 0;
    bool const numbers = std::sscanf(before.age.c_str(), "%d", &was) == 1 && std::sscanf(after.age.c_str(), "%d", &now) == 1;
    return ProfileChange{numbers && now < was ? "profile.younger" : "profile.age",
                         {{"old", before.age}, {"new", after.age}}};
  }
  return {};
}
}  // namespace backseat
