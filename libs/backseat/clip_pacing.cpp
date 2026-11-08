#include "backseat/clip_pacing.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace backseat::pacing
{
namespace
{
double Db(double x) { return 20 * std::log10(x); }

// Shared by the 16-bit and float paths; `full` is full scale (32768 or 1).
template <typename T>
std::vector<T> Pace(T const * s, size_t n, int channels, int sampleRate, int pauseMs, double full, double lo, double hi)
{
  channels = std::max(1, channels);
  sampleRate = std::max(1, sampleRate);
  pauseMs = std::clamp(pauseMs, 0, 60000);
  if (!s)
    n = 0;
  size_t const pause = static_cast<size_t>(channels) * (static_cast<size_t>(sampleRate) * pauseMs / 1000);
  double const threshold = full * std::pow(10.0, kSilenceThresholdDbfs / 20);
  auto loud = [&](size_t i) { return std::fabs(static_cast<double>(s[i])) > threshold; };
  size_t first = 0;
  while (first < n && !loud(first))
    ++first;
  std::vector<T> out;
  if (first == n)  // silent clip: keep it as it is
  {
    out.assign(s, s + n);
    out.resize(n + pause, T(0));
    return out;
  }
  size_t last = n - 1;
  while (!loud(last))
    --last;
  size_t const ch = static_cast<size_t>(channels);
  size_t const firstF = first / ch, lastF = last / ch, frames = n / ch;
  size_t const lead = static_cast<size_t>(sampleRate) * kKeepLeadMs / 1000;
  size_t const tail = static_cast<size_t>(sampleRate) * kKeepTailMs / 1000;
  size_t const start = firstF > lead ? firstF - lead : 0;
  size_t const end = std::min(frames, lastF + 1 + tail);
  size_t const vBegin = firstF * ch, vEnd = std::min(n, (lastF + 1) * ch);
  double sum = 0, peak = 0;
  for (size_t i = vBegin; i < vEnd; ++i)
  {
    double const v = static_cast<double>(s[i]);
    sum += v * v;
    peak = std::max(peak, std::fabs(v));
  }
  double const rms = vEnd > vBegin ? std::sqrt(sum / static_cast<double>(vEnd - vBegin)) : 0;
  double gain = 1.0;
  if (rms > 0 && peak > 0 && std::isfinite(rms) && std::isfinite(peak))
  {
    bool const shortClip = static_cast<double>(lastF - firstF + 1) * 1000 / sampleRate < kShortClipMs;
    double const limit = shortClip ? kMaxGainShortDb : kMaxGainDb;
    double gainDb = std::clamp(kTargetDbfs - Db(rms / full), -limit, limit);
    gainDb = std::min(gainDb, kPeakCeilingDbfs - Db(peak / full));
    if (std::fabs(gainDb) >= 0.1)
      gain = std::pow(10.0, gainDb / 20);
  }
  size_t const kBegin = std::min(n, start * ch), kEnd = std::min(n, end * ch);
  out.reserve(kEnd - kBegin + pause);
  for (size_t i = kBegin; i < kEnd; ++i)
  {
    if (gain == 1.0)
    {
      if constexpr (std::is_floating_point_v<T>)
        out.push_back(std::isfinite(s[i]) ? std::clamp(s[i], T(lo), T(hi)) : T(0));
      else
        out.push_back(s[i]);
      continue;
    }
    double v = static_cast<double>(s[i]) * gain;
    if constexpr (std::is_integral_v<T>)
      v = std::nearbyint(v);
    out.push_back(static_cast<T>(std::clamp(std::isfinite(v) ? v : 0.0, lo, hi)));
  }
  out.resize(out.size() + pause, T(0));
  return out;
}

template <typename T>
std::optional<double> Level(T const * s, size_t n, double full)
{
  if (!s)
    return std::nullopt;
  double const threshold = full * std::pow(10.0, kSilenceThresholdDbfs / 20);
  size_t first = 0;
  while (first < n && !(std::fabs(static_cast<double>(s[first])) > threshold))
    ++first;
  if (first == n)
    return std::nullopt;
  size_t last = n - 1;
  while (!(std::fabs(static_cast<double>(s[last])) > threshold))
    --last;
  double sum = 0;
  for (size_t i = first; i <= last; ++i)
    sum += static_cast<double>(s[i]) * static_cast<double>(s[i]);
  return Db(std::sqrt(sum / static_cast<double>(last - first + 1)) / full);
}
}  // namespace

int PauseMs(Boundary b)
{
  switch (b)
  {
  case Boundary::MidSentence: return 120;
  case Boundary::Interruption: return 200;
  case Boundary::Clause: return 300;
  case Boundary::VoiceChange: return 500;
  case Boundary::Sentence: return 550;
  case Boundary::DialogueTurn: return 700;
  case Boundary::Paragraph: return 950;
  case Boundary::Heading: return 1400;
  case Boundary::SceneBreak:
  case Boundary::End: return 2000;
  }
  return 550;
}

char const * Name(Boundary b)
{
  switch (b)
  {
  case Boundary::MidSentence: return "mid_sentence";
  case Boundary::Interruption: return "interruption";
  case Boundary::Clause: return "clause";
  case Boundary::VoiceChange: return "voice_change";
  case Boundary::Sentence: return "sentence";
  case Boundary::DialogueTurn: return "dialogue_turn";
  case Boundary::Paragraph: return "paragraph";
  case Boundary::Heading: return "heading";
  case Boundary::SceneBreak: return "scene_break";
  case Boundary::End: return "chapter_end";
  }
  return "sentence";
}

std::vector<int16_t> PacePcm(int16_t const * samples, size_t count, int channels, int sampleRate, int pauseMs)
{
  return Pace<int16_t>(samples, count, channels, sampleRate, pauseMs, 32768.0, -32768.0, 32767.0);
}

std::vector<float> PaceFloat(float const * samples, size_t count, int channels, int sampleRate, int pauseMs)
{
  return Pace<float>(samples, count, channels, sampleRate, pauseMs, 1.0, -1.0, 1.0);
}

std::optional<double> LevelDbfs(int16_t const * samples, size_t count) { return Level(samples, count, 32768.0); }
std::optional<double> LevelDbfs(float const * samples, size_t count) { return Level(samples, count, 1.0); }
}  // namespace backseat::pacing
