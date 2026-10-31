// Stop order for a trip planned in Draw mode ("Optimize stop order"): the start and the end stay
// put, and the stops in between are visited in the order that makes the trip shortest.
#pragma once

#include "backseat/types.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace backseat
{
/// How far it is from one place to another, in any unit; straight-line meters if none is given.
using LegCost = std::function<double(LatLon const & from, LatLon const & to)>;

/// The visiting order of `points` (start first, end last, stops between) that makes the total
/// shortest, as indexes into `points`: always starts with 0 and ends with points.size() - 1. Every
/// order is tried for up to 6 stops; beyond that, nearest stop next, then tidied by swapping pairs
/// (2-opt) while that helps.
std::vector<std::size_t> OptimizeStopOrder(std::vector<LatLon> const & points, LegCost const & cost = {});

/// The total cost of visiting `points` in `order`.
double OrderCost(std::vector<LatLon> const & points, std::vector<std::size_t> const & order, LegCost const & cost = {});
}  // namespace backseat
