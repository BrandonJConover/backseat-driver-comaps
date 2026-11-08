// Clip pacing (CROSS X-031, BookLyre's ClipPacing), part 1: the pause table and the trim / level /
// pause maths on synthetic clips. The boundary rules (part 2) come with BookLyre's golden file.
// One-off parity check at build time (loop B70): PacePcm output was byte-identical to BookLyre's
// audio/clip_pacing.py pace_pcm on these clips (mono/stereo, quiet/short/loud/peaky).
#include "check.hpp"

#include "backseat/clip_pacing.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace backseat::pacing;

namespace
{
int constexpr kRate = 24000;

// Mono 16-bit clip: `leadMs` of silence, a 220 Hz tone at `rmsDbfs` for `toneMs`, `tailMs` of silence.
std::vector<int16_t> Tone(int leadMs, int toneMs, int tailMs, double rmsDbfs, int channels = 1)
{
  double const amp = 32768.0 * std::pow(10.0, rmsDbfs / 20) * std::sqrt(2.0);
  std::vector<int16_t> out;
  auto frames = [](int ms) { return kRate * ms / 1000; };
  for (int f = 0; f < frames(leadMs); ++f)
    out.insert(out.end(), channels, int16_t(0));
  for (int f = 0; f < frames(toneMs); ++f)
    out.insert(out.end(), channels, static_cast<int16_t>(std::lround(amp * std::sin(2 * M_PI * 220 * (f + 0.5) / kRate))));
  for (int f = 0; f < frames(tailMs); ++f)
    out.insert(out.end(), channels, int16_t(0));
  return out;
}

double const kThreshold = 32768.0 * std::pow(10.0, kSilenceThresholdDbfs / 20);

size_t FirstLoud(std::vector<int16_t> const & s)
{
  size_t i = 0;
  while (i < s.size() && std::abs(s[i]) <= kThreshold)
    ++i;
  return i;
}

size_t LastLoud(std::vector<int16_t> const & s)
{
  size_t i = s.size();
  while (i > 0 && std::abs(s[i - 1]) <= kThreshold)
    --i;
  return i - 1;
}
}  // namespace

TEST(ClipPacingPauseTableMatchesBookLyre)
{
  // BookLyre's pause_ms (tests/fixtures/clip_pacing_boundaries.json) and its names for them.
  std::pair<Boundary, int> const want[] = {
      {Boundary::MidSentence, 120}, {Boundary::Interruption, 200}, {Boundary::Clause, 300},
      {Boundary::VoiceChange, 500}, {Boundary::Sentence, 550},     {Boundary::DialogueTurn, 700},
      {Boundary::Paragraph, 950},   {Boundary::Heading, 1400},     {Boundary::SceneBreak, 2000},
      {Boundary::End, 2000}};
  for (auto const & [b, ms] : want)
    CHECK_EQUAL(PauseMs(b), ms);
  CHECK_EQUAL(std::string(Name(Boundary::VoiceChange)), std::string("voice_change"));
  CHECK_EQUAL(std::string(Name(Boundary::End)), std::string("chapter_end"));
}

TEST(ClipPacingTrimsKokoroPaddingAndLevels)
{
  // Kokoro-like clip: 300 ms lead-in, 1.5 s of speech at -26 dBFS, 600 ms tail.
  auto const clip = Tone(300, 1500, 600, -26.0);
  auto const out = PacePcm(clip.data(), clip.size(), 1, kRate, 550);
  size_t const pause = kRate * 550 / 1000;
  size_t const lead = kRate * kKeepLeadMs / 1000, tail = kRate * kKeepTailMs / 1000;
  CHECK_EQUAL(FirstLoud(out), lead);
  std::vector<int16_t> const voicedPart(out.begin(), out.end() - static_cast<long>(pause));
  CHECK_EQUAL(LastLoud(voicedPart) + 1 + tail, voicedPart.size());
  for (size_t i = voicedPart.size(); i < out.size(); ++i)
    CHECK(out[i] == 0);
  auto const level = LevelDbfs(out.data(), out.size());
  CHECK(level && std::fabs(*level - kTargetDbfs) < 0.1);
  // A nav line now starts 280 ms sooner than the raw clip would.
  CHECK(FirstLoud(clip) - FirstLoud(out) >= static_cast<size_t>(kRate * 280 / 1000));
}

TEST(ClipPacingCapsGainForQuietShortAndPeakyClips)
{
  auto const quiet = Tone(100, 1500, 100, -40.0);  // +17 dB wanted, +6 allowed
  auto const q = PacePcm(quiet.data(), quiet.size(), 1, kRate, 0);
  CHECK(std::fabs(*LevelDbfs(q.data(), q.size()) - (-34.0)) < 0.1);

  auto const shortClip = Tone(100, 500, 100, -35.0);  // "Left." under 1 s: +3 only
  auto const s = PacePcm(shortClip.data(), shortClip.size(), 1, kRate, 0);
  CHECK(std::fabs(*LevelDbfs(s.data(), s.size()) - (-32.0)) < 0.1);

  auto peaky = Tone(100, 1500, 100, -30.0);  // one spike at -2 dBFS limits gain to +0.5 dB
  peaky[kRate] = static_cast<int16_t>(32768.0 * std::pow(10.0, -2.0 / 20));
  auto const p = PacePcm(peaky.data(), peaky.size(), 1, kRate, 0);
  int peak = 0;
  for (auto v : p)
    peak = std::max(peak, std::abs(static_cast<int>(v)));
  CHECK(20 * std::log10(peak / 32768.0) <= kPeakCeilingDbfs + 0.01);

  auto const loud = Tone(100, 1500, 100, -12.0);  // too loud: comes down, at most 6 dB
  auto const l = PacePcm(loud.data(), loud.size(), 1, kRate, 0);
  CHECK(std::fabs(*LevelDbfs(l.data(), l.size()) - (-18.0)) < 0.1);
}

TEST(ClipPacingStereoFloatSilentAndEmpty)
{
  auto const stereo = Tone(300, 1500, 600, -26.0, 2);
  auto const st = PacePcm(stereo.data(), stereo.size(), 2, kRate, 300);
  CHECK(st.size() % 2 == 0);
  // 20 ms + the tone + 60 ms + 300 ms pause, less the tone's first/last few near-zero frames.
  size_t const want = static_cast<size_t>(2 * (kRate * (20 + 1500 + 60 + 300) / 1000));
  CHECK(st.size() <= want && st.size() + 2 * 10 >= want);

  auto const mono = Tone(300, 1500, 600, -26.0);
  std::vector<float> f(mono.size());
  for (size_t i = 0; i < mono.size(); ++i)
    f[i] = static_cast<float>(mono[i] / 32768.0);
  auto const pf = PaceFloat(f.data(), f.size(), 1, kRate, 120);
  auto const pi = PacePcm(mono.data(), mono.size(), 1, kRate, 120);
  CHECK_EQUAL(pf.size(), pi.size());
  for (size_t i = 0; i < pf.size(); ++i)
    CHECK(std::fabs(pf[i] * 32768.0 - pi[i]) <= 1.01);

  f[kRate / 2] = std::nanf("");
  f[kRate / 2 + 1] = 5.0f;
  for (float v : PaceFloat(f.data(), f.size(), 1, kRate, 0))
    CHECK(std::isfinite(v) && v >= -1.0f && v <= 1.0f);

  std::vector<int16_t> silent(1000, 3);
  auto const si = PacePcm(silent.data(), silent.size(), 1, kRate, 100);
  CHECK_EQUAL(si.size(), silent.size() + kRate / 10);
  CHECK(si[0] == 3 && !LevelDbfs(silent.data(), silent.size()));

  CHECK_EQUAL(PacePcm(nullptr, 0, 1, kRate, 200).size(), static_cast<size_t>(kRate / 5));
  CHECK_EQUAL(PacePcm(nullptr, 99, 0, 0, -5).size(), static_cast<size_t>(0));
  CHECK(!LevelDbfs(static_cast<int16_t const *>(nullptr), 4));
}
