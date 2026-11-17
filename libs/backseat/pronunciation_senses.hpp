#pragma once

// Internal to libs/backseat: the heteronym sense table and its context helpers, used by PickSenses()
// and the passes in pronunciation.cpp. Included only by pronunciation.cpp (internal linkage); moved
// out of it unchanged (loop B103). Not public API; hosts include pronunciation.hpp.

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>

namespace backseat::pronunciation
{
namespace
{
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
}  // namespace
}  // namespace backseat::pronunciation
