// The roast library: three tiers per key, picked by frustration.
//   0 = smug and passive-aggressive, 1 = openly exasperated, 2 = completely unhinged.
// The lines themselves are generated from engine/src/lines.ts into lines_data.cpp.
#pragma once

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace backseat
{
using Vars = std::map<std::string, std::string>;

/// Templates for `key` at `tier` (0..2). Throws std::out_of_range for an unknown key.
std::vector<std::string> const & LinesFor(std::string const & key, int tier);

/// Fills {placeholders} from vars. {NAME} in capitals inserts the value in capitals.
/// Unknown placeholders are left as they are.
std::string Fill(std::string const & tmpl, Vars const & vars);

/// Text as it should be spoken. The fed-up lines are written in capitals for shouting, but a
/// voice reads a capitalized word as letters ("W-H-O"), so shouted words go back to normal case
/// (the exclamation marks carry the shouting). Real abbreviations (GPS, ETA, NASCAR) stay.
std::string SpeakableText(std::string const & text);

/// What the GPS said lately, per key, kept across trips so it keeps the driver guessing: a line
/// doesn't come back until the others for its key have had a turn.
class SaidLog
{
public:
  /// A line from `options` not said recently for `key`, chosen by `r` in [0, 1), and remembered.
  std::string const & Pick(std::string const & key, std::vector<std::string> const & options, double r);

  /// A value kept with the log, like how many trips have started ("" or 0 when never set).
  std::string Value(std::string const & key) const;
  void SetValue(std::string const & key, std::string value);
  void Erase(std::string const & key);
  int Counter(std::string const & key) const { return std::atoi(Value(key).c_str()); }
  void SetCounter(std::string const & key, int value) { SetValue(key, std::to_string(value)); }

  /// Whether anything was picked since the last ClearDirty(), i.e. the log needs saving.
  bool Dirty() const { return m_dirty; }
  void ClearDirty() { m_dirty = false; }

  std::string Serialize() const;
  static SaidLog Parse(std::string const & text);

private:
  std::map<std::string, std::vector<std::string>> m_recent;
  bool m_dirty = false;
};
}  // namespace backseat
