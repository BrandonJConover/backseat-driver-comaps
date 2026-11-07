// Lines for the GPS's other personalities, generated from personas/*.txt into persona_lines_data.cpp.
#pragma once

#include "backseat/lines_data.hpp"

namespace backseat
{
/// Key -> three tiers, for the keys each personality says in its own words. Keys it leaves out
/// are said in the Critic's lines.
LineTable const & MelancholyLineData();
LineTable const & ProudLineData();
}  // namespace backseat
