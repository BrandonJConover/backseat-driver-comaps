#include "backseat/lines.hpp"

#include "backseat/debug_log.hpp"

#include "backseat/lines_data.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <sstream>

namespace backseat
{
namespace
{
std::atomic<TravelMode> g_travelMode{TravelMode::Drive};
}  // namespace

void SetLineTravelMode(TravelMode mode)
{
  g_travelMode = mode;
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
  std::string lower = " ";
  for (unsigned char const ch : text)
  {
    bool const inWord = std::isalnum(ch) || ch == '\'' || ch == '{' || ch == '}';
    lower += inWord ? static_cast<char>(std::tolower(ch)) : ' ';
  }
  lower += ' ';
  auto const has = [&lower](char const * word) { return lower.find(" " + std::string(word) + " ") != std::string::npos; };
  for (char const * phrase : {" speed limit ", " turn signal ", " turn signals ", " road trip ", " seat belt ", " pull over "})
    if (lower.find(phrase) != std::string::npos)
      return false;
  if (std::any_of(std::begin(kCar), std::end(kCar), has))
    return false;
  return mode != TravelMode::Walk || std::none_of(std::begin(kWheels), std::end(kWheels), has);
}

std::vector<std::string> const & LinesFor(std::string const & key, int tier)
{
  auto const & raw = LineData().at(key).at(static_cast<size_t>(tier));
  TravelMode const mode = g_travelMode;
  // Story beats are read by position, and mode lines are written for their mode.
  if (mode == TravelMode::Drive || key.rfind("mode.", 0) == 0 || key.rfind("speeding.story", 0) == 0)
    return raw;
  static std::mutex mutex;
  static std::map<std::string, std::vector<std::string>> cache;
  std::string const suffix = mode == TravelMode::Walk ? ".walk" : ".bike";
  std::lock_guard lock(mutex);
  auto const [it, added] = cache.try_emplace(key + suffix + std::to_string(tier));
  if (!added)
    return it->second;
  auto & fitting = it->second;
  auto const own = LineData().find(key + suffix);
  if (own != LineData().end())
    fitting = own->second.at(static_cast<size_t>(tier));
  for (auto const & line : raw)
    if (LineFitsMode(line, mode))
      fitting.push_back(line);
  if (fitting.empty())
    fitting = LineData().at("mode.generic").at(static_cast<size_t>(tier));
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
