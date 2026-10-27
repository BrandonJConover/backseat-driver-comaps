#include "backseat/voice_command.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <map>

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
                           " let's go to ", " lets go to ", " head to ", " go to ", " bring me to "})
  {
    auto const at = words.find(cue);
    if (at != std::string::npos)
    {
      target = ParseTarget(words.substr(at + std::string(cue).size()));
      break;
    }
  }
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
                                      " what was that ", " say again ", " repeat yourself ", " what's the next turn ",
                                      " whats the next turn ", " where do i turn "}},
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
}  // namespace backseat
