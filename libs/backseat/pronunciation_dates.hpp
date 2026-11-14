#pragma once

// Internal to libs/backseat: clock times, years, decades, dates and weekdays read aloud (Clock,
// YearWords, Years, Decades, Dates, Weekdays and MonthName), used by ForSpeech() in pronunciation.cpp.
// Included only by pronunciation.cpp; moved out of it unchanged (loop B136). Not public API beyond
// what pronunciation.hpp declares; hosts include pronunciation.hpp.

#include "backseat/pronunciation.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>

namespace backseat::pronunciation
{
namespace
{
// Text and number helpers these passes share with the rest of pronunciation.cpp, defined there (Lower,
// Replace, Space and IsAsciiLetter come from pronunciation_streets.hpp's declarations).
bool Digit(char ch);
std::string Ordinal(long long n);
long long ToNumber(std::string const & digits);
std::string WordBefore(std::string const & s, size_t pos, size_t & start);
std::string WordAfter(std::string const & s, size_t pos);
}  // namespace

// Defined later in pronunciation.cpp: the passes below skip their regex scans on lines with no digit.
bool HasDigit(std::string const & s);
bool HasAny(std::string const & s, std::string_view bytes);

std::string Clock(std::string const & text)
{
  // "9am-5pm", "9-5pm", "9:30 a.m.–noon": a dash next to a clock time is "to".
  static std::regex const span(
      "(\\b\\d{1,2}(?::[0-5]\\d)?(?: ?[AaPp]\\.? ?[Mm]\\.?)?) ?(?:-|\xE2\x80\x93|\xE2\x80\x94) ?"
      "(?=\\d{1,2}(?::[0-5]\\d)? ?[AaPp]\\.? ?[Mm](?![A-Za-z])|noon\\b|midnight\\b)");
  if (!HasDigit(text) && text.find("a.") == std::string::npos && text.find("p.") == std::string::npos &&
      text.find("A.") == std::string::npos && text.find("P.") == std::string::npos &&
      text.find("AM") == std::string::npos && text.find("PM") == std::string::npos)
    return text;
  auto const s = !HasDigit(text) ? text : Replace(text, span, [&](std::smatch const & m, size_t pos) -> std::optional<std::string> {
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
  if (!HasLetterWord(text, {"Mon", "Tues", "Tue", "Wed", "Thurs", "Thur", "Thu", "Fri", "Sat", "Sun"}))
    return text;
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
}  // namespace backseat::pronunciation
