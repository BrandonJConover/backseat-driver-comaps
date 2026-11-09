#include "backseat/lines.hpp"

#include "backseat/debug_log.hpp"

#include "backseat/lines_data.hpp"
#include "backseat/pronunciation.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>
#include <map>
#include <mutex>
#include <sstream>

namespace backseat
{
namespace
{
std::atomic<TravelMode> g_travelMode{TravelMode::Drive};
std::atomic<Housing> g_housing{Housing::Unknown};
std::atomic<bool> g_metric{false};
std::atomic<bool> g_leftHand{false};

/// `text` lowercased with every run of non-word characters turned into one space, padded with
/// spaces, so " word " finds whole words.
std::string LineWords(std::string const & text)
{
  std::string lower = " ";
  for (unsigned char const ch : text)
  {
    bool const inWord = std::isalnum(ch) || ch == '\'' || ch == '{' || ch == '}';
    lower += inWord ? static_cast<char>(std::tolower(ch)) : ' ';
  }
  lower += ' ';
  return lower;
}
}  // namespace

void SetLineTravelMode(TravelMode mode)
{
  g_travelMode = mode;
}

char const * HousingName(Housing housing)
{
  switch (housing)
  {
  case Housing::Unknown: return "";
  case Housing::House: return "house";
  case Housing::Apartment: return "apartment";
  case Housing::Streets: return "streets";
  }
  return "";
}

Housing HousingFrom(std::string const & name)
{
  for (Housing const h : {Housing::House, Housing::Apartment, Housing::Streets})
    if (name == HousingName(h))
      return h;
  return Housing::Unknown;
}

void SetLineHousing(Housing housing)
{
  g_housing = housing;
}

bool LineFitsHousing(std::string const & text, Housing housing)
{
  if (housing == Housing::Unknown || housing == Housing::House)
    return true;
  // A yard and a mortgage come with a house, not an apartment.
  static char const * const kHouseOwner[] = {"driveway", "driveways", "lawn", "lawns", "backyard", "yard", "mortgage",
                                             "homeowner", "homeowners", "hoa"};
  // And nothing that brings up a home at all for someone without one.
  static char const * const kHome[] = {"home", "homes", "house", "houses", "garage", "garages", "rent", "landlord",
                                       "apartment", "apartments", "couch", "basement", "bedroom", "kitchen", "roof"};
  std::string const lower = LineWords(text);
  auto const has = [&lower](char const * word) { return lower.find(" " + std::string(word) + " ") != std::string::npos; };
  if (std::any_of(std::begin(kHouseOwner), std::end(kHouseOwner), has))
    return false;
  return housing != Housing::Streets || std::none_of(std::begin(kHome), std::end(kHome), has);
}

char const * TravelModeName(TravelMode mode)
{
  switch (mode)
  {
  case TravelMode::Drive: return "drive";
  case TravelMode::Walk: return "walk";
  case TravelMode::Bike: return "bike";
  case TravelMode::OffRoad: return "offroad";
  case TravelMode::Hike: return "hike";
  }
  return "drive";
}

bool LineFitsMode(std::string const & text, TravelMode mode)
{
  if (mode == TravelMode::Drive)
    return true;
  // No line about these works without a car, on foot or on a bike.
  static char const * const kCar[] = {
      "drive", "drives", "driver", "drivers", "driver's", "driving", "drove", "driven", "car", "cars", "car's", "gas",
      "steering", "highway", "highways", "freeway", "freeways", "horn", "honk", "engine", "tank", "passenger", "toll",
      "tolls", "blinker", "windshield", "cupholder", "seatbelt", "trunk", "dashboard", "mirror", "mirrors", "{weight}",
      "speedometer", "parallel", "motorway", "backseat", "pedestrian", "cyclist"};
  // And these only work on foot.
  static char const * const kWheels[] = {"brake", "brakes", "braking", "pedal", "pedals", "wheel", "wheels", "lane", "lanes",
                                         "tire", "tires", "park", "parked", "parking", "traffic", "merge", "merging",
                                         "accelerate", "accelerating", "mph", "reverse", "bike", "bicycle", "ride"};
  std::string const lower = LineWords(text);
  auto const has = [&lower](char const * word) { return lower.find(" " + std::string(word) + " ") != std::string::npos; };
  if (mode == TravelMode::OffRoad)
  {
    static char const * const kPavement[] = {"highway", "highways", "freeway", "freeways", "motorway", "toll", "tolls",
                                             "lane", "lanes", "traffic", "merge", "merging", "parking", "parallel"};
    for (char const * phrase : {" speed limit ", " road trip "})
      if (lower.find(phrase) != std::string::npos)
        return false;
    return std::none_of(std::begin(kPavement), std::end(kPavement), has);
  }
  for (char const * phrase : {" speed limit ", " turn signal ", " turn signals ", " road trip ", " seat belt ", " pull over "})
    if (lower.find(phrase) != std::string::npos)
      return false;
  if (std::any_of(std::begin(kCar), std::end(kCar), has))
    return false;
  if (mode == TravelMode::Bike)
    return true;
  if (std::any_of(std::begin(kWheels), std::end(kWheels), has))
    return false;
  // A trail has no streets, crosswalks or stoplights either.
  static char const * const kStreets[] = {"street", "streets", "sidewalk", "sidewalks", "crosswalk", "crosswalks",
                                          "intersection", "intersections", "stoplight", "stoplights", "block", "blocks",
                                          "downtown", "curb", "curbs"};
  return mode != TravelMode::Hike || std::none_of(std::begin(kStreets), std::end(kStreets), has);
}

void SetLineMetric(bool metric) { g_metric = metric; }

std::string MetricWording(std::string const & text)
{
  // Whole phrases, so a body part ("one foot in front of the other") or a fact ("twelve miles an
  // hour in 1899") is left alone. Distances are rounded the way the joke needs, not converted exactly.
  static std::pair<char const *, char const *> const kPhrases[] = {
      {"a quarter mile ago", "a few hundred meters ago"},
      {"for a quarter mile", "for a few hundred meters"},
      {"For a quarter mile", "For a few hundred meters"},
      {"for a hundred miles", "for a hundred and fifty kilometers"},
      {"hundreds of miles", "hundreds of kilometers"},
      {"for miles", "for kilometers"},
      {"two whole miles", "three whole kilometers"},
      {"Two miles of nothing", "Three kilometers of nothing"},
      {"many miles later", "many kilometers later"},
      {"the miles go by", "the kilometers go by"},
      {"Ten feet of glory", "Three meters of glory"},
      {"moved four feet", "moved a meter"},
      {"moved three feet", "moved one meter"},
      {"three miles an hour", "five kilometers an hour"},
      {"It's miles.", "It's kilometers."},
  };
  std::string out = text;
  for (auto const & [from, to] : kPhrases)
  {
    std::string const f = from;
    for (size_t at = out.find(f); at != std::string::npos; at = out.find(f, at + std::char_traits<char>::length(to)))
      out.replace(at, f.size(), to);
  }
  return out;
}

namespace
{
/// "UK_England" -> "uk", "New Zealand_North " -> "new zealand north": the country part, lower case.
std::string CountryKey(std::string const & country)
{
  std::string name;
  for (unsigned char const ch : country)
  {
    if (ch == '_' || ch == ',')  // "UK_England", "New Zealand_North"
      break;
    name += static_cast<char>(std::tolower(ch));
  }
  while (!name.empty() && name.back() == ' ')
    name.pop_back();
  return name;
}

bool CountryIsOneOf(std::string const & country, std::initializer_list<char const *> names)
{
  std::string const key = CountryKey(country);
  return std::any_of(names.begin(), names.end(), [&](char const * n) { return key == n; });
}
}  // namespace

bool InNorthAmerica(std::string const & country)
{
  return CountryIsOneOf(country, {"", "us", "usa", "united states", "united states of america", "ca", "canada"});
}

bool PlatesNameAState(std::string const & country)
{
  return InNorthAmerica(country) || CountryIsOneOf(country, {"mx", "mexico"});
}

bool DrivesOnLeft(std::string const & country)
{
  // The map files' top-level names, their usual spellings, and ISO codes.
  static char const * const kLeft[] = {
      "uk", "gb", "united kingdom", "great britain", "england", "scotland", "wales", "northern ireland",
      "ireland", "ie", "isle of man", "jersey", "guernsey", "australia", "au", "new zealand", "nz", "japan",
      "jp", "india", "in", "south africa", "za", "indonesia", "id", "thailand", "th", "malaysia", "my",
      "singapore", "sg", "hong kong", "hk", "macau", "macao", "pakistan", "pk", "bangladesh", "bd",
      "sri lanka", "lk", "nepal", "np", "bhutan", "kenya", "ke", "tanzania", "uganda", "zambia",
      "zimbabwe", "botswana", "namibia", "lesotho", "eswatini", "swaziland", "mozambique", "malawi",
      "mauritius", "cyprus", "cy", "malta", "mt", "jamaica", "jm", "bahamas", "barbados", "trinidad and tobago",
      "guyana", "suriname", "fiji", "papua new guinea", "brunei", "east timor", "timor-leste", "maldives",
      "seychelles", "falkland islands", "bermuda", "cayman islands"};
  std::string const name = CountryKey(country);
  if (name.empty())
    return false;
  // A map region of the country ("New Zealand North") counts; two-letter codes only match whole.
  return std::any_of(std::begin(kLeft), std::end(kLeft),
                     [&](char const * c)
                     {
                       std::string const code = c;
                       return name == code ||
                              (code.size() > 2 && name.rfind(code + " ", 0) == 0);
                     });
}

void SetLineDriveSide(bool leftHand) { g_leftHand = leftHand; }

LineFilters CurrentLineFilters()
{
  LineFilters filters;
  filters.mode = g_travelMode;
  filters.housing = g_housing;
  filters.metric = g_metric;
  filters.leftHand = g_leftHand;
  return filters;
}

void SetLineFilters(LineFilters const & filters)
{
  g_travelMode = filters.mode;
  g_housing = filters.housing;
  g_metric = filters.metric;
  g_leftHand = filters.leftHand;
}

std::string DescribeLineFilters(LineFilters const & filters)
{
  std::string const housing = HousingName(filters.housing);
  return std::string(TravelModeName(filters.mode)) + "/" + (housing.empty() ? "unknown" : housing) + "/" +
         (filters.metric ? "metric" : "imperial") + "/" + (filters.leftHand ? "left" : "right");
}

std::string DriveSideWording(std::string const & text)
{
  // Whole sentences, so a lane call ("Left lane, please.") or a turn is never touched: only the jokes
  // that take the driver's seat or the passing lane for granted. The two seat jokes trade places, so
  // "you turned toward yourself" stays true on the other side of the car.
  static std::pair<char const *, char const *> const kPhrases[] = {
      {"Left is the side with the driver. You're sitting on it right now.",
       "Left is the side with the passenger seat. Usually. You turned toward yourself."},
      {"Right is the side with the passenger seat. Usually. You turned toward yourself.",
       "Right is the side with the driver. You're sitting on it right now."},
      {"The left lane is for passing.", "The right lane is for passing."},
      {"Like you drive in the left lane.", "Like you drive in the right lane."},
      {"don't drive in the left lane at forty", "don't drive in the right lane at forty"},
  };
  for (auto const & [from, to] : kPhrases)
  {
    // One swap per line: the seat jokes map onto each other, so a second pass would undo the first.
    size_t const at = text.find(from);
    if (at == std::string::npos)
      continue;
    std::string out = text;
    out.replace(at, std::char_traits<char>::length(from), to);
    return out;
  }
  return text;
}

namespace
{
/// The line table as a metric and/or left-hand-traffic driver hears it: built once each, the first
/// time it's needed.
LineTable const & LineDataFor(bool metric, bool leftHand)
{
  auto const build = [](bool m, bool l)
  {
    LineTable t = LineData();
    for (auto & [key, tiers] : t)
      for (auto & lines : tiers)
        for (auto & line : lines)
        {
          if (m)
            line = MetricWording(line);
          if (l)
            line = DriveSideWording(line);
        }
    return t;
  };
  if (!metric && !leftHand)
    return LineData();
  if (metric && !leftHand)
  {
    static LineTable const table = build(true, false);
    return table;
  }
  if (!metric)
  {
    static LineTable const table = build(false, true);
    return table;
  }
  static LineTable const table = build(true, true);
  return table;
}

}  // namespace

std::vector<std::string> const & LinesFor(std::string const & key, int tier)
{
  bool const metric = g_metric;
  bool const leftHand = g_leftHand;
  LineTable const & table = LineDataFor(metric, leftHand);
  // A key or tier that isn't in the table (a typo, a key from a newer line bank) says nothing rather
  // than taking the app down mid-drive; the trace shows which one.
  static std::vector<std::string> const kNone;
  auto const found = table.find(key);
  if (found == table.end() || tier < 0 || static_cast<size_t>(tier) >= found->second.size())
  {
    Trace("lines.missing", {{"key", key}, {"tier", std::to_string(tier)}});
    return kNone;
  }
  auto const & raw = found->second[static_cast<size_t>(tier)];
  TravelMode const mode = g_travelMode;
  Housing const housing = g_housing;
  // Story beats are read by position, and mode and outdoors lines are written for their mode.
  bool const positional = key.rfind("speeding.story", 0) == 0;
  bool const anyMode = mode == TravelMode::Drive || positional || key.rfind("mode.", 0) == 0 ||
                       key.rfind("outdoors.", 0) == 0;
  // The first-launch script is said word for word, whoever's listening.
  bool const anyHousing = housing == Housing::Unknown || housing == Housing::House || positional ||
                          key.rfind("onboard.", 0) == 0 || key.rfind("setup.", 0) == 0;
  if (anyMode && anyHousing)
    return raw;
  static std::mutex mutex;
  static std::map<std::string, std::vector<std::string>> cache;
  std::string const suffix = std::string(".") + TravelModeName(mode);
  std::lock_guard lock(mutex);
  auto const [it, added] =
      cache.try_emplace(key + suffix + "." + HousingName(housing) + std::to_string(tier) + (metric ? "m" : "") +
                         (leftHand ? "L" : ""));
  if (!added)
    return it->second;
  auto & fitting = it->second;
  auto const fitHousing = [&]
  {
    if (anyHousing)
      return;
    std::vector<std::string> kept;
    for (auto const & line : fitting)
      if (LineFitsHousing(line, housing))
        kept.push_back(line);
    // A pool that's all about home (the garage door line) stays: the caller decides whether to say it.
    if (!kept.empty())
      fitting = std::move(kept);
  };
  if (anyMode)
  {
    fitting = raw;
    fitHousing();
    return fitting;
  }
  auto own = table.find(key + suffix);
  // A hike borrows the walk's lines where it has none of its own.
  if (own == table.end() && mode == TravelMode::Hike)
    own = table.find(key + ".walk");
  if (own != table.end())
    fitting = own->second.at(static_cast<size_t>(tier));
  for (auto const & line : raw)
    if (LineFitsMode(line, mode))
      fitting.push_back(line);
  if (fitting.empty())
    fitting = table.at("mode.generic").at(static_cast<size_t>(tier));
  fitHousing();
  return fitting;
}

std::string SpeakableText(std::string const & text)
{
  static char const * const kKeep[] = {"GPS", "ETA", "NASCAR", "OK", "TV", "AI", "ID", "USA", "SUV", "DMV", "DUI", "MPH",
                                       "I", "I'M", "I'LL", "I'D", "I'VE"};
  std::string out = text;
  bool sentenceStart = true;
  size_t i = 0;
  while (i < out.size())
  {
    if (!std::isalpha(static_cast<unsigned char>(out[i])))
    {
      if (out[i] == '.' || out[i] == '!' || out[i] == '?')
        sentenceStart = true;
      ++i;
      continue;
    }
    size_t j = i;
    while (j < out.size() && (std::isalpha(static_cast<unsigned char>(out[j])) || out[j] == '\''))
      ++j;
    std::string const word = out.substr(i, j - i);
    bool const allCaps = std::none_of(word.begin(), word.end(), [](char c) { return std::islower(static_cast<unsigned char>(c)); });
    bool const keep = std::any_of(std::begin(kKeep), std::end(kKeep), [&](char const * k) { return word == k; });
    if (allCaps && !keep && (word.size() >= 2 || (word == "A" && !sentenceStart)))
    {
      for (size_t k = i + (sentenceStart ? 1 : 0); k < j; ++k)
        out[k] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[k])));
    }
    else if (allCaps && keep && word.size() > 1 && word[0] == 'I' && (word[1] == '\''))
    {
      // "I'M" reads fine as "I'm".
      for (size_t k = i + 2; k < j; ++k)
        out[k] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[k])));
    }
    sentenceStart = false;
    i = j;
  }
  return out;
}

void Warm(bool metric, bool leftHand)
{
  LineDataFor(metric, leftHand);
  LineDataFor(false, false);
  // ForSpeech builds each pass's patterns the first time that pass runs, and most passes only run
  // when their trigger (a digit, a degree sign, $, %, /, :, a dash) is in the line. These lines
  // between them trip every pass, so none is left to build mid-drive.
  static char const * const kProbes[] = {
      "Turn left onto I-35 N at 5pm, then Exit 12B toward N Main St NW in 1/2 mi, 0.3 mi.",
      "Mon-Fri 9am-5pm, 3\xE2\x80\x93""5 min, ~20 ft, \xC2\xB1""2 km, -5\xC2\xB0""F \xE2\x80\x94 50% off $1,234.99/gal & #5.",
      "On 11/6/2026 or 2026-11-06 or Nov. 6 in the 1990s and 90s, at 10:30, 24/7, \xC2\xBD.",
      "King George VI Ave to St. John's Svc Rd, Ave B, Mt Hood, 1600 Pennsylvania Ave, US-101, SR 71.",
      "See www.example.com or jo@example.com about Calle Ocho, rue de la Paix and Burnet Rd.",
      "\xD1\x83\xD0\xBB\xD0\xB8\xD1\x86\xD0\xB0 \xE3\x83\xA9\xE3\x83\xBC\xE3\x83\xA1\xE3\x83\xB3 lead the way, read it, live here.",
  };
  for (char const * probe : kProbes)
    pronunciation::ForSpeech(probe);
}

std::string DropRepeatedTags(std::string text, std::set<std::string> & said)
{
  for (std::string const tag : {"There is valuable cargo aboard this car.", "Phones don't like being wet."})
  {
    auto const at = text.find(tag);
    if (at == std::string::npos)
      continue;
    if (said.insert(tag).second)
      continue;
    auto end = at + tag.size();
    while (end < text.size() && text[end] == ' ')
      ++end;
    text.erase(at, end - at);
    while (!text.empty() && text.back() == ' ')
      text.pop_back();
    // What followed the wet phones was about phones.
    if (auto const they = text.find("They also don't like"); they == at)
      text.replace(they, 20, "Phones don't like");
  }
  return text;
}

Delivery DeliveryFor(std::string const & key, std::string const & text)
{
  if (key.empty() || text.empty() || text.find('!') != std::string::npos)
    return Delivery::Normal;
  for (size_t start = 0; start <= key.size();)
  {
    size_t const dot = key.find('.', start);
    size_t const end = dot == std::string::npos ? key.size() : dot;
    if (key.compare(start, end - start, "aside") == 0)
      return Delivery::Aside;
    if (dot == std::string::npos)
      break;
    start = dot + 1;
  }
  return Delivery::Normal;
}

std::string Fill(std::string const & tmpl, Vars const & vars)
{
  std::string out;
  out.reserve(tmpl.size());
  size_t i = 0;
  while (i < tmpl.size())
  {
    if (tmpl[i] == '{')
    {
      size_t j = i + 1;
      while (j < tmpl.size() && (std::isalnum(static_cast<unsigned char>(tmpl[j])) || tmpl[j] == '_'))
        ++j;
      if (j < tmpl.size() && tmpl[j] == '}' && j > i + 1)
      {
        std::string const key = tmpl.substr(i + 1, j - i - 1);
        std::string lower = key;
        std::string upper = key;
        for (auto & c : lower)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto & c : upper)
          c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        auto const it = vars.find(lower);
        if (it != vars.end())
        {
          std::string value = it->second;
          if (key == upper && key != lower)
            for (auto & c : value)
              c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          out += value;
          i = j + 1;
          continue;
        }
      }
    }
    out += tmpl[i++];
  }
  return out;
}
namespace
{
/// A key's memory holds at most this many lines (and always leaves one fresh option), so a pool
/// of up to this size is used up before anything in it comes back.
size_t constexpr kSaidKeep = 30;
/// A saved log is a few hundred keys of short lines; anything past these is a corrupt or
/// hand-mangled file, dropped so it can't grow the log (and the save) without end.
size_t constexpr kSaidKeysMax = 4096;
size_t constexpr kSaidRowMax = 4096;

/// A value with a line break (a destination name, say) would split its row in the saved file.
std::string OneRow(std::string text)
{
  for (char & c : text)
    if (c == '\n' || c == '\r')
      c = ' ';
  return text;
}
}  // namespace

std::string const & SaidLog::Pick(std::string const & key, std::vector<std::string> const & options, double r)
{
  // Nothing to pick from (a key missing from the line bank): say nothing, remember nothing.
  static std::string const kNothing;
  if (options.empty())
  {
    Trace("pick.empty", {{"key", key}});
    return kNothing;
  }
  // r should be in [0, 1); a broken random source (NaN, negative, 1 or more) still picks a real line.
  if (!(r >= 0))
    r = 0;
  else if (r >= 1)
    r = std::nextafter(1.0, 0.0);
  auto & recent = m_recent[key];
  m_before[key] = recent;
  // A key's first ever line is its first option, which is Brandon's line where there is one.
  if (recent.empty())
    r = 0;
  std::vector<std::string const *> fresh;
  for (auto const & l : options)
    if (std::find(recent.begin(), recent.end(), l) == recent.end())
      fresh.push_back(&l);
  if (fresh.empty())
    for (auto const & l : options)
      fresh.push_back(&l);
  std::string const & line = *fresh[std::min(fresh.size() - 1, static_cast<size_t>(std::floor(r * fresh.size())))];
  recent.erase(std::remove(recent.begin(), recent.end(), line), recent.end());
  recent.push_back(line);
  size_t const keep = std::min(kSaidKeep, options.size() - 1);
  if (recent.size() > keep)
    recent.erase(recent.begin(), recent.end() - static_cast<std::ptrdiff_t>(keep));
  m_dirty = true;
  Trace("pick", {{"key", key},
                 {"option", std::to_string(std::find(options.begin(), options.end(), line) - options.begin())},
                 {"of", std::to_string(options.size())}});
  return line;
}

std::string SaidLog::Value(std::string const & key) const
{
  auto const it = m_recent.find(key);
  return it == m_recent.end() || it->second.empty() ? std::string() : it->second.back();
}

void SaidLog::SetValue(std::string const & key, std::string value)
{
  m_before.erase(key);
  m_recent[key] = {std::move(value)};
  m_dirty = true;
}

void SaidLog::Erase(std::string const & key)
{
  m_before.erase(key);
  if (m_recent.erase(key))
    m_dirty = true;
}

void SaidLog::Unpick(std::string const & key, std::string const & line)
{
  auto const before = m_before.find(key);
  auto const now = m_recent.find(key);
  if (before == m_before.end() || now == m_recent.end() || now->second.empty() || now->second.back() != line)
    return;
  if (before->second.empty())
    m_recent.erase(key);
  else
    m_recent[key] = std::move(before->second);
  m_before.erase(before);
  m_dirty = true;
}

std::string SaidLog::Serialize() const
{
  std::string out;
  for (auto const & [key, lines] : m_recent)
    for (auto const & l : lines)
      out += OneRow(key) + "\t" + OneRow(l) + "\n";
  return out;
}

SaidLog SaidLog::Parse(std::string const & text)
{
  SaidLog log;
  std::istringstream in(text);
  std::string row;
  while (std::getline(in, row))
  {
    if (!row.empty() && row.back() == '\r')  // saved on Windows or hand-edited
      row.pop_back();
    auto const tab = row.find('\t');
    if (tab == std::string::npos || tab == 0 || tab + 1 >= row.size() || row.size() > kSaidRowMax)
      continue;
    auto key = row.substr(0, tab);
    if (log.m_recent.size() >= kSaidKeysMax && log.m_recent.count(key) == 0)
      continue;
    auto & lines = log.m_recent[std::move(key)];
    if (lines.size() < kSaidKeep)
      lines.push_back(row.substr(tab + 1));
  }
  return log;
}
namespace
{
std::vector<std::string> HandlingClauses(std::string const & line)
{
  std::vector<std::string> clauses;
  std::string cur;
  auto flush = [&] {
    size_t const b = cur.find_first_not_of(" ,\"");
    size_t const e = cur.find_last_not_of(" ,\"");
    if (b != std::string::npos)
      clauses.push_back(cur.substr(b, e - b + 1));
    cur.clear();
  };
  for (char ch : line)
  {
    if (ch == '.' || ch == '!' || ch == '?' || ch == '|')
      flush();
    else
      cur += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  flush();
  return clauses;
}

bool HasWord(std::string const & text, std::string const & word)
{
  for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1))
  {
    bool const startOk = at == 0 || !std::isalpha(static_cast<unsigned char>(text[at - 1]));
    size_t const end = at + word.size();
    bool const endOk = end >= text.size() || !std::isalpha(static_cast<unsigned char>(text[end]));
    if (startOk && endOk)
      return true;
  }
  return false;
}
}  // namespace

bool AsksToHandlePhone(std::string const & line)
{
  static char const * const kAsks[] = {"plug me in", "hold me", "holding me", "pick me up", "find the charger",
                                       "vent at me", "vents at me", "me over the vent", "me up to the vent",
                                       "me to the vent"};
  static char const * const kSoft[] = {"might want", "think about", "if you want"};
  auto const clauses = HandlingClauses(line);
  // "PLUG. ME. IN." is one ask, spread over three clauses.
  std::string squashed;
  for (auto const & c : clauses)
    squashed += (squashed.empty() ? "" : " ") + c;
  if (squashed.find("plug me in") != std::string::npos)
  {
    bool split = true;
    for (auto const & c : clauses)
      split = split && c.find("plug me in") == std::string::npos;
    if (split)
      return true;
  }
  bool urgent = false;
  for (auto const & c : clauses)
    urgent = urgent || HasWord(c, "now");
  std::string previous;
  for (auto const & clause : clauses)
  {
    bool const negatedBefore = previous == "don't" || previous == "do not" || previous == "never" || previous == "no";
    previous = clause;
    if (negatedBefore)
      continue;
    // "Charger. Now." / "CHARGER! CABLE! OUTLET!": the one-word demand.
    if (clause == "charger" || clause.rfind("charger ", 0) == 0)
      return true;
    for (char const * ask : kAsks)
    {
      size_t const at = clause.find(ask);
      if (at == std::string::npos)
        continue;
      // "Pick me up a charger while you're in there": an errand, not a hand on the phone.
      if (clause.compare(at, 12, "pick me up a") == 0 || clause.compare(at, 15, "pick me up some") == 0)
        continue;
      std::string const before = " " + clause.substr(0, at);
      bool negated = false;
      for (char const * no : {"n't ", " not ", " never ", " no ", " without "})
        negated = negated || before.find(no) != std::string::npos;
      if (negated)
        continue;
      bool soft = false;
      for (char const * s : kSoft)
        soft = soft || clause.find(s) != std::string::npos;
      if (!soft || urgent)
        return true;
    }
  }
  return false;
}

bool LooksOrPicks(std::string const & line)
{
  // "Look at it", "LOOK!", "Pick a voice", "Change anything you like": eyes or a finger on the screen.
  static char const * const kLook[] = {"look at it", "look at this", "look at that", "look at all", "look at the map",
                                       "look at the screen", "look at the route", "look closely", "look how",
                                       "see how", "memorize it", "feel free to look", "have a look", "take a look"};
  // Picks answered out loud ("Pick one. Say first, second, or third.") are fine.
  static char const * const kPick[] = {"pick one", "pick a voice", "pick whichever", "choose a voice", "choose one",
                                       "choose carefully", "choose wisely", "try them all", "try one",
                                       "change anything", "change whatever", "change the voice"};
  auto const clauses = HandlingClauses(line);
  bool saidAloud = false;
  for (auto const & c : clauses)
    saidAloud = saidAloud || c.rfind("say ", 0) == 0 || c.find(" say ") != std::string::npos;
  auto const found = [](std::string const & clause, char const * phrase) {
    size_t const at = clause.find(phrase);
    if (at == std::string::npos || (at > 0 && std::isalpha(static_cast<unsigned char>(clause[at - 1]))))
      return false;
    std::string const before = " " + clause.substr(0, at);
    for (char const * no : {"n't ", " not ", " never ", " no ", " without "})
      if (before.find(no) != std::string::npos)
        return false;
    return true;
  };
  std::string previous;
  for (auto const & clause : clauses)
  {
    bool const negatedBefore = previous == "don't" || previous == "do not" || previous == "never" || previous == "no";
    previous = clause;
    if (negatedBefore)
      continue;
    if (clause == "look" || (!saidAloud && (clause == "pick" || clause == "choose")))
      return true;
    for (char const * phrase : kLook)
      if (found(clause, phrase))
        return true;
    if (!saidAloud)
      for (char const * phrase : kPick)
        if (found(clause, phrase))
          return true;
  }
  return false;
}

}  // namespace backseat
