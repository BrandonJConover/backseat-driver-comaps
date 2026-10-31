#include "backseat/achievements.hpp"

#include <cstdlib>
#include <sstream>

namespace backseat
{
std::vector<Badge> const & Badges()
{
  static std::vector<Badge> const badges = {
      {"nascar", "NASCAR Driver", "Three left turns in a row. Round and round we go."},
  };
  return badges;
}

Badge const * FindBadge(std::string const & id)
{
  for (auto const & b : Badges())
    if (id == b.id)
      return &b;
  return nullptr;
}

bool Achievements::Unlock(std::string const & id, double tMs)
{
  if (!FindBadge(id) || !m_unlocked.emplace(id, tMs).second)
    return false;
  m_dirty = true;
  return true;
}

std::string Achievements::Serialize() const
{
  std::ostringstream out;
  out.precision(15);
  for (auto const & [id, t] : m_unlocked)
    out << id << '\t' << t << '\n';
  return out.str();
}

Achievements Achievements::Parse(std::string const & text)
{
  Achievements a;
  std::istringstream in(text);
  std::string row;
  while (std::getline(in, row))
  {
    auto const tab = row.find('\t');
    if (tab == std::string::npos || tab == 0)
      continue;
    char const * when = row.c_str() + tab + 1;
    char * end = nullptr;
    double const tMs = std::strtod(when, &end);
    if (end == when)
      continue;
    // Badges that were renamed or removed stay in the file but don't count.
    a.m_unlocked.emplace(row.substr(0, tab), tMs);
  }
  return a;
}
}  // namespace backseat
