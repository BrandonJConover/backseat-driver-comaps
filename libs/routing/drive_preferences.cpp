#include "routing/drive_preferences.hpp"

#include "routing/segment.hpp"

namespace routing
{
bool DrivePreferences::Snapshot::Avoids(Segment const & segment) const
{
  return !m_avoided.empty() && m_avoided.count({segment.GetMwmId(), segment.GetFeatureId()}) > 0;
}

DrivePreferences & DrivePreferences::Instance()
{
  static DrivePreferences instance;
  return instance;
}

std::shared_ptr<DrivePreferences::Snapshot const> DrivePreferences::Get() const
{
  std::lock_guard<std::mutex> guard(m_mutex);
  return m_snapshot;
}

void DrivePreferences::Avoid(std::set<Road> const & roads)
{
  std::lock_guard<std::mutex> guard(m_mutex);
  auto next = std::make_shared<Snapshot>(*m_snapshot);
  next->m_avoided.insert(roads.begin(), roads.end());
  m_snapshot = std::move(next);
}

void DrivePreferences::ClearAvoided()
{
  std::lock_guard<std::mutex> guard(m_mutex);
  if (m_snapshot->m_avoided.empty())
    return;
  auto next = std::make_shared<Snapshot>(*m_snapshot);
  next->m_avoided.clear();
  m_snapshot = std::move(next);
}

void DrivePreferences::SetFewerLefts(bool fewer)
{
  std::lock_guard<std::mutex> guard(m_mutex);
  if (m_snapshot->m_fewerLefts == fewer)
    return;
  auto next = std::make_shared<Snapshot>(*m_snapshot);
  next->m_fewerLefts = fewer;
  m_snapshot = std::move(next);
}

bool DrivePreferences::FewerLefts() const
{
  std::lock_guard<std::mutex> guard(m_mutex);
  return m_snapshot->m_fewerLefts;
}
}  // namespace routing
