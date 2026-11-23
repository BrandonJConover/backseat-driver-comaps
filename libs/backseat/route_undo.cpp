#include "backseat/route_undo.hpp"

#include <cmath>

namespace backseat
{
namespace
{
double DistanceM(LatLon const & a, LatLon const & b)
{
  double constexpr kRad = M_PI / 180;
  double constexpr kEarthM = 6371000;
  double const dLat = (b.lat - a.lat) * kRad;
  double const dLon = (b.lon - a.lon) * kRad;
  double const h = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(a.lat * kRad) * std::cos(b.lat * kRad) * std::sin(dLon / 2) * std::sin(dLon / 2);
  return 2 * kEarthM * std::asin(std::min(1.0, std::sqrt(h)));
}

size_t constexpr kKeep = 4;
}  // namespace

void RouteUndo::Built(double tMs, LatLon finish)
{
  if (m_now && DistanceM(m_now->at, finish) < kSameM)
    return;
  if (m_now)
  {
    m_now->endedMs = tMs;
    m_before.push_back(*m_now);
    if (m_before.size() > kKeep)
      m_before.erase(m_before.begin());
  }
  m_now = Destination{finish, {}, tMs, 0};
}

void RouteUndo::Named(std::string const & name)
{
  if (m_now && !name.empty())
    m_now->name = name;
}

void RouteUndo::Closed(double tMs)
{
  if (!m_now)
    return;
  m_now->endedMs = tMs;
  m_before.push_back(*m_now);
  if (m_before.size() > kKeep)
    m_before.erase(m_before.begin());
  m_now.reset();
}

RouteUndo::Plan RouteUndo::Undo(double tMs)
{
  // Newest first: one that lasted a while, else the newest at all.
  std::optional<size_t> pick, newest;
  for (size_t i = m_before.size(); i-- > 0;)
  {
    auto const & d = m_before[i];
    if (tMs - d.endedMs > kRememberMs)
      break;
    if (m_now && DistanceM(m_now->at, d.at) < kSameM)
      continue;
    if (!newest)
      newest = i;
    if (!pick && d.endedMs - d.startedMs >= kMistakeMs)
      pick = i;
  }
  if (!pick)
    pick = newest;
  Plan plan;
  if (pick)
  {
    plan.kind = Plan::Kind::Restore;
    plan.to = m_before[*pick];
    m_before.erase(m_before.begin() + static_cast<long>(*pick));
    return plan;
  }
  plan.kind = m_now ? Plan::Kind::Cancel : Plan::Kind::Nothing;
  return plan;
}
}  // namespace backseat
