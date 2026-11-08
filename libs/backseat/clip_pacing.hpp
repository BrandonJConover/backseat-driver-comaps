// How spoken clips are joined: trim each Kokoro clip's own lead-in and tail of silence, level it
// toward one loudness, and put a pause after it that fits what comes next. A port of BookLyre's
// ClipPacing (iOS ClipPacing, Android core/pacing/ClipPacing.kt, Python audio/clip_pacing.py;
// CROSS X-031), so both apps join clips the same way. For the GPS this means a navigation line
// starts ~0.3 s sooner (Kokoro pads every clip), a quiet roast and a loud countdown come out at
// one level, and two lines queued back to back get the gap their punctuation asks for instead of
// both clips' padding added together.
//
// Pure functions; the hosts call them on the samples Kokoro hands back (Android sherpa-onnx floats,
// iOS FluidAudio floats). This part (loop B70) is the trim / level / pause maths and the pause
// table; the boundary rules that pick the pause from punctuation come next (B71, checked against
// BookLyre's golden file).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace backseat::pacing
{
/// What separates a clip from the next one.
enum class Boundary
{
  MidSentence,   // same sentence; or a lowercase tag after "?" / "!"
  Interruption,  // a dash or ellipsis cuts the sentence off
  Clause,        // , ; :
  VoiceChange,   // a clause boundary where the voice changes
  Sentence,
  DialogueTurn,  // a new paragraph where the speaker changes
  Paragraph,
  Heading,
  SceneBreak,
  End,           // nothing follows ("chapter_end" in BookLyre)
};

/// The pause after a clip, in ms: 120, 200, 300, 500, 550, 700, 950, 1400, 2000, 2000.
int PauseMs(Boundary b);
/// BookLyre's name for it ("mid_sentence", ..., "chapter_end"), for logs and the shared golden file.
char const * Name(Boundary b);

// Loudness and trimming. Constants match BookLyre's.
inline constexpr double kTargetDbfs = -23.0;
inline constexpr double kMaxGainDb = 6.0;
inline constexpr double kMaxGainShortDb = 3.0;  // clips under 1 s ("Left.") have unreliable levels
inline constexpr int kShortClipMs = 1000;
inline constexpr double kPeakCeilingDbfs = -1.5;
inline constexpr double kSilenceThresholdDbfs = -45.0;
inline constexpr int kKeepLeadMs = 20;
inline constexpr int kKeepTailMs = 60;

/// 16-bit interleaved samples trimmed to the voiced part (keeping 20 ms before and 60 ms after),
/// levelled toward -23 dBFS (+-6 dB, +-3 dB under 1 s, never past a -1.5 dBFS peak), with
/// `pauseMs` of silence after. A silent clip is kept as it is, plus the pause.
std::vector<int16_t> PacePcm(int16_t const * samples, size_t count, int channels, int sampleRate, int pauseMs);
/// The same on float samples in [-1, 1] (what Kokoro returns); output is clamped to [-1, 1].
std::vector<float> PaceFloat(float const * samples, size_t count, int channels, int sampleRate, int pauseMs);

/// Level of the voiced part in dBFS (between the first and last sample above -45 dBFS); nullopt
/// when silent. For tests and diagnostics.
std::optional<double> LevelDbfs(int16_t const * samples, size_t count);
std::optional<double> LevelDbfs(float const * samples, size_t count);
}  // namespace backseat::pacing
