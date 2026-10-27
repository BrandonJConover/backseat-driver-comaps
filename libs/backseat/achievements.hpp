// Achievements: badges the driver unlocks by driving (badly, mostly). Unlocked once, kept forever,
// saved across trips the same way as the SaidLog.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace backseat
{
struct Badge
{
  char const * id;
  char const * name;
  /// What it was for, as shown in the badge list.
  char const * description;
};

/// Every badge there is, in display order.
std::vector<Badge> const & Badges();

/// The badge with this id, or null.
Badge const * FindBadge(std::string const & id);

class Achievements
{
public:
  /// Unlocks a badge from Badges(). True only the first time; false if it was already unlocked or
  /// isn't a known badge.
  bool Unlock(std::string const & id, double tMs);
  bool Has(std::string const & id) const { return m_unlocked.count(id) > 0; }
  /// Badge id -> when it was unlocked (ms since the epoch).
  std::map<std::string, double> const & Unlocked() const { return m_unlocked; }

  /// Whether anything was unlocked since the last ClearDirty(), i.e. it needs saving.
  bool Dirty() const { return m_dirty; }
  void ClearDirty() { m_dirty = false; }

  std::string Serialize() const;
  static Achievements Parse(std::string const & text);

private:
  std::map<std::string, double> m_unlocked;
  bool m_dirty = false;
};
}  // namespace backseat
