#include "backseat/pop_quiz.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace backseat
{
std::vector<QuizQuestion> const & QuizQuestions()
{
  // Road rules are the general US ones every driver's ed covers. Trivia is clean and settled:
  // nothing a passenger can win an argument about.
  static std::vector<QuizQuestion> const questions = {
      // Road rules and driver's ed.
      {"yellowLight", true, "What does a yellow light mean?",
       {"Floor it|floor|gun it|speed up", "Stop if you safely can|stop|slow down|safely", "Honk at it|honk"}, 1},
      {"stopSides", true, "How many sides does a stop sign have?", {"6|six", "8|eight|octagon", "10|ten"}, 1},
      {"yieldShape", true, "What shape is a yield sign?",
       {"A circle|circle|round", "A triangle|triangle", "A diamond|diamond"}, 1},
      {"flashingRed", true, "A flashing red light means what?",
       {"Treat it like a stop sign|stop sign|stop", "Go faster|faster", "The light is broken, ignore it|broken|ignore"}, 0},
      {"fourWay", true, "Two cars reach a four way stop at the same time. Who goes first?",
       {"The car on the right|right", "The car on the left|left", "The bigger car|bigger"}, 0},
      {"followDistance", true, "How many seconds behind the car ahead should you stay, at least?",
       {"1|one", "3|three", "10|ten"}, 1},
      {"schoolBus", true, "A school bus stops with its red lights flashing. What do you do?",
       {"Stop|stop", "Pass it quickly|pass|quickly", "Honk|honk"}, 0},
      {"siren", true, "An ambulance comes up behind you with its siren on. What do you do?",
       {"Speed up to stay ahead|speed up|ahead", "Pull to the right and stop|pull over|pull|right", "Brake right where you are|brake"}, 1},
      {"handsOnWheel", true, "Where do driving instructors now say to put your hands on the wheel?",
       {"10 and 2|ten and two|10|ten", "9 and 3|nine and three|9|nine", "12 and 6|twelve and six|12|twelve"}, 1},
      {"orangeSign", true, "What does an orange diamond sign usually mean?",
       {"A work zone ahead|work zone|construction|work", "A school ahead|school", "Free parking|free|parking"}, 0},
      {"brownSign", true, "What do brown road signs point you to?",
       {"Parks and attractions|parks|park|attractions|recreation", "Hospitals|hospital", "Speed traps|speed trap|traps"}, 0},
      {"blueSign", true, "What do blue road signs tell you about?",
       {"Gas, food and lodging|gas|food|lodging|services", "Road work|road work|work", "School zones|school"}, 0},
      {"hydroplane", true, "Your car starts hydroplaning. What should you do?",
       {"Slam the brakes|slam|brakes", "Ease off the gas and steer straight|ease|steer straight|ease off", "Turn hard|turn hard|hard"}, 1},
      {"fog", true, "In thick fog, which headlights should you use?",
       {"High beams|high", "Low beams|low", "None, go by feel|none|feel"}, 1},
      {"merge", true, "When you merge onto a highway, what speed should you aim for?",
       {"Way under the speed of traffic|under|slow", "The speed of traffic|same|match", "Double the speed limit|double"}, 1},
      {"leftLane", true, "On the highway, what is the left lane for?",
       {"Passing|passing|pass", "Cruising forever|cruising|forever", "Texting|texting|text"}, 0},
      {"brokenWhite", true, "What does a broken white line between lanes mean?",
       {"You may change lanes|change lanes|change|may", "Never cross it|never", "The road is under construction|construction"}, 0},
      {"solidYellow", true, "A solid yellow line on your side of the center means what?",
       {"Pass whenever you want|whenever", "No passing|no passing|no", "Bike lane|bike"}, 1},
      {"crosswalk", true, "Someone is walking in the crosswalk. Who has the right of way?",
       {"The walker|walker|person walking|walking|them", "You, you're in a car|car|me|you", "Whoever is louder|louder"}, 0},
      {"blindSpot", true, "Before changing lanes, what checks your blind spot?",
       {"A quick look over your shoulder|shoulder|look", "Honking first|honking|honk", "Closing your eyes|closing|eyes"}, 0},
      {"uphillCurb", true, "Parking uphill next to a curb, which way do you turn the front wheels?",
       {"Away from the curb|away", "Toward the curb|toward|towards", "Straight ahead|straight"}, 0},
      {"bac", true, "In most of the US, what blood alcohol level makes it illegal to drive at 21 and over?",
       {"0.02|point oh two|point zero two|02", "0.08|point oh eight|point zero eight|08", "0.20|point two|point two zero|20"}, 1},
      {"turnSignal", true, "How far before a turn should you signal, in town?",
       {"About 100 feet|100|hundred|feet", "Halfway through the turn|halfway", "Signals are optional|optional"}, 0},
      {"rightOnRed", true, "Where it's allowed, what do you have to do before turning right on red?",
       {"Come to a full stop|full stop|stop", "Honk twice|honk|twice", "Nothing, just go|nothing|go"}, 0},
      {"skid", true, "When the back of the car skids, where do you steer?",
       {"Where you want to go|want|want to go", "The opposite way|opposite", "Let go of the wheel|let go"}, 0},
      {"headlightsDusk", true, "When should your headlights be on?",
       {"From sunset to sunrise|sunset|sunrise|night|dark", "Only on highways|only highways|highways", "Only when it rains|rains"}, 0},
      {"wipersRain", true, "The road is most slippery when?",
       {"Right after it starts to rain|starts|right after|first", "After it's rained for hours|hours", "It's always the same|same"}, 0},
      {"railroad", true, "The gates are down at a railroad crossing. What do you do?",
       {"Go around them|around", "Stop and wait|stop|wait", "Back up and floor it|back up|floor"}, 1},
      {"modelT", false, "What year did the Ford Model T come out?", {"1908|nineteen oh eight", "1935|nineteen thirty five", "1955|nineteen fifty five"}, 0},
      {"firstTicket", false, "When was the first speeding ticket given out?",
       {"1896|eighteen ninety six", "1925|nineteen twenty five", "1950|nineteen fifty"}, 0},
      // Roads and cars, harmless anywhere.
      {"longestInterstate", false, "What is the longest interstate in the US?",
       {"I-95|95|ninety five", "I-90|90|ninety", "I-70|70|seventy"}, 1},
      {"evenInterstate", false, "Interstates with even numbers mostly run which way?",
       {"North and south|north|south", "East and west|east|west", "In circles|circles"}, 1},
      {"route66", false, "Route 66 starts in Chicago and ends near which city?",
       {"Los Angeles|la|los angeles|santa monica|angeles", "Las Vegas|vegas", "Dallas"}, 0},
      {"gpsStands", false, "What does GPS stand for?",
       {"Global Positioning System|global|positioning", "Guy Pointing Sideways|guy|pointing|sideways", "Great Personal Support|great|personal|support"}, 0},
      {"leftSide", false, "Which of these countries drives on the left?",
       {"France", "Germany", "The United Kingdom|uk|england|britain|kingdom|united kingdom"}, 2},
      {"carColor", false, "What is the most common car color in the world?", {"Red", "White", "Blue"}, 1},
      {"ferrari", false, "Which car brand has a prancing horse on its logo?", {"Ferrari", "Lamborghini", "Jaguar"}, 0},
      {"bestSeller", false, "What has been the best selling vehicle in the US for decades?",
       {"Ford F-Series|ford|f series|f-150|f150|f", "Toyota Camry|camry|toyota", "Honda Civic|civic|honda"}, 0},
      // General trivia.
      {"jupiter", false, "What is the biggest planet in our solar system?", {"Saturn", "Jupiter", "Earth"}, 1},
      {"cheetah", false, "What is the fastest land animal?", {"Cheetah", "Horse", "Ostrich"}, 0},
      {"octopus", false, "How many hearts does an octopus have?", {"1|one", "3|three", "8|eight"}, 1},
      {"canberra", false, "What is the capital of Australia?", {"Sydney", "Melbourne", "Canberra"}, 2},
      {"everest", false, "What is the tallest mountain above sea level?",
       {"Mount Everest|everest", "K2|k two|k", "Mount Kilimanjaro|kilimanjaro"}, 0},
      {"spiderLegs", false, "How many legs does a spider have?", {"6|six", "8|eight", "10|ten"}, 1},
      {"bones", false, "About how many bones are in an adult human body?",
       {"106|one hundred six|one oh six", "206|two hundred six|two oh six", "306|three hundred six|three oh six"}, 1},
      {"venus", false, "What is the hottest planet in our solar system?", {"Mercury", "Venus", "Mars"}, 1},
      {"rhodeIsland", false, "What is the smallest US state?", {"Delaware", "Rhode Island|rhode|road island", "Connecticut"}, 1},
      {"mercury", false, "Which metal is a liquid at room temperature?", {"Mercury", "Iron", "Aluminum|aluminium"}, 0},
      {"gold", false, "What is the chemical symbol for gold?", {"Au|a u|ay you", "Ag|a g|ay gee", "Gd|g d|gee dee"}, 0},
      {"armstrong", false, "Who was the first person to walk on the moon?",
       {"Buzz Aldrin|buzz|aldrin", "Neil Armstrong|neil|armstrong", "Yuri Gagarin|yuri|gagarin"}, 1},
      {"marathon", false, "About how many miles is a marathon?", {"13|thirteen", "26|twenty six|26.2", "50|fifty"}, 1},
      {"plantsGas", false, "Which gas do plants take in to make food?",
       {"Oxygen", "Carbon dioxide|carbon|co2|dioxide", "Helium"}, 1},
      {"pacific", false, "What is the largest ocean?", {"Atlantic", "Indian", "Pacific"}, 2},
      {"peanut", false, "A peanut is not a nut. What is it?",
       {"A legume|legume|bean", "A berry|berry", "A seed from a tree|tree"}, 0},
      {"banana", false, "Botanically speaking, a banana is a what?", {"A berry|berry", "A nut|nut", "A vegetable|vegetable"}, 0},
      {"crows", false, "What is a group of crows called?", {"A flock of doom|flock|doom", "A murder|murder", "A gang|gang"}, 1},
      {"saturnMoons", false, "Which planet has the most known moons?", {"Jupiter", "Saturn", "Neptune"}, 1},
      {"giraffeTongue", false, "What color is a giraffe's tongue?", {"Pink", "Dark purple|purple|blue|black", "Green"}, 1},
      {"honey", false, "Which food can last for thousands of years without spoiling?",
       {"Bread", "Honey", "Cheese"}, 1},
      {"sloth", false, "Which animal can hold its breath longer than a dolphin?", {"A sloth|sloth", "A cat|cat", "A chicken|chicken"}, 0},
      {"shrimpHeart", false, "Where is a shrimp's heart?", {"In its tail|tail", "In its head|head", "It has none|none"}, 1},
      {"strawberry", false, "Which fruit has its seeds on the outside?",
       {"Strawberry|strawberries", "Blueberry|blueberries", "Grape|grapes"}, 0},
      {"flamingo", false, "Why are flamingos pink?", {"Their food|food|eat|diet|shrimp", "The sun|sun", "Embarrassment|embarrassed"}, 0},
      {"fingerprints", false, "Which animal has fingerprints almost like a human's?",
       {"A koala|koala", "A dog|dog", "A parrot|parrot"}, 0},
      {"eiffel", false, "In summer heat, the Eiffel Tower gets about how much taller?",
       {"6 inches|6|six|inches", "6 feet|six feet|feet", "Not at all|not|none"}, 0},
  };
  return questions;
}

QuizQuestion const * FindQuizQuestion(std::string const & id)
{
  for (auto const & q : QuizQuestions())
    if (id == q.id)
      return &q;
  return nullptr;
}

std::string QuizChoiceText(char const * choice)
{
  std::string const s(choice);
  return s.substr(0, s.find('|'));
}

namespace
{
/// Lowercase words, with apostrophes dropped ("it's" is "its") and decimals kept ("0.08").
std::vector<std::string> QuizWords(std::string const & text)
{
  std::vector<std::string> words;
  std::string word;
  for (size_t i = 0; i < text.size(); ++i)
  {
    unsigned char const ch = static_cast<unsigned char>(text[i]);
    bool const decimal = ch == '.' && !word.empty() && std::isdigit(static_cast<unsigned char>(word.back())) &&
                         i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1]));
    if (std::isalnum(ch) || decimal)
      word += static_cast<char>(std::tolower(ch));
    else if (ch == '\'' || ch == 0xE2)  // Straight or curly (UTF-8 E2 80 99) apostrophe.
    {
      if (ch == 0xE2)
        i += 2;
    }
    else if (!word.empty())
      words.push_back(std::exchange(word, std::string()));
  }
  if (!word.empty())
    words.push_back(word);
  return words;
}

bool QuizAmong(std::string const & w, std::initializer_list<char const *> list)
{
  return std::any_of(list.begin(), list.end(), [&w](char const * s) { return w == s; });
}

/// Words that say nothing about which choice was meant.
bool QuizFiller(std::string const & w)
{
  return QuizAmong(w, {"a", "an", "the", "of", "to", "and", "it", "is", "in", "on", "you", "your", "for", "at", "with", "by",
                   "its", "or", "i", "think", "um", "uh", "be", "that", "this", "one", "my", "answer", "guess", "go",
                   "say", "final", "pick", "option", "letter", "number", "are", "they", "do", "just", "up", "down", "so", "well", "like", "okay", "ok"});
}

/// Spoken numbers as the choices write them, so "three" finds "3". "The first one" stays a place.
std::string QuizNumberWord(std::string const & w)
{
  static char const * const kNumbers[] = {"zero", "one", "two",   "three",  "four",   "five",    "six",
                                          "seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen"};
  for (size_t i = 0; i < std::size(kNumbers); ++i)
    if (w == kNumbers[i])
      return std::to_string(i);
  return {};
}

std::set<std::string> QuizChoiceWords(char const * choice)
{
  std::set<std::string> words;
  std::string const all(choice);
  size_t start = 0;
  while (start <= all.size())
  {
    size_t const bar = std::min(all.find('|', start), all.size());
    for (auto const & w : QuizWords(all.substr(start, bar - start)))
      if (!QuizFiller(w) || QuizNumberWord(w).size())
        words.insert(QuizNumberWord(w).empty() ? w : QuizNumberWord(w));
    start = bar + 1;
  }
  return words;
}
}  // namespace

QuizHeard ParseQuizAnswer(QuizQuestion const & q, std::string const & spoken)
{
  auto const words = QuizWords(spoken);
  QuizHeard heard;
  if (words.empty())
    return heard;

  // Said word for word: "the speed of traffic".
  std::string said;
  for (auto const & w : words)
    said += w + " ";
  for (size_t i = 0; i < 3; ++i)
  {
    std::string const all(q.choices[i]);
    size_t start = 0;
    while (start <= all.size())
    {
      size_t const bar = std::min(all.find('|', start), all.size());
      std::string spelled;
      for (auto const & w : QuizWords(all.substr(start, bar - start)))
        spelled += w + " ";
      if (!spelled.empty() && (said == spelled || said == "the " + spelled || "the " + said == spelled))
        return {QuizHeard::Kind::Choice, static_cast<int>(i)};
      start = bar + 1;
    }
  }

  // By its words: the words of a choice that no other choice shares.
  std::array<std::set<std::string>, 3> own;
  for (size_t i = 0; i < 3; ++i)
    own[i] = QuizChoiceWords(q.choices[i]);
  std::array<int, 3> hits{};
  for (size_t k = 0; k < words.size(); ++k)
  {
    std::string w = words[k];
    bool const place = k > 0 && QuizAmong(words[k - 1], {"first", "second", "third", "last", "the", "that", "this"});
    if (!QuizNumberWord(w).empty() && !(w == "one" && place))
      w = QuizNumberWord(w);
    if (QuizFiller(w) && QuizNumberWord(words[k]).empty())
      continue;
    for (size_t i = 0; i < 3; ++i)
    {
      bool const shared = own[(i + 1) % 3].count(w) || own[(i + 2) % 3].count(w);
      if (own[i].count(w) && !shared)
        ++hits[i];
    }
  }
  int const best = static_cast<int>(std::max_element(hits.begin(), hits.end()) - hits.begin());
  int const tied = static_cast<int>(std::count(hits.begin(), hits.end(), hits[best]));
  if (hits[best] > 0 && tied == 1)
    return {QuizHeard::Kind::Choice, best};

  // By its letter or place: "B", "option c", "the second one".
  int byLetter = -1;
  bool const brief = words.size() <= 3;
  for (size_t k = 0; k < words.size(); ++k)
  {
    auto const & w = words[k];
    bool const cued = brief || (k > 0 && QuizAmong(words[k - 1], {"option", "letter", "answer", "pick", "choose", "is", "its",
                                                              "say", "guess", "be", "go", "with", "number"}));
    int pick = -1;
    if (QuizAmong(w, {"first", "1st"}))
      pick = 0;
    else if (QuizAmong(w, {"second", "2nd"}))
      pick = 1;
    else if (QuizAmong(w, {"third", "3rd", "last"}))
      pick = 2;
    else if (cued && QuizAmong(w, {"a", "ay", "eh"}))
      pick = 0;
    else if (cued && QuizAmong(w, {"b", "bee", "be"}) && !(w == "be" && k + 1 < words.size()))
      pick = 1;
    else if (cued && QuizAmong(w, {"c", "see", "sea", "si"}))
      pick = 2;
    else if (k > 0 && QuizAmong(words[k - 1], {"option", "number"}) && !QuizNumberWord(w).empty())
    {
      int const n = std::stoi(QuizNumberWord(w));
      pick = n >= 1 && n <= 3 ? n - 1 : -1;
    }
    if (pick >= 0)
    {
      if (byLetter >= 0 && byLetter != pick)
        return heard;
      byLetter = pick;
    }
  }
  if (byLetter >= 0)
    return {QuizHeard::Kind::Choice, byLetter};

  std::string padded = " ";
  for (auto const & w : words)
    padded += w + " ";
  for (char const * cue : {" dont know ", " no idea ", " pass ", " skip ", " give up ", " no clue ", " beats me ",
                           " i have no ", " who knows ", " idk "})
    if (padded.find(cue) != std::string::npos)
      return {QuizHeard::Kind::GiveUp, -1};
  return heard;
}

bool AsksForPopQuiz(std::string const & spoken)
{
  std::string padded = " ";
  for (auto const & w : QuizWords(spoken))
    padded += w + " ";
  for (char const * cue : {" pop quiz ", " quiz me ", " give me a quiz ", " ask me a question ", " trivia question ",
                           " another quiz ", " a quiz "})
    if (padded.find(cue) != std::string::npos)
      return padded.find(" no ") == std::string::npos && padded.find(" dont ") == std::string::npos;
  return false;
}

void PopQuiz::Start(QuizQuestion const & q, double t, double answerBy, bool tappable)
{
  m_question = &q;
  m_startedT = t;
  m_answerBy = answerBy;
  m_tappable = tappable;
  m_picked = -1;
  m_outcome.reset();
}

std::optional<PopQuiz::Outcome> PopQuiz::Finish(Outcome outcome, double t)
{
  m_outcome = outcome;
  m_answeredT = t;
  return outcome;
}

std::optional<PopQuiz::Outcome> PopQuiz::Answer(int choice, double t)
{
  if (!Asking() || choice < 0 || choice > 2)
    return std::nullopt;
  m_picked = choice;
  return Finish(choice == m_question->answer ? Outcome::Right : Outcome::Wrong, t);
}

std::optional<PopQuiz::Outcome> PopQuiz::GiveUp(double t)
{
  if (!Asking())
    return std::nullopt;
  return Finish(Outcome::GaveUp, t);
}

std::optional<PopQuiz::Outcome> PopQuiz::Tick(double t)
{
  if (!Asking() || t < m_answerBy)
    return std::nullopt;
  return Finish(Outcome::TimedOut, t);
}
}  // namespace backseat
