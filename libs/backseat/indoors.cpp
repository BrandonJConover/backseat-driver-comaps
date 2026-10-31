#include "backseat/indoors.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace backseat
{
namespace
{
struct IndoorXy
{
  double x, y;
};

/// Meters east and north of `origin`; flat is fine at building size.
IndoorXy Local(LatLon const & origin, LatLon const & p)
{
  double constexpr kMPerDeg = 111320;
  double const kPi = std::acos(-1.0);
  return {(p.lon - origin.lon) * kMPerDeg * std::cos(origin.lat * kPi / 180), (p.lat - origin.lat) * kMPerDeg};
}

double Cross(IndoorXy const & o, IndoorXy const & a, IndoorXy const & b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

bool Contains(IndoorXy const & a, IndoorXy const & b, IndoorXy const & c, IndoorXy const & p)
{
  double const d1 = Cross(a, b, p), d2 = Cross(b, c, p), d3 = Cross(c, a, p);
  bool const neg = d1 < 0 || d2 < 0 || d3 < 0;
  bool const pos = d1 > 0 || d2 > 0 || d3 > 0;
  return !(neg && pos);
}

/// Distance from the origin (the point) to segment ab.
double DistToSegment(IndoorXy const & a, IndoorXy const & b)
{
  double const dx = b.x - a.x, dy = b.y - a.y;
  double const len2 = dx * dx + dy * dy;
  double t = len2 > 0 ? -(a.x * dx + a.y * dy) / len2 : 0;
  t = std::clamp(t, 0.0, 1.0);
  return std::hypot(a.x + t * dx, a.y + t * dy);
}

using Key = std::pair<double, double>;
using Edge = std::pair<Key, Key>;

Edge MakeEdge(LatLon const & a, LatLon const & b)
{
  Key const ka{a.lat, a.lon}, kb{b.lat, b.lon};
  return ka < kb ? Edge{ka, kb} : Edge{kb, ka};
}
}  // namespace

std::optional<double> DepthInsideM(std::vector<FootprintTriangle> const & footprint, LatLon const & p)
{
  IndoorXy const origin{0, 0};
  bool inside = false;
  std::map<Edge, int> edges;
  for (auto const & t : footprint)
  {
    if (!inside && Contains(Local(p, t.a), Local(p, t.b), Local(p, t.c), origin))
      inside = true;
    ++edges[MakeEdge(t.a, t.b)];
    ++edges[MakeEdge(t.b, t.c)];
    ++edges[MakeEdge(t.c, t.a)];
  }
  if (!inside)
    return std::nullopt;
  double depth = 1e18;
  for (auto const & [e, n] : edges)
  {
    if (n != 1)
      continue;
    IndoorXy const a = Local(p, {e.first.first, e.first.second});
    IndoorXy const b = Local(p, {e.second.first, e.second.second});
    depth = std::min(depth, DistToSegment(a, b));
  }
  return depth;
}

IndoorStart JudgeIndoorStart(std::optional<Building> const & building, double accuracyM, double speedMps, bool atHome)
{
  if (!building || building->depthM < kIndoorMinDepthM)
    return IndoorStart::No;
  if (accuracyM > kIndoorMaxAccuracyM || speedMps > kIndoorMaxMps)
    return IndoorStart::No;
  return atHome || building->garage ? IndoorStart::Garage : IndoorStart::Building;
}
}  // namespace backseat
