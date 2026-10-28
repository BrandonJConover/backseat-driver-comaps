#include "backseat/route_order.hpp"

#include "backseat/route_sketch.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

namespace backseat
{
namespace
{
/// Up to this many stops, every order is tried (6! = 720).
std::size_t constexpr kExhaustiveStops = 6;

LegCost OrStraightLine(LegCost const & cost)
{
  return cost ? cost : LegCost([](LatLon const & a, LatLon const & b) { return SketchDistanceM(a, b); });
}
}  // namespace

double OrderCost(std::vector<LatLon> const & points, std::vector<std::size_t> const & order, LegCost const & cost)
{
  LegCost const leg = OrStraightLine(cost);
  double total = 0;
  for (std::size_t i = 0; i + 1 < order.size(); ++i)
    total += leg(points[order[i]], points[order[i + 1]]);
  return total;
}

std::vector<std::size_t> OptimizeStopOrder(std::vector<LatLon> const & points, LegCost const & cost)
{
  std::vector<std::size_t> order(points.size());
  std::iota(order.begin(), order.end(), 0);
  if (points.size() <= 3)
    return order;
  LegCost const leg = OrStraightLine(cost);
  std::size_t const stops = points.size() - 2;

  if (stops <= kExhaustiveStops)
  {
    std::vector<std::size_t> best = order;
    double bestCost = OrderCost(points, order, leg);
    while (std::next_permutation(order.begin() + 1, order.end() - 1))
    {
      double const c = OrderCost(points, order, leg);
      if (c < bestCost)
      {
        bestCost = c;
        best = order;
      }
    }
    return best;
  }

  // Nearest stop next...
  std::vector<std::size_t> greedy{0};
  std::vector<bool> used(points.size(), false);
  used[0] = used.back() = true;
  for (std::size_t n = 0; n < stops; ++n)
  {
    std::size_t next = 0;
    double nearest = std::numeric_limits<double>::max();
    for (std::size_t i = 1; i + 1 < points.size(); ++i)
    {
      if (used[i])
        continue;
      double const c = leg(points[greedy.back()], points[i]);
      if (c < nearest)
      {
        nearest = c;
        next = i;
      }
    }
    used[next] = true;
    greedy.push_back(next);
  }
  greedy.push_back(points.size() - 1);

  // ...then 2-opt: reverse a run of stops whenever that makes the trip shorter.
  bool improved = true;
  for (int round = 0; improved && round < 50; ++round)
  {
    improved = false;
    double current = OrderCost(points, greedy, leg);
    for (std::size_t i = 1; i + 2 < greedy.size(); ++i)
    {
      for (std::size_t j = i + 1; j + 1 < greedy.size(); ++j)
      {
        std::reverse(greedy.begin() + i, greedy.begin() + j + 1);
        double const c = OrderCost(points, greedy, leg);
        if (c + 1e-9 < current)
        {
          current = c;
          improved = true;
        }
        else
        {
          std::reverse(greedy.begin() + i, greedy.begin() + j + 1);
        }
      }
    }
  }
  return greedy;
}
}  // namespace backseat
