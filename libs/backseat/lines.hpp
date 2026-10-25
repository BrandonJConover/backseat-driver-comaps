// The roast library: three tiers per key, picked by frustration.
//   0 = smug and passive-aggressive, 1 = openly exasperated, 2 = completely unhinged.
// The lines themselves are generated from engine/src/lines.ts into lines_data.cpp.
#pragma once

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
}  // namespace backseat
