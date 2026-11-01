// Stop-and-go in a jam, judged from the car's own speed. No traffic feed needed.
// "So indecisive. First you go. Then you stop. Make up your mind already."
#pragma once

namespace backseat
{
class StopGo
{
public:
  enum class Quip
  {
    None,
    /// Rolled a bit, stopped again.
    StopGo,
    /// Sitting still for a while.
    Standstill
  };

  /// Forget everything; call when a jam starts or ends.
  void Reset();
  /// Feed every fix while in a jam. Returns what's worth a remark right now.
  Quip Update(double tMs, double mps);

private:
  enum class State
  {
    Unknown,
    Rolling,
    Stopped
  };
  State m_state = State::Unknown;
  int m_restops = 0;
  double m_stoppedSince = 0;
  bool m_standstillSaid = false;
  double m_lastQuipT = -1e18;
};
}  // namespace backseat
