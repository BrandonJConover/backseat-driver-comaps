#include "backseat/stop_go.hpp"

namespace backseat
{
namespace
{
/// Faster than this is rolling (~6 mph), slower than kStopGoStoppedMps is stopped; in between keeps
/// the last state so creeping forward a car length doesn't count as a go.
double constexpr kStopGoRollingMps = 2.5;
double constexpr kStopGoStoppedMps = 0.5;
/// The first stop is just the jam; it's the second one onwards that's indecisive.
int constexpr kRestopsBeforeQuip = 2;
double constexpr kStandstillMs = 30000;
/// At most one of these remarks this often, on top of the engine's own traffic roasts.
double constexpr kQuipGapMs = 75000;
}  // namespace

void StopGo::Reset() { *this = StopGo(); }

StopGo::Quip StopGo::Update(double tMs, double mps)
{
  bool const gapOk = tMs - m_lastQuipT >= kQuipGapMs;
  if (mps > kStopGoRollingMps)
  {
    m_state = State::Rolling;
    m_standstillSaid = false;
    return Quip::None;
  }
  if (mps >= kStopGoStoppedMps)
  {
    // Inching forward: not a go, but not sitting still either.
    m_stoppedSince = tMs;
    return Quip::None;
  }
  if (m_state != State::Stopped)
  {
    bool const restop = m_state == State::Rolling;
    m_state = State::Stopped;
    m_stoppedSince = tMs;
    if (restop && ++m_restops >= kRestopsBeforeQuip && gapOk)
    {
      m_lastQuipT = tMs;
      return Quip::StopGo;
    }
    return Quip::None;
  }
  if (!m_standstillSaid && tMs - m_stoppedSince >= kStandstillMs && gapOk)
  {
    m_standstillSaid = true;
    m_lastQuipT = tMs;
    return Quip::Standstill;
  }
  return Quip::None;
}
}  // namespace backseat
