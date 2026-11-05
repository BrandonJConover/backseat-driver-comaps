#include "backseat/pronunciation.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <initializer_list>
#include <map>
#include <regex>
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
  std::string p2, p1, n1, n2;
};

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
         if (In(c.n1, {"foot", "feet", "footed", "pipe", "pipes", "paint", "poisoning", "weight", "weights",
                       "balloon", "pencil", "pencils", "apron", "shot"}))
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
         return In(c.n1, {"detail", "details", "amount", "amounts", "difference", "differences", "fraction",
                          "changes", "adjustments", "particles"})
                    ? "mynoot"
                    : nullptr;
       }},
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

// MARK: - Public

std::string SpellNumber(long long n)
{
  static char const * const ones[] = {"zero",    "one",     "two",       "three",    "four",     "five",    "six",
                                      "seven",   "eight",   "nine",      "ten",      "eleven",   "twelve",  "thirteen",
                                      "fourteen", "fifteen", "sixteen",  "seventeen", "eighteen", "nineteen"};
  static char const * const tens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
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
  std::string out;
  size_t last = 0;
  for (size_t k = 0; k < words.size(); ++k)
  {
    auto const rule = rules.find(words[k].lower);
    if (rule == rules.end())
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
        c.n2 = words[k + 2].lower;
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

std::string ForSpeech(std::string const & text)
{
  std::string s = ReplaceAll(text, "\xC2\xA0", " ");
  // Ranges before dashes become pauses: "3–5" is "3 to 5", not "3, 5".
  s = Ranges(s);
  // Temperatures and approximations: "-5°F" is "minus 5 degrees Fahrenheit", "~5" "about 5".
  static std::regex const degrees("(^|[\\s(])-(?=\\d+(?:\\.\\d+)? ?\xC2\xB0)");
  s = std::regex_replace(s, degrees, "$1minus ");
  static std::regex const degree("(\\d+(?:\\.\\d+)?) ?\xC2\xB0(?: ?([FC])\\b)?");
  s = Replace(s, degree, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    std::string out = m.str(1) + (m.str(1) == "1" ? " degree" : " degrees");
    if (m[2].matched)
      out += m.str(2) == "F" ? " Fahrenheit" : " Celsius";
    return out;
  });
  static std::regex const about("~ ?(?=\\d)");
  s = std::regex_replace(s, about, "about ");
  static std::regex const plusMinus("\xC2\xB1 ?(?=\\d)");
  s = std::regex_replace(s, plusMinus, "plus or minus ");
  static std::regex const dash("\\s*(?:\xE2\x80\x94|\xE2\x80\x93)\\s*");
  s = std::regex_replace(s, dash, ", ");
  s = ReplaceAll(s, " & ", " and ");

  s = PickSenses(s);

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
  // Highways: "I-25" is "I twenty five", "US-36" is "U S thirty six".
  static std::regex const highway("\\b(I|US|SR|CR)[- ](\\d+)\\b");
  s = Replace(s, highway, [](std::smatch const & m, size_t) -> std::optional<std::string> {
    static std::map<std::string, std::string> const names = {
        {"I", "I"}, {"US", "yooess"}, {"SR", "State Route"}, {"CR", "County Road"}};
    return names.at(m.str(1)) + " " + m.str(2);
  });
  // Money and percentages.
  static std::regex const money("\\$(\\d{1,3}(?:,\\d{3})+|\\d+)(?:\\.(\\d{2}))?\\b");
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

std::vector<std::pair<std::string, std::string>> const & Lexicon()
{
  static std::vector<std::pair<std::string, std::string>> const lexicon = [] {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto const & w : kWords)
    {
      std::string said = w.said;
      if (said.size() > 2 && said.front() == '/' && said.back() == '/')
        out.emplace_back(w.word, said.substr(1, said.size() - 2));
      else if (auto const ph = RespellingPhonemes(said))
        out.emplace_back(w.word, *ph);
    }
    return out;
  }();
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
        if (stressed)
          out += U'ˈ';
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
