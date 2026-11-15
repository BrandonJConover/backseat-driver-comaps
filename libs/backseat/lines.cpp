#include "backseat/lines.hpp"

#include "backseat/debug_log.hpp"

#include "backseat/lines_data.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
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
/// The line table as a metric driver hears it: built once, the first time it's needed.
LineTable const & MetricLineData()
{
  static LineTable const table = []
  {
    LineTable t = LineData();
    for (auto & [key, tiers] : t)
      for (auto & lines : tiers)
        for (auto & line : lines)
          line = MetricWording(line);
    return t;
  }();
  return table;
}

}  // namespace

std::vector<std::string> const & LinesFor(std::string const & key, int tier)
{
  bool const metric = g_metric;
  LineTable const & table = metric ? MetricLineData() : LineData();
  auto const & raw = table.at(key).at(static_cast<size_t>(tier));
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
      cache.try_emplace(key + suffix + "." + HousingName(housing) + std::to_string(tier) + (metric ? "m" : ""));
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

bool LinesFitTravelMode(std::string const & key, int tier)
{
  if (g_travelMode == TravelMode::Drive || key.rfind("mode.", 0) == 0 || key.rfind("cmd.", 0) == 0)
    return true;
  LineTable const & table = g_metric ? MetricLineData() : LineData();
  return LinesFor(key, tier) != table.at("mode.generic").at(static_cast<size_t>(tier));
}

namespace
{
/// A lone capital letter ending at `end` is a letter grade ("Grade: A.", "an A minus"), not the
/// word "a" shouted.
bool IsGradeLetter(std::string const & text, size_t end)
{
  if (end >= text.size())
    return true;
  char const next = text[end];
  return next == '.' || next == ',' || next == '!' || next == '?' || text.compare(end, 5, " plus") == 0 ||
         text.compare(end, 6, " minus") == 0;
}

/// The word at `at` follows "a " or "an ": "a A" is never the article twice, it's a grade.
bool AfterArticle(std::string const & text, size_t at)
{
  for (std::string_view const article : {"a ", "an "})
    if (at >= article.size() && text.compare(at - article.size(), article.size(), article) == 0 &&
        (at == article.size() || !std::isalpha(static_cast<unsigned char>(text[at - article.size() - 1]))))
      return true;
  return false;
}
}  // namespace

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
    if (allCaps && !keep && (word.size() >= 2 || (word == "A" && !sentenceStart && !IsGradeLetter(out, j) && !AfterArticle(out, i))))
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
  // "It would have been a {better}": an A, an F.
  for (size_t at = out.find(" a "); at != std::string::npos; at = out.find(" a ", at + 1))
  {
    size_t const letter = at + 3;
    bool const lone = letter + 1 >= out.size() || !std::isalpha(static_cast<unsigned char>(out[letter + 1]));
    if (letter < out.size() && (out[letter] == 'A' || out[letter] == 'F') && lone)
      out.insert(at + 2, "n");
  }
  return out;
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
}  // namespace

std::string const & SaidLog::Pick(std::string const & key, std::vector<std::string> const & options, double r)
{
  auto & recent = m_recent[key];
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
  m_recent[key] = {std::move(value)};
  m_dirty = true;
}

void SaidLog::Erase(std::string const & key)
{
  if (m_recent.erase(key))
    m_dirty = true;
}

std::string SaidLog::Serialize() const
{
  std::string out;
  for (auto const & [key, lines] : m_recent)
    for (auto const & l : lines)
      out += key + "\t" + l + "\n";
  return out;
}

SaidLog SaidLog::Parse(std::string const & text)
{
  SaidLog log;
  std::istringstream in(text);
  std::string row;
  while (std::getline(in, row))
  {
    auto const tab = row.find('\t');
    if (tab == std::string::npos || tab == 0 || tab + 1 >= row.size())
      continue;
    auto & lines = log.m_recent[row.substr(0, tab)];
    if (lines.size() < kSaidKeep)
      lines.push_back(row.substr(tab + 1));
  }
  return log;
}
}  // namespace backseat
