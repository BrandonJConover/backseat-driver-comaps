#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

namespace backseat
{
/// Line key ("speeding.barely") -> three tiers of templates, calm to unhinged.
using LineTable = std::map<std::string, std::array<std::vector<std::string>, 3>>;

LineTable const & LineData();
}  // namespace backseat
