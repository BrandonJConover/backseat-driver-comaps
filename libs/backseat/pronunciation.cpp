#include "backseat/pronunciation.hpp"
#include "backseat/pronunciation_text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
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
using detail::Decode;
using detail::Encode;
using detail::Letter;

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

// Place names Kokoro's Misaki dictionaries lack (the fallback guesses them: "Buda" like the city in
// Hungary, "Pflugerville" with a P), as the locals say them. Reader respellings, or Misaki IPA between
// slashes; Lexicon() adds them after kNames without replacing a word already there (Kyle is a name).
// Scout BI-033 checked each against misaki 0.7.4 us_gold/us_silver. Golden cases in
// backseat_tests/spoken_places.json, shared with BookLyre (CROSS).
constexpr Word kPlaces[] = {
    // Central Texas.
    {"buda", "BYOO-duh"}, {"manchaca", "MAN-shak"}, {"pflugerville", "FLOO-gur-vil"}, {"gruene", "GREEN"},
    {"boerne", "BUR-nee"}, {"mexia", "muh-HAY-uh"}, {"refugio", "reh-FYUR-ee-oh"}, {"bexar", "BAIR"},
    {"pedernales", "pur-duh-NAL-iss"}, {"guadalupe", "GWAH-duh-loop"}, {"lamar", "luh-MAR"},
    {"cibolo", "SEE-buh-loh"}, {"bastrop", "BASS-trop"}, {"lampasas", "lam-PASS-us"}, {"kerrville", "KUR-vil"},
    {"seguin", "seh-GEEN"}, {"hutto", "HUH-toh"}, {"helotes", "heh-LOH-tiss"}, {"uvalde", "yoo-VAL-dee"},
    {"waxahachie", "wawk-suh-HATCH-ee"}, {"wimberley", "WIM-bur-lee"}, {"marfa", "MAR-fuh"},
    {"llano", "LAN-oh"}, {"leander", "lee-AN-dur"}, {"bandera", "ban-DAIR-uh"}, {"goliad", "GOH-lee-ad"},
    {"nacogdoches", "nak-uh-DOH-chiss"},
    // Elsewhere in the US.
    {"natchitoches", "NAK-uh-tish"}, {"puyallup", "pyoo-AL-up"}, {"schuylkill", "SKOO-kul"},
    {"willamette", "wil-AM-it"}, {"kosciusko", "koz-ee-US-koh"}, {"moines", "MOYN"},
    {"yosemite", "yoh-SEM-ih-tee"}, {"sequim", "SKWIM"}, {"tualatin", "too-AWL-uh-tin"},
    {"mukilteo", "muk-il-TEE-oh"},
};

// Places spelled like an English word but said another way there ("Burnet Rd" in Austin is BURN-it,
// Humble TX is UM-bul). Swapped for a stand-in only where the sentence says it's the place (see
// LocalPlaces()), so "be humble" and "the manor" keep their sound.
struct LocalPlace
{
  char const * word;
  char const * said;
  /// After "to", "onto", "in"... too; false when the word is also a country or common noun there.
  bool afterPreposition;
};

constexpr LocalPlace kLocalPlaces[] = {
    {"Burnet", "BURN-it", true},
    {"Humble", "UM-bul", true},
    {"Manor", "MAY-nur", true},
    // "in Palestine" is the country; only "Palestine, TX" or "Palestine Ave".
    {"Palestine", "PAL-uh-steen", false},
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
    {"Sq", "Square"},     {"Cv", "Cove"},     {"Xing", "Crossing"},  {"Ext", "Extension"},
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
  // "Ranch Rd 2222", "County Rd 12": a capitalised word, then Rd and a number.
  static std::regex const roadBefore("\\b(Hwy|Fwy|Expy|Pkwy|Rte|Rd)\\.? (?=\\d)");
  s = Replace(s, roadBefore, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (m.str(1) == "Rd" && (pos < 2 || s[pos - 1] != ' ' || !std::isupper(static_cast<unsigned char>(
                                                                    s[s.find_last_of(' ', pos - 2) + 1]))))
      return std::nullopt;
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

// Digits as a number. A digit run too long for long long (a garbled street name, "99999999999999999999-5")
// stops at LLONG_MAX instead of overflowing, which is undefined behaviour; every caller treats that as huge.
long long ToNumber(std::string const & digits)
{
  long long n = 0;
  for (char ch : digits)
    if (std::isdigit(static_cast<unsigned char>(ch)))
    {
      if (n > (LLONG_MAX - 9) / 10)
        return LLONG_MAX;
      n = n * 10 + (ch - '0');
    }
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

// Words that make a bare hyphen between two hours "to": "Monday to Friday 9-5", "Open 9-5", "Sat 10-4".
// "the 9-5 grind" and scores stay as written. Lowercase.
bool HoursWordBefore(std::string const & w)
{
  static std::array<std::string_view, 33> const words = {
      "mon",      "tue",      "tues",      "wed",     "thu",      "fri",     "sat",      "sun",
      "monday",   "tuesday",  "wednesday", "thursday", "friday",  "saturday", "sunday",  "mondays",
      "tuesdays", "wednesdays", "thursdays", "fridays", "saturdays", "sundays", "weekdays", "weekends",
      "daily",    "nightly",  "open",      "opens",   "hours",    "closes",  "closed",   "everyday",
      "weeknights"};
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
      // Opening hours count down too: "Monday to Friday, 9-5" is "9 to 5". Whole hours 1-12 only.
      size_t commaStart = 0;
      std::string const hoursWord =
          before.empty() && wordStart > 0 && s[wordStart - 1] == ',' ? WordBefore(s, wordStart - 1, commaStart) : before;
      auto const hourA = ToNumber(a);
      auto const hourB = ToNumber(b);
      bool const hours = hyphen && HoursWordBefore(hoursWord) && a.size() <= 2 && b.size() <= 2 &&
                         hourA >= 1 && hourA <= 12 && hourB >= 1 && hourB <= 12 && hourA != hourB &&
                         a.find_first_not_of("0123456789") == std::string::npos &&
                         b.find_first_not_of("0123456789") == std::string::npos;
      if (hours)
        said = a + " to " + b;
      // Scores and "the 9-5 grind" count down; ranges go up.
      else if (Value(b) > Value(a) && !ScoreWordBefore(before) && (!hyphen || (yearA && yearB) || counting))
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
    {"Je te l'avais dit", "zhuh tuh lah-VEH DEE"},
    {"Hab ich dir doch gesagt", "HAHP ish DEER DOKH geh-ZAHKT"},
    {"Route wird neu berechnet", "ROO-teh VEERT NOY beh-RESH-net"},
    // "learn to drive"
    {"Aprende a conducir", "ah-PREN-deh ah kohn-doo-SEER"},
    {"Apprends à conduire", "ah-PRAHN ah kohn-DWEER"},
    {"Lern Auto fahren", "LAIRN OW-toh FAH-ren"},
    {"Impara a guidare", "eem-PAH-rah ah gwee-DAH-reh"},
    {"Aprenda a dirigir", "ah-PREN-dah ah jee-ree-ZHEER"},
    {"運転を習ってください", "OON-tehn oh nah-RAHT-teh koo-dah-SIGH"},
    {"Leer autorijden", "LAYR OW-toh-ray-den"},
    {"Научись водить", "nah-oo-CHEES vah-DEET"},
    // "I am an excellent driver"
    {"Soy un conductor excelente", "SOY oon kohn-dook-TOR ehk-seh-LEN-teh"},
    {"Je suis un excellent conducteur", "zhuh SWEE uhn ehk-seh-LAHN kohn-dook-TUR"},
    {"Ich bin ein ausgezeichneter Fahrer", "ISH BIN EYEN OWS-geh-tsighsh-neh-ter FAH-rer"},
    {"Sono un ottimo guidatore", "SOH-noh oon OHT-tee-moh gwee-dah-TOH-reh"},
    {"Я отличный водитель", "yah aht-LEECH-nee vah-DEE-tyel"},
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

// MARK: - Local place names

std::string LocalPlaces(std::string const & text)
{
  bool any = false;
  for (auto const & p : kLocalPlaces)
    any = any || text.find(p.word) != std::string::npos;
  if (!any)
    return text;
  struct Span
  {
    size_t at, end;
  };
  std::vector<Span> words;
  for (size_t i = 0; i < text.size();)
  {
    if (!IsAsciiLetter(text[i]))
    {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < text.size() && IsAsciiLetter(text[j]))
      ++j;
    words.push_back({i, j});
    i = j;
  }
  auto const word = [&](size_t k) { return text.substr(words[k].at, words[k].end - words[k].at); };
  // Only spaces, or one comma ("Humble, TX"), between two words.
  auto const adjacent = [&](size_t a, size_t b) {
    int commas = 0;
    for (size_t i = words[a].end; i < words[b].at; ++i)
    {
      if (text[i] == ',')
        ++commas;
      else if (text[i] != ' ')
        return false;
    }
    return commas <= 1;
  };
  static Set const placeAfter = {"Rd", "Road", "St", "Street", "Ave", "Avenue", "Dr", "Drive", "Ln", "Lane", "Blvd",
                                 "Boulevard", "Hwy", "Highway", "Pkwy", "Parkway", "Ct", "Way", "Loop", "County",
                                 "TX", "Tex", "Texas", "ISD", "Exit", "Station", "Square", "Plaza", "Center"};
  static Set const toward = {"to", "onto", "into", "in", "toward", "towards", "near", "from", "via", "through",
                             "past", "outside", "reach", "leaving", "entering"};
  std::string out;
  size_t last = 0;
  for (size_t k = 0; k < words.size(); ++k)
  {
    auto const w = word(k);
    auto const place = std::find_if(std::begin(kLocalPlaces), std::end(kLocalPlaces),
                                    [&](LocalPlace const & p) { return w == p.word; });
    if (place == std::end(kLocalPlaces))
      continue;
    // "Burnet Rd", "Humble, TX"; a comma only before the state.
    bool here = false;
    if (k + 1 < words.size() && adjacent(k, k + 1))
    {
      auto const next = word(k + 1);
      bool const comma = text.find(',', words[k].end) < words[k + 1].at;
      here = In(next, placeAfter) && (!comma || In(next, {"TX", "Tex", "Texas"}));
    }
    // "onto Burnet", "in Humble": the place unless a capitalised word follows ("to Manor House").
    if (!here && place->afterPreposition && k > 0 && adjacent(k - 1, k) &&
        text.find(',', words[k - 1].end) >= words[k].at && In(word(k - 1), toward))
      here = !(k + 1 < words.size() && adjacent(k, k + 1) && text.find(',', words[k].end) >= words[k + 1].at &&
               std::isupper(static_cast<unsigned char>(text[words[k + 1].at])) && !In(word(k + 1), placeAfter));
    if (!here)
      continue;
    out.append(text, last, words[k].at - last);
    out += StandIn(std::string(place->said));
    last = words[k].end;
  }
  out.append(text, last, std::string::npos);
  return out;
}

std::vector<std::string> LocalPlaceRespellings()
{
  std::vector<std::string> out;
  for (auto const & p : kLocalPlaces)
    out.push_back(p.said);
  return out;
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
      {"I", "I"},         {"IH", "I"},           {"US", "yooess"},      {"SR", "State Route"},
      {"CR", "County Road"}, {"FM", "F M"},      {"RM", "R M"},         {"RR", "Ranch Road"},
      {"SH", "State Highway"}, {"M", "M"}};
  // What the prefix says out loud, or nothing when it isn't a route: "PO 5", "TV 2", "IN 5".
  auto const spoken = [](std::string const & prefix, bool hyphen) -> std::optional<std::string> {
    if (auto const it = names.find(prefix); it != names.end())
    {
      if (prefix == "M" && !hyphen)
        return std::nullopt;
      return it->second;
    }
    if (auto const st = states.find(prefix); st != states.end())
    {
      if (st->second.second && !hyphen)
        return std::nullopt;
      return std::string(st->second.first);
    }
    if (prefix.size() == 2 && std::isupper(static_cast<unsigned char>(prefix[1])))
      return std::nullopt;
    return prefix;
  };
  // Business, alternate and bypass routes after the number: "US 290 Bus", "US 183 Alt", "US 87 Byp".
  // Not when a lowercase word follows that makes it something else ("the I-35 bus stop").
#define BACKSEAT_ROUTE_PREFIX "Interstate|Highway|Route|Road|Loop|[Ee]xit|I|IH|US|SR|CR|FM|RM|RR|SH|M|[A-Z]{2}"
#define BACKSEAT_ROUTE_KIND "Bus|BUS|Business|Alt|ALT|Byp|BYP|Bypass"
  // OSM joins the refs of a road shared by two routes with ';' ("US 290;TX 71"), signs use '/'.
  static std::regex const pair(
      "\\b(" BACKSEAT_ROUTE_PREFIX ")(-| )(\\d{1,4}[A-Z]?(?: (?:" BACKSEAT_ROUTE_KIND ")\\.?)?) ?[;/] ?"
      "(?=(" BACKSEAT_ROUTE_PREFIX ")(-| )\\d)");
  auto s = Replace(text, pair, [&](std::smatch const & m, size_t) -> std::optional<std::string> {
    if (!spoken(m.str(1), m.str(2) == "-") || !spoken(m.str(4), m.str(5) == "-"))
      return std::nullopt;
    return m.str(1) + m.str(2) + m.str(3) + " or ";
  });
  static std::regex const road(
      "\\b(" BACKSEAT_ROUTE_PREFIX ")(-| )(\\d{1,4})([A-Z]?)"
      "(?![A-Za-z0-9]|[.,:/]\\d| ?(?:ft|mi|mph|km|m|yd)\\b)"
      "(?: (" BACKSEAT_ROUTE_KIND ")(\\.)?(?![A-Za-z0-9]| (?:stop|stops|station|lane|lanes|route|routes|key)\\b))?");
#undef BACKSEAT_ROUTE_PREFIX
#undef BACKSEAT_ROUTE_KIND
  auto const before = s;
  s = Replace(before, road, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    auto const name = spoken(m.str(1), m.str(2) == "-");
    if (!name)
      return std::nullopt;
    std::string out = *name + " " + RouteNumber(m.str(3)) + (m[4].length() > 0 ? " " + m.str(4) : "");
    if (m[5].matched)
    {
      auto const kind = m.str(5);
      auto const k = static_cast<char>(std::tolower(static_cast<unsigned char>(kind[0])));
      out += k == 'b' && std::tolower(static_cast<unsigned char>(kind[1])) == 'u' ? " Business"
             : k == 'a'                                                           ? " Alternate"
                                                                                  : " Bypass";
      // "Byp." may end the sentence too: the period stays unless a lowercase word carries on.
      auto const end = pos + static_cast<size_t>(m.length(0));
      if (m[6].matched && !(end + 1 < before.size() && before[end] == ' ' &&
                            std::islower(static_cast<unsigned char>(before[end + 1]))))
        out += ".";
    }
    return out;
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

std::string Symbols(std::string const & text)
{
  std::string s = text;
  // Vulgar fractions as the ASCII ones the fraction pass reads: "½ mile" is "1/2 mile", and after
  // a whole number "1½" is "1 and 1/2".
  if (HasAny(s, "\xBC\xBD\xBE\x85"))
  {
    static std::pair<std::string_view, std::string_view> const kFractions[] = {
        {"\xC2\xBD", "1/2"}, {"\xC2\xBC", "1/4"}, {"\xC2\xBE", "3/4"}, {"\xE2\x85\x93", "1/3"}, {"\xE2\x85\x94", "2/3"}};
    for (auto const & [glyph, ascii] : kFractions)
      for (size_t at = s.find(glyph); at != std::string::npos; at = s.find(glyph, at))
      {
        bool const afterDigit = at > 0 && std::isdigit(static_cast<unsigned char>(s[at - 1]));
        std::string const with = (afterDigit ? " and " : "") + std::string(ascii);
        s.replace(at, glyph.size(), with);
        at += with.size();
      }
  }
  // "#5" and "Starbucks #12345" are "number 5": a hash right before digits, at a word start.
  // "#blessed" and "C#5" are left.
  if (HasAny(s, "#"))
  {
    static std::regex const hash("#(?=\\d)");
    s = Replace(s, hash, [&](std::smatch const &, size_t pos) -> std::optional<std::string> {
      if (pos > 0 && !Space(s[pos - 1]) && s[pos - 1] != '(')
        return std::nullopt;
      return std::string("number ");
    });
  }
  // Rates after a number or a unit: "$3.49/gal" is "per gallon", "60 mi/hr" and "km/h" "per hour".
  // "and/or", "w/o" and "24/7" are left.
  if (HasAny(s, "/"))
  {
    static std::regex const rate("\\b([0-9A-Za-z.,]+)/(gal|hr|h|mi|km|day|wk|mo|yr)\\b");
    s = Replace(s, rate, [&](std::smatch const & m, size_t) -> std::optional<std::string> {
      static std::set<std::string> const units = {"mi", "km", "m", "ft", "gal", "miles", "kilometers", "meters", "feet"};
      auto const left = m.str(1);
      bool const number = std::isdigit(static_cast<unsigned char>(left.back())) &&
                          std::all_of(left.begin(), left.end(), [](char ch) {
                            return std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == ',';
                          });
      if (!number && units.count(left) == 0)
        return std::nullopt;
      static std::map<std::string, char const *> const per = {
          {"gal", "gallon"},   {"hr", "hour"}, {"h", "hour"},  {"mi", "mile"},  {"km", "kilometer"},
          {"day", "day"},      {"wk", "week"}, {"mo", "month"}, {"yr", "year"}};
      return left + " per " + per.at(m.str(2));
    });
  }
  return s;
}

std::string Decades(std::string const & text)
{
  // "the 90s", "the '90s", "the 80's", "in my 20s", "mid-70s": two-digit decades from the twenties on.
  // Not "10s" (tens of what?), "$90s", "A90s", "90sec" or "1990s" (Years has those).
  static std::regex const decade("'?([2-9])0'?s(?![A-Za-z0-9'])");
  return Replace(text, decade, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    // "the '90s": the apostrophe stands for the missing "19" and goes with it.
    bool const ok = pos == 0 || Space(text[pos - 1]) || text[pos - 1] == '(' || text[pos - 1] == '"' ||
                    (text[pos - 1] == '-' && pos > 1 && IsAsciiLetter(text[pos - 2]));
    if (!ok)
      return std::nullopt;
    static char const * const kTens[] = {"", "", "twenties", "thirties", "forties", "fifties",
                                         "sixties", "seventies", "eighties", "nineties"};
    return std::string(kTens[m.str(1)[0] - '0']);
  });
}

std::string FractionsWithUnits(std::string const & text)
{
  // "1/2 mi" is "a half mile" (not "a half miles"), "3/4 mi" "three quarters of a mile", "2/3 hr"
  // "two thirds of an hour", "1 1/2 mi" "1 and a half miles", "2 1/2 years" "2 and a half years".
  // Dates ("1/2/2026"), "11/2" and other fractions are left for the passes after.
  static std::regex const re(
      "\\b(?:(\\d{1,3}) (?:and )?)?([123])/([234])(?![\\d/])"
      "(?: ?(mi|miles?|km|kilometers?|ft|feet|foot|yd|yards?|m|meters?|hrs?|hours?|mins?|minutes?|secs?|"
      "seconds?|gal|gallons?|inch|inches)\\b)?");
  struct Unit
  {
    char const * one;
    char const * many;
  };
  static std::map<std::string, Unit> const units = {
      {"mi", {"mile", "miles"}},         {"mile", {"mile", "miles"}},     {"miles", {"mile", "miles"}},
      {"km", {"kilometer", "kilometers"}}, {"kilometer", {"kilometer", "kilometers"}},
      {"kilometers", {"kilometer", "kilometers"}}, {"ft", {"foot", "feet"}}, {"feet", {"foot", "feet"}},
      {"foot", {"foot", "feet"}},        {"yd", {"yard", "yards"}},       {"yard", {"yard", "yards"}},
      {"yards", {"yard", "yards"}},      {"m", {"meter", "meters"}},      {"meter", {"meter", "meters"}},
      {"meters", {"meter", "meters"}},   {"hr", {"hour", "hours"}},       {"hrs", {"hour", "hours"}},
      {"hour", {"hour", "hours"}},       {"hours", {"hour", "hours"}},    {"min", {"minute", "minutes"}},
      {"mins", {"minute", "minutes"}},   {"minute", {"minute", "minutes"}}, {"minutes", {"minute", "minutes"}},
      {"sec", {"second", "seconds"}},    {"secs", {"second", "seconds"}}, {"second", {"second", "seconds"}},
      {"seconds", {"second", "seconds"}}, {"gal", {"gallon", "gallons"}}, {"gallon", {"gallon", "gallons"}},
      {"gallons", {"gallon", "gallons"}}, {"inch", {"inch", "inches"}},   {"inches", {"inch", "inches"}}};
  return Replace(text, re, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    auto const frac = m.str(2) + "/" + m.str(3);
    static std::map<std::string, std::pair<char const *, char const *>> const words = {
        // {said after a whole number or before a unit that reads fine bare, before "of a"}
        {"1/2", {"a half", "a half"}},          {"1/4", {"a quarter", "a quarter"}},
        {"3/4", {"three quarters", "three quarters of"}}, {"1/3", {"a third", "a third of"}},
        {"2/3", {"two thirds", "two thirds of"}}};
    auto const w = words.find(frac);
    if (w == words.end())
      return std::nullopt;
    // Glued to something before it: "1.1/2", "x1/2", "-1/2".
    if (pos > 0 && std::string_view(".,-/$#").find(text[pos - 1]) != std::string_view::npos)
      return std::nullopt;
    bool const whole = m[1].matched;
    if (!whole && !m[4].matched)
      return std::nullopt;
    if (whole)
    {
      std::string out = m.str(1) + " and " + w->second.first;
      if (m[4].matched)
        out += " " + std::string(units.at(m.str(4)).many);
      return out;
    }
    std::string const one = units.at(m.str(4)).one;
    std::string const before = w->second.second;
    if (before == w->second.first)  // "a half mile", "a quarter mile": what drivers say.
      return before + " " + one;
    return before + (one == "hour" ? " an " : " a ") + one;
  });
}

static std::string MonthName(int month)
{
  static char const * const kMonths[] = {"",       "January",   "February", "March",    "April",
                                         "May",    "June",      "July",     "August",   "September",
                                         "October", "November", "December"};
  return month >= 1 && month <= 12 ? kMonths[month] : "";
}

/// "11/6/2026", "1/2/26", "2026-11-06", "31/12/1999", "Nov. 6", "November 6, 2026", "on 12/25":
/// said the way people say dates ("November sixth, twenty twenty six"; day-first when the first
/// number can't be a month: "the thirty first of December"). Fractions ("3/4 mile", "1/2"), versions
/// ("1/2/3"), counts after a month-ish word ("May 3 cars") and invalid days stay for the later passes.
std::string Dates(std::string const & text)
{
  // A unit or count after the number makes it a quantity, not a day.
  static std::set<std::string> const counted = {
      "mi", "mile", "miles", "ft", "feet", "foot", "km", "kilometers", "m", "meters", "yd", "yards", "mph",
      "kph", "min", "mins", "minute", "minutes", "hr", "hrs", "hour", "hours", "sec", "secs", "seconds",
      "gal", "gallon", "gallons", "inch", "inches", "cup", "cups", "tank", "people", "cars", "times", "of",
      "lb", "lbs", "pounds", "kg", "points", "lanes", "exits", "blocks", "stops", "days", "weeks", "years"};
  // Words that put a month/day without a year after them: "on 12/25", "Fri 11/6", "due 3/1".
  static std::set<std::string> const dateWord = {
      "on", "by", "until", "till", "due", "before", "after", "since", "from", "through", "thru", "dated",
      "mon", "tue", "tues", "wed", "thu", "thur", "thurs", "fri", "sat", "sun", "monday", "tuesday",
      "wednesday", "thursday", "friday", "saturday", "sunday"};
  auto const yearWords = [](std::string const & y) {
    auto year = ToNumber(y);
    if (y.size() == 2)
      year += year < 50 ? 2000 : 1900;
    return YearWords(year, false);
  };
  auto const say = [&](int month, int day, std::string const & year, bool dayFirst) {
    std::string out = dayFirst ? "the " + Ordinal(day) + " of " + MonthName(month)
                               : MonthName(month) + " " + Ordinal(day);
    if (!year.empty())
      out += ", " + yearWords(year);
    return out;
  };
  static int const kDays[] = {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  auto const valid = [](long long month, long long day) {
    return month >= 1 && month <= 12 && day >= 1 && day <= kDays[month];
  };
  std::string s = text;
  if (HasAny(s, "/"))
  {
    // Not glued to more digits, slashes or letters on either side ("1/2/3/4", "A1/2/2026").
    static std::regex const slashed("(\\d{1,2})/(\\d{1,2})(?:/(\\d{4}|\\d{2}))?(?![\\d/A-Za-z])");
    s = Replace(s, slashed, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
      if (pos > 0 && (Digit(s[pos - 1]) || IsAsciiLetter(s[pos - 1]) ||
                      std::string_view("/-.,$#").find(s[pos - 1]) != std::string_view::npos))
        return std::nullopt;
      auto const after = pos + static_cast<size_t>(m.length(0));
      // "11/6. 5 more" is fine; "11/6.5" is a number.
      if (after + 1 < s.size() && s[after] == '.' && Digit(s[after + 1]))
        return std::nullopt;
      auto const a = ToNumber(m.str(1)), b = ToNumber(m.str(2));
      std::string const year = m[3].matched ? m.str(3) : "";
      if (counted.count(WordAfter(s, after)))
        return std::nullopt;
      if (year.empty())
      {
        size_t wordStart = 0;
        if (!dateWord.count(WordBefore(s, pos, wordStart)))
          return std::nullopt;
        // "by 1/2", "on 3/4": the common fractions stay fractions without a year.
        if (a < b && b <= 4)
          return std::nullopt;
      }
      if (valid(a, b))
        return say(static_cast<int>(a), static_cast<int>(b), year, false);
      if (valid(b, a) && !year.empty())
        return say(static_cast<int>(b), static_cast<int>(a), year, true);
      return std::nullopt;
    });
  }
  if (HasAny(s, "-"))
  {
    static std::regex const iso("\\b(1\\d{3}|20\\d{2})-(\\d{2})-(\\d{2})(?![\\d\\-A-Za-z])");
    s = Replace(s, iso, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
      if (pos > 0 && (s[pos - 1] == '-' || s[pos - 1] == '/'))
        return std::nullopt;
      auto const month = ToNumber(m.str(2)), day = ToNumber(m.str(3));
      if (!valid(month, day))
        return std::nullopt;
      return say(static_cast<int>(month), static_cast<int>(day), m.str(1), false);
    });
  }
  // "Nov. 6", "November 6th, 2026": the day as an ordinal and the year said with it. Capital month
  // only ("may 5" is the verb), and not before a count ("May 3 cars").
  static std::regex const named(
      "\\b(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Sept|Oct|Nov|Dec)(uary|ruary|ch|il|e|y|ust|tember|ober|ember)?"
      "(\\.)? (\\d{1,2})(?:st|nd|rd|th)?(?:, (1\\d{3}|20\\d{2}))?(?![\\d/:A-Za-z])");
  s = Replace(s, named, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    static std::map<std::string, int> const kMonth = {
        {"Jan", 1}, {"Feb", 2}, {"Mar", 3}, {"Apr", 4},  {"May", 5},  {"Jun", 6},  {"Jul", 7},
        {"Aug", 8}, {"Sep", 9}, {"Sept", 9}, {"Oct", 10}, {"Nov", 11}, {"Dec", 12}};
    int const month = kMonth.at(m.str(1));
    // Only the month's own ending: "Mayday 5" or "Junee 5" aren't months.
    if (m[2].matched && MonthName(month) != m.str(1) + m.str(2))
      return std::nullopt;
    // "May. 5" and "March. 5" have no abbreviation period.
    if (m[3].matched && m[2].matched)
      return std::nullopt;
    auto const after = pos + static_cast<size_t>(m.length(0));
    if (after + 1 < s.size() && (s[after] == '.' || s[after] == ',') && Digit(s[after + 1]))
      return std::nullopt;
    auto const day = ToNumber(m.str(4));
    if (!valid(month, day) || (!m[5].matched && counted.count(WordAfter(s, after))))
      return std::nullopt;
    return say(month, static_cast<int>(day), m[5].matched ? m.str(5) : "", false);
  });
  // "Oct 2026", "Jan. 2027", "Sept 2025": an abbreviated month before a year is the month's name; the
  // year pass reads the year. Capital only, and not glued to more digits ("Oct 20261", "Mar 2026:30").
  static std::regex const monthYear(
      "\\b(Jan|Feb|Mar|Apr|Jun|Jul|Aug|Sep|Sept|Oct|Nov|Dec)\\.? (1\\d{3}|20\\d{2})(?![\\d/:A-Za-z]|[.,]\\d)");
  s = Replace(s, monthYear, [&](std::smatch const & m, size_t) -> std::optional<std::string> {
    static std::map<std::string, int> const kMonth = {
        {"Jan", 1}, {"Feb", 2}, {"Mar", 3}, {"Apr", 4},   {"Jun", 6},  {"Jul", 7},
        {"Aug", 8}, {"Sep", 9}, {"Sept", 9}, {"Oct", 10}, {"Nov", 11}, {"Dec", 12}};
    return MonthName(kMonth.at(m.str(1))) + " " + m.str(2);
  });
  return s;
}

std::string Weekdays(std::string const & text)
{
  if (!HasAny(text, "MTWFS"))
    return text;
  // One day, or a run of days joined by dashes, slashes, commas, "&", "and" or "or". Capital only; never part of a
  // longer word ("Monday", "Sunday's", "Friar", "Wedge" stay).
#define BACKSEAT_DAY "(?:Mon|Tues|Tue|Wed|Thurs|Thur|Thu|Fri|Sat|Sun)(?![A-Za-z']|\xE2\x80\x99)\\.?"
#define BACKSEAT_DAY_SEP "(?:\\s*(?:-|\xE2\x80\x93|/|&|,)\\s*| (?:and|or) )"
  static std::regex const run("\\b" BACKSEAT_DAY "(?:" BACKSEAT_DAY_SEP BACKSEAT_DAY ")*");
  static std::regex const part("(Mon|Tues|Tue|Wed|Thurs|Thur|Thu|Fri|Sat|Sun)\\.?|" BACKSEAT_DAY_SEP);
#undef BACKSEAT_DAY
#undef BACKSEAT_DAY_SEP
  static std::map<std::string, std::string> const kDay = {
      {"Mon", "Monday"},     {"Tue", "Tuesday"},  {"Tues", "Tuesday"},  {"Wed", "Wednesday"},
      {"Thu", "Thursday"},   {"Thur", "Thursday"}, {"Thurs", "Thursday"}, {"Fri", "Friday"},
      {"Sat", "Saturday"},   {"Sun", "Sunday"}};
  std::string const & s = text;
  return Replace(s, run, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    std::string const all = m.str(0);
    std::vector<std::string> days, seps;
    for (std::sregex_iterator it(all.begin(), all.end(), part), end; it != end; ++it)
    {
      if ((*it)[1].matched)
        days.push_back(kDay.at((*it).str(1)));
      else
      {
        std::string sep = (*it).str(0);
        sep.erase(std::remove_if(sep.begin(), sep.end(), [](char c) { return c == ' ' || c == '\t'; }), sep.end());
        seps.push_back(sep);
      }
    }
    if (days.empty() || seps.size() + 1 != days.size())
      return std::nullopt;
    auto const after = pos + static_cast<size_t>(m.length(0));
    if (days.size() == 1)
    {
      // "Sat" and "Sun" are words too ("Sat on it", "Sun's down", "Sun. The glare"): only a date after
      // them makes them days ("Sun 11/9" is read "Sun November ninth" by then, "Sat, Nov. 8" too).
      if (days[0] == "Saturday" || days[0] == "Sunday")
      {
        size_t at = after;
        if (all.back() == '.')
          return std::nullopt;
        if (at < s.size() && s[at] == ',')
          ++at;
        if (at >= s.size() || s[at] != ' ')
          return std::nullopt;
        ++at;
        bool dated = at < s.size() && Digit(s[at]);
        for (int month = 1; month <= 12 && !dated; ++month)
          dated = s.compare(at, MonthName(month).size(), MonthName(month)) == 0;
        if (!dated)
          return std::nullopt;
      }
    }
    std::string out = days[0];
    // The last list joint reads "and" ("Mon/Wed/Fri" is "Monday, Wednesday and Friday") unless a
    // range follows it ("Thu, Fri-Sun" is "Thursday, Friday to Sunday").
    auto const isRange = [](std::string const & sep) { return sep == "-" || sep == "\xE2\x80\x93"; };
    size_t lastList = seps.size();
    if (!seps.empty() && !isRange(seps.back()))
      lastList = seps.size() - 1;
    for (size_t i = 0; i < seps.size(); ++i)
    {
      if (isRange(seps[i]))
        out += " to ";
      else if (seps[i] == "or")
        out += " or ";
      else if (seps[i] == "&" || seps[i] == "and" || i == lastList)
        out += " and ";
      else
        out += ", ";
      out += days[i + 1];
    }
    // "Closed Fri." keeps its full stop; "Fri. 11/7" and "Wed. at noon" lose the abbreviation's.
    if (all.back() == '.' && (after >= s.size() || (s[after] == ' ' && after + 1 < s.size() &&
                                                       std::isupper(static_cast<unsigned char>(s[after + 1])))))
      out += '.';
    return out;
  });
}

namespace
{
// Web addresses and e-mail, the same way BookLyre reads them (text/spoken_text.web_addresses, golden
// cases shared in backseat_tests/spoken_web.json). Hand-scanned rather than std::regex: libstdc++'s
// regex recurses per character, and a 20 KB run of dots and letters must not blow the stack.
bool WebWordByte(char c)
{
  auto const u = static_cast<unsigned char>(c);
  return std::isalnum(u) != 0 || c == '_' || u >= 0x80;
}

bool LabelByte(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-'; }

bool IsWebTld(std::string_view t)
{
  static char const * const kTlds[] = {"com", "org", "net", "edu", "gov", "mil", "int", "info", "biz",
                                       "io",  "uk",  "ca",  "au",  "nz",  "ie",  "de",  "fr",   "es",
                                       "it",  "nl",  "eu",  "in",  "jp",  "app", "dev"};
  bool const lower = std::all_of(t.begin(), t.end(), [](char c) { return c >= 'a' && c <= 'z'; });
  bool const upper = std::all_of(t.begin(), t.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
  if (t.empty() || !(lower || upper))
    return false;
  for (auto const * tld : kTlds)
  {
    std::string_view const want(tld);
    if (want.size() == t.size() &&
        std::equal(want.begin(), want.end(), t.begin(), [](char a, char b) { return a == (b | 0x20); }))
      return true;
  }
  return false;
}

// Dot-separated host labels from `at`: each starts and ends with a letter or digit, at most 63
// bytes. Stops before a dot not followed by one. `end` is just past the last label taken.
std::vector<std::string_view> HostLabels(std::string_view s, size_t at, size_t & end)
{
  std::vector<std::string_view> labels;
  end = at;
  size_t i = at;
  while (i < s.size() && std::isalnum(static_cast<unsigned char>(s[i])))
  {
    size_t j = i;
    while (j < s.size() && LabelByte(s[j]))
      ++j;
    size_t k = j;
    while (k > i && s[k - 1] == '-')
      --k;
    if (k - i > 63)
      break;
    labels.push_back(s.substr(i, k - i));
    end = k;
    if (k != j || k + 1 >= s.size() || s[k] != '.' || !std::isalnum(static_cast<unsigned char>(s[k + 1])))
      break;
    i = k + 1;
  }
  return labels;
}

// A host the voice can say without a scheme in front: two labels or more, the last one a common
// top-level domain all in one case, and no label byte or "_" right after it.
bool HostWithTld(std::string_view s, std::vector<std::string_view> const & labels, size_t end)
{
  if (labels.size() < 2 || !IsWebTld(labels.back()))
    return false;
  if (end < s.size() && (LabelByte(s[end]) || s[end] == '_'))
    return false;
  // "a-b.com-": the last label lost a trailing hyphen, so it isn't the whole word.
  return labels.back().data() + labels.back().size() == s.data() + end;
}

std::string SpokenHost(std::vector<std::string_view> const & labels)
{
  std::string out;
  for (auto const & label : labels)
  {
    if (!out.empty())
      out += " dot ";
    for (char c : label)
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool StartsWithNoCase(std::string_view s, size_t at, std::string_view word)
{
  if (at + word.size() > s.size())
    return false;
  for (size_t i = 0; i < word.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(s[at + i])) != word[i])
      return false;
  return true;
}

// "www." or "www2." at `at`: returns the length, or 0.
size_t Www(std::string_view s, size_t at)
{
  if (!StartsWithNoCase(s, at, "www"))
    return 0;
  size_t i = at + 3;
  if (i < s.size() && Digit(s[i]))
    ++i;
  return i < s.size() && s[i] == '.' ? i + 1 - at : 0;
}

bool TrailingPunct(std::string_view s, size_t end, size_t & width)
{
  static std::string_view const kAscii = ".,;:!?)]}'\"";
  if (end >= 1 && kAscii.find(s[end - 1]) != std::string_view::npos)
  {
    width = 1;
    return true;
  }
  // ’ and ” in UTF-8.
  if (end >= 3 && s[end - 3] == '\xE2' && s[end - 2] == '\x80' && (s[end - 1] == '\x99' || s[end - 1] == '\x9D'))
  {
    width = 3;
    return true;
  }
  return false;
}

// An e-mail address starting at `at`, or nullopt. `end` is just past it.
std::optional<std::string> Email(std::string_view s, size_t at, size_t & end)
{
  size_t j = at;
  while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == '_' ||
                          s[j] == '+' || s[j] == '-'))
    ++j;
  if (j == at || j - at > 64 || j >= s.size() || s[j] != '@' || !std::isalnum(static_cast<unsigned char>(s[at])) ||
      !std::isalnum(static_cast<unsigned char>(s[j - 1])))
    return std::nullopt;
  size_t hostEnd = 0;
  auto const labels = HostLabels(s, j + 1, hostEnd);
  if (!HostWithTld(s, labels, hostEnd))
    return std::nullopt;
  std::string local;
  for (size_t i = at; i < j; ++i)
  {
    char const c = s[i];
    if (c == '.')
      local += " dot ";
    else if (c == '_')
      local += " underscore ";
    else if (c == '+')
      local += " plus ";
    else
      local += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  end = hostEnd;
  return local + " at " + SpokenHost(labels);
}

// A web address starting at `at`, or nullopt: scheme or "www." or a host ending in a known
// top-level domain, then an optional port and path. The path is dropped but sentence punctuation
// it swallowed comes back. `end` is just past what was read.
std::optional<std::string> Url(std::string_view s, size_t at, size_t & end)
{
  size_t i = at;
  bool lead = false;
  for (std::string_view scheme : {"https://", "http://", "ftp://"})
  {
    if (StartsWithNoCase(s, i, scheme))
    {
      i += scheme.size();
      i += Www(s, i);
      lead = true;
      break;
    }
  }
  bool www = false;
  if (!lead)
  {
    if (size_t const w = Www(s, i))
    {
      i += w;
      www = true;
    }
  }
  size_t hostEnd = 0;
  auto labels = HostLabels(s, i, hostEnd);
  if (www && labels.size() < 2)
  {
    // "www.com" isn't www plus a host; it may still be a host itself.
    www = false;
    i = at;
    labels = HostLabels(s, i, hostEnd);
  }
  if (labels.empty())
    return std::nullopt;
  if (!lead && !www)
  {
    if (!HostWithTld(s, labels, hostEnd))
      return std::nullopt;
  }
  size_t k = hostEnd;
  if ((lead || www) && k + 1 < s.size() && s[k] == ':' && Digit(s[k + 1]))
  {
    size_t p = k + 1;
    while (p < s.size() && p - k <= 5 && Digit(s[p]))
      ++p;
    k = p;
  }
  std::string trailing;
  if (k < s.size() && (s[k] == '/' || s[k] == '?' || s[k] == '#'))
  {
    size_t p = k;
    while (p < s.size() && !Space(s[p]) && s[p] != '<' && s[p] != '>' && s[p] != '"')
      ++p;
    size_t t = p, width = 0;
    while (t > k && TrailingPunct(s, t, width))
      t -= width;
    trailing = std::string(s.substr(t, p - t));
    // "(see en.wikipedia.org/wiki/Foo_(bar))": the first ")" closes the path's own "(".
    if (!trailing.empty() && trailing[0] == ')' && s.substr(at, p - at).find('(') != std::string_view::npos)
      trailing.erase(0, 1);
    k = p;
  }
  end = k;
  return SpokenHost(labels) + trailing;
}
}  // namespace

std::string WebAddresses(std::string const & text)
{
  // Fast path: an address needs "://", or a dot between two letters or digits.
  bool maybe = text.find("://") != std::string::npos;
  for (size_t i = 1; !maybe && i + 1 < text.size(); ++i)
    maybe = text[i] == '.' && std::isalnum(static_cast<unsigned char>(text[i - 1])) &&
            std::isalnum(static_cast<unsigned char>(text[i + 1]));
  if (!maybe)
    return text;
  std::string_view const s(text);
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < s.size())
  {
    char const before = i == 0 ? ' ' : s[i - 1];
    bool const wordBefore = i > 0 && WebWordByte(before);
    size_t end = 0;
    if (std::isalnum(static_cast<unsigned char>(s[i])) && !wordBefore)
    {
      std::optional<std::string> said;
      if (before != '@' && before != '.' && before != '+' && before != '-')
        said = Email(s, i, end);
      if (!said && before != '@' && before != '.' && before != '/' && before != '-')
        said = Url(s, i, end);
      if (said)
      {
        out += *said;
        i = end;
        continue;
      }
    }
    out += s[i];
    ++i;
  }
  return out;
}

std::string ForSpeech(std::string const & text)
{
  std::string s = ReplaceAll(text, "\xC2\xA0", " ");
  // Web addresses and e-mail first, before the symbol, date and street passes take them apart:
  // "www.heb.com" is "heb dot com", "jo@example.com" "jo at example dot com".
  s = WebAddresses(s);
  // Foreign phrases first, before anything reads their letters as English abbreviations.
  s = Foreign(s);
  // Cyrillic map names and quotes the English voice would skip: "улица Ленина" is "ulitsa Lenina".
  s = Cyrillic(s);
  // Kana by Hepburn, the same way: "ラーメン" is "ramen", "さくら通り" "sakura 通 ri".
  s = Kana(s);
  // Symbols the voice would skip or spell: "½" is "1/2", "#5" "number 5", "/gal" "per gallon".
  s = Symbols(s);
  // Dates before ranges and fractions take their parts: "11/6/2026" is "November sixth, twenty twenty
  // six", "2026-11-06" too, "Nov. 6" "November sixth".
  if (HasDigit(s))
    s = Dates(s);
  // Day abbreviations after dates, so "Sun 11/9" sees the date: "Mon-Fri" is "Monday to Friday".
  s = Weekdays(s);
  // Ranges before dashes become pauses: "3–5" is "3 to 5", not "3, 5".
  if (HasDigit(s))
    s = Ranges(s);
  // Clock times before the dash pass can split "9am-5pm".
  s = Clock(s);
  // Years as years: "in 1868" is "in eighteen sixty eight", "the 1990s" "the nineteen nineties".
  if (HasDigit(s))
    s = Years(s);
  // Two-digit decades: "the 90s" is "the nineties".
  if (HasDigit(s))
    s = Decades(s);
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
  // Places said the local way: "Burnet Rd" is BURN-it, "Humble, TX" UM-bul.
  s = LocalPlaces(s);
  s = PickSenses(s);
  // Regnal numbers after a name: "King George VI Ave" is "King George the sixth Ave".
  s = Regnal(s);
  // Saint, Doctor, Fort, Junior before the street pass can take them for street types.
  s = Titles(s);

  // "Main St", "Elm Dr." after a word, at the end or before punctuation, a lowercase word or a
  // compass point ("Main St NW"). Not "Dr Smith" or "St Louis". A period after it stays when it
  // may end the sentence ("Elm Ave. Then right.", "onto Oak Rd."); it goes when the sentence plainly
  // carries on: ",;:" or a lowercase word next ("Oak Dr. and then left", "Main St., then right"), a
  // compass point, or a lone letter or number naming the street ("on Ave. B now"; not "A"/"I",
  // which start sentences).
  static std::regex const street(
      "(St|Ave|Blvd|Rd|Dr|Ln|Ct|Pkwy|Hwy|Pl|Cir|Ter|Trl|Expy|Fwy|Sq|Cv|Xing|Ext)"
      "(?:\\.(?=[,;:]|\\s+(?:[a-z]|(?:NE|NW|SE|SW|N|S|E|W)\\b|(?:[B-HJ-Z]|\\d+)(?=$|[\\s,;:!?.)])))"
      "|(?=$|[,;:!?.)]|\\s+(?:[a-z]|Ext\\b|(?:NE|NW|SE|SW|N|S|E|W)\\b)))");
  s = Replace(s, street, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    if (pos < 2 || s[pos - 1] != ' ' || !IsAsciiLetter(s[pos - 2]))
      return std::nullopt;
    return Street(m.str(1));
  });
  // "Frontage Svc Rd" is a service road; "Ave B" and "Ln 5" name the street after its type.
  static std::regex const service("\\bSvc\\.? (?=(?:Rd|Road|Dr|Drive)\\b)");
  s = std::regex_replace(s, service, "Service ");
  static std::regex const typeFirst("(^|\\s)(Ave|Cir|Ln)\\.? (?=(?:[A-Z]|\\d+)(?:$|[\\s,;:!?.)]))");
  s = Replace(s, typeFirst, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    return m.str(1) + Street(m.str(2)) + " ";
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
    // "Avenue N" in Brooklyn is the letter, not north.
    size_t start = pos - 1;
    while (start > 0 && IsAsciiLetter(s[start - 1]))
      --start;
    auto const before = s.substr(start, pos - 1 - start);
    if (m.str(1).size() == 1 && (before == "Avenue" || before == "Lane" || before == "Circle"))
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
  // Fractions before a unit and after a whole number: "1/2 mi" is "a half mile", "1 1/2 mi" "1 and a
  // half miles".
  if (HasAny(s, "/"))
    s = FractionsWithUnits(s);
  // Units after a number ("1 mile", "3 miles").
  static std::regex const unit("(\\d) ?(ft|mi|mph|km|m|yd|hrs|hr|mins|min|secs|sec)\\b");
  s = Replace(s, unit, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    size_t start = pos;
    while (start > 0 && (std::isdigit(static_cast<unsigned char>(s[start - 1])) || s[start - 1] == ',' ||
                         s[start - 1] == '.'))
      --start;
    bool const one = s.substr(start, pos + 1 - start) == "1";
    static std::map<std::string, std::pair<char const *, char const *>> const words = {
        {"ft", {"foot", "feet"}},           {"mi", {"mile", "miles"}},   {"mph", {"mile an hour", "miles an hour"}},
        {"km", {"kilometer", "kilometers"}}, {"m", {"meter", "meters"}}, {"yd", {"yard", "yards"}},
        {"hr", {"hour", "hours"}},          {"hrs", {"hour", "hours"}},  {"min", {"minute", "minutes"}},
        {"mins", {"minute", "minutes"}},    {"sec", {"second", "seconds"}}, {"secs", {"second", "seconds"}}};
    auto const & w = words.at(m.str(2));
    return m.str(1) + " " + (one ? w.first : w.second);
  });
  static std::regex const fraction("(\\d+)/(\\d+)");
  if (HasAny(s, "/"))
    s = Replace(s, fraction, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
    auto const a = m.str(1), b = m.str(2);
    // "open 24/7" is "twenty four seven"; "24/7/365" is left whole.
    auto const after = pos + static_cast<size_t>(m.length(0));
    if (a == "24" && b == "7" && (pos == 0 || !Digit(s[pos - 1])) && (after >= s.size() || s[after] != '/'))
      return std::string("twenty four seven");
    if (a == "1" && b == "2")
      return std::string("a half");
    if (a == "1" && b == "4")
      return std::string("a quarter");
    if (a == "3" && b == "4")
      return std::string("three quarters");
    if (a == "1" && b == "3")
      return std::string("a third");
    if (a == "2" && b == "3")
      return std::string("two thirds");
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
    for (auto const & p : kLocalPlaces)
      if (auto const ph = RespellingPhonemes(p.said); ph && seen.insert(StandIn(std::string(p.said))).second)
        out.emplace_back(StandIn(std::string(p.said)), *ph);
    // Names and places last, never replacing a word or stand-in above.
    std::set<std::string> taken;
    for (auto const & [w, ph] : out)
      taken.insert(w);
    for (auto const & n : kNames)
      if (taken.insert(n.word).second)
        out.emplace_back(n.word, n.phonemes);
    for (auto const & p : kPlaces)
    {
      std::string said = p.said;
      std::optional<std::string> ph;
      if (said.size() > 2 && said.front() == '/' && said.back() == '/')
        ph = said.substr(1, said.size() - 2);
      else
        ph = ForeignWordPhonemes(said);
      if (ph && taken.insert(p.word).second)
        out.emplace_back(p.word, *ph);
    }
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

namespace detail
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

bool Letter(char32_t c)
{
  return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
}
}  // namespace detail

namespace
{
bool Mark(char32_t c)
{
  return c == U'\'' || c == U'\u00B4' || c == U'\u02C8';
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
