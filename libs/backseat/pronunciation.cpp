#include "backseat/pronunciation.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>

namespace backseat::pronunciation
{
namespace
{
// MARK: - The dictionary

struct Word
{
  char const * word;
  /// Misaki IPA between slashes, or a respelling ("Ab-DEE-rus").
  char const * said;
};

// Words the voice gets wrong, and the stand-ins PickSenses() swaps in for a sense that isn't the
// dictionary's default (Kokoro's frontends don't look at the sentence, so they always say the
// default). Keys are lowercase: Android's lexicon lowercases what it reads, so a stand-in must
// never be a real word with another sound ("us").
constexpr Word kWords[] = {
    // The highway ("US 36"), not the pronoun.
    {"yooess", "/jˌuˈɛs/"},
    // live, lives: the verb ("Where do you live?"); the default is the adjective ("live music").
    {"liv", "/lˈɪv/"},
    {"livz", "/lˈɪvz/"},
    // wind, winds, wound: turning ("wind down", "the road winds"); the default is the weather.
    {"wynd", "/wˈInd/"},
    {"wyndz", "/wˈIndz/"},
    {"wownd", "/wˈWnd/"},
    // close: shutting ("close your eyes"); the default is near ("close to").
    {"kloze", "/klˈOz/"},
    // Nouns where the default is the verb.
    {"ekskyoos", "/ɪkskjˈus/"},   // an excuse
    {"estimit", "/ˈɛstəmət/"},    // the estimate
    {"yoos", "/jˈus/"},           // no use
    {"kontent", "/kˈɑntɛnt/"},    // the content
    // Verbs where the default is the noun.
    {"rikord", "/ɹəkˈɔɹd/"},      // I'll record it
    {"insullt", "/ɪnsˈʌlt/"},     // insult your family
    {"subjekt", "/səbʤˈɛkt/"},    // subject others
    {"suspekt", "/səspˈɛkt/"},    // I suspect
    {"kunduct", "/kəndˈʌkt/"},    // conduct the car
    {"objekt", "/əbʤˈɛkt/"},      // I object
    {"dezzert", "/dəzˈɜɹt/"},     // desert you
    {"permitt", "/pəɹmˈɪt/"},     // permit it
    {"kontrakt", "/kəntɹˈækt/"},  // to contract
    {"progres", "/pɹəɡɹˈɛs/"},    // we progress
    {"prezent", "/pɹizˈɛnt/"},    // I present
    {"tair", "/tˈɛɹ/"},           // tear it up
    {"tairz", "/tˈɛɹz/"},         // he tears
    // Music, not the fish.
    {"bayss", "/bˈAs/"},
    // Tiny, not sixty seconds.
    {"mynoot", "/mInˈut/"},
    // bow, bowed, bowing: bending, or a ship's front ("took a bow", "bowed his head", "the bow of the
    // ship"); the default is the ribbon or the archer's bow. "bough"/"boughs" are real words with that
    // sound; the other two aren't words at all.
    {"bough", "/bˈW/"},
    {"boughs", "/bˈWz/"},
    {"boughed", "/bˈWd/"},
    {"boughing", "/bˈWɪŋ/"},
    // Clock times: "5pm", "5:30 p.m." (Clock()). Spelled "AM"/"PM" read as "am"/"pim" or get skipped.
    {"ayem", "/ˌAˈɛm/"},
    {"peeem", "/pˌiˈɛm/"},
};

// Names Kokoro's Misaki dictionaries lack, so the fallback guesses them ("Simon" -> "sim-on").
// Misaki phonemes from BookLyre's names lexicon (EpubToM4B tools/pronunciation/build_names_lexicon.py,
// CMUdict converted to Misaki; CROSS X-003). Keep this to names a line or the driver can bring up.
struct Name
{
  char const * word;
  char const * phonemes;
};

constexpr Name kNames[] = {
    // Line-bank names (Neil Armstrong, Gertrude, Floyd...).
    {"neil", "nˈil"}, {"armstrong", "ˈɑɹmstɹˌɑŋ"}, {"gertrude", "ɡˈɜɹtɹud"}, {"nigel", "nˈIʤᵊl"},
    {"darlene", "dˈɑɹlˌin"}, {"sheila", "ʃˈilə"}, {"duane", "dwˈAn"}, {"bev", "bˈɛv"}, {"wanda", "wˈɑndə"},
    {"floyd", "flˈYd"}, {"trudy", "tɹˈudi"}, {"maxine", "mæksˈin"}, {"lyle", "lˈIl"}, {"rhonda", "ɹˈɑndə"},
    {"yee", "jˈi"}, {"angeles", "ˈænʤələs"}, {"ferrari", "fəɹˈɑɹi"}, {"yuri", "jˈʊɹi"}, {"katy", "kˈAɾi"},
    // First names PronounsForName() knows, so the driver's own name is said right.
    {"allison", "ˈæləsən"}, {"amanda", "əmˈændə"}, {"andrea", "ˈændɹˌiə"}, {"andrew", "ˈændɹu"}, {"andy", "ˈændi"},
    {"angela", "ˈænʤələ"}, {"ann", "ˈæn"}, {"anthony", "ˈænθəni"}, {"ashley", "ˈæʃli"}, {"ava", "ˈAvə"},
    {"betty", "bˈɛɾi"}, {"brian", "bɹˈIən"}, {"bruce", "bɹˈus"}, {"carolyn", "kˈɛɹələn"}, {"catherine", "kˈæθəɹən"},
    {"cheryl", "ʃˈɛɹəl"}, {"chloe", "klˈOi"}, {"christine", "kɹəstˈin"}, {"connor", "kˈɑnəɹ"}, {"cynthia", "sˈɪnθiə"},
    {"danielle", "dˌæniˈɛl"}, {"debbie", "dˈɛbi"}, {"debra", "dˈɛbɹə"}, {"denise", "dənˈis"}, {"dennis", "dˈɛnəs"},
    {"derek", "dˈɛɹək"}, {"diane", "dIˈæn"}, {"donald", "dˈɑnəld"}, {"dorothy", "dˈɔɹəθi"}, {"doug", "dˈʌɡ"},
    {"douglas", "dˈʌɡləs"}, {"dylan", "dˈɪlən"}, {"ed", "ˈɛd"}, {"emily", "ˈɛməli"}, {"eric", "ˈɛɹək"},
    {"ethan", "ˈiθən"}, {"evan", "ˈɛvən"}, {"evelyn", "ˈɛvələn"}, {"gerald", "ʤˈɛɹəld"}, {"hannah", "hˈænə"},
    {"ian", "ˈiən"}, {"jacqueline", "ʤˈækwələn"}, {"janet", "ʤˈænət"}, {"janice", "ʤˈænəs"}, {"jeremy", "ʤˈɛɹəmˌi"},
    {"jerry", "ʤˈɛɹi"}, {"joan", "ʤˈOn"}, {"jose", "hˌOzˈA"}, {"juan", "wˈɑn"}, {"julie", "ʤˈuli"},
    {"katherine", "kˈæθəɹən"}, {"kathleen", "kæθlˈin"}, {"kathryn", "kˈæθɹən"}, {"kathy", "kˈæθi"},
    {"katie", "kˈAɾi"}, {"kayla", "kˈAlə"}, {"keith", "kˈiθ"}, {"kenneth", "kˈɛnəθ"}, {"kevin", "kˈɛvən"},
    {"kim", "kˈɪm"}, {"kimberly", "kˈɪmbəɹli"}, {"kristen", "kɹˈɪstən"}, {"kyle", "kˈIl"}, {"larry", "lˈɛɹi"},
    {"lauren", "lˈɔɹən"}, {"leah", "lˈiə"}, {"linda", "lˈɪndə"}, {"lindsay", "lˈɪndzi"}, {"liz", "lˈɪz"},
    {"lori", "lˈɔɹi"}, {"lucas", "lˈukəs"}, {"luis", "luˈis"}, {"luke", "lˈuk"}, {"marcus", "mˈɑɹkəs"},
    {"margaret", "mˈɑɹɡəɹət"}, {"marie", "məɹˈi"}, {"marilyn", "mˈɛɹələn"}, {"martha", "mˈɑɹθə"}, {"matt", "mˈæt"},
    {"matthew", "mˈæθju"}, {"megan", "mˈAɡən"}, {"mia", "mˈiə"}, {"michelle", "məʃˈɛl"}, {"natalie", "nˈæɾəli"},
    {"nathan", "nˈAθən"}, {"nicole", "nəkˈOl"}, {"olivia", "Olˈɪviə"}, {"paige", "pˈAʤ"}, {"pamela", "pˈæmələ"},
    {"patricia", "pətɹˈɪʃə"}, {"patrick", "pˈætɹək"}, {"paul", "pˈɔl"}, {"raymond", "ɹˈAmənd"},
    {"rebecca", "ɹəbˈɛkə"}, {"ronald", "ɹˈɑnəld"}, {"russell", "ɹˈʌsᵊl"}, {"ryan", "ɹˈIən"}, {"samantha", "səmˈænθə"},
    {"sara", "sˈɛɹə"}, {"scott", "skˈɑt"}, {"sean", "ʃˈɔn"}, {"shirley", "ʃˈɜɹli"}, {"sophia", "sOfˈiə"},
    {"stephanie", "stˈɛfəni"}, {"teresa", "təɹˈisə"}, {"theresa", "təɹˈisə"}, {"travis", "tɹˈævəs"},
    {"willie", "wˈɪli"}, {"zach", "zˈæk"}, {"zachary", "zˈækəɹi"}, {"zoe", "zˈOi"},
};

// MARK: - Picking the sense

using Set = std::initializer_list<std::string_view>;

bool In(std::string const & w, Set set)
{
  return std::find(set.begin(), set.end(), w) != set.end();
}

bool EndsWith(std::string const & w, std::string_view tail)
{
  return w.size() >= tail.size() && w.compare(w.size() - tail.size(), tail.size(), tail) == 0;
}

// Words before a verb: "to", modals, "don't", subject pronouns.
bool Verby(std::string const & w)
{
  return In(w, {"to", "will", "can", "can't", "cannot", "could", "would", "should", "must", "might", "may", "please",
                "don't", "didn't", "doesn't", "never", "gonna", "wanna", "let's", "i", "we", "they", "you", "won't",
                "wouldn't", "couldn't", "shouldn't", "gotta"}) ||
         EndsWith(w, "'ll") || EndsWith(w, "'d");
}

// Words before a noun.
bool Det(std::string const & w)
{
  return In(w, {"a", "an", "the", "my", "your", "his", "her", "its", "our", "their", "this", "that", "no", "any",
                "every", "another", "one", "some", "of"});
}

// Words after a verb.
bool Obj(std::string const & w)
{
  return In(w, {"you", "your", "me", "my", "him", "her", "them", "us", "it", "the", "anything", "anyone", "everyone",
                "people", "everybody", "somebody", "someone", "yourself", "myself", "himself", "herself",
                "themselves", "ourselves", "others", "this", "that", "his", "their", "our"});
}

struct Ctx
{
  std::string p2, p1, n1, n2, n3;
  /// Punctuation or the end of the text right after the word ("He bowed." "He bowed, then left").
  bool endsClause = false;
};

// MARK: - bow (BookLyre's spoken_heteronyms, CROSS X-016)

bool BowManner(std::string const & w)
{
  return In(w, {"deep", "low", "slight", "polite", "courtly", "stiff", "formal", "curt", "quick", "mocking", "sweeping",
                "graceful", "gracious", "respectful", "solemn", "theatrical", "elaborate", "exaggerated", "ironic",
                "courteous", "humble", "brief", "awkward", "gallant", "final", "last", "curtain"});
}

bool Ship(std::string const & w)
{
  return In(w, {"ship", "boat", "vessel", "yacht", "canoe", "ferry", "galley", "schooner", "frigate", "steamer",
                "barge", "liner", "launch"});
}

// "bow and arrow", "bow tie", "violin bow strings", "took a bow from the rack": the ribbon or the weapon.
bool BowIsAThing(Ctx const & c)
{
  return (c.n1 == "and" && In(c.n2, {"arrow", "arrows"})) ||
         In(c.n1, {"tie", "ties", "string", "strings", "from", "off"}) || (c.n1 == "out" && c.n2 == "of");
}

bool TakeVerb(std::string const & w)
{
  return In(w, {"take", "takes", "took", "taken", "taking"});
}

// "the bow of the ship", "from bow to stern", "the boat's bows", "stern to bow".
bool ShipsBow(Ctx const & c)
{
  if (c.n1 == "of" && In(c.n2, {"the", "our", "his", "her", "their", "a"}) && Ship(c.n3))
    return true;
  if (In(c.n1, {"to", "and"}) && c.n2 == "stern")
    return true;
  if (c.p1.size() > 2 && EndsWith(c.p1, "'s") && Ship(c.p1.substr(0, c.p1.size() - 2)))
    return true;
  return c.p2 == "stern" && c.p1 == "to";
}

// "bowed low", "bowing to the queen", "bows deeply", "bowed his head", "bowed and scraped".
bool BendsAfter(Ctx const & c)
{
  if (In(c.n1, {"low", "lower", "deeply", "slightly", "politely", "stiffly", "formally", "gravely", "gracefully",
                "courteously", "respectfully", "solemnly", "humbly", "graciously", "to", "before", "down", "out"}))
    return true;
  if (In(c.n1, {"his", "her", "their", "its", "my", "your", "our"}) && In(c.n2, {"head", "heads"}))
    return true;
  if (c.n1 == "and" && In(c.n2, {"scraped", "scraping"}))
    return true;
  return c.n1 == "in" && In(c.n2, {"greeting", "return"});
}

char const * Bow(Ctx const & c, char const * bent)
{
  if (BowIsAThing(c))
    return nullptr;
  // "took a bow", "took his final bow", "taking bows".
  if (TakeVerb(c.p1) || (TakeVerb(c.p2) && (Det(c.p1) || In(c.p1, {"another"}) || BowManner(c.p1))))
    return bent;
  // "a deep bow", "with a low sweeping bow".
  if (BowManner(c.p1) && (Det(c.p2) || In(c.p2, {"another"}) || BowManner(c.p2)))
    return bent;
  return ShipsBow(c) ? bent : nullptr;
}

using Rule = std::function<char const *(Ctx const &)>;

std::map<std::string, Rule> const & Rules()
{
  static std::map<std::string, Rule> const rules = {
      {"live",
       [](Ctx const & c) -> char const * {
         // "go live", "it's live", "a live show", "live music": the adjective (the default).
         if (In(c.p1, {"a", "an", "the", "go", "goes", "going", "went", "gone", "is", "are", "was", "were", "be",
                       "been", "being", "it's", "we're", "they're", "you're", "i'm", "he's", "she's", "that's",
                       "isn't", "aren't", "wasn't", "still", "totally"}))
           return nullptr;
         if (In(c.n1, {"music", "show", "shows", "broadcast", "stream", "streaming", "feed", "tv", "audience", "bait",
                       "wire", "wires", "performance", "concert", "band", "version", "event", "coverage", "footage",
                       "update", "updates", "traffic", "camera", "cam", "action", "recording", "radio", "chat",
                       "demo", "sports", "game", "games", "tour", "set", "mic", "data", "map", "view", "location",
                       "animals", "ammo", "round", "rounds", "theater", "comedy", "studio", "album", "oak", "oaks"}))
           return nullptr;
         return "liv";
       }},
      {"lives",
       [](Ctx const & c) -> char const * {
         return In(c.p1, {"he", "she", "it", "who", "that", "which", "everyone", "someone", "somebody", "nobody",
                          "one", "anyone", "everybody", "mom", "dad", "still", "only", "also"})
                    ? "livz"
                    : nullptr;
       }},
      {"read",
       [](Ctx const & c) -> char const * {
         return In(c.p1, {"have", "has", "had", "i've", "you've", "we've", "they've", "been", "was", "were", "already",
                          "being", "haven't", "hasn't", "hadn't", "i'd", "you'd", "we'd", "they'd", "well", "widely",
                          "once", "just"})
                    ? "red"
                    : nullptr;
       }},
      {"lead",
       [](Ctx const & c) -> char const * {
         if (In(c.n1, {"foot", "feet", "footed", "pipe", "pipes", "piping", "paint", "poisoning", "weight", "weights",
                       "balloon", "pencil", "pencils", "apron", "shot", "soldier", "soldiers", "crystal", "mine",
                       "mines", "miner", "miners", "ore", "sinker", "sinkers", "bullet", "bullets", "pellet",
                       "pellets", "slug", "slugs", "roof", "roofs", "roofing", "coffin", "coffins", "casket",
                       "acetate", "oxide", "glass", "lined", "based", "free", "framed"}))
           return "led";
         // "made of lead", "heavy as lead", "red lead", but not "heavy as lead singers go".
         if (!In(c.n1, {"the", "a", "an", "us", "them", "him", "her", "me", "you", "to", "singer", "singers", "role",
                       "roles", "story", "vocals", "vocalist", "guitar", "guitarist", "actor", "actress",
                       "character", "dog", "car"}) &&
             ((In(c.p1, {"of", "with", "in", "as", "to", "than"}) &&
               In(c.p2, {"made", "full", "lined", "filled", "weighted", "heavy", "cast", "coated", "plated", "turned",
                         "sheathed", "heavier"})) ||
              In(c.p1, {"red", "white"})))
           return "led";
         if (c.p1 == "the" && c.n1 == "out")  // "get the lead out"
           return "led";
         return nullptr;
       }},
      {"wind",
       [](Ctx const & c) -> char const * {
         if (Det(c.p1) || In(c.p1, {"wind", "cold", "strong", "high", "west", "east", "north", "south", "against"}))
           return nullptr;
         if (In(c.p1, {"to", "will", "can", "please", "gonna", "wanna", "i", "you", "we", "they", "don't", "let's",
                       "and", "should", "must"}) ||
             EndsWith(c.p1, "'ll"))
           return "wynd";
         if (c.p1.empty() && In(c.n1, {"down", "up", "it", "the", "your", "back"}))
           return "wynd";
         if (In(c.n1, {"down", "up"}))
           return "wynd";
         return nullptr;
       }},
      {"winds",
       [](Ctx const & c) -> char const * {
         return In(c.p1, {"road", "path", "river", "trail", "route", "it", "that", "which", "street", "highway",
                          "creek", "stream", "drive", "lane"})
                    ? "wyndz"
                    : nullptr;
       }},
      {"wound",
       [](Ctx const & c) -> char const * {
         if (In(c.n1, {"up", "down", "around", "through", "along", "tight", "back"}) &&
             !In(c.p1, {"a", "the", "my", "your", "open", "his", "her", "flesh", "head", "gunshot"}))
           return "wownd";
         return nullptr;
       }},
      {"close",
       [](Ctx const & c) -> char const * {
         // "so close", "that was close", "close to", "close enough": near (the default).
         static Set const nearBefore = {"so", "very", "too", "is", "was", "it's", "not", "getting", "how", "pretty",
                                        "really", "this", "that", "up", "quite", "almost", "come", "came", "stay",
                                        "keep", "get", "got", "be", "i'm", "we're", "you're", "they're", "are",
                                        "were", "a", "the", "real", "dangerously", "uncomfortably", "fairly",
                                        "as", "super", "way", "kind", "kinda", "sort"};
         static Set const nearAfter = {"to", "enough", "by", "call", "behind", "second", "shave", "friend",
                                       "friends", "one", "range", "quarters"};
         if (In(c.p1, nearBefore) || In(c.n1, nearAfter))
           return nullptr;
         if (In(c.n1, {"my", "your", "his", "her", "its", "their", "our", "it", "them", "this", "that", "the", "a",
                       "an", "eyes", "down", "out", "off", "shop", "up", "everything", "all"}))
           return "kloze";
         // Not "Am I close?": after "I" or "we" it takes an object, handled above.
         if (In(c.p1, {"to", "will", "can", "please", "don't", "gonna", "let's", "should", "must", "never"}) ||
             EndsWith(c.p1, "'ll"))
           return "kloze";
         return nullptr;
       }},
      {"excuse",
       [](Ctx const & c) -> char const * {
         return Det(c.p1) || In(c.p1, {"good", "bad", "great", "perfect", "poor", "lame", "best", "better", "real",
                                       "valid", "convenient", "terrible", "flimsy", "weak", "new", "same", "old"})
                    ? "ekskyoos"
                    : nullptr;
       }},
      {"record",
       [](Ctx const & c) -> char const * { return Verby(c.p1) || c.p1 == "and" ? "rikord" : nullptr; }},
      {"estimate",
       [](Ctx const & c) -> char const * {
         return Det(c.p1) || In(c.p1, {"rough", "best", "good", "bad", "first", "new", "latest", "original",
                                       "conservative", "generous", "optimistic", "honest"})
                    ? "estimit"
                    : nullptr;
       }},
      {"insult",
       [](Ctx const & c) -> char const * {
         if (Det(c.p1) || In(c.p1, {"an", "personal", "huge", "big", "real", "great", "final", "added"}))
           return nullptr;
         return Verby(c.p1) || c.p1 == "how" || Obj(c.n1) ? "insullt" : nullptr;
       }},
      {"subject",
       [](Ctx const & c) -> char const * {
         if (Det(c.p1))
           return nullptr;
         return Obj(c.n1) || Verby(c.p1) ? "subjekt" : nullptr;
       }},
      {"suspect",
       [](Ctx const & c) -> char const * {
         return In(c.p1, {"i", "we", "they", "you", "i'd", "we'd", "don't", "didn't", "to", "also", "strongly"})
                    ? "suspekt"
                    : nullptr;
       }},
      {"conduct",
       [](Ctx const & c) -> char const * {
         if (Det(c.p1))
           return nullptr;
         return Verby(c.p1) || In(c.n1, {"the", "a", "your", "this", "yourself", "yourselves", "an"}) ? "kunduct"
                                                                                                     : nullptr;
       }},
      {"object",
       [](Ctx const & c) -> char const * {
         return In(c.p1, {"i", "we", "they", "you", "don't", "to", "will", "would", "didn't"}) || EndsWith(c.p1, "'ll")
                    ? "objekt"
                    : nullptr;
       }},
      {"desert",
       [](Ctx const & c) -> char const * {
         return !Det(c.p1) && In(c.n1, {"you", "me", "him", "her", "them", "us"}) ? "dezzert" : nullptr;
       }},
      {"permit",
       [](Ctx const & c) -> char const * {
         if (Det(c.p1))
           return nullptr;
         return Verby(c.p1) || In(c.n1, {"you", "me", "him", "her", "them", "us", "it", "anyone"}) ? "permitt"
                                                                                                 : nullptr;
       }},
      {"contract",
       [](Ctx const & c) -> char const * {
         return !Det(c.p1) && Verby(c.p1) && !In(c.p1, {"i", "we", "they", "you"}) ? "kontrakt" : nullptr;
       }},
      {"progress",
       [](Ctx const & c) -> char const * {
         return In(c.p1, {"to", "will", "can", "we", "they", "i", "you", "don't", "didn't", "never"}) ||
                        EndsWith(c.p1, "'ll")
                    ? "progres"
                    : nullptr;
       }},
      {"present",
       [](Ctx const & c) -> char const * {
         if (In(c.n1, {"day", "time", "moment", "tense", "company", "situation"}))
           return nullptr;
         return In(c.p1, {"to", "will", "can", "i", "we", "may", "please", "proudly", "now"}) || EndsWith(c.p1, "'ll")
                    ? "prezent"
                    : nullptr;
       }},
      {"bass",
       [](Ctx const & c) -> char const * {
         // The fish only when it's about fishing.
         if (In(c.n1, {"fishing", "fish", "boat", "pro", "lake", "lure", "lures", "tournament", "pond"}))
           return nullptr;
         if (In(c.p1, {"caught", "catch", "largemouth", "smallmouth", "striped", "sea"}))
           return nullptr;
         return "bayss";
       }},
      {"tear",
       [](Ctx const & c) -> char const * {
         if (Det(c.p1) || In(c.p1, {"single", "a"}))
           return nullptr;
         // "tear up" is crying as often as ripping: left alone.
         return In(c.n1, {"down", "off", "apart", "into", "through", "out", "it", "them", "the", "my", "your", "his",
                          "her", "our", "their", "open"})
                    ? "tair"
                    : nullptr;
       }},
      {"tears",
       [](Ctx const & c) -> char const * {
         return In(c.p1, {"he", "she", "it", "who", "that", "which"}) &&
                        In(c.n1, {"down", "off", "apart", "into", "through", "out", "it", "them", "the", "up",
                                  "open"})
                    ? "tairz"
                    : nullptr;
       }},
      {"minute",
       [](Ctx const & c) -> char const * {
         static Set const tiny = {
             "detail", "details", "amount", "amounts", "difference", "differences", "fraction", "fractions",
             "changes", "adjustment", "adjustments", "particle", "particles", "quantity", "quantities", "trace",
             "traces", "insect", "insects", "organism", "organisms", "creature", "creatures", "crack", "cracks",
             "hole", "holes", "droplet", "droplets", "grain", "grains", "speck", "specks", "portion", "portions",
             "variation", "variations", "examination", "inspection", "scrutiny", "crystal", "crystals", "fragment",
             "fragments", "fleck", "flecks", "bubble", "bubbles", "hair", "hairs", "cell", "cells", "spore",
             "spores", "scale", "scales", "precision"};
         // "minute particles", "minute and careful examination".
         if (In(c.n1, tiny) || (c.n1 == "and" && In(c.n3, tiny)))
           return "mynoot";
         // "so minute that nobody saw them", but not "a very minute later", "every minute or so".
         if (In(c.p1, {"so", "very", "too", "quite", "most", "extremely", "rather", "incredibly", "infinitely",
                       "exceedingly", "almost"}) &&
             !In(c.n1, {"hand", "later", "ago", "or", "before", "after", "of", "to"}) &&
             !(c.n1 == "and" && c.n2 == "a"))
           return "mynoot";
         return nullptr;
       }},
      {"bow", [](Ctx const & c) -> char const * { return Bow(c, "bough"); }},
      {"bows",
       [](Ctx const & c) -> char const * {
         if (auto const said = Bow(c, "boughs"))
           return said;
         // "He bows deeply." Not "bows to match" on a dress: only after someone.
         return BendsAfter(c) && In(c.p1, {"he", "she", "it", "who", "everyone", "and", "then", "always"}) ? "boughs"
                                                                                                         : nullptr;
       }},
      {"bowed",
       [](Ctx const & c) -> char const * {
         if (BendsAfter(c) || In(c.p1, {"head", "heads"}) || In(c.n1, {"head", "heads"}))
           return "boughed";
         // "He bowed." "and bowed, then left".
         return c.endsClause && In(c.p1, {"i", "he", "she", "they", "we", "you", "and"}) ? "boughed" : nullptr;
       }},
      {"bowing", [](Ctx const & c) -> char const * { return BendsAfter(c) ? "boughing" : nullptr; }},
      {"use",
       [](Ctx const & c) -> char const * {
         if (Obj(c.n1) || In(c.n1, {"a", "an", "to"}))
           return nullptr;
         return In(c.p1, {"no", "the", "any", "of", "much", "its", "good", "little", "a", "some", "what's", "whats",
                          "for", "in", "personal", "business", "commercial", "regular", "daily", "heavy"})
                    ? "yoos"
                    : nullptr;
       }},
      {"content",
       [](Ctx const & c) -> char const * {
         return In(c.p1, {"the", "your", "my", "our", "their", "this", "that", "its", "his", "her", "original", "new",
                          "sponsored", "more", "some", "any"})
                    ? "kontent"
                    : nullptr;
       }},
  };
  return rules;
}

bool IsAsciiLetter(char ch)
{
  return std::isalpha(static_cast<unsigned char>(ch)) != 0;
}

// ’ (U+2019) as UTF-8.
constexpr std::string_view kCurly = "\xE2\x80\x99";

bool CurlyAt(std::string const & s, size_t i)
{
  return s.compare(i, kCurly.size(), kCurly) == 0;
}

std::string Lower(std::string_view w)
{
  std::string out;
  for (size_t i = 0; i < w.size(); ++i)
  {
    if (w.compare(i, kCurly.size(), kCurly) == 0)
    {
      out += '\'';
      i += kCurly.size() - 1;
      continue;
    }
    out += static_cast<char>(std::tolower(static_cast<unsigned char>(w[i])));
  }
  return out;
}

// MARK: - Regex helpers

/// Every match of `re` in `s`, replaced by what `fn` returns (nullopt keeps the match as it was).
/// `fn` also gets where the match starts, for the lookbehinds std::regex doesn't have.
std::string Replace(std::string const & s, std::regex const & re,
                    std::function<std::optional<std::string>(std::smatch const &, size_t)> const & fn)
{
  std::string out;
  size_t last = 0;
  for (std::sregex_iterator it(s.begin(), s.end(), re), end; it != end; ++it)
  {
    auto const & m = *it;
    auto const pos = static_cast<size_t>(m.position(0));
    out.append(s, last, pos - last);
    auto const with = fn(m, pos);
    out += with ? *with : m.str(0);
    last = pos + static_cast<size_t>(m.length(0));
  }
  out.append(s, last, std::string::npos);
  return out;
}

std::string ReplaceAll(std::string s, std::string_view from, std::string_view to)
{
  for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
    s.replace(at, from.size(), to);
  return s;
}

std::string const kStreetWords[][2] = {
    {"St", "Street"},     {"Ave", "Avenue"},  {"Blvd", "Boulevard"}, {"Rd", "Road"},        {"Dr", "Drive"},
    {"Ln", "Lane"},       {"Ct", "Court"},    {"Pkwy", "Parkway"},   {"Hwy", "Highway"},    {"Pl", "Place"},
    {"Cir", "Circle"},    {"Ter", "Terrace"}, {"Trl", "Trail"},      {"Expy", "Expressway"}, {"Fwy", "Freeway"},
    {"Sq", "Square"},
};

std::string Street(std::string const & abbreviation)
{
  for (auto const & w : kStreetWords)
    if (w[0] == abbreviation)
      return w[1];
  return abbreviation;
}

std::string Compass(std::string const & c)
{
  static std::map<std::string, std::string> const words = {
      {"N", "North"},     {"S", "South"},     {"E", "East"},      {"W", "West"},
      {"NE", "Northeast"}, {"NW", "Northwest"}, {"SE", "Southeast"}, {"SW", "Southwest"}};
  auto const it = words.find(c);
  return it == words.end() ? c : it->second;
}

bool Space(char ch)
{
  return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

// MARK: - Titles in names

// Capitalised words that can sit right before "St"/"Dr"/"Ft" without being the street's own name
// ("Take St. Johns Rd", "Call Dr Smith"). Lowercase.
bool LeadInWord(std::string const & w)
{
  static std::set<std::string> const words = {
      "take", "turn", "continue", "merge", "exit", "keep", "stay", "head", "follow", "use", "visit",
      "call", "ask", "see", "meet", "find", "blame", "thank", "onto", "on", "to", "at", "via", "toward",
      "towards", "from", "into", "past", "near", "then", "and", "or", "by", "for", "in", "of", "the", "a", "off"};
  return words.count(w) > 0;
}

/// True when the word just before `pos` names a street, so "St"/"Dr" at `pos` is its type ("Main St.",
/// "5th St"): an ordinal, or a capitalised word that isn't a lead-in. Start of text, punctuation or a
/// lowercase word before it mean a title ("onto St. Johns Rd", "Go to St Louis").
bool StreetNameBefore(std::string const & s, size_t pos)
{
  if (pos < 2 || s[pos - 1] != ' ')
    return false;
  size_t const end = pos - 1;
  size_t start = end;
  while (start > 0 && (std::isalnum(static_cast<unsigned char>(s[start - 1])) || s[start - 1] == '\''))
    --start;
  if (start == end)
    return false;
  auto const first = static_cast<unsigned char>(s[start]);
  // "5th St" is a street; a bare house number before it is not ("3502 St. Johns Rd").
  if (std::isdigit(first))
    return !std::all_of(s.begin() + static_cast<std::ptrdiff_t>(start), s.begin() + static_cast<std::ptrdiff_t>(end),
                        [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; });
  return std::isupper(first) && !LeadInWord(Lower(std::string_view(s).substr(start, end - start)));
}

bool StreetWord(std::string const & w)
{
  for (auto const & sw : kStreetWords)
    if (w == sw[0] || w == sw[1])
      return true;
  return false;
}

/// "St. Johns Rd" is "Saint Johns Rd", "Dr Martin Luther King Jr Blvd" is "Doctor Martin Luther
/// King Junior Blvd", "Ft Worth" is "Fort Worth", "Hwy 290" is "Highway 290", "101 Fwy" is "101
/// Freeway". "Main St. Then left" and "Elm Dr." stay street types for the street pass.
std::string Titles(std::string s)
{
  static std::regex const title("\\b(St|Ste|Dr|Ft)\\.? (?=[A-Z][a-z])");
  s = Replace(s, title, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (StreetNameBefore(s, pos))
      return std::nullopt;
    static std::map<std::string, std::string> const words = {
        {"St", "Saint"}, {"Ste", "Saint"}, {"Dr", "Doctor"}, {"Ft", "Fort"}};
    return words.at(m.str(1)) + " ";
  });
  // Jr and Sr after a name. The period goes when the sentence carries on ("King Jr. Blvd",
  // "Jr. was"), and stays when it may end the sentence ("Sammy Davis Jr.").
  static std::regex const suffix("(,? )(Jr|Sr)(\\.?)(?=$|[\\s,;:!?)])");
  s = Replace(s, suffix, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (pos == 0 || !IsAsciiLetter(s[pos - 1]))
      return std::nullopt;
    size_t start = pos;
    while (start > 0 && IsAsciiLetter(s[start - 1]))
      --start;
    if (!std::isupper(static_cast<unsigned char>(s[start])))
      return std::nullopt;
    std::string out = m.str(1) + (m.str(2) == "Jr" ? "Junior" : "Senior");
    if (m[3].length() > 0)
    {
      size_t next = pos + static_cast<size_t>(m.length(0));
      while (next < s.size() && Space(s[next]))
        ++next;
      size_t wordEnd = next;
      while (wordEnd < s.size() && IsAsciiLetter(s[wordEnd]))
        ++wordEnd;
      auto const word = s.substr(next, wordEnd - next);
      bool const carriesOn = !word.empty() && (std::islower(static_cast<unsigned char>(word[0])) || StreetWord(word));
      if (!carriesOn)
        out += ".";
    }
    return out;
  });
  // Road types next to a route number.
  static std::regex const roadBefore("\\b(Hwy|Fwy|Expy|Pkwy|Rte)\\.? (?=\\d)");
  s = Replace(s, roadBefore, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    return (m.str(1) == "Rte" ? std::string("Route") : Street(m.str(1))) + " ";
  });
  static std::regex const roadAfter("\\b(\\d+[A-Z]?) (Hwy|Fwy|Expy|Pkwy)\\b");
  s = Replace(s, roadAfter, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    return m.str(1) + " " + Street(m.str(2));
  });
  return s;
}

std::string Ordinal(long long n);

/// I..XXXIX as a number, or 0 when it isn't a well-formed numeral ("IIII", "VX", "IC").
int RomanValue(std::string const & r)
{
  static char const * const ones[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"};
  static char const * const tens[] = {"", "X", "XX", "XXX"};
  for (int n = 1; n < 40; ++n)
    if (r == std::string(tens[n / 10]) + ones[n % 10])
      return n;
  return 0;
}

/// Given names that carry a regnal number on streets and squares abroad and at home. Any other
/// capitalised name counts too when a title (King, Pope, ...) comes right before it.
bool RegnalName(std::string const & lower)
{
  static std::set<std::string> const names = {
      "abdullah", "adrian", "albert", "alexander", "alfonso", "amadeus", "anne", "baudouin", "benedict",
      "boniface", "carlo", "carlos", "catherine", "charles", "christian", "clement", "constantine",
      "david", "edward", "elizabeth", "emmanuel", "felipe", "ferdinand", "fernando", "francis",
      "franz", "frederick", "friedrich", "george", "gregory", "gustav", "gustavo", "haakon", "harald",
      "hassan", "henri", "henry", "innocent", "isabel", "isabella", "ivan", "jaime", "james", "jean",
      "joão", "john", "joseph", "juan", "julius", "kamehameha", "karl", "leo", "leopold", "louis",
      "ludwig", "manuel", "mary", "mohammed", "napoleon", "nicholas", "olav", "otto", "pablo",
      "paul", "pedro", "peter", "philip", "philippe", "pio", "pius", "rama", "ramesses", "richard",
      "robert", "rudolf", "sixtus", "stephen", "umberto", "urban", "victor", "wilhelm", "william"};
  return names.count(lower) > 0;
}

bool RegnalTitle(std::string const & lower)
{
  static std::set<std::string> const titles = {
      "king", "queen", "pope", "tsar", "czar", "emperor", "empress", "prince", "princess", "sultan",
      "kaiser", "shah", "pharaoh", "rey", "roi", "re", "rei", "könig", "papa", "san", "saint"};
  return titles.count(lower) > 0;
}

/// The word that ends just before `end` (a run of anything but spaces), and where it starts.
std::string WordEndingAt(std::string const & s, size_t end, size_t & start)
{
  start = end;
  while (start > 0 && !Space(s[start - 1]))
    --start;
  return s.substr(start, end - start);
}
}  // namespace

std::string Regnal(std::string const & text)
{
  static std::regex const numeral("\\b([IVX]{1,6})(?=$|[\\s,;:!?.)])");
  return Replace(text, numeral, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    auto const roman = m.str(1);
    int const n = RomanValue(roman);
    if (n == 0 || pos < 2 || text[pos - 1] != ' ')
      return std::nullopt;
    size_t nameStart = 0;
    auto const name = WordEndingAt(text, pos - 1, nameStart);
    if (name.empty() || !std::isupper(static_cast<unsigned char>(name[0])))
      return std::nullopt;
    auto const lowerName = Lower(name);
    bool named = RegnalName(lowerName);
    if (!named && nameStart >= 2 && text[nameStart - 1] == ' ')
    {
      size_t titleStart = 0;
      named = RegnalTitle(Lower(WordEndingAt(text, nameStart - 1, titleStart))) && !RegnalTitle(lowerName);
    }
    if (!named || (lowerName == "malcolm" && roman == "X"))
      return std::nullopt;
    size_t const after = pos + roman.size();
    // "John V. Smith Rd": one letter and a period before another name is a middle initial.
    if (roman.size() == 1 && after + 2 < text.size() && text[after] == '.' && text[after + 1] == ' ' &&
        std::isupper(static_cast<unsigned char>(text[after + 2])))
      return std::nullopt;
    // "Henry I think": a lone I before a lowercase word is the pronoun.
    if (roman == "I" && after + 1 < text.size() && text[after] == ' ' &&
        std::islower(static_cast<unsigned char>(text[after + 1])) && text.compare(after + 1, 3, "of ") != 0)
      return std::nullopt;
    return "the " + Ordinal(n);
  });
}

namespace
{
std::string Ordinal(long long n)
{
  auto const words = SpellNumber(n);
  auto const cut = words.find_last_of(' ');
  auto const head = cut == std::string::npos ? std::string() : words.substr(0, cut + 1);
  auto const last = cut == std::string::npos ? words : words.substr(cut + 1);
  static std::map<std::string, std::string> const irregular = {
      {"one", "first"}, {"two", "second"}, {"three", "third"}, {"five", "fifth"},
      {"eight", "eighth"}, {"nine", "ninth"}, {"twelve", "twelfth"}};
  if (auto const it = irregular.find(last); it != irregular.end())
    return head + it->second;
  if (!last.empty() && last.back() == 'y')
    return head + last.substr(0, last.size() - 1) + "ieth";
  return head + last + "th";
}

long long ToNumber(std::string const & digits)
{
  long long n = 0;
  for (char ch : digits)
    if (std::isdigit(static_cast<unsigned char>(ch)))
      n = n * 10 + (ch - '0');
  return n;
}

std::string Digits(std::string const & digits)
{
  std::string out;
  for (char ch : digits)
  {
    if (!std::isdigit(static_cast<unsigned char>(ch)))
      continue;
    if (!out.empty())
      out += ' ';
    out += SpellNumber(ch - '0');
  }
  return out;
}

// MARK: - Pauses

constexpr std::string_view kEllipsis = "\xE2\x80\xA6";

bool StartsWithAny(std::string const & s, size_t at, std::initializer_list<std::string_view> options, size_t & len)
{
  for (auto const o : options)
    if (s.compare(at, o.size(), o) == 0)
    {
      len = o.size();
      return true;
    }
  return false;
}

/// "…" is a pause, never a sound of its own: dropped at the start and before other punctuation, a
/// full stop at the end, a comma anywhere else (the same pause, measured on Brandon's phone).
std::string Ellipses(std::string s)
{
  static std::regex const dots(R"(\.(?:\s?\.){2,})");
  s = std::regex_replace(s, dots, std::string(kEllipsis));
  static std::initializer_list<std::string_view> const opening = {"(", "\xE2\x80\x9C", "\"", "\xE2\x80\x98", "'"};
  static std::initializer_list<std::string_view> const closing = {"\xE2\x80\x9D", "\"", "\xE2\x80\x99", "'", ")",
                                                                  "]"};
  for (size_t at = s.find(kEllipsis); at != std::string::npos; at = s.find(kEllipsis, at))
  {
    size_t end = at;
    while (s.compare(end, kEllipsis.size(), kEllipsis) == 0)
      end += kEllipsis.size();
    bool const atStart = std::all_of(s.begin(), s.begin() + static_cast<long>(at), Space);
    size_t len = 0;
    bool afterOpening = at > 0 && (Space(s[at - 1]) || [&] {
                          for (auto const o : opening)
                            if (at >= o.size() && s.compare(at - o.size(), o.size(), o) == 0)
                              return true;
                          return false;
                        }());
    std::string with;
    size_t cut = end;
    if (atStart || afterOpening)
    {
      while (cut < s.size() && Space(s[cut]))
        ++cut;
    }
    else
    {
      size_t i = end;
      while (i < s.size() && Space(s[i]))
        ++i;
      if (i < s.size() && std::string_view("?!.,;:").find(s[i]) != std::string_view::npos)
      {
        with = "";
      }
      else
      {
        size_t j = i;
        while (j < s.size() && (Space(s[j]) || StartsWithAny(s, j, closing, len)))
        {
          if (!Space(s[j]))
            j += len;
          else
            ++j;
        }
        with = j == s.size() ? "." : ",";
        // "wait…what": the comma needs a space after it.
        if (end < s.size() && !Space(s[end]) && IsAsciiLetter(s[end]))
          with += ' ';
      }
    }
    s.replace(at, cut - at, with);
    at += with.size();
  }
  return s;
}

// MARK: - The driver's own words

struct UserWord
{
  std::string word;   // as typed, lowercase for matching
  std::string said;   // what replaces it in the text: a stand-in or plain words
  std::string phonemes;  // the stand-in's sound; empty for plain words
};

std::mutex g_userMutex;
std::vector<UserWord> g_userWords;

std::string Trim(std::string s)
{
  while (!s.empty() && Space(s.front()))
    s.erase(s.begin());
  while (!s.empty() && Space(s.back()))
    s.pop_back();
  return s;
}

/// The stand-in for the driver's word number `i`: "qzva", "qzvb"... Letters only and never a real
/// word, so both phones' dictionaries can hold it.
std::string StandIn(size_t i)
{
  std::string tail;
  do
  {
    tail.insert(tail.begin(), static_cast<char>('a' + i % 26));
    i /= 26;
  } while (i > 0);
  return "qzv" + tail;
}

bool WordChar(char ch)
{
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '\'' || (static_cast<unsigned char>(ch) & 0x80);
}

/// Every whole-word, any-case occurrence of the driver's words, replaced.
std::string ApplyUserWords(std::string const & text)
{
  std::vector<UserWord> words;
  {
    std::lock_guard lock(g_userMutex);
    words = g_userWords;
  }
  if (words.empty())
    return text;
  // Longest first, so "Mt Hood" wins over "Hood".
  std::stable_sort(words.begin(), words.end(),
                   [](UserWord const & a, UserWord const & b) { return a.word.size() > b.word.size(); });
  std::string s = text;
  for (auto const & w : words)
  {
    std::string lower = Lower(s);
    // Lower() turns ’ into ', which changes lengths: match on a copy with ’ kept as is.
    if (lower.size() != s.size())
    {
      lower.clear();
      for (char ch : s)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    std::string out;
    size_t last = 0;
    for (size_t at = lower.find(w.word); at != std::string::npos; at = lower.find(w.word, at + 1))
    {
      size_t const end = at + w.word.size();
      if (at < last || (at > 0 && WordChar(s[at - 1]) && WordChar(w.word.front())) ||
          (end < s.size() && WordChar(s[end]) && WordChar(w.word.back())))
        continue;
      out.append(s, last, at - last);
      std::string said = w.said;
      if (!w.phonemes.empty() && std::isupper(static_cast<unsigned char>(s[at])))
        said[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(said[0])));
      out += said;
      last = end;
    }
    out.append(s, last, std::string::npos);
    s = out;
  }
  return s;
}

// MARK: - Ranges

// Words a hyphen between two numbers can only mean "to" after ("pages 3-5", "from 10-20") or
// before ("3-5 miles"). Anything else with a hyphen (phone numbers, scores, "the 9-5 grind")
// stays as written. Lowercase, no trailing period.
bool CountingWordBefore(std::string const & w)
{
  static std::array<std::string_view, 22> const words = {
      "page", "pages", "pp", "p", "age", "ages", "aged", "verse", "verses", "chapter", "chapters",
      "from", "between", "year", "years", "exit", "exits", "lane", "lanes", "mile", "miles", "step"};
  return std::find(words.begin(), words.end(), w) != words.end();
}

bool CountingWordAfter(std::string const & w)
{
  static std::array<std::string_view, 24> const words = {
      "mi", "mile", "miles", "ft", "feet", "km", "kilometers", "minute", "minutes", "min", "hour", "hours",
      "hrs", "day", "days", "mph", "blocks", "exits", "lanes", "stops", "seconds", "weeks", "years", "times"};
  return std::find(words.begin(), words.end(), w) != words.end();
}

bool Digit(char ch)
{
  return std::isdigit(static_cast<unsigned char>(ch)) != 0;
}

// The lowercase word that ends just before `pos` (skipping spaces and one trailing period), and
// where it starts.
// Words before a score: "lost 2–3" is a score, not a range.
bool ScoreWordBefore(std::string const & w)
{
  static std::array<std::string_view, 9> const words = {"won", "lost", "beat", "drew", "tied", "score",
                                                         "scored", "trailing", "leading"};
  return std::find(words.begin(), words.end(), w) != words.end();
}

std::string WordBefore(std::string const & s, size_t pos, size_t & start)
{
  size_t end = pos;
  while (end > 0 && s[end - 1] == ' ')
    --end;
  if (end > 0 && s[end - 1] == '.')
    --end;
  start = end;
  while (start > 0 && IsAsciiLetter(s[start - 1]))
    --start;
  return Lower(std::string_view(s).substr(start, end - start));
}

std::string WordAfter(std::string const & s, size_t pos)
{
  if (pos < s.size() && s[pos] == '%')
    return "percent";
  while (pos < s.size() && s[pos] == ' ')
    ++pos;
  size_t end = pos;
  while (end < s.size() && IsAsciiLetter(s[end]))
    ++end;
  return Lower(std::string_view(s).substr(pos, end - pos));
}

double Value(std::string const & number)
{
  std::string plain;
  for (char ch : number)
    if (ch != ',')
      plain += ch;
  return std::stod(plain);
}

std::string FractionWords(std::string const & a, std::string const & b)
{
  if (a == "1" && b == "2")
    return "a half";
  if (a == "1" && b == "4")
    return "a quarter";
  if (a == "3" && b == "4")
    return "three quarters";
  return a + "/" + b;
}
}  // namespace

void SetUserWords(std::string const & lines)
{
  std::vector<UserWord> words;
  size_t start = 0;
  while (start <= lines.size())
  {
    auto end = lines.find('\n', start);
    if (end == std::string::npos)
      end = lines.size();
    auto const line = lines.substr(start, end - start);
    start = end + 1;
    auto const tab = line.find('\t');
    if (tab == std::string::npos)
      continue;
    auto const word = Lower(Trim(line.substr(0, tab)));
    auto said = Trim(line.substr(tab + 1));
    if (word.empty() || said.empty())
      continue;
    UserWord w{word, said, ""};
    if (said.size() > 2 && said.front() == '/' && said.back() == '/')
      w.phonemes = said.substr(1, said.size() - 2);
    else if (auto const ph = RespellingPhonemes(said))
      w.phonemes = *ph;
    else if (std::none_of(said.begin(), said.end(), [](char c) { return std::islower(static_cast<unsigned char>(c)); }))
      w.said = Lower(said);  // "KWAH" alone would be spelled out letter by letter
    if (!w.phonemes.empty())
      w.said = StandIn(words.size());
    words.push_back(std::move(w));
  }
  std::lock_guard lock(g_userMutex);
  g_userWords = std::move(words);
}

// MARK: - Public

std::string SpellNumber(long long n)
{
  static char const * const ones[] = {"zero",    "one",     "two",       "three",    "four",     "five",    "six",
                                      "seven",   "eight",   "nine",      "ten",      "eleven",   "twelve",  "thirteen",
                                      "fourteen", "fifteen", "sixteen",  "seventeen", "eighteen", "nineteen"};
  static char const * const tens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
  if (n == std::numeric_limits<long long>::min())
    return std::to_string(n);  // -n overflows; nothing real is this big
  if (n < 0)
    return "minus " + SpellNumber(-n);
  if (n < 20)
    return ones[n];
  if (n < 100)
    return std::string(tens[n / 10]) + (n % 10 ? std::string(" ") + ones[n % 10] : "");
  if (n < 1000)
    return std::string(ones[n / 100]) + " hundred" + (n % 100 ? " " + SpellNumber(n % 100) : "");
  static std::pair<long long, char const *> const scales[] = {
      {1000000000000LL, "trillion"}, {1000000000LL, "billion"}, {1000000LL, "million"}, {1000LL, "thousand"}};
  for (auto const & [size, name] : scales)
    if (n >= size)
      return SpellNumber(n / size) + " " + name + (n % size ? " " + SpellNumber(n % size) : "");
  return std::to_string(n);
}

std::string PickSenses(std::string const & text)
{
  struct Token
  {
    size_t start, len;
    std::string lower;
    bool breakBefore;
  };
  std::vector<Token> words;
  bool pendingBreak = true;
  size_t i = 0;
  while (i < text.size())
  {
    if (!IsAsciiLetter(text[i]))
    {
      if (text[i] == '.' || text[i] == '!' || text[i] == '?' || text[i] == ':' || text[i] == ';')
        pendingBreak = true;
      ++i;
      continue;
    }
    size_t j = i;
    while (j < text.size())
    {
      if (IsAsciiLetter(text[j]))
        ++j;
      else if (text[j] == '\'' && j + 1 < text.size() && IsAsciiLetter(text[j + 1]))
        ++j;
      else if (CurlyAt(text, j) && j + kCurly.size() < text.size() && IsAsciiLetter(text[j + kCurly.size()]))
        j += kCurly.size();
      else
        break;
    }
    words.push_back({i, j - i, Lower(std::string_view(text).substr(i, j - i)), pendingBreak});
    pendingBreak = false;
    i = j;
  }

  auto const & rules = Rules();
  // A word the driver gave their own sound is theirs, whatever the sentence says.
  std::vector<std::string> theirs;
  {
    std::lock_guard lock(g_userMutex);
    for (auto const & w : g_userWords)
      theirs.push_back(w.word);
    for (auto const & w : g_userWords)
      theirs.push_back(Lower(w.said));
  }
  std::string out;
  size_t last = 0;
  for (size_t k = 0; k < words.size(); ++k)
  {
    auto const rule = rules.find(words[k].lower);
    if (rule == rules.end() || std::find(theirs.begin(), theirs.end(), words[k].lower) != theirs.end())
      continue;
    Ctx c;
    if (!words[k].breakBefore && k >= 1)
    {
      c.p1 = words[k - 1].lower;
      if (!words[k - 1].breakBefore && k >= 2)
        c.p2 = words[k - 2].lower;
    }
    if (k + 1 < words.size() && !words[k + 1].breakBefore)
    {
      c.n1 = words[k + 1].lower;
      if (k + 2 < words.size() && !words[k + 2].breakBefore)
      {
        c.n2 = words[k + 2].lower;
        if (k + 3 < words.size() && !words[k + 3].breakBefore)
          c.n3 = words[k + 3].lower;
      }
    }
    {
      size_t at = words[k].start + words[k].len;
      while (at < text.size() && Space(text[at]))
        ++at;
      c.endsClause = at >= text.size() || std::string_view(",.;:!?)").find(text[at]) != std::string_view::npos ||
                     text.compare(at, 3, "\xE2\x80\x94") == 0 || text.compare(at, 3, "\xE2\x80\x93") == 0;
    }
    char const * token = rule->second(c);
    if (token == nullptr)
      continue;
    std::string said = token;
    if (std::isupper(static_cast<unsigned char>(text[words[k].start])))
      said[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(said[0])));
    out.append(text, last, words[k].start - last);
    out += said;
    last = words[k].start + words[k].len;
  }
  out.append(text, last, std::string::npos);
  return out;
}

std::string Ranges(std::string const & text)
{
  // A number, a hyphen, en dash or unspaced em dash, a number; "3-1/2" too.
  static std::regex const range(
      "(\\d{1,3}(?:,\\d{3})+(?:\\.\\d+)?|\\d+(?:\\.\\d+)?)(-|\xE2\x80\x93|\xE2\x80\x94)"
      "(\\d{1,3}(?:,\\d{3})+(?:\\.\\d+)?|\\d+(?:\\.\\d+)?)(?:/(\\d+))?");
  auto const & s = text;
  std::string out;
  size_t last = 0;
  for (std::sregex_iterator it(s.begin(), s.end(), range), end; it != end; ++it)
  {
    auto const & m = *it;
    auto const pos = static_cast<size_t>(m.position(0));
    auto const after = pos + static_cast<size_t>(m.length(0));
    std::string prefix = s.substr(last, pos - last);
    std::optional<std::string> said;
    // Part of something longer: "I-35-40", "-5", "800-555-1234", "2024-01-15", "66-A", "3-5x".
    bool const glued =
        (pos > 0 && (IsAsciiLetter(s[pos - 1]) || std::string_view("-./,").find(s[pos - 1]) != std::string_view::npos)) ||
        (after < s.size() && (IsAsciiLetter(s[after]) || Digit(s[after]) ||
                              std::string_view("-/:").find(s[after]) != std::string_view::npos)) ||
        s.compare(after, 3, "\xE2\x80\x93") == 0 || s.compare(after, 3, "\xE2\x80\x94") == 0;
    bool const hyphen = m.str(2) == "-";
    if (!glued && m[4].matched)
    {
      // "a 3-1/2 inch nail": three and a half.
      if (hyphen)
        said = m.str(1) + " and " + FractionWords(m.str(3), m.str(4));
    }
    else if (!glued)
    {
      auto const a = m.str(1);
      auto b = m.str(3);
      bool const yearA = a.size() == 4 && a[0] >= '1' && a[0] <= '2';
      // "1914–18", "1905-06": the end year keeps the start's century.
      if (yearA && b.size() == 2)
        b = std::to_string(ToNumber(a) / 100 * 100 + ToNumber(b));
      bool const yearB = b.size() == 4 && b[0] >= '1' && b[0] <= '2';
      size_t wordStart = 0;
      auto const before = WordBefore(s, pos, wordStart);
      bool const counting = CountingWordBefore(before) || CountingWordAfter(WordAfter(s, after));
      // Scores and "the 9-5 grind" count down; ranges go up.
      if (Value(b) > Value(a) && !ScoreWordBefore(before) && (!hyphen || (yearA && yearB) || counting))
      {
        said = a + (before == "between" ? " and " : " to ") + b;
        if ((before == "pp" || before == "p") && wordStart >= last)
          prefix = s.substr(last, wordStart - last) + (before == "pp" ? "pages " : "page ");
      }
    }
    out += prefix;
    out += said ? *said : m.str(0);
    last = after;
  }
  out.append(s, last, std::string::npos);
  return out;
}

// MARK: - Foreign phrases

namespace
{
struct Phrase
{
  char const * text;
  /// One reader-style respelling per word, stressed syllable in capitals.
  char const * said;
};

// Phrases the lines quote in another language ("Gire a la izquierda means 'turn left' in Spanish",
// the template in gps-comments.md). Kokoro's English frontend reads them as English ("jire a la
// iz-kwer-da", and nothing at all for Japanese), so ForSpeech swaps each word for a stand-in
// ("fx" + the respelling's letters) that the dictionary says the way the respelling does.
// Latin-script phrases match ignoring case on word boundaries; other scripts match as written.
constexpr Phrase kPhrases[] = {
    // "turn left"
    {"Gire a la izquierda", "HEE-reh ah lah ees-kee-AIR-dah"},
    {"Tournez à gauche", "toor-NAY ah GOHSH"},
    {"Biegen Sie links ab", "BEE-gen zee LINKS ahp"},
    {"Gira a sinistra", "JEE-rah ah see-NEES-trah"},
    {"Vire à esquerda", "VEE-ree ah es-KEHR-dah"},
    {"Sla linksaf", "slah LINKS-ahf"},
    {"Sväng vänster", "svehng VEN-ster"},
    {"Skręć w lewo", "skrench v LEH-vo"},
    {"Поверните налево", "pah-vyer-NEE-tyeh nah-LYEH-vah"},
    {"左に曲がってください", "hee-DAH-ree nee mah-GAHT-teh koo-dah-SIGH"},
    {"좌회전하세요", "jwah-HWEH-jun hah-SEH-yo"},
    {"向左转", "SHYAHNG DZWOH JWAHN"},
    {"Käänny vasemmalle", "KAHN-noo VAH-sem-mahl-leh"},
    {"Verte ad sinistram", "WER-teh ahd sin-IS-trahm"},
    // "turn right", "recalculating", "I told you so"
    {"Gire a la derecha", "HEE-reh ah lah deh-REH-chah"},
    {"Tournez à droite", "toor-NAY ah DRWAHT"},
    {"Recalculando", "reh-kahl-koo-LAHN-doh"},
    {"Recalcul en cours", "reh-kahl-KOOL ahn KOOR"},
    {"Te lo dije", "teh loh DEE-heh"},
    // "learn to drive"
    {"Aprende a conducir", "ah-PREN-deh ah kohn-doo-SEER"},
    {"Apprends à conduire", "ah-PRAHN ah kohn-DWEER"},
    {"Lern Auto fahren", "LAIRN OW-toh FAH-ren"},
    {"Impara a guidare", "eem-PAH-rah ah gwee-DAH-reh"},
    {"Aprenda a dirigir", "ah-PREN-dah ah jee-ree-ZHEER"},
    {"運転を習ってください", "OON-tehn oh nah-RAHT-teh koo-dah-SIGH"},
    // "I am an excellent driver"
    {"Soy un conductor excelente", "SOY oon kohn-dook-TOR ehk-seh-LEN-teh"},
    {"Je suis un excellent conducteur", "zhuh SWEE uhn ehk-seh-LAHN kohn-dook-TUR"},
    {"Ich bin ein ausgezeichneter Fahrer", "ISH BIN EYEN OWS-geh-tsighsh-neh-ter FAH-rer"},
    {"Sono un ottimo guidatore", "SOH-noh oon OHT-tee-moh gwee-dah-TOH-reh"},
    {"私は運転がとても上手です", "wah-TAH-shee wah OON-tehn gah toh-TEH-moh JOH-zoo dess"},
};

std::string AsciiLower(std::string s)
{
  for (char & c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool WordByte(std::string const & s, size_t i)
{
  auto const c = static_cast<unsigned char>(s[i]);
  return std::isalnum(c) || c >= 0x80;
}

std::vector<std::string> SplitWords(std::string const & said)
{
  std::vector<std::string> out;
  std::istringstream in(said);
  for (std::string w; in >> w;)
    out.push_back(w);
  return out;
}

std::string StandIn(std::string const & respelling)
{
  std::string out = "fx";
  for (char c : respelling)
    if (IsAsciiLetter(c))
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

/// A respelled word as phonemes. A one-syllable word has no capitals to mark its stress, so it
/// is stressed when written in capitals ("GOHSH") and unstressed when not ("ah").
std::optional<std::string> ForeignWordPhonemes(std::string const & respelling)
{
  if (respelling.find('-') != std::string::npos)
    return RespellingPhonemes(respelling);
  auto ph = RespellingPhonemes("\xCB\x88" + respelling);
  if (ph && respelling != AsciiLower(respelling))
    return ph;
  if (ph)
    *ph = ReplaceAll(*ph, "\xCB\x88", "");
  return ph;
}
}  // namespace

std::vector<std::pair<std::string, std::string>> const & ForeignPhrases()
{
  static std::vector<std::pair<std::string, std::string>> const phrases = [] {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto const & p : kPhrases)
    {
      std::string standIns;
      for (auto const & w : SplitWords(p.said))
        standIns += (standIns.empty() ? "" : " ") + StandIn(w);
      out.emplace_back(p.text, standIns);
    }
    return out;
  }();
  return phrases;
}

std::string Foreign(std::string const & text)
{
  std::string s = text;
  for (auto const & [phrase, standIns] : ForeignPhrases())
  {
    bool const latin = IsAsciiLetter(phrase[0]);
    std::string const needle = latin ? AsciiLower(phrase) : phrase;
    std::string out;
    std::string const hay = latin ? AsciiLower(s) : s;
    size_t last = 0;
    for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
    {
      size_t const end = at + needle.size();
      if (at < last || (latin && ((at > 0 && WordByte(s, at - 1)) || (end < s.size() && WordByte(s, end)))))
        continue;
      out.append(s, last, at - last);
      out += standIns;
      last = end;
    }
    out.append(s, last, std::string::npos);
    s = out;
  }
  return s;
}

// MARK: - Street names abroad

namespace
{
struct ForeignWord
{
  char const * word;
  /// Reader-style respelling, stressed syllable in capitals (see kPhrases).
  char const * said;
  /// The languages whose particles may follow ("es pt"); the first picks how "de" sounds.
  char const * langs;
};

// Street-type words map data carries abroad (and in Miami, LA, Quebec) that English Kokoro reads
// as English: "Calle Ocho" is "call Ocho", "Straße" loses its ß. Avenue, Boulevard, Plaza and Place
// read fine in English and are left; Rue does too, but its "de"/"du" don't.
constexpr ForeignWord kStreetTypes[] = {
    {"Calle", "KAH-yeh", "es"},          {"Avenida", "ah-veh-NEE-dah", "es pt"},
    {"Paseo", "pah-SEH-oh", "es"},       {"Calzada", "kahl-SAH-dah", "es"},
    {"Carrera", "kah-REH-rah", "es"},    {"Carretera", "kah-reh-TEH-rah", "es"},
    {"Callejón", "kah-yeh-HOHN", "es"},  {"Glorieta", "gloh-RYEH-tah", "es"},
    {"Rue", "ROO", "fr"},                {"Chemin", "sheh-MAN", "fr"},
    {"Allée", "ah-LAY", "fr"},           {"Quai", "KAY", "fr"},
    {"Via", "VEE-ah", "it"},             {"Viale", "vee-AH-leh", "it"},
    {"Piazza", "pee-AHT-sah", "it"},     {"Vicolo", "VEE-koh-loh", "it"},
    {"Rua", "HOO-ah", "pt"},             {"Travessa", "trah-VEH-sah", "pt"},
    {"Praça", "PRAH-sah", "pt"},         {"Straße", "SHTRAH-seh", "de"},
    {"Strasse", "SHTRAH-seh", "de"},     {"Gasse", "GAH-seh", "de"},
    {"Platz", "PLAHTS", "de"},
};

// Little words inside those names: "Paseo de la Reforma", "Rue du Bac", "Via della Spiga".
constexpr ForeignWord kStreetParticles[] = {
    {"de", "deh", "es"},   {"del", "dehl", "es"},   {"la", "lah", "es"},     {"las", "lahs", "es"},
    {"los", "lohs", "es"}, {"de", "duh", "fr"},     {"du", "dew", "fr"},     {"des", "day", "fr"},
    {"la", "lah", "fr"},   {"le", "luh", "fr"},     {"di", "dee", "it"},     {"del", "dehl", "it"},
    {"della", "DEL-lah", "it"}, {"dei", "day", "it"}, {"delle", "DEL-leh", "it"},
    {"degli", "DEL-yee", "it"}, {"de", "deh", "pt"}, {"do", "doo", "pt"},    {"da", "dah", "pt"},
    {"dos", "doos", "pt"}, {"das", "dahs", "pt"},
};

bool InLangs(std::string_view langs, std::string_view lang)
{
  for (size_t at = langs.find(lang); at != std::string_view::npos; at = langs.find(lang, at + 1))
    if ((at == 0 || langs[at - 1] == ' ') && (at + lang.size() == langs.size() || langs[at + lang.size()] == ' '))
      return true;
  return false;
}

/// The particle's respelling in the street word's languages, the first language winning.
char const * StreetParticle(std::string const & word, std::string_view langs)
{
  std::string const w = AsciiLower(word);
  if (w != word && !(std::isupper(static_cast<unsigned char>(word[0])) && AsciiLower(word.substr(1)) == word.substr(1)))
    return nullptr;
  std::istringstream in{std::string(langs)};
  for (std::string lang; in >> lang;)
    for (auto const & p : kStreetParticles)
      if (w == p.word && lang == p.langs)
        return p.said;
  return nullptr;
}

/// A word that can carry a street name on: capitalised, a number, or non-ASCII ("Élysées").
bool NameWord(std::string const & w)
{
  auto const c = static_cast<unsigned char>(w[0]);
  return std::isupper(c) || std::isdigit(c) || c >= 0x80;
}
}  // namespace

std::string ForeignStreets(std::string const & text)
{
  struct Span
  {
    size_t at, end;
  };
  std::vector<Span> words;
  for (size_t i = 0; i < text.size();)
  {
    if (!WordByte(text, i))
    {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < text.size() && WordByte(text, j))
      ++j;
    words.push_back({i, j});
    i = j;
  }
  auto const word = [&](size_t k) { return text.substr(words[k].at, words[k].end - words[k].at); };
  // The next word is on the same name: only spaces between, or an elided "d'"/"l'" right after.
  auto const spaced = [&](size_t k) {
    for (size_t i = words[k].end; i < words[k + 1].at; ++i)
      if (text[i] != ' ')
        return false;
    return true;
  };
  auto const elided = [&](size_t k) {
    auto const w = AsciiLower(word(k));
    return (w == "d" || w == "l") && words[k].end < text.size() &&
           (text[words[k].end] == '\'' || text.compare(words[k].end, 3, "\xE2\x80\x99") == 0);
  };

  std::vector<std::pair<Span, std::string>> swaps;
  for (size_t k = 0; k < words.size(); ++k)
  {
    std::string const w = word(k);
    // "Hauptstraße", "Bahnhofstrasse": the type is glued on.
    for (std::string_view tail : {"stra\xC3\x9F" "e", "strasse"})
      if (w.size() > tail.size() + 1 && std::isupper(static_cast<unsigned char>(w[0])) &&
          AsciiLower(w.substr(w.size() - tail.size())) == tail)
        swaps.push_back({{words[k].at + w.size() - tail.size(), words[k].end}, " " + StandIn(std::string("SHTRAH-seh"))});
    ForeignWord const * type = nullptr;
    for (auto const & t : kStreetTypes)
      if (w == t.word)
        type = &t;
    if (!type)
      continue;
    bool const atEnd = k + 1 == words.size() || !spaced(k);
    auto const carriesOn = [&](size_t n) {
      return NameWord(word(n)) || StreetParticle(word(n), type->langs) || elided(n);
    };
    // German types follow the name: "Potsdamer Platz", "Lange Gasse".
    bool const after = InLangs(type->langs, "de") && k > 0 && spaced(k - 1) && NameWord(word(k - 1));
    if (!after && (atEnd || !carriesOn(k + 1)))
      continue;
    std::string standIns;
    for (auto const & part : SplitWords(type->said))
      standIns += (standIns.empty() ? "" : " ") + StandIn(part);
    swaps.push_back({words[k], standIns});
    for (size_t n = k + 1; !atEnd && n + 1 < words.size() && spaced(n); ++n)
    {
      auto const said = StreetParticle(word(n), type->langs);
      if (!said || !carriesOn(n + 1))
        break;
      swaps.push_back({words[n], StandIn(std::string(said))});
      k = n;
    }
  }
  if (swaps.empty())
    return text;
  std::string out;
  size_t last = 0;
  for (auto const & [span, said] : swaps)
  {
    out.append(text, last, span.at - last);
    out += said;
    last = span.end;
  }
  out.append(text, last, std::string::npos);
  return out;
}

std::vector<std::string> ForeignStreetRespellings()
{
  std::vector<std::string> out;
  for (auto const & t : kStreetTypes)
    for (auto const & part : SplitWords(t.said))
      out.push_back(part);
  for (auto const & p : kStreetParticles)
    out.push_back(p.said);
  return out;
}

// MARK: - Cyrillic

namespace
{
std::u32string Decode(std::string const & s);
std::string Encode(std::u32string const & s);
bool Letter(char32_t c);

bool CyrillicLetter(char32_t c)
{
  return c >= U'Ѐ' && c <= U'ӿ';
}

char32_t CyrillicLower(char32_t c)
{
  if (c >= U'А' && c <= U'Я')
    return c + 0x20;
  if (c >= U'Ѐ' && c <= U'Џ')
    return c + 0x50;
  if (c >= U'Ѡ' && c <= U'ӿ' && (c % 2) == 0)
    return c + 1;
  return c;
}

/// Reader-style romanisation, the same table as BookLyre's text/spoken_foreign_scripts.py (Russian,
/// Ukrainian, Belarusian), plus the Serbian and Macedonian letters map names use.
char const * CyrillicRoman(char32_t lower)
{
  switch (lower)
  {
  case U'а': return "a";   case U'б': return "b";  case U'в': return "v";  case U'г': return "g";
  case U'д': return "d";   case U'е': return "e";  case U'ё': return "yo"; case U'ж': return "zh";
  case U'з': return "z";   case U'и': return "i";  case U'й': return "y";  case U'к': return "k";
  case U'л': return "l";   case U'м': return "m";  case U'н': return "n";  case U'о': return "o";
  case U'п': return "p";   case U'р': return "r";  case U'с': return "s";  case U'т': return "t";
  case U'у': return "u";   case U'ф': return "f";  case U'х': return "kh"; case U'ц': return "ts";
  case U'ч': return "ch";  case U'ш': return "sh"; case U'щ': return "shch";
  case U'ы': return "y";   case U'э': return "e";  case U'ю': return "yu"; case U'я': return "ya";
  case U'і': return "i";   case U'ї': return "yi"; case U'є': return "ye"; case U'ґ': return "g";
  case U'ў': return "w";
  case U'ј': return "y";   case U'љ': return "ly"; case U'њ': return "ny"; case U'ћ': return "ch";
  case U'ђ': return "j";   case U'џ': return "j";  case U'ѓ': return "gy"; case U'ќ': return "ky";
  case U'ѕ': return "dz";
  default: return "";
  }
}

bool CyrillicVowel(char32_t lower)
{
  return std::u32string_view(U"аеёиоуыэюяіїє").find(lower) != std::u32string_view::npos;
}

std::string CyrillicWord(std::u32string const & word)
{
  std::string out;
  bool upper = true;
  char32_t prev = 0;
  for (char32_t c : word)
  {
    char32_t const low = CyrillicLower(c);
    upper = upper && low != c;
    // "е" starting a word or after a vowel or sign is "ye": Елена Yelena, моё moyo.
    if (low == U'е' && (!prev || CyrillicVowel(prev) || prev == U'ъ' || prev == U'ь'))
      out += "ye";
    else
      out += CyrillicRoman(low);
    prev = low;
  }
  if (out.empty())
    return out;
  // A vowelless acronym in capitals is spelled ("СССР" is "S, S, S, R"); a shouted word reads as a word.
  if (upper && word.size() > 1 && out.find_first_of("aeiouy") == std::string::npos)
  {
    std::string spelled;
    for (char c : out)
      spelled += std::string(spelled.empty() ? "" : "-") + static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return spelled;
  }
  if (CyrillicLower(word[0]) != word[0])
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}
}  // namespace

std::string Cyrillic(std::string const & text)
{
  std::u32string const in = Decode(text);
  bool any = false;
  for (char32_t c : in)
    any = any || CyrillicLetter(c);
  if (!any)
    return text;
  std::u32string out;
  for (size_t i = 0; i < in.size();)
  {
    if (!CyrillicLetter(in[i]))
    {
      out += in[i++];
      continue;
    }
    size_t end = i;
    std::u32string word;
    for (; end < in.size() && (CyrillicLetter(in[end]) || in[end] == U'́'); ++end)
      if (in[end] != U'́')
        word += in[end];
    // A token that mixes Latin and Cyrillic letters ("Мoscow") is a look-alike spelling: leave it.
    bool const mixed = (i > 0 && Letter(in[i - 1])) || (end < in.size() && Letter(in[end]));
    if (mixed)
      out.append(in, i, end - i);
    else
      out += Decode(CyrillicWord(word));
    i = end;
  }
  return Encode(out);
}

// MARK: - Kana

namespace
{
bool KanaLetter(char32_t c)
{
  return (c >= U'ぁ' && c <= U'ゖ') || (c >= U'ァ' && c <= U'ヺ') || c == U'ー';
}

bool Ideograph(char32_t c)
{
  return (c >= U'㐀' && c <= U'鿿') || (c >= U'豈' && c <= U'﫿');
}

/// Katakana is hiragana + 0x60; fold it so one table covers both.
char32_t Hiragana(char32_t c)
{
  return (c >= U'ァ' && c <= U'ヶ') ? c - 0x60 : c;
}

/// Hepburn for one kana, the same table as BookLyre's text/spoken_foreign_scripts.py.
std::string KanaRoman(char32_t c)
{
  static std::map<char32_t, char const *> const table = {
      {U'あ', "a"},  {U'い', "i"},   {U'う', "u"},   {U'え', "e"},  {U'お', "o"},
      {U'か', "ka"}, {U'き', "ki"},  {U'く', "ku"},  {U'け', "ke"}, {U'こ', "ko"},
      {U'が', "ga"}, {U'ぎ', "gi"},  {U'ぐ', "gu"},  {U'げ', "ge"}, {U'ご', "go"},
      {U'さ', "sa"}, {U'し', "shi"}, {U'す', "su"},  {U'せ', "se"}, {U'そ', "so"},
      {U'ざ', "za"}, {U'じ', "ji"},  {U'ず', "zu"},  {U'ぜ', "ze"}, {U'ぞ', "zo"},
      {U'た', "ta"}, {U'ち', "chi"}, {U'つ', "tsu"}, {U'て', "te"}, {U'と', "to"},
      {U'だ', "da"}, {U'ぢ', "ji"},  {U'づ', "zu"},  {U'で', "de"}, {U'ど', "do"},
      {U'な', "na"}, {U'に', "ni"},  {U'ぬ', "nu"},  {U'ね', "ne"}, {U'の', "no"},
      {U'は', "ha"}, {U'ひ', "hi"},  {U'ふ', "fu"},  {U'へ', "he"}, {U'ほ', "ho"},
      {U'ば', "ba"}, {U'び', "bi"},  {U'ぶ', "bu"},  {U'べ', "be"}, {U'ぼ', "bo"},
      {U'ぱ', "pa"}, {U'ぴ', "pi"},  {U'ぷ', "pu"},  {U'ぺ', "pe"}, {U'ぽ', "po"},
      {U'ま', "ma"}, {U'み', "mi"},  {U'む', "mu"},  {U'め', "me"}, {U'も', "mo"},
      {U'や', "ya"}, {U'ゆ', "yu"},  {U'よ', "yo"},
      {U'ら', "ra"}, {U'り', "ri"},  {U'る', "ru"},  {U'れ', "re"}, {U'ろ', "ro"},
      {U'わ', "wa"}, {U'ゐ', "i"},   {U'ゑ', "e"},   {U'を', "o"},  {U'ん', "n"},  {U'ゔ', "vu"},
      {U'ぁ', "a"},  {U'ぃ', "i"},   {U'ぅ', "u"},   {U'ぇ', "e"},  {U'ぉ', "o"},
  };
  auto const it = table.find(Hiragana(c));
  return it == table.end() ? std::string() : std::string(it->second);
}

char YoonVowel(char32_t c)
{
  switch (Hiragana(c))
  {
  case U'ゃ': return 'a';
  case U'ゅ': return 'u';
  case U'ょ': return 'o';
  default: return 0;
  }
}

char SmallVowel(char32_t c)
{
  switch (Hiragana(c))
  {
  case U'ぁ': return 'a';
  case U'ぃ': return 'i';
  case U'ぅ': return 'u';
  case U'ぇ': return 'e';
  case U'ぉ': return 'o';
  default: return 0;
  }
}

/// Drops "o"+"o"/"u" and "uu" left to right without overlap, like Python's re.sub.
std::string CollapseLong(std::string const & s)
{
  std::string a;
  for (size_t i = 0; i < s.size(); ++i)
  {
    a += s[i];
    if (s[i] == 'o' && i + 1 < s.size() && (s[i + 1] == 'o' || s[i + 1] == 'u'))
      ++i;
  }
  std::string b;
  for (size_t i = 0; i < a.size(); ++i)
  {
    b += a[i];
    if (a[i] == 'u' && i + 1 < a.size() && a[i + 1] == 'u')
      ++i;
  }
  return b;
}

std::string KanaWord(std::u32string const & run)
{
  // Greetings where は is a particle, spelled as said.
  if (run == U"こんにちは")
    return "konnichiwa";
  if (run == U"こんばんは")
    return "konbanwa";
  std::string out;
  bool twice = false;
  for (size_t i = 0; i < run.size(); ++i)
  {
    char32_t const c = run[i];
    char32_t const next = i + 1 < run.size() ? run[i + 1] : 0;
    if (c == U'っ' || c == U'ッ')
    {
      twice = true;
      continue;
    }
    if (c == U'ー')
      continue;
    std::string syl = KanaRoman(c);
    if (char const v = YoonVowel(next); v && syl.size() >= 2 && syl.back() == 'i')
    {
      // shi+ya sha, chi+ya cha, ji+ya ja; ki+ya kya.
      std::string const stem = syl.substr(0, syl.size() - 1);
      syl = (stem == "sh" || stem == "ch" || stem == "j" ? stem : stem + "y") + v;
      ++i;
    }
    else if (char const v = SmallVowel(next); v && !syl.empty())
    {
      // Katakana loan sounds: ティ ti, ファ fa, ウィ wi, チェ che, ヴァ va.
      std::string stem = syl.substr(0, syl.size() - 1);
      if (syl == "fu" || syl == "vu" || syl == "u")
        stem = syl == "fu" ? "f" : syl == "vu" ? "v" : "w";
      else if (syl == "te" || syl == "de")
        stem = syl.substr(0, 1);
      syl = stem + v;
      ++i;
    }
    if (twice && !syl.empty() && std::string_view("aeiou").find(syl[0]) == std::string_view::npos)
      syl = (syl.rfind("ch", 0) == 0 ? std::string("t") : std::string(1, syl[0])) + syl;
    twice = false;
    out += syl;
  }
  return CollapseLong(out);
}
}  // namespace

std::string Kana(std::string const & text)
{
  std::u32string const in = Decode(text);
  bool any = false;
  for (char32_t c : in)
    any = any || KanaLetter(c);
  if (!any)
    return text;
  std::u32string out;
  for (size_t i = 0; i < in.size();)
  {
    if (!KanaLetter(in[i]))
    {
      out += in[i++];
      continue;
    }
    size_t end = i;
    while (end < in.size() && KanaLetter(in[end]))
      ++end;
    std::string word = KanaWord(in.substr(i, end - i));
    if (word.empty())
    {
      out.append(in, i, end - i);
      i = end;
      continue;
    }
    // Kanji stay as they are; keep the romaji from running into them: "ちょっと待って" "chotto 待 tte".
    if (i > 0 && Ideograph(in[i - 1]))
      word = " " + word;
    if (end < in.size() && Ideograph(in[end]))
      word += " ";
    out += Decode(word);
    i = end;
  }
  return Encode(out);
}

std::string RouteNumber(std::string const & digits)
{
  if (digits.empty() || digits.size() > 4 ||
      !std::all_of(digits.begin(), digits.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); }))
    return digits;
  if (digits.size() > 1 && digits.front() == '0')
    return Digits(digits);
  auto const n = ToNumber(digits);
  if (n < 100)
    return SpellNumber(n);
  // "405" is "four oh five", "183" "one eighty three", "1431" "fourteen thirty one".
  auto const high = n / 100, low = n % 100;
  if (low == 0)
    return (digits.size() == 4 && high % 10 == 0) ? SpellNumber(n) : SpellNumber(high) + " hundred";
  return SpellNumber(high) + (low < 10 ? " oh " : " ") + SpellNumber(low);
}

std::string Roads(std::string const & text)
{
  // Two-letter state codes in front of a route number. Ones that are also words ("IN 5", "OR 3")
  // only count with a hyphen ("IN-46").
  static std::map<std::string, std::pair<char const *, bool>> const states = {
      {"AK", {"Alaska", false}},        {"AL", {"Alabama", true}},        {"AR", {"Arkansas", false}},
      {"AZ", {"Arizona", false}},       {"CA", {"California", false}},    {"CO", {"Colorado", true}},
      {"CT", {"Connecticut", false}},   {"DE", {"Delaware", true}},       {"FL", {"Florida", false}},
      {"GA", {"Georgia", false}},       {"HI", {"Hawaii", true}},         {"IA", {"Iowa", false}},
      {"ID", {"Idaho", true}},          {"IL", {"Illinois", false}},      {"IN", {"Indiana", true}},
      {"KS", {"Kansas", false}},        {"KY", {"Kentucky", false}},      {"LA", {"Louisiana", true}},
      {"MA", {"Massachusetts", true}},  {"MD", {"Maryland", false}},      {"ME", {"Maine", true}},
      {"MI", {"Michigan", false}},      {"MN", {"Minnesota", false}},     {"MO", {"Missouri", false}},
      {"MS", {"Mississippi", false}},   {"MT", {"Montana", false}},       {"NC", {"North Carolina", false}},
      {"ND", {"North Dakota", false}},  {"NE", {"Nebraska", true}},       {"NH", {"New Hampshire", false}},
      {"NJ", {"New Jersey", false}},    {"NM", {"New Mexico", false}},    {"NV", {"Nevada", false}},
      {"NY", {"New York", false}},      {"OH", {"Ohio", true}},           {"OK", {"Oklahoma", true}},
      {"OR", {"Oregon", true}},         {"PA", {"Pennsylvania", true}},   {"RI", {"Rhode Island", false}},
      {"SC", {"South Carolina", false}}, {"SD", {"South Dakota", false}}, {"TN", {"Tennessee", false}},
      {"TX", {"Texas", false}},         {"UT", {"Utah", false}},          {"VA", {"Virginia", false}},
      {"VT", {"Vermont", false}},       {"WA", {"Washington", false}},    {"WI", {"Wisconsin", false}},
      {"WV", {"West Virginia", false}}, {"WY", {"Wyoming", false}}};
  static std::map<std::string, std::string> const names = {
      {"I", "I"},         {"US", "yooess"},      {"SR", "State Route"}, {"CR", "County Road"},
      {"FM", "F M"},      {"RM", "R M"},         {"SH", "State Highway"}, {"M", "M"}};
  static std::regex const road(
      "\\b(Interstate|Highway|Route|Loop|[Ee]xit|I|US|SR|CR|FM|RM|SH|M|[A-Z]{2})(-| )(\\d{1,4})([A-Z]?)"
      "(?![A-Za-z0-9]|[.,:/]\\d| ?(?:ft|mi|mph|km|m|yd)\\b)");
  auto s = Replace(text, road, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    auto const prefix = m.str(1);
    bool const hyphen = m.str(2) == "-";
    std::string name;
    if (auto const it = names.find(prefix); it != names.end())
    {
      if (prefix == "M" && !hyphen)
        return std::nullopt;
      name = it->second;
    }
    else if (auto const st = states.find(prefix); st != states.end())
    {
      if (st->second.second && !hyphen)
        return std::nullopt;
      name = st->second.first;
    }
    else if (prefix.size() == 2 && std::isupper(static_cast<unsigned char>(prefix[1])))
      return std::nullopt;  // Some other two capitals ("PO 5", "TV 2").
    else
      name = prefix;
    return name + " " + RouteNumber(m.str(3)) + (m[4].length() > 0 ? " " + m.str(4) : "");
  });
  // "the 101 Freeway", "the 405 Expressway".
  static std::regex const after("\\b(\\d{3,4}) (Freeway|Highway|Expressway|Parkway)\\b");
  return Replace(s, after, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    return RouteNumber(m.str(1)) + " " + m.str(2);
  });
}

std::string HouseNumbers(std::string const & text)
{
  // A 3 or 4 digit number, then up to four name words (a compass point, capitalised words or a
  // numbered street like "5th"), then a street type: "1600 Pennsylvania Avenue", "4521 Oak Street".
  static std::regex const address(
      "\\b(\\d{3,4})(?= (?:(?:North|South|East|West|Northeast|Northwest|Southeast|Southwest) )?"
      "(?:(?:[A-Z][A-Za-z'.-]*|\\d+(?:st|nd|rd|th)) ){1,4}"
      "(?:Street|Avenue|Boulevard|Road|Drive|Lane|Court|Parkway|Place|Circle|Terrace|Trail|Square|Way|Plaza)\\b)");
  return Replace(text, address, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    // Part of a bigger number ("1,600", "2.500") or a word ("A123"): leave it.
    if (pos > 0 && (std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == ',' ||
                    text[pos - 1] == '.' || text[pos - 1] == '-' || text[pos - 1] == '#' || text[pos - 1] == '$'))
      return std::nullopt;
    return RouteNumber(m.str(1));
  });
}

std::string Clock(std::string const & text)
{
  // "9am-5pm", "9-5pm", "9:30 a.m.–noon": a dash next to a clock time is "to".
  static std::regex const span(
      "(\\b\\d{1,2}(?::[0-5]\\d)?(?: ?[AaPp]\\.? ?[Mm]\\.?)?) ?(?:-|\xE2\x80\x93|\xE2\x80\x94) ?"
      "(?=\\d{1,2}(?::[0-5]\\d)? ?[AaPp]\\.? ?[Mm](?![A-Za-z])|noon\\b|midnight\\b)");
  auto const s = Replace(text, span, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    // Only when one side really is a clock time: "9am-5pm", "9-5pm", not "3-5pm" inside "13-5pm".
    if (pos > 0 && (std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '.' ||
                    text[pos - 1] == ',' || text[pos - 1] == '$'))
      return std::nullopt;
    if (auto const hour = ToNumber(m.str(1).substr(0, m.str(1).find_first_not_of("0123456789")));
        hour < 1 || hour > 12)
      return std::nullopt;
    return m.str(1) + " to ";
  });
  // Digits, or a spelled hour before a dotted or capital "a.m."/"PM" ("five a.m.": a plain "five am"
  // could be "am" the verb).
  static std::regex const clock(
      "\\b(\\d{1,2}|one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve)(?::([0-5]\\d))?"
      "( ?)([AaPp])(\\. ?)?([Mm])(\\.)?(?![A-Za-z0-9])");
  return Replace(s, clock, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    // Part of something bigger: "1.5 am", "$5 pm", "12:30:45 pm".
    if (pos > 0 && (s[pos - 1] == '.' || s[pos - 1] == ',' || s[pos - 1] == ':' || s[pos - 1] == '$' ||
                    s[pos - 1] == '#'))
      return std::nullopt;
    static std::vector<std::string> const words = {"one", "two",   "three", "four",   "five",  "six",
                                                   "seven", "eight", "nine", "ten", "eleven", "twelve"};
    long long hour = 0;
    if (auto const w = std::find(words.begin(), words.end(), m.str(1)); w != words.end())
    {
      bool const capital = std::isupper(static_cast<unsigned char>(m.str(4)[0])) &&
                           std::isupper(static_cast<unsigned char>(m.str(6)[0]));
      if (m.length(3) == 0 || (!m[5].matched && !capital))
        return std::nullopt;
      hour = static_cast<long long>(w - words.begin()) + 1;
    }
    else
      hour = ToNumber(m.str(1));
    auto const minutes = m[2].matched ? ToNumber(m.str(2)) : 0;
    if (hour < 1 || hour > 12)
      return std::nullopt;
    bool const pm = m.str(4) == "p" || m.str(4) == "P";
    std::string said;
    if (hour == 12 && minutes == 0)
      said = pm ? "noon" : "midnight";
    else
    {
      said = SpellNumber(hour);
      if (minutes > 0)
        said += (minutes < 10 ? " oh " : " ") + SpellNumber(minutes);
      said += pm ? " peeem" : " ayem";
    }
    // "by 5 p.m. Then..." or "by 5 p.m." at the end: that period ended the sentence too.
    if (m[7].matched)
    {
      size_t i = pos + static_cast<size_t>(m.length(0));
      if (i == s.size())
        said += '.';
      else if (Space(s[i]))
      {
        while (i < s.size() && Space(s[i]))
          ++i;
        if (i < s.size() && std::isupper(static_cast<unsigned char>(s[i])))
          said += '.';
      }
    }
    return said;
  });
}

std::string YearWords(long long year, bool decade)
{
  // "1868" eighteen sixty eight, "1905" nineteen oh five, "1900" nineteen hundred, "2005" two
  // thousand five, "2026" twenty twenty six; decades "1990s" nineteen nineties, "1800s" eighteen
  // hundreds, "2000s" two thousands, "2010s" twenty tens.
  auto const high = year / 100, low = year % 100;
  std::string said;
  if (year % 1000 < 10)  // 1000-1009 and 2000-2009: "two thousand five".
    said = SpellNumber(year / 1000) + " thousand" + (low == 0 ? "" : " " + SpellNumber(low));
  else if (low == 0)
    said = SpellNumber(high) + " hundred";
  else
    said = SpellNumber(high) + (low < 10 ? " oh " : " ") + SpellNumber(low);
  if (!decade)
    return said;
  if (said.size() > 1 && said.back() == 'y')
    return said.substr(0, said.size() - 1) + "ies";
  return said + "s";
}

std::string Years(std::string const & text)
{
  static std::regex const yearRe("\\b(1\\d\\d\\d|20\\d\\d)(s)?(?![A-Za-z0-9])");
  // Words that put a year after them: "in 1868", "since 1999", "the summer of 1969" (of only after
  // a season or "class"). Not "around"/"about"/"of" on their own: "around 1500 people".
  static std::set<std::string> const before = {
      "in", "since", "from", "until", "till", "by", "circa", "ca", "c", "before", "after", "during",
      "between", "year", "january", "february", "march", "april", "may", "june", "july", "august",
      "september", "october", "november", "december", "jan", "feb", "mar", "apr", "jun", "jul", "aug",
      "sep", "sept", "oct", "nov", "dec", "spring", "summer", "fall", "autumn", "winter"};
  static std::set<std::string> const seasonOf = {"spring", "summer", "fall", "autumn", "winter", "class"};
  // Counts and units after the number: "in 1500 feet" is a distance, never a year.
  static std::set<std::string> const counted = {
      "mi", "mile", "miles", "ft", "feet", "foot", "km", "kilometers", "kilometres", "m", "meters", "metres",
      "yd", "yds", "yard", "yards", "mph", "kph", "minute", "minutes", "min", "mins", "hour", "hours", "hrs",
      "second", "seconds", "day", "days", "weeks", "years", "times", "people", "cars", "dollars", "bucks",
      "pounds", "lbs", "kg", "calories", "steps", "blocks", "exits", "lanes", "stops", "points", "rpm"};
  static std::set<std::string> const era = {"ad", "bc", "bce", "ce"};
  static std::regex const monthDay(
      "\\b(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Sept|Oct|Nov|Dec)[a-z]*\\.? \\d{1,2}(?:st|nd|rd|th)?, $");
  static std::regex const joined("^ (?:to|and|through|or) $");
  auto const isNumberGlue = [&](size_t i) { return i < text.size() && Digit(text[i]); };
  size_t lastYearEnd = std::string::npos;
  std::string out;
  size_t last = 0;
  for (std::sregex_iterator it(text.begin(), text.end(), yearRe), end; it != end; ++it)
  {
    auto const & m = *it;
    auto const pos = static_cast<size_t>(m.position(0));
    auto const after = pos + static_cast<size_t>(m.length(0));
    out.append(text, last, pos - last);
    last = after;
    auto const keep = [&] { out += m.str(0); };
    // Part of something bigger: "1,868", "$1868", "#1868", "I-1868", "18:68", "1868.5", "1868/9".
    if (pos > 0 && std::string_view(",.-#$/:'").find(text[pos - 1]) != std::string_view::npos)
    {
      keep();
      continue;
    }
    if (after < text.size() && (text[after] == '%' || text[after] == '\xC2' ||
                                (std::string_view(",.:/-").find(text[after]) != std::string_view::npos &&
                                 isNumberGlue(after + 1))))
    {
      keep();
      continue;
    }
    auto const year = ToNumber(m.str(1));
    bool const decade = m[2].matched;
    if (decade && year % 10 != 0)
    {
      keep();
      continue;
    }
    size_t wordStart = 0;
    auto const prev = WordBefore(text, pos, wordStart);
    auto const next = WordAfter(text, after);
    // "from 1500 to 2000 feet": the unit after the second number counts the first one too.
    std::string unitAfter = next;
    static std::regex const pairedNumber("^ (?:to|and|through|or) [\\d,.]+");
    std::smatch pn;
    if (auto const rest = text.substr(after); std::regex_search(rest, pn, pairedNumber))
      unitAfter = WordAfter(rest, static_cast<size_t>(pn.length(0)));
    bool isYear = decade || era.count(next) > 0;
    if (!isYear && !counted.count(next) && !counted.count(unitAfter))
    {
      if (before.count(prev))
        isYear = true;
      else if (prev == "of")
      {
        size_t ofStart = 0;
        isYear = seasonOf.count(WordBefore(text, wordStart, ofStart)) > 0;
      }
      else if (std::regex_search(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(pos), monthDay))
        isYear = true;
      else if (lastYearEnd != std::string::npos && lastYearEnd <= pos &&
               std::regex_match(text.substr(lastYearEnd, pos - lastYearEnd), joined))
        isYear = true;  // "from 1914 to 1918", "between 1914 and 1918".
      else
      {
        // "It was 1914 to 1918": the second year says the first is one too.
        static std::regex const pairRe("^ (?:to|through) (1\\d\\d\\d|20\\d\\d)(?![A-Za-z0-9])");
        std::smatch pm;
        auto const rest = text.substr(after);
        if (std::regex_search(rest, pm, pairRe) && ToNumber(pm.str(1)) > year &&
            !counted.count(WordAfter(rest, static_cast<size_t>(pm.length(0)))))
          isYear = true;
      }
    }
    if (!isYear)
    {
      keep();
      continue;
    }
    out += YearWords(year, decade);
    lastYearEnd = after;
  }
  out.append(text, last, std::string::npos);
  return out;
}

/// Whether `s` has an ASCII digit, or any byte of `bytes`. The passes below are std::regex scans,
/// slow ones, and most lines have no digit at all: a pass that can't match skips its scan. Checked
/// right before each pass, on the text as it is then, so skipping never changes what is said.
bool HasDigit(std::string const & s)
{
  return std::any_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool HasAny(std::string const & s, std::string_view bytes) { return s.find_first_of(bytes) != std::string::npos; }

std::string ForSpeech(std::string const & text)
{
  std::string s = ReplaceAll(text, "\xC2\xA0", " ");
  // Foreign phrases first, before anything reads their letters as English abbreviations.
  s = Foreign(s);
  // Cyrillic map names and quotes the English voice would skip: "улица Ленина" is "ulitsa Lenina".
  s = Cyrillic(s);
  // Kana by Hepburn, the same way: "ラーメン" is "ramen", "さくら通り" "sakura 通 ri".
  s = Kana(s);
  // Ranges before dashes become pauses: "3–5" is "3 to 5", not "3, 5".
  if (HasDigit(s))
    s = Ranges(s);
  // Clock times before the dash pass can split "9am-5pm".
  s = Clock(s);
  // Years as years: "in 1868" is "in eighteen sixty eight", "the 1990s" "the nineteen nineties".
  if (HasDigit(s))
    s = Years(s);
  // Temperatures and approximations: "-5°F" is "minus 5 degrees Fahrenheit", "~5" "about 5".
  // "\xB0" is the second byte of the UTF-8 degree sign, "\xB1" of plus-minus, "\x80" of both dashes.
  bool const degreeSign = HasDigit(s) && HasAny(s, "\xB0");
  static std::regex const degrees("(^|[\\s(])-(?=\\d+(?:\\.\\d+)? ?\xC2\xB0)");
  if (degreeSign)
    s = std::regex_replace(s, degrees, "$1minus ");
  static std::regex const degree("(\\d+(?:\\.\\d+)?) ?\xC2\xB0(?: ?([FC])\\b)?");
  if (degreeSign)
    s = Replace(s, degree, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    std::string out = m.str(1) + (m.str(1) == "1" ? " degree" : " degrees");
    if (m[2].matched)
      out += m.str(2) == "F" ? " Fahrenheit" : " Celsius";
    return out;
  });
  static std::regex const about("~ ?(?=\\d)");
  if (HasAny(s, "~") && HasDigit(s))
    s = std::regex_replace(s, about, "about ");
  static std::regex const plusMinus("\xC2\xB1 ?(?=\\d)");
  if (HasAny(s, "\xB1") && HasDigit(s))
    s = std::regex_replace(s, plusMinus, "plus or minus ");
  static std::regex const dash("\\s*(?:\xE2\x80\x94|\xE2\x80\x93)\\s*");
  if (HasAny(s, "\x80"))
    s = std::regex_replace(s, dash, ", ");
  s = ReplaceAll(s, " & ", " and ");

  // The driver's own words first: they know best.
  s = ApplyUserWords(s);
  // Street types abroad: "Calle Ocho" is "KAH-yeh Ocho", "Paseo de la Reforma" keeps its Spanish.
  s = ForeignStreets(s);
  s = PickSenses(s);
  // Regnal numbers after a name: "King George VI Ave" is "King George the sixth Ave".
  s = Regnal(s);
  // Saint, Doctor, Fort, Junior before the street pass can take them for street types.
  s = Titles(s);

  // "Main St", "Elm Dr." after a word, at the end or before punctuation, a lowercase word or a
  // compass point ("Main St NW"). Not "Dr Smith" or "St Louis". A period after it stays: it was
  // probably the end of the sentence too.
  static std::regex const street(
      "(St|Ave|Blvd|Rd|Dr|Ln|Ct|Pkwy|Hwy|Pl|Cir|Ter|Trl|Expy|Fwy|Sq)(?=$|[,;:!?.)]|\\s+(?:[a-z]|(?:NE|NW|SE|SW|N|S|E|W)\\b))");
  s = Replace(s, street, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (pos < 2 || s[pos - 1] != ' ' || !IsAsciiLetter(s[pos - 2]))
      return std::nullopt;
    return Street(m.str(1));
  });
  static std::regex const mount("\\bMt\\.? (?=[A-Z])");
  s = std::regex_replace(s, mount, "Mount ");
  // Compass points next to a street name: "N Main", "Main St NW".
  static std::regex const compassBefore("(NE|NW|SE|SW|N|S|E|W)\\.?(?= [A-Z0-9])");
  s = Replace(s, compassBefore, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (pos > 0 && !Space(s[pos - 1]))
      return std::nullopt;
    return Compass(m.str(1));
  });
  static std::regex const compassAfter("(NE|NW|SE|SW|N|S|E|W)(?=$|[\\s,;:!?.])");
  s = Replace(s, compassAfter, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (pos < 2 || s[pos - 1] != ' ' || !std::isalnum(static_cast<unsigned char>(s[pos - 2])))
      return std::nullopt;
    return Compass(m.str(1));
  });
  // Highways, routes and exits the way drivers say them: "I-405" is "I four oh five", "FM 1431"
  // "F M fourteen thirty one", "TX-71" "Texas seventy one", "Exit 238B" "Exit two thirty eight B".
  if (HasDigit(s))
    s = Roads(s);
  // House numbers in pairs: "1600 Pennsylvania Avenue" is "sixteen hundred ...".
  if (HasDigit(s))
    s = HouseNumbers(s);
  // Everything from here on reads numbers: no digit, nothing left to do.
  if (!HasDigit(s))
    return Ellipses(s);
  // Money and percentages.
  static std::regex const money("\\$(\\d{1,3}(?:,\\d{3})+|\\d+)(?:\\.(\\d{2}))?\\b");
  if (HasAny(s, "$"))
    s = Replace(s, money, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    auto const whole = m.str(1);
    std::string out = whole + (whole == "1" ? " dollar" : " dollars");
    if (m[2].matched && m.str(2) != "00")
    {
      auto const cents = ToNumber(m.str(2));
      out += " and " + std::to_string(cents) + (cents == 1 ? " cent" : " cents");
    }
    return out;
  });
  static std::regex const percent("(\\d) ?%");
  if (HasAny(s, "%"))
    s = std::regex_replace(s, percent, "$1 percent");
  // Units after a number ("1 mile", "3 miles").
  static std::regex const unit("(\\d) ?(ft|mi|mph|km|m|yd)\\b");
  s = Replace(s, unit, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    size_t start = pos;
    while (start > 0 && (std::isdigit(static_cast<unsigned char>(s[start - 1])) || s[start - 1] == ',' ||
                         s[start - 1] == '.'))
      --start;
    bool const one = s.substr(start, pos + 1 - start) == "1";
    static std::map<std::string, std::pair<char const *, char const *>> const words = {
        {"ft", {"foot", "feet"}},           {"mi", {"mile", "miles"}},   {"mph", {"mile an hour", "miles an hour"}},
        {"km", {"kilometer", "kilometers"}}, {"m", {"meter", "meters"}}, {"yd", {"yard", "yards"}}};
    auto const & w = words.at(m.str(2));
    return m.str(1) + " " + (one ? w.first : w.second);
  });
  static std::regex const fraction("(\\d+)/(\\d+)");
  if (HasAny(s, "/"))
    s = Replace(s, fraction, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    auto const a = m.str(1), b = m.str(2);
    if (a == "1" && b == "2")
      return std::string("a half");
    if (a == "1" && b == "4")
      return std::string("a quarter");
    if (a == "3" && b == "4")
      return std::string("three quarters");
    return a + " over " + b;
  });
  // Times: "2:30" is "two thirty".
  static std::regex const time("\\b(\\d{1,2}):(\\d{2})\\b");
  if (HasAny(s, ":"))
    s = Replace(s, time, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    auto const hour = SpellNumber(ToNumber(m.str(1)));
    auto const minutes = ToNumber(m.str(2));
    if (minutes == 0)
      return hour + " o'clock";
    return hour + (minutes < 10 ? " oh " : " ") + SpellNumber(minutes);
  });
  // Ordinals: "8th Street" is "eighth Street".
  static std::regex const ordinal("\\b(\\d+)(st|nd|rd|th)\\b");
  s = Replace(s, ordinal, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    if (m.str(1).size() > 9)
      return std::nullopt;
    return Ordinal(ToNumber(m.str(1)));
  });
  // Decimals and plain numbers ("1,200" too). Long ones (phone numbers, zip codes) digit by digit.
  static std::regex const number("\\b(\\d{1,3}(?:,\\d{3})+|\\d+)(?:\\.(\\d+))?\\b");
  s = Replace(s, number, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    std::string whole;
    for (char ch : m.str(1))
      if (ch != ',')
        whole += ch;
    std::string said = whole.size() > 6 ? Digits(whole) : SpellNumber(ToNumber(whole));
    if (m[2].matched)
      said += " point " + Digits(m.str(2));
    return said;
  });

  return Ellipses(s);
}

std::vector<std::pair<std::string, std::string>> Lexicon()
{
  static std::vector<std::pair<std::string, std::string>> const builtIn = [] {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto const & w : kWords)
    {
      std::string said = w.said;
      if (said.size() > 2 && said.front() == '/' && said.back() == '/')
        out.emplace_back(w.word, said.substr(1, said.size() - 2));
      else if (auto const ph = RespellingPhonemes(said))
        out.emplace_back(w.word, *ph);
    }
    std::set<std::string> seen;
    for (auto const & p : kPhrases)
      for (auto const & w : SplitWords(p.said))
        if (auto const ph = ForeignWordPhonemes(w); ph && seen.insert(StandIn(w)).second)
          out.emplace_back(StandIn(w), *ph);
    for (auto const & w : ForeignStreetRespellings())
      if (auto const ph = ForeignWordPhonemes(w); ph && seen.insert(StandIn(w)).second)
        out.emplace_back(StandIn(w), *ph);
    // Names last, never replacing a word or stand-in above.
    std::set<std::string> taken;
    for (auto const & [w, ph] : out)
      taken.insert(w);
    for (auto const & n : kNames)
      if (taken.insert(n.word).second)
        out.emplace_back(n.word, n.phonemes);
    return out;
  }();
  auto lexicon = builtIn;
  std::lock_guard lock(g_userMutex);
  for (auto const & w : g_userWords)
    if (!w.phonemes.empty())
      lexicon.emplace_back(w.said, w.phonemes);
  return lexicon;
}

// MARK: - Respellings

namespace
{
std::u32string Decode(std::string const & s)
{
  std::u32string out;
  for (size_t i = 0; i < s.size();)
  {
    auto const c = static_cast<unsigned char>(s[i]);
    int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    char32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k < len && i + k < s.size(); ++k)
      cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    out += cp;
    i += len;
  }
  return out;
}

std::string Encode(std::u32string const & s)
{
  std::string out;
  for (char32_t cp : s)
  {
    if (cp < 0x80)
      out += static_cast<char>(cp);
    else if (cp < 0x800)
    {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  return out;
}

bool Mark(char32_t c)
{
  return c == U'\'' || c == U'\u00B4' || c == U'\u02C8';
}

bool Letter(char32_t c)
{
  return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
}

std::pair<char const *, char32_t const *> const kDigraphVowels[] = {
    {"eye", U"I"}, {"igh", U"I"}, {"air", U"ɛɹ"}, {"are", U"ɛɹ"}, {"eer", U"ɪɹ"}, {"ear", U"ɪɹ"},
    {"ee", U"i"},  {"ea", U"i"},  {"ie", U"i"},   {"oo", U"u"},   {"ou", U"W"},   {"ow", U"W"},
    {"oy", U"Y"},  {"oi", U"Y"},  {"ay", U"A"},   {"ai", U"A"},   {"ey", U"A"},   {"aw", U"ɔ"},
    {"au", U"ɔ"},  {"ah", U"ɑ"},  {"ih", U"ɪ"},   {"eh", U"ɛ"},   {"oh", U"O"},   {"oa", U"O"},
    {"oe", U"O"},  {"uh", U"ʌ"},  {"ew", U"ju"},  {"ar", U"ɑɹ"},  {"or", U"ɔɹ"},  {"er", U"ɜɹ"},
    {"ir", U"ɜɹ"}, {"ur", U"ɜɹ"},
};

std::pair<char const *, char32_t const *> const kConsonants[] = {
    {"tch", U"ʧ"}, {"ch", U"ʧ"}, {"sh", U"ʃ"}, {"th", U"θ"}, {"zh", U"ʒ"}, {"ng", U"ŋ"}, {"ph", U"f"},
    {"ck", U"k"},  {"qu", U"kw"}, {"kh", U"k"}, {"j", U"ʤ"},  {"c", U"k"},  {"x", U"ks"}, {"q", U"k"},
    {"g", U"ɡ"},   {"r", U"ɹ"},  {"y", U"j"},  {"b", U"b"},  {"d", U"d"},  {"f", U"f"},  {"h", U"h"},
    {"k", U"k"},   {"l", U"l"},  {"m", U"m"},  {"n", U"n"},  {"p", U"p"},  {"s", U"s"},  {"t", U"t"},
    {"v", U"v"},   {"w", U"w"},  {"z", U"z"},
};

char32_t const * SingleVowel(char v, bool stressed, bool open, bool last)
{
  switch (v)
  {
  case 'a': return stressed ? (open ? U"ɑ" : U"æ") : U"ə";
  case 'e':
    if (stressed)
      return open ? U"i" : U"ɛ";
    return open && last ? U"i" : U"ə";
  case 'i': return stressed ? (open ? U"I" : U"ɪ") : U"ɪ";
  case 'o':
    if (open)
      return U"O";
    return stressed ? U"ɑ" : U"ə";
  case 'u': return stressed ? (open ? U"u" : U"ʌ") : U"ə";
  default: return U"ə";
  }
}

/// One syllable: onset consonants, the vowel (stress mark before it), coda consonants.
std::optional<std::u32string> Syllable(std::string const & s, bool stressed, bool last)
{
  std::string_view rest = s;
  std::u32string out;
  bool vowelDone = false;
  auto const startsWith = [&](char const * p) { return rest.substr(0, std::char_traits<char>::length(p)) == p; };
  while (!rest.empty())
  {
    if (!vowelDone)
    {
      bool matched = false;
      for (auto const & [from, to] : kDigraphVowels)
        if (startsWith(from))
        {
          std::u32string const t = to;
          std::u32string const reduced = t[0] == U'ɜ' ? U"əɹ" : (t == U"ʌ" ? U"ə" : t);  // "ROOZ-uh"
          if (stressed)
            out += U'ˈ';
          out += stressed ? t : reduced;
          rest.remove_prefix(std::char_traits<char>::length(from));
          vowelDone = matched = true;
          break;
        }
      if (matched)
        continue;
      if (std::string_view("aeiou").find(rest[0]) != std::string_view::npos)
      {
        bool const open = rest.size() == 1;
        // "WINE", "NAME", "HOPE": a final e after one consonant makes the vowel say its name.
        bool const silentE = rest.size() == 3 && rest[2] == 'e' &&
                             std::string_view("aeiouy").find(rest[1]) == std::string_view::npos;
        if (stressed)
          out += U'ˈ';
        if (silentE)
        {
          static std::map<char, char32_t const *> const named = {
              {'a', U"A"}, {'e', U"i"}, {'i', U"I"}, {'o', U"O"}, {'u', U"u"}};
          out += named.at(rest[0]);
        }
        else
          out += SingleVowel(rest[0], stressed, open, last);
        rest.remove_prefix(1);
        vowelDone = true;
        continue;
      }
      if (rest[0] == 'y' && rest.size() == 1 && !out.empty())  // "-ly", "-ty"
      {
        out += stressed ? U"ˈI" : U"i";
        rest.remove_prefix(1);
        vowelDone = true;
        continue;
      }
    }
    bool matched = false;
    for (auto const & [from, to] : kConsonants)
      if (startsWith(from))
      {
        out += to;
        rest.remove_prefix(std::char_traits<char>::length(from));
        matched = true;
        break;
      }
    if (matched)
      continue;
    if (std::string_view("aeiouy").find(rest[0]) != std::string_view::npos)
    {
      rest.remove_prefix(1);
      continue;
    }
    return std::nullopt;
  }
  if (vowelDone)
    return out;
  if (out.empty())
    return std::nullopt;
  return out;
}
}  // namespace

std::optional<std::string> RespellingPhonemes(std::string const & respelling)
{
  auto raw = Decode(respelling);
  while (!raw.empty() && (raw.front() == U' ' || raw.front() == U'\t' || raw.front() == U'\n'))
    raw.erase(raw.begin());
  while (!raw.empty() && (raw.back() == U' ' || raw.back() == U'\t' || raw.back() == U'\n'))
    raw.pop_back();
  if (raw.empty())
    return std::nullopt;
  for (char32_t c : raw)
    if (!Letter(c) && c != U'-' && !Mark(c))
      return std::nullopt;

  std::vector<std::u32string> parts;
  std::u32string part;
  for (char32_t c : raw)
  {
    if (c == U'-')
    {
      if (!part.empty())
        parts.push_back(part);
      part.clear();
    }
    else
      part += c;
  }
  if (!part.empty())
    parts.push_back(part);

  auto const hasMark = [](std::u32string const & p) { return std::any_of(p.begin(), p.end(), Mark); };
  auto const letters = [](std::u32string const & p) {
    std::u32string out;
    for (char32_t c : p)
      if (Letter(c))
        out += c;
    return out;
  };
  auto const allUpper = [](std::u32string const & l) {
    return std::none_of(l.begin(), l.end(), [](char32_t c) { return c >= U'a' && c <= U'z'; });
  };
  bool const hasCapsSyllable = parts.size() > 1 && std::any_of(parts.begin(), parts.end(), [&](auto const & p) {
                                 auto const l = letters(p);
                                 return !l.empty() && allUpper(l);
                               });
  if (parts.size() <= 1 && !hasMark(raw))
    return std::nullopt;

  std::optional<size_t> stressIndex;
  for (size_t k = 0; k < parts.size() && !stressIndex; ++k)
    if (hasMark(parts[k]))
      stressIndex = k;
  if (!stressIndex && hasCapsSyllable)
  {
    for (size_t k = 0; k < parts.size() && !stressIndex; ++k)
    {
      auto const l = letters(parts[k]);
      if (!l.empty() && allUpper(l) && (l.size() > 1 || parts.size() > 1) &&
          !(parts.front() == parts[k] && l.size() == 1 && parts.size() > 2))
        stressIndex = k;
    }
  }
  size_t const stress = stressIndex.value_or(0);

  std::u32string out;
  for (size_t k = 0; k < parts.size(); ++k)
  {
    std::string sounds;
    for (char32_t c : letters(parts[k]))
      sounds += static_cast<char>(std::tolower(static_cast<int>(c)));
    auto const ph = Syllable(sounds, k == stress, k + 1 == parts.size());
    if (!ph)
      return std::nullopt;
    out += *ph;
  }
  // "A-KIL-leez": a doubled consonant across syllables is one sound.
  std::u32string const merge = U"bdfhklmnpstvwzɡɹʃʒθŋʧʤj";
  std::u32string collapsed;
  for (char32_t c : out)
  {
    if (!collapsed.empty() && c == collapsed.back() && merge.find(c) != std::u32string::npos)
      continue;
    collapsed += c;
  }
  if (collapsed.empty())
    return std::nullopt;
  return Encode(collapsed);
}
}  // namespace backseat::pronunciation
