#include "backseat/twenty_questions.hpp"

#include "backseat/plate_game.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace backseat
{
namespace
{
using T = Trait;

/// Lowercase words separated by single spaces, padded with a space on each side, so " car "
/// doesn't match inside " carpet ". Apostrophes go: "it's" -> "its".
std::string TqWords(std::string const & s)
{
  std::string out = " ";
  for (unsigned char const c : s)
  {
    if (std::isalnum(c))
      out += static_cast<char>(std::tolower(c));
    else if (out.back() != ' ' && c != '\'')
      out += ' ';
  }
  if (out.back() != ' ')
    out += ' ';
  return out;
}

bool TqHas(std::string const & words, char const * phrase) { return words.find(TqWords(phrase)) != std::string::npos; }

bool TqHasAny(std::string const & words, std::vector<char const *> const & phrases)
{
  return std::any_of(phrases.begin(), phrases.end(), [&](char const * p) { return TqHas(words, p); });
}

/// Where a thing is, for "I'm thinking of something {where}".
char const * const kOutside = "outside";
char const * const kInCar = "in this car";
char const * const kSky = "up in the sky";
char const * const kRoad = "on the road";

std::vector<TwentyThing> const kThings = {
    // On every drive.
    {"a stop sign", {"stop sign"}, "", kOutside,
     {T::Sign, T::Metal, T::Red, T::White, T::ManMade, T::Roadside}, {}, false, false,
     "You know, the red one. The one that means stop. Fully."},
    {"a traffic light", {"traffic light", "stop light", "stoplight", "traffic signal", "red light"}, "", kOutside,
     {T::Lights, T::Metal, T::Electric, T::ManMade, T::Red, T::Yellow, T::Green, T::Tall}, {T::OnRoad, T::Roadside},
     false, false, "Yellow means slow down, by the way. Not speed up."},
    {"a pothole", {"pothole", "pot hole", "hole"}, "", kRoad, {T::OnRoad, T::Dangerous, T::Gray, T::Black},
     {T::Round, T::Water}, false, false, "You've hit at least three today. I counted."},
    {"a fire hydrant", {"fire hydrant", "hydrant"}, "", kOutside,
     {T::Metal, T::Red, T::Water, T::Roadside, T::ManMade}, {T::Yellow, T::Small}, false, false,
     "Don't park next to one. I know you've thought about it."},
    {"a mailbox", {"mailbox", "mail box", "mailboxes"}, "", kOutside, {T::Roadside, T::ManMade},
     {T::Metal, T::Wood, T::Black, T::Blue, T::Small}, false, false, "Please don't hit one. They're federal."},
    {"a tree", {"tree", "trees"}, "", kOutside,
     {T::Alive, T::Plant, T::Natural, T::Wood, T::Green, T::Brown, T::Tall, T::Roadside}, {T::BiggerThanCar},
     false, false, "There have been about four hundred of them. You looked at none."},
    {"a cloud", {"cloud", "clouds"}, "", kSky, {T::Sky, T::Natural, T::White, T::Moves, T::BiggerThanCar, T::Water},
     {T::Gray}, true, false, "It's right there. Up. The direction you never look."},
    {"the sun", {"sun", "the sun", "sunshine"}, "", kSky,
     {T::Sky, T::Natural, T::Yellow, T::Round, T::Lights, T::BiggerThanCar}, {T::Dangerous}, true, false,
     "The biggest thing in the sky, and you missed it."},
    {"the moon", {"moon", "the moon"}, "", kSky,
     {T::Sky, T::Natural, T::Round, T::White, T::Gray, T::BiggerThanCar}, {T::Lights}, false, true,
     "It's been following us the whole time. Like me."},
    {"a streetlight", {"streetlight", "street light", "street lamp", "lamp post", "light pole"}, "", kOutside,
     {T::Lights, T::Metal, T::Electric, T::Tall, T::Roadside, T::ManMade, T::Gray}, {}, false, false, ""},
    {"a traffic cone", {"traffic cone", "cone", "cones", "orange cone"}, "", kRoad, {T::Orange, T::ManMade},
     {T::OnRoad, T::Roadside, T::White, T::Small, T::Round}, false, false,
     "You've been treating them like an obstacle course."},
    {"a billboard", {"billboard", "billboards", "sign board"}, "", kOutside,
     {T::Sign, T::BiggerThanCar, T::Tall, T::ManMade, T::Roadside}, {T::Lights, T::Metal}, false, false,
     "Huge letters. Still too small for you, apparently."},
    {"a semi truck", {"semi", "semi truck", "truck", "eighteen wheeler", "18 wheeler", "big rig", "tractor trailer"},
     "", kRoad,
     {T::Vehicle, T::Wheels, T::Metal, T::Moves, T::Noise, T::BiggerThanCar, T::OnRoad, T::ManMade, T::Lights},
     {T::Dangerous}, false, false, "Give them room. They can't see you. Neither can I, emotionally."},
    {"a motorcycle", {"motorcycle", "motorbike", "motor bike", "harley"}, "", kRoad,
     {T::Vehicle, T::Wheels, T::Metal, T::Moves, T::Noise, T::OnRoad, T::Lights, T::ManMade}, {T::Dangerous}, false,
     false, ""},
    {"a bicycle", {"bicycle", "bike", "bikes", "cyclist"}, "", kOutside, {T::Vehicle, T::Wheels, T::Metal, T::Moves, T::ManMade},
     {T::OnRoad, T::Roadside}, false, false, "Three feet when you pass one. That's the law, and also manners."},
    {"a police car", {"police car", "cop car", "squad car", "cruiser", "police"}, "", kRoad,
     {T::Vehicle, T::Wheels, T::Metal, T::Moves, T::Noise, T::OnRoad, T::Lights, T::ManMade, T::Black, T::White},
     {T::Blue}, false, false, "You got very quiet just now. Interesting."},
    {"a car", {"car", "another car", "other car", "cars"}, "", kRoad,
     {T::Vehicle, T::Wheels, T::Metal, T::Moves, T::Noise, T::OnRoad, T::Lights, T::ManMade}, {}, false, false,
     "You've been tailgating one for a mile."},
    {"a bird", {"bird", "birds", "crow", "pigeon", "hawk", "seagull"}, "", kSky,
     {T::Alive, T::Animal, T::Legs, T::Flies, T::Moves, T::Noise, T::Small, T::Natural}, {T::Sky}, false, false,
     "It flies better than you drive. Low bar."},
    {"a squirrel", {"squirrel", "squirrels", "chipmunk"}, "", kOutside,
     {T::Alive, T::Animal, T::Legs, T::Small, T::Brown, T::Gray, T::Moves, T::Natural}, {T::Roadside, T::OnRoad},
     false, false, "It crossed the road more decisively than you change lanes."},
    {"a dog in another car", {"dog", "dogs", "puppy", "a dog"}, "", kRoad,
     {T::Alive, T::Animal, T::Legs, T::Moves, T::Noise}, {T::Brown, T::Black, T::White}, false, false,
     "It had its head out the window. Happiest passenger on this road. Not you, though."},
    {"a crosswalk", {"crosswalk", "cross walk", "zebra crossing"}, "", kRoad, {T::OnRoad, T::White, T::ManMade},
     {T::Sign}, false, false, "It's where you stop for people. In theory."},
    {"a speed limit sign", {"speed limit sign", "speed limit", "speed sign"}, "", kOutside,
     {T::Sign, T::Metal, T::White, T::Black, T::Roadside, T::ManMade}, {}, false, false,
     "I wasn't sure you knew what one looked like."},
    {"the lines on the road", {"lines", "lane lines", "the lines", "road lines", "lane markings", "yellow line",
                               "white line", "stripes"},
     "", kRoad, {T::OnRoad, T::White, T::Yellow, T::ManMade}, {}, false, false,
     "The things you're supposed to stay between. Usually."},
    {"a guardrail", {"guardrail", "guard rail", "railing"}, "", kOutside,
     {T::Metal, T::Roadside, T::ManMade, T::Gray}, {T::Dangerous}, false, false,
     "It's there for people like you. Specifically you."},
    {"a flag", {"flag", "american flag", "flags"}, "", kOutside, {T::Moves, T::Red, T::White, T::Blue, T::ManMade},
     {T::Roadside}, false, false, ""},
    {"the road", {"road", "the road", "street", "the street", "pavement", "asphalt", "highway"}, "", kRoad,
     {T::OnRoad, T::ManMade, T::Gray, T::Black, T::BiggerThanCar}, {}, false, false,
     "The road. The one thing you're supposed to be looking at."},
    {"a pickup truck", {"pickup", "pickup truck", "pick up truck", "pick up"}, "", kRoad,
     {T::Vehicle, T::Wheels, T::Metal, T::Moves, T::Noise, T::OnRoad, T::Lights, T::ManMade}, {}, false, false, ""},

    // In the car.
    {"your turn signal", {"turn signal", "blinker", "blinkers", "indicator", "signal"}, "", kInCar,
     {T::InCar, T::Lights, T::Electric, T::Noise, T::ManMade, T::Small, T::Orange}, {T::Yellow}, false, false,
     "Your turn signal. The thing you have never used. I wasn't sure you'd recognize it."},
    {"me", {"you", "yourself", "the phone", "phone", "the gps", "gps", "your phone", "my phone", "the app"}, "",
     kInCar, {T::InCar, T::Electric, T::Lights, T::Noise, T::Small, T::Metal, T::Black, T::ManMade}, {}, false, false,
     "It was me. It's always me. I'm the most interesting thing in this car."},
    {"the steering wheel", {"steering wheel", "wheel", "the wheel"}, "", kInCar,
     {T::InCar, T::Round, T::ManMade, T::Black}, {T::Moves}, false, false,
     "Two hands on it would be nice. Ten and two. Look it up."},
    {"the cup holder", {"cup holder", "cupholder", "cup holders"}, "", kInCar, {T::InCar, T::Small, T::Round, T::ManMade, T::Black},
     {}, false, false, "Your favorite feature. The only one you use correctly."},
    {"the rearview mirror", {"rearview mirror", "rear view mirror", "mirror", "mirrors", "side mirror"}, "", kInCar,
     {T::InCar, T::Small, T::ManMade}, {T::Black}, false, false, "Your rearview mirror. Try it sometime."},
    {"your seatbelt", {"seatbelt", "seat belt", "belt"}, "", kInCar, {T::InCar, T::ManMade, T::Black}, {T::Metal, T::Gray},
     false, false, "Keep it on. I need you alive to keep criticizing."},
    {"the brake pedal", {"brake", "brakes", "brake pedal", "pedal", "the brakes"}, "", kInCar,
     {T::InCar, T::Metal, T::Black, T::ManMade, T::Small}, {}, false, false,
     "The brake pedal. You know. The other pedal."},
    {"the radio", {"radio", "the radio", "stereo", "speakers", "speaker", "music"}, "", kInCar,
     {T::InCar, T::Electric, T::Noise, T::Lights, T::ManMade}, {T::Black}, false, false,
     "Which you could turn on, by the way. I'd welcome the competition."},
    {"the gas pedal", {"gas pedal", "accelerator", "gas"}, "", kInCar, {T::InCar, T::Metal, T::Black, T::ManMade, T::Small},
     {}, false, false, "Your favorite pedal. Obviously."},
    {"you", {"me", "myself", "the driver", "driver"}, "", kInCar,
     {T::Person, T::Alive, T::InCar, T::Legs, T::Moves, T::Noise}, {}, false, false,
     "It was you. You were the something. It never occurred to you to ask if it was you."},
    {"the windshield wipers", {"windshield wipers", "wipers", "wiper", "windshield wiper"}, "", kInCar,
     {T::Moves, T::ManMade, T::Black, T::Electric, T::Water}, {T::Noise}, false, false, ""},

    // Only near the route, when the map says so.
    {"a gas station", {"gas station", "gas stations", "fuel station", "shell", "conoco", "chevron"}, "fuel", kOutside,
     {T::Building, T::Business, T::BiggerThanCar, T::ManMade, T::Lights, T::Roadside, T::Electric}, {T::Food}, false,
     false, "You'll need one eventually. Not that you'd plan ahead."},
    {"a fast food place", {"fast food", "fast food place", "mcdonalds", "burger king", "taco bell", "wendys",
                           "drive thru", "drive through", "restaurant"},
     "fast_food", kOutside,
     {T::Building, T::Business, T::Food, T::Lights, T::BiggerThanCar, T::ManMade, T::Roadside}, {T::Red, T::Yellow},
     false, false, "And no, we're not stopping."},
    {"a coffee shop", {"coffee shop", "coffee", "starbucks", "cafe", "dunkin"}, "cafe", kOutside,
     {T::Building, T::Business, T::Food, T::BiggerThanCar, T::ManMade, T::Roadside}, {T::Green}, false, false,
     "Which would explain a lot, honestly."},
    {"a church", {"church", "churches", "chapel", "steeple"}, "church", kOutside,
     {T::Building, T::BiggerThanCar, T::Tall, T::ManMade}, {T::Noise, T::White}, false, false,
     "Fitting. I've been praying for your driving the whole time."},
    {"a school", {"school", "schools", "elementary school", "high school"}, "school", kOutside,
     {T::Building, T::BiggerThanCar, T::ManMade, T::Noise}, {T::Sign}, false, false,
     "School zones mean slow. Just something to learn."},
    {"a school bus", {"school bus", "bus", "buses"}, "school", kRoad,
     {T::Vehicle, T::Wheels, T::Yellow, T::Black, T::Moves, T::Noise, T::BiggerThanCar, T::OnRoad, T::Lights,
      T::ManMade, T::Metal},
     {T::Sign}, false, false, "When the red sign comes out, you stop. Every lane. Yes, you."},
    {"a water tower", {"water tower", "water tank", "tower"}, "water_tower", kOutside,
     {T::Metal, T::Water, T::Tall, T::BiggerThanCar, T::Round, T::ManMade}, {T::White, T::Blue}, false, false,
     "It's the tallest thing around. You still didn't see it."},
    {"a bridge", {"bridge", "bridges", "overpass", "underpass"}, "bridge", kRoad, {T::BiggerThanCar, T::ManMade, T::Gray},
     {T::Metal, T::OnRoad, T::Water}, false, false, "We drive over one and you don't even notice. Classic."},
    {"a railroad crossing", {"railroad crossing", "train crossing", "railroad tracks", "train tracks", "tracks",
                             "railroad", "crossing"},
     "railway", kRoad,
     {T::Sign, T::Lights, T::Noise, T::Metal, T::ManMade, T::OnRoad, T::Red, T::White, T::Electric, T::Dangerous}, {},
     false, false, "Look both ways. Even you."},
    {"a train", {"train", "trains", "freight train", "locomotive"}, "railway", kOutside,
     {T::Vehicle, T::Wheels, T::Metal, T::Moves, T::Noise, T::BiggerThanCar, T::Lights, T::ManMade}, {T::Dangerous},
     false, false, "It always wins. Remember that."},
    {"a river", {"river", "creek", "stream"}, "river", kOutside, {T::Water, T::Natural, T::Moves, T::BiggerThanCar},
     {T::Blue, T::Brown}, false, false, "Water flows downhill without directions. Imagine."},
    {"a lake", {"lake", "pond", "reservoir"}, "lake", kOutside, {T::Water, T::Natural, T::BiggerThanCar, T::Blue},
     {T::Alive}, false, false, ""},
    {"a power line", {"power line", "power lines", "telephone pole", "power pole", "utility pole", "wires"},
     "power_line", kOutside, {T::Metal, T::Electric, T::Tall, T::ManMade, T::Dangerous, T::Black}, {T::Roadside, T::Wood},
     false, false, "There's been one beside us for miles."},
    {"a wind turbine", {"wind turbine", "windmill", "wind mill", "turbine", "wind farm"}, "wind_turbine", kOutside,
     {T::Tall, T::BiggerThanCar, T::Moves, T::Electric, T::White, T::ManMade}, {T::Noise}, false, false,
     "It spins in circles. Like you, when you miss a turn."},
    {"a parking lot", {"parking lot", "parking", "parking garage"}, "parking", kOutside,
     {T::BiggerThanCar, T::ManMade, T::Gray, T::Black}, {T::Lights}, false, false,
     "Your natural enemy. All those lines."},
    {"a grocery store", {"grocery store", "supermarket", "grocery", "walmart", "target", "safeway", "king soopers"},
     "supermarket", kOutside, {T::Building, T::Business, T::Food, T::BiggerThanCar, T::ManMade, T::Lights}, {}, false,
     false, ""},
    {"a hospital", {"hospital", "emergency room", "clinic"}, "hospital", kOutside,
     {T::Building, T::BiggerThanCar, T::Tall, T::ManMade, T::Lights}, {T::Noise}, false, false,
     "I like knowing where the nearest one is. Given everything."},
    {"a fire truck", {"fire truck", "fire engine", "firetruck"}, "fire_station", kRoad,
     {T::Vehicle, T::Wheels, T::Red, T::Metal, T::Noise, T::Lights, T::BiggerThanCar, T::Water, T::ManMade, T::Moves},
     {}, false, false, "Pull over for one. Right side. I'll remind you."},
    {"a cow", {"cow", "cows", "cattle", "bull"}, "farm", kOutside,
     {T::Alive, T::Animal, T::Legs, T::Black, T::White, T::Noise, T::Natural}, {T::Brown, T::Moves, T::Food}, false,
     false, "It has a better sense of direction than you. It walks home every night."},
    {"a horse", {"horse", "horses", "pony"}, "farm", kOutside,
     {T::Alive, T::Animal, T::Legs, T::Brown, T::Moves, T::Noise, T::Natural}, {T::White, T::Black}, false, false,
     "One horsepower. Still more control than this car."},
    {"a cornfield", {"cornfield", "corn field", "corn", "field", "farm", "crops"}, "farm", kOutside,
     {T::Plant, T::Alive, T::Natural, T::Food, T::Green, T::BiggerThanCar}, {T::Yellow}, false, false, ""},
    {"a deer", {"deer", "doe", "buck", "elk"}, "forest", kOutside,
     {T::Alive, T::Animal, T::Legs, T::Brown, T::Moves, T::Natural}, {T::Dangerous, T::Roadside, T::OnRoad}, false,
     false, "Watch for them. They don't watch for you."},
    {"a mountain", {"mountain", "mountains", "hill", "hills", "peak"}, "mountain", kOutside,
     {T::Natural, T::BiggerThanCar, T::Tall, T::Brown}, {T::Green, T::White}, false, false,
     "It's enormous. It's been there for millions of years. And you missed it."},
    {"a bus stop", {"bus stop", "bus shelter", "bus bench"}, "bus_stop", kOutside, {T::Sign, T::Roadside, T::ManMade},
     {T::Metal}, false, false, ""},
};

struct TraitWords
{
  Trait trait;
  std::vector<char const *> words;
};

/// Checked in order; the first that's in the question wins. Size words are handled first, on
/// their own, because "bigger than a house" isn't asking about a house.
std::vector<TraitWords> const kTraitWords = {
    {T::InCar, {"in the car", "inside the car", "in this car", "in my car", "in here", "part of the car",
                "part of my car", "dashboard", "with us"}},
    {T::ManMade, {"man made", "manmade", "made by", "built", "artificial", "human made", "plastic", "invented"}},
    {T::Alive, {"alive", "living", "living thing", "breathe", "breathing", "breathes"}},
    {T::Animal, {"animal", "animals", "creature", "mammal", "critter", "pet", "wildlife"}},
    {T::Plant, {"plant", "plants", "leaves", "leaf", "flower", "grow", "grows", "vegetation"}},
    {T::Person, {"person", "human", "people", "someone", "somebody", "man", "woman", "guy"}},
    {T::Wheels, {"wheels", "tires", "tire"}},
    {T::Vehicle, {"vehicle", "drive it", "ride it", "ride in it", "transportation", "transport"}},
    {T::Building, {"building", "house", "structure", "go inside", "go in it", "walk into it"}},
    {T::Business, {"store", "shop", "business", "buy", "restaurant", "sell", "company", "brand", "pay"}},
    {T::Food, {"eat", "food", "edible", "drink", "taste", "snack"}},
    {T::Sign, {"sign", "words on it", "writing on it", "read it", "letters"}},
    {T::Flies, {"fly", "flies", "flying", "wings"}},
    {T::Sky, {"sky", "in the air", "up there", "above us", "overhead", "space"}},
    {T::Lights, {"light up", "lights up", "lights", "light", "glow", "glows", "lit", "shine", "shiny", "bright"}},
    {T::Metal, {"metal", "steel", "iron", "aluminum", "metallic"}},
    {T::Wood, {"wood", "wooden"}},
    {T::Water, {"water", "wet", "liquid", "swim", "ocean"}},
    {T::Round, {"round", "circle", "circular", "sphere", "ball shaped"}},
    {T::Roadside, {"side of the road", "next to the road", "by the road", "beside the road", "sidewalk", "curb",
                   "shoulder", "roadside", "on the side"}},
    {T::OnRoad, {"on the road", "in the road", "part of the road", "on the street", "in the street", "pavement",
                 "asphalt", "on the highway", "road"}},
    {T::Natural, {"nature", "natural"}},
    {T::Noise, {"noise", "noises", "sound", "sounds", "loud", "beep", "honk", "hear it"}},
    {T::Electric, {"electric", "electricity", "power", "plug", "battery", "electronic", "electronics"}},
    {T::Legs, {"legs", "leg", "feet", "walk", "walks", "paws"}},
    {T::Dangerous, {"dangerous", "hurt", "harm", "harmful", "deadly", "danger", "kill"}},
    {T::Moves, {"move", "moves", "moving", "motion", "go anywhere", "travel"}},
    {T::Red, {"red"}},
    {T::Orange, {"orange"}},
    {T::Yellow, {"yellow"}},
    {T::Green, {"green"}},
    {T::Blue, {"blue"}},
    {T::White, {"white"}},
    {T::Black, {"black"}},
    {T::Brown, {"brown"}},
    {T::Gray, {"gray", "grey", "silver"}},
};

/// For hints: "it {fact}".
char const * Fact(Trait t)
{
  switch (t)
  {
  case T::Alive: return "is alive";
  case T::Animal: return "is an animal";
  case T::Plant: return "is a plant";
  case T::Person: return "is a person";
  case T::Vehicle: return "is a vehicle";
  case T::Wheels: return "has wheels";
  case T::Building: return "is a building";
  case T::Business: return "is a business";
  case T::Food: return "has to do with food";
  case T::Sign: return "has writing on it";
  case T::Lights: return "lights up";
  case T::Metal: return "is made of metal";
  case T::Wood: return "is made of wood";
  case T::Moves: return "moves";
  case T::Flies: return "flies";
  case T::Sky: return "is up in the sky";
  case T::Water: return "has to do with water";
  case T::BiggerThanCar: return "is bigger than this car";
  case T::Small: return "is smaller than a breadbox";
  case T::Tall: return "is tall";
  case T::Round: return "is round";
  case T::OnRoad: return "is on the road";
  case T::Roadside: return "is by the side of the road";
  case T::ManMade: return "was made by people";
  case T::Natural: return "is part of nature";
  case T::Noise: return "makes noise";
  case T::Electric: return "uses electricity";
  case T::InCar: return "is in this car";
  case T::Legs: return "has legs";
  case T::Dangerous: return "can be dangerous";
  case T::Red: return "is red";
  case T::Orange: return "is orange";
  case T::Yellow: return "is yellow";
  case T::Green: return "is green";
  case T::Blue: return "is blue";
  case T::White: return "is white";
  case T::Black: return "is black";
  case T::Brown: return "is brown";
  case T::Gray: return "is gray";
  case T::Count: break;
  }
  return "exists";
}

/// What the driver has to start a guess with: "is it", "it's", "I guess", "the answer is".
std::vector<char const *> const kGuessCues = {"is it", "is that", "its", "it is", "i guess", "my guess is", "guess",
                                              "the answer is", "is the answer", "are you thinking of", "are you", "was it",
                                              "you re thinking of", "youre thinking of", "how about"};
std::vector<char const *> const kArticles = {"a", "an", "the", "my", "your", "some", "another", "that"};

/// The words right after a guess cue ("is it a deer" -> "deer"), padded, or empty when no cue.
/// With `article`, only when an article came first ("is it a giraffe" -> "giraffe").
std::vector<std::string> AfterCues(std::string const & words, bool & hadArticle)
{
  std::vector<std::string> out;
  for (auto const * cue : kGuessCues)
  {
    std::string const c = TqWords(cue);
    for (size_t at = words.find(c); at != std::string::npos; at = words.find(c, at + 1))
    {
      std::string rest = words.substr(at + c.size() - 1);
      for (auto const * a : kArticles)
      {
        std::string const art = TqWords(a);
        if (rest.rfind(art, 0) == 0)
        {
          rest = rest.substr(art.size() - 1);
          hadArticle = true;
          break;
        }
      }
      out.push_back(rest);
    }
  }
  return out;
}

/// The thing whose name or alias `rest` starts with, longest alias winning.
std::optional<size_t> ThingAtStart(std::string const & rest)
{
  std::optional<size_t> best;
  size_t bestLen = 0;
  for (size_t i = 0; i < kThings.size(); ++i)
  {
    auto const & t = kThings[i];
    // "me" and "you" are named the way the GPS says them; the driver says them the other way
    // round, so for those two only the aliases count.
    std::vector<std::string> names;
    if (std::string(t.name) != "me" && std::string(t.name) != "you")
      names.push_back(t.name);
    for (auto const * a : t.aliases)
      names.push_back(a);
    for (auto const & n : names)
    {
      std::string w = TqWords(n);
      for (auto const * a : kArticles)
        if (w.rfind(TqWords(a), 0) == 0 && w.size() > TqWords(a).size())
          w = w.substr(TqWords(a).size() - 1);
      if (w.size() > bestLen && rest.rfind(w, 0) == 0)
      {
        best = i;
        bestLen = w.size();
      }
    }
  }
  return best;
}

/// Whether the question is about size, and which way: "bigger than a breadbox" asks whether
/// it's not Small; "smaller than a car" asks whether it's not BiggerThanCar.
std::optional<std::pair<Trait, bool>> SizeQuestion(std::string const & w)
{
  bool const bigger = TqHasAny(w, {"bigger", "larger", "huge", "giant", "big", "large", "massive", "enormous"});
  bool const smaller = TqHasAny(w, {"smaller", "tiny", "little", "small", "fit in", "hold it", "pick it up", "carry it"});
  if (!bigger && !smaller)
    return TqHasAny(w, {"tall", "taller", "high up", "tower over"}) ? std::optional{std::pair{T::Tall, false}}
                                                                   : std::nullopt;
  bool const vsSmall = TqHasAny(w, {"breadbox", "bread box", "hand", "ball", "basketball", "football", "shoe",
                                    "toaster", "microwave", "phone", "cat", "backpack", "box"});
  bool const vsBig = TqHasAny(w, {"car", "truck", "house", "building", "bus", "person", "me", "this car"});
  if (bigger)
  {
    if (vsSmall)
      return std::pair{T::Small, true};
    return std::pair{T::BiggerThanCar, false};
  }
  if (vsBig)
    return std::pair{T::BiggerThanCar, true};
  return std::pair{T::Small, false};
}

/// Categories close enough that guessing one for the other is "warm".
std::vector<Trait> const kKinds = {T::Animal, T::Vehicle, T::Building, T::Sign, T::InCar, T::Sky, T::Water, T::Plant};
}  // namespace

Answer TwentyThing::Has(Trait t) const
{
  auto const listed = [this](Trait x)
  {
    if (std::find(yes.begin(), yes.end(), x) != yes.end())
      return Answer::Yes;
    if (std::find(sometimes.begin(), sometimes.end(), x) != sometimes.end())
      return Answer::Sometimes;
    return Answer::No;
  };
  Answer const a = listed(t);
  if (a != Answer::No)
    return a;
  bool const alive = listed(T::Alive) == Answer::Yes;
  // Everything is either part of nature or made by somebody.
  if (t == T::ManMade && !alive && listed(T::Natural) == Answer::No)
    return Answer::Yes;
  if (t == T::Natural && alive)
    return Answer::Yes;
  if (listed(T::Vehicle) == Answer::Yes)
  {
    if (t == T::Moves)
      return Answer::Yes;
    // Electric cars, and anything with an engine can hurt you.
    if (t == T::Electric || t == T::Dangerous)
      return Answer::Sometimes;
  }
  return Answer::No;
}

std::vector<TwentyThing> const & TwentyThings() { return kThings; }

std::optional<size_t> MatchTwentyThing(std::string const & spoken)
{
  bool article = false;
  std::optional<size_t> best;
  for (auto const & rest : AfterCues(TqWords(spoken), article))
    if (auto const t = ThingAtStart(rest))
      if (!best)
        best = t;
  return best;
}

bool AsksForTwentyQuestions(std::string const & spoken)
{
  std::string const w = TqWords(spoken);
  return TqHasAny(w, {"20 questions", "twenty questions", "20 question", "twenty question", "20q"});
}

TwentyHeard ParseTwentyHeard(std::string const & spoken)
{
  std::string const w = TqWords(spoken);
  TwentyHeard h;
  if (TqHasAny(w, {"give up", "i give", "just tell me", "tell me what it is", "what is it", "what was it",
                   "i surrender", "you win"}))
  {
    h.kind = TwentyHeard::Kind::GiveUp;
    return h;
  }
  if (TqHasAny(w, {"stop playing", "stop the game", "end the game", "quit", "im done", "i m done", "never mind",
                   "nevermind", "no more", "enough", "dont want to play", "don t want to play", "game over"}))
  {
    h.kind = TwentyHeard::Kind::Quit;
    return h;
  }
  if (TqHasAny(w, {"hint", "clue", "help me"}))
  {
    h.kind = TwentyHeard::Kind::Hint;
    return h;
  }
  bool article = false;
  auto const rests = AfterCues(w, article);
  for (auto const & rest : rests)
    if (auto const t = ThingAtStart(rest))
    {
      h.kind = TwentyHeard::Kind::Guess;
      h.thing = *t;
      return h;
    }
  if (auto const size = SizeQuestion(w))
  {
    h.kind = TwentyHeard::Kind::Question;
    h.trait = size->first;
    h.flipped = size->second;
    return h;
  }
  if (TqHasAny(w, {"outside", "outdoors", "out there"}) && !TqHas(w, "in the car"))
  {
    h.kind = TwentyHeard::Kind::Question;
    h.trait = T::InCar;
    h.flipped = true;
    return h;
  }
  if (TqHasAny(w, {"is it safe", "safe"}))
  {
    h.kind = TwentyHeard::Kind::Question;
    h.trait = T::Dangerous;
    h.flipped = true;
    return h;
  }
  for (auto const & tw : kTraitWords)
    if (TqHasAny(w, tw.words))
    {
      h.kind = TwentyHeard::Kind::Question;
      h.trait = tw.trait;
      return h;
    }
  // "Is it a giraffe": a guess at something it was never going to be.
  if (article)
    for (auto const & rest : rests)
    {
      std::istringstream in(rest);
      std::string word, guess;
      for (int n = 0; n < 3 && in >> word; ++n)
        guess += (guess.empty() ? "" : " ") + word;
      if (!guess.empty())
      {
        h.kind = TwentyHeard::Kind::UnknownGuess;
        h.guess = (std::string("aeiou").find(guess[0]) != std::string::npos ? "an " : "a ") + guess;
        return h;
      }
    }
  return h;
}

std::string TwentyLog::Serialize() const
{
  std::string out = "games " + std::to_string(games) + "\n";
  out += "gpsWins " + std::to_string(gpsWins) + "\n";
  out += "driverWins " + std::to_string(driverWins) + "\n";
  out += "grudges " + std::to_string(grudges) + "\n";
  for (auto const & r : recent)
    out += "recent " + r + "\n";
  return out;
}

TwentyLog TwentyLog::Parse(std::string const & text)
{
  TwentyLog log;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line))
  {
    std::istringstream in(line);
    std::string kind;
    in >> kind;
    if (kind == "games")
      in >> log.games;
    else if (kind == "gpsWins")
      in >> log.gpsWins;
    else if (kind == "driverWins")
      in >> log.driverWins;
    else if (kind == "grudges")
      in >> log.grudges;
    else if (kind == "recent")
    {
      std::string name;
      std::getline(in >> std::ws, name);
      if (!name.empty())
        log.recent.push_back(name);
    }
  }
  return log;
}

TwentyQuestions::TwentyQuestions(std::function<double()> rng) : m_rng(std::move(rng)) {}

void TwentyQuestions::NewTrip()
{
  m_state = State::Idle;
  m_asked = 0;
  m_wrongGuesses = 0;
  m_askedTraits.clear();
  m_nudged = false;
  m_nudges = 0;
}

TwentyThing const * TwentyQuestions::Thing() const
{
  return m_state == State::Idle ? nullptr : &kThings[m_thing];
}

void TwentyQuestions::Pick(std::set<std::string> const & nearby, bool night)
{
  auto const fits = [&](TwentyThing const & t, bool skipRecent)
  {
    if ((t.dayOnly && night) || (t.nightOnly && !night))
      return false;
    return !skipRecent || std::find(m_log.recent.begin(), m_log.recent.end(), t.name) == m_log.recent.end();
  };
  auto const pool = [&](bool nearRoute, bool skipRecent)
  {
    std::vector<size_t> out;
    for (size_t i = 0; i < kThings.size(); ++i)
    {
      auto const & t = kThings[i];
      bool const tagged = *t.tag != '\0';
      if (tagged != nearRoute || (tagged && !nearby.count(t.tag)) || !fits(t, skipRecent))
        continue;
      out.push_back(i);
    }
    return out;
  };
  auto const roll = [&] { return m_rng ? m_rng() : 0.0; };
  // Something the driver will actually pass, most of the time there is one.
  std::vector<size_t> options = pool(true, true);
  m_nearRoute = !options.empty() && roll() < 0.7;
  if (!m_nearRoute)
    options = pool(false, true);
  if (options.empty())
    options = pool(false, false);
  m_thing = options[std::min(options.size() - 1, static_cast<size_t>(roll() * options.size()))];
  m_log.recent.push_back(kThings[m_thing].name);
  while (m_log.recent.size() > kRecentKeep)
    m_log.recent.pop_front();
  m_asked = 0;
  m_wrongGuesses = 0;
  m_askedTraits.clear();
  m_nudged = false;
  m_nudges = 0;
}

TwentyQuestions::Reply TwentyQuestions::Offer(std::set<std::string> const & nearby, bool night, double tMs, int tier)
{
  Pick(nearby, night);
  m_state = State::Offered;
  m_lastHeardT = tMs;
  return Make("twenty.offer", tier);
}

TwentyQuestions::Reply TwentyQuestions::Start(std::set<std::string> const & nearby, bool night, double tMs, int tier)
{
  Pick(nearby, night);
  m_state = State::Playing;
  m_lastHeardT = tMs;
  m_log.games++;
  return Make("twenty.start", tier);
}

std::optional<TwentyQuestions::Reply> TwentyQuestions::OnDriverSaid(std::string const & spoken, double tMs, int tier)
{
  if (!Active())
    return {};
  m_lastHeardT = tMs;
  m_nudged = false;
  m_nudges = 0;
  auto const heard = ParseTwentyHeard(spoken);
  if (m_state == State::Offered)
  {
    bool const playing = heard.kind == TwentyHeard::Kind::Question || heard.kind == TwentyHeard::Kind::Guess ||
                         heard.kind == TwentyHeard::Kind::UnknownGuess;
    if (!playing)
    {
      YesNo const yn = heard.kind == TwentyHeard::Kind::Quit ? YesNo::No : ParseYesNo(spoken);
      if (yn == YesNo::Unclear)
        return {};
      if (yn == YesNo::No)
      {
        m_state = State::Over;
        m_log.grudges++;
        auto r = Make("twenty.declined", tier);
        r.grudge = true;
        r.over = true;
        return r;
      }
      m_state = State::Playing;
      m_log.games++;
      return Make("twenty.accepted", tier);
    }
    // Asking a question straight away is a yes.
    m_state = State::Playing;
    m_log.games++;
  }
  return Answered(heard, tier);
}

TwentyQuestions::Reply TwentyQuestions::Answered(TwentyHeard const & heard, int tier)
{
  auto const & thing = kThings[m_thing];
  Reply r;
  switch (heard.kind)
  {
  case TwentyHeard::Kind::Unclear: return Make("twenty.unclear", tier);
  case TwentyHeard::Kind::GiveUp: return End("twenty.giveUp", true, tier);
  case TwentyHeard::Kind::Quit: return End("twenty.quit", true, tier);
  case TwentyHeard::Kind::Guess: return Guessed(heard.thing, tier);
  case TwentyHeard::Kind::UnknownGuess:
    m_asked++;
    m_wrongGuesses++;
    r = Make("twenty.wrongUnknown", tier, {{"guess", heard.guess}});
    break;
  case TwentyHeard::Kind::Hint:
  {
    // Colors last: "it is white" is a lousy hint.
    std::vector<Trait> fresh;
    for (bool const colors : {false, true})
    {
      for (auto const t : thing.yes)
        if (!m_askedTraits.count(static_cast<int>(t) * 2) && (t >= T::Red) == colors)
          fresh.push_back(t);
      if (!fresh.empty())
        break;
    }
    if (fresh.empty())
      return Make("twenty.noHint", tier);
    Trait const t = fresh[std::min(fresh.size() - 1, static_cast<size_t>((m_rng ? m_rng() : 0) * fresh.size()))];
    m_askedTraits.insert(static_cast<int>(t) * 2);
    m_asked++;
    r = Make("twenty.hint", tier, {{"fact", Fact(t)}});
    break;
  }
  case TwentyHeard::Kind::Question:
  {
    m_asked++;
    Answer a = thing.Has(heard.trait);
    if (heard.flipped && a != Answer::Sometimes)
      a = a == Answer::Yes ? Answer::No : Answer::Yes;
    char const * said = a == Answer::Yes ? "yes" : a == Answer::No ? "no" : "sometimes";
    // Asked before, either way round ("is it big" then "is it small" doesn't count as new, though).
    int const id = static_cast<int>(heard.trait) * 2 + (heard.flipped ? 1 : 0);
    if (m_askedTraits.count(id))
      r = Make("twenty.again", tier, {{"answer", said}});
    else
    {
      m_askedTraits.insert(id);
      r = Make(a == Answer::Yes ? "twenty.yes" : a == Answer::No ? "twenty.no" : "twenty.sometimes", tier);
    }
    break;
  }
  }
  if (m_asked >= kQuestions)
  {
    auto end = End("twenty.lose", true, tier);
    end.text = r.text + " " + end.text;
    return end;
  }
  int const left = QuestionsLeft();
  if (left == 10 || left == 5 || left <= 3)
    r.text += " " + Make(left == 1 ? "twenty.lastOne" : "twenty.left", tier).text;
  return r;
}

TwentyQuestions::Reply TwentyQuestions::Guessed(size_t guess, int tier)
{
  auto const & thing = kThings[m_thing];
  m_asked++;
  // Two things with the same name ("another car" is also "a car"): the name is what counts.
  if (guess == m_thing || std::string(kThings[guess].name) == thing.name)
  {
    m_log.driverWins++;
    m_state = State::Over;
    auto r = Make(m_asked <= 5 ? "twenty.winFast" : "twenty.win", tier);
    r.over = true;
    return r;
  }
  m_wrongGuesses++;
  auto const & other = kThings[guess];
  bool const close = std::any_of(kKinds.begin(), kKinds.end(),
                                 [&](Trait k) { return thing.Has(k) == Answer::Yes && other.Has(k) == Answer::Yes; });
  auto r = Make(close ? "twenty.wrongClose" : "twenty.wrongFar", tier, {{"guess", other.name}});
  if (m_asked >= kQuestions)
  {
    auto end = End("twenty.lose", true, tier);
    end.text = r.text + " " + end.text;
    return end;
  }
  int const left = QuestionsLeft();
  if (left == 10 || left == 5 || left <= 3)
    r.text += " " + Make(left == 1 ? "twenty.lastOne" : "twenty.left", tier).text;
  return r;
}

TwentyQuestions::Reply TwentyQuestions::End(std::string const & key, bool gpsWon, int tier, Vars vars)
{
  auto const & thing = kThings[m_thing];
  if (gpsWon)
    m_log.gpsWins++;
  m_state = State::Over;
  auto r = Make(key, tier, std::move(vars));
  if (*thing.quip)
    r.text += " " + std::string(thing.quip);
  if (gpsWon && m_nearRoute)
    r.text += " " + Make("twenty.nearby", tier).text;
  r.over = true;
  return r;
}

std::optional<TwentyQuestions::Reply> TwentyQuestions::Tick(double tMs, int tier)
{
  if (m_state == State::Offered && !m_nudged && tMs - m_lastHeardT >= kNudgeAfterMs)
  {
    // Maybe they answered and the mic missed it: ask again before taking it personally.
    m_nudged = true;
    m_lastHeardT = tMs;
    return Make("twenty.offerAgain", tier);
  }
  if (m_state == State::Offered && tMs - m_lastHeardT >= kNudgeAfterMs)
  {
    // Ignored. That's a no, and a rude one.
    m_state = State::Over;
    m_log.grudges++;
    auto r = Make("twenty.ignored", tier);
    r.grudge = true;
    r.over = true;
    return r;
  }
  if (m_state != State::Playing)
    return {};
  if (tMs - m_lastHeardT < kNudgeAfterMs)
    return {};
  // Silence isn't a loss: the mic may have missed them. Say so and ask again; after a few tries,
  // call the game off without a winner.
  m_lastHeardT = tMs;
  if (m_nudges >= kMaxNudges)
    return End("twenty.timeout", false, tier);
  ++m_nudges;
  return Make("twenty.nudge", tier);
}

std::optional<TwentyQuestions::Reply> TwentyQuestions::WrapUp(int tier)
{
  if (m_state != State::Playing)
    return {};
  return End("twenty.arrived", true, tier);
}

TwentyQuestions::Reply TwentyQuestions::Resume(double tMs, int tier)
{
  // The pause for directions wasn't the driver going quiet.
  m_lastHeardT = tMs;
  m_nudged = false;
  m_nudges = 0;
  return Make("twenty.resume", tier);
}

Vars TwentyQuestions::Common() const
{
  auto const & thing = kThings[m_thing];
  return {{"thing", thing.name},
          {"where", m_nearRoute ? "on this drive" : thing.where},
          {"left", std::to_string(QuestionsLeft())},
          {"used", std::to_string(m_asked)},
          {"wins", std::to_string(m_log.gpsWins)},
          {"losses", std::to_string(m_log.driverWins)}};
}

TwentyQuestions::Reply TwentyQuestions::Make(std::string const & key, int tier, Vars vars)
{
  for (auto const & [k, v] : Common())
    vars.emplace(k, v);

  Reply r;
  r.key = key;
  r.text = Fill(Line(key, tier), vars);
  // "{guess}? No." and "Yes. {thing}." start a sentence with a name: "a deer? No." -> "A deer? No."
  for (size_t i = 0; i < r.text.size(); ++i)
    if (i == 0 || (i >= 2 && r.text[i - 1] == ' ' && std::string(".?!").find(r.text[i - 2]) != std::string::npos))
      r.text[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(r.text[i])));
  return r;
}

std::string TwentyQuestions::Line(std::string const & key, int tier)
{
  tier = std::clamp(tier, 0, 2);
  if (m_picker)
    return m_picker(key, tier);
  auto const & options = LinesFor(key, tier);
  double const r = m_rng ? m_rng() : 0;
  return options[std::min(options.size() - 1, static_cast<size_t>(r * options.size()))];
}
}  // namespace backseat
