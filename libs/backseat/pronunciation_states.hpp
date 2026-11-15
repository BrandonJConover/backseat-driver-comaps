#pragma once

// Internal to libs/backseat: US state codes (StateCodes, shared with the route pass Roads()) and
// "City, ST ZIP" read as a place (CityStates), used by ForSpeech() in pronunciation.cpp. Included only
// by pronunciation.cpp after pronunciation_phones.hpp (internal linkage). Not public API.
//
// Without it "Austin, TX." was "Austin, tex", "Portland, OR now" "Portland, or now" and "Austin, TX
// 78701" "seventy eight thousand seven hundred one" (loop B169, BI-149). Golden cases in
// backseat_tests/spoken_city_states.json, shared with BookLyre (CROSS).
//   - A two-letter code right after "Capitalised, " is the state's name: "Springfield, IL" ->
//     "Springfield, Illinois"; DC (or D.C.) is "D C".
//   - A ZIP (and ZIP+4) after the state, code or name, is digit by digit: "TX 78701-1234" ->
//     "Texas seven eight seven oh one, one two three four".
//   - Codes that are also words (OR, IN, ME, OK, HI, OH, ...) only when nothing conversational is
//     around them: not after "Yes," / "So," / "Hey," and the like, never before "?" or "!", and the
//     interjection-like ones (OK, OH, HI, ME) only before a ZIP, a period or the end, after a name of four
//     or more letters ("Tulsa, OK." yes, "Jo, OK." no). The word before must have a lowercase letter, so
//     a list of codes ("TX, CA, NY") stays letters.
// The output has no digits and no codes, so a second pass leaves it alone (idempotent).

#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace backseat::pronunciation
{
namespace
{
/// Two-letter US state codes: the name, and whether the code is also an English word (those need a
/// hyphen in a route ref, "IN-46", and more context after a city).
std::map<std::string, std::pair<char const *, bool>> const & StateCodes()
{
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
  return states;
}

namespace city_states
{
// Capitalised words that start a sentence or a reply, not a city: "Yes, OK.", "So, IN the car".
constexpr std::string_view kNotCities[] = {
    "yes",   "yeah",  "yep",    "no",     "nope",   "fine",   "so",     "hey",    "well",    "oh",
    "okay",  "ok",    "sure",   "right",  "now",    "then",   "look",   "listen", "sorry",   "please",
    "thanks", "wait", "good",   "great",  "alright", "and",   "but",    "or",     "also",    "here",
    "there", "again", "honestly", "seriously", "really", "anyway", "wow", "whoa",  "ah",      "uh",
    "um",    "hmm",   "hi",     "hello",  "first",  "second", "next",   "finally", "still",  "yet",
    "plus",  "i",     "me",     "you",    "go",     "stop",   "turn",   "left",   "hold",    "ugh",
    "man",   "dude",  "buddy",  "friend", "folks",  "guys",   "ma'am",  "sir",    "kid",     "honey"};
// Codes that read as interjections or pronouns; they need a ZIP, a period or the end after them.
constexpr std::string_view kShy[] = {"OK", "OH", "HI", "ME"};

bool Upper(char c) { return c >= 'A' && c <= 'Z'; }
bool Lowercase(char c) { return c >= 'a' && c <= 'z'; }
bool Digit(char c) { return c >= '0' && c <= '9'; }
bool Alnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

template <size_t N>
bool Has(std::string_view w, std::string_view const (&list)[N])
{
  for (auto const & x : list)
    if (x == w)
      return true;
  return false;
}

/// The city word ending just before the comma at `comma`, or empty when it isn't one.
std::string_view CityBefore(std::string const & s, size_t comma)
{
  size_t e = comma;
  if (e > 0 && s[e - 1] == '.')  // "St. Paul, MN" is fine; "Ft., TX" too
    --e;
  size_t b = e;
  while (b > 0 && (std::isalpha(static_cast<unsigned char>(s[b - 1])) || s[b - 1] == '\'' || s[b - 1] == '-'))
    --b;
  if (b == e || !Upper(s[b]) || (b > 0 && Alnum(s[b - 1])))
    return {};
  auto const w = std::string_view(s).substr(b, e - b);
  bool lower = false;
  for (char c : w)
    lower = lower || Lowercase(c);
  if (!lower)
    return {};
  std::string folded(w);
  for (auto & c : folded)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (Has(folded, kNotCities))
    return {};
  return w;
}

/// A ZIP or ZIP+4 at j (after one space), said digit by digit; `end` is where it stops.
bool Zip(std::string const & s, size_t j, std::string & said, size_t & end)
{
  if (j + 6 > s.size() || s[j] != ' ')
    return false;
  size_t const z = j + 1;
  for (size_t k = 0; k < 5; ++k)
    if (!Digit(s[z + k]))
      return false;
  size_t e = z + 5;
  std::string plus;
  if (e + 5 <= s.size() && s[e] == '-' && Digit(s[e + 1]) && Digit(s[e + 2]) && Digit(s[e + 3]) && Digit(s[e + 4]))
  {
    plus = s.substr(e + 1, 4);
    e += 5;
  }
  if (e < s.size() && (Alnum(s[e]) || ((s[e] == '-' || s[e] == '.' || s[e] == ',') && e + 1 < s.size() && Digit(s[e + 1]))))
    return false;
  said = " " + phones::Spell(std::string_view(s).substr(z, 5));
  if (!plus.empty())
    said += ", " + phones::Spell(plus);
  end = e;
  return true;
}
}  // namespace city_states

// "Austin, TX 78701" -> "Austin, Texas seven eight seven oh one", "Portland, OR now" -> "Portland, Oregon
// now", "Washington, DC" -> "Washington, D C". Conversation ("Yes, OK.", "So, IN the car") stays.
std::string CityStates(std::string const & s)
{
  using namespace city_states;
  if (s.find(", ") == std::string::npos)
    return s;
  std::string out;
  size_t copied = 0;
  for (size_t p = s.find(", "); p != std::string::npos; p = s.find(", ", p + 1))
  {
    auto const city = CityBefore(s, p);
    if (city.empty())
      continue;
    size_t const q = p + 2;
    std::string name;
    size_t after = 0;
    bool wordLike = false, shy = false;
    if (q + 4 <= s.size() && s.compare(q, 4, "D.C.") == 0 && (q + 4 == s.size() || !Alnum(s[q + 4])))
    {
      name = "D C";
      after = q + 4;
      // "D.C." keeps its last period when it ends the sentence ("Washington, D C."); before a ZIP or a
      // lowercase word the dot goes.
      bool const ends = after == s.size() || (s[after] == ' ' && after + 1 < s.size() && Upper(s[after + 1]));
      if (ends)
        after = q + 3;
    }
    else if (q + 2 <= s.size() && Upper(s[q]) && Upper(s[q + 1]) && (q + 2 == s.size() || !Alnum(s[q + 2])))
    {
      auto const code = s.substr(q, 2);
      if (code == "DC")
        name = "D C";
      else if (auto const it = StateCodes().find(code); it != StateCodes().end())
      {
        name = it->second.first;
        wordLike = it->second.second;
        shy = Has(code, kShy);
      }
      else
        continue;
      after = q + 2;
      // "TX-71" after a city is a road; "IN." with a letter after is an initialism.
      if (after < s.size() && (s[after] == '-' || s[after] == '\'' || s[after] == '&' || s[after] == '/'))
        continue;
    }
    else
    {
      // A state spelled out still takes a ZIP: "Austin, Texas 78701".
      for (auto const & [code, st] : StateCodes())
      {
        std::string_view const full = st.first;
        if (s.compare(q, full.size(), full) == 0)
        {
          std::string zipSaid;
          size_t zipEnd = 0;
          if (Zip(s, q + full.size(), zipSaid, zipEnd))
          {
            out.append(s, copied, q + full.size() - copied);
            out += zipSaid;
            copied = zipEnd;
          }
          break;
        }
      }
      continue;
    }
    std::string zipSaid;
    size_t zipEnd = 0;
    bool const dotted = s.compare(q, 4, "D.C.") == 0;
    bool const zip = Zip(s, dotted ? q + 4 : after, zipSaid, zipEnd);
    if (wordLike && !zip)
    {
      char const next = after < s.size() ? s[after] : '\0';
      if (next == '?' || next == '!')
        continue;
      if (shy && !(next == '\0' || next == '.'))
        continue;
      if (shy && city.size() < 4)
        continue;
      // Another capitalised word after it: "Hi, OR Main St"? Too unsure.
      if (next == ' ' && after + 1 < s.size() && !Lowercase(s[after + 1]))
        continue;
    }
    out.append(s, copied, q - copied);
    out += name;
    if (zip)
    {
      out += zipSaid;
      copied = zipEnd;
    }
    else
      copied = after;
    p = copied > 0 ? copied - 1 : p;
  }
  if (copied == 0)
    return s;
  out.append(s, copied, std::string::npos);
  return out;
}
}  // namespace
}  // namespace backseat::pronunciation
