#include "backseat/lines.hpp"

#include "backseat/lines_data.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace backseat
{
std::vector<std::string> const & LinesFor(std::string const & key, int tier)
{
  return LineData().at(key).at(static_cast<size_t>(tier));
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
  return line;
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
