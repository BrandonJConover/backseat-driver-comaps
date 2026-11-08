#include "backseat/clip_pacing.hpp"

#include "backseat/pronunciation_text.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace backseat::pacing
{
namespace
{
using U32 = std::u32string;

bool In(char32_t c, char32_t const * set)
{
  for (; *set; ++set)
    if (*set == c)
      return true;
  return false;
}

// Python's str.strip() whitespace, near enough.
bool Space(char32_t c)
{
  return c == ' ' || (c >= 0x09 && c <= 0x0D) || c == 0x1C || c == 0x1D || c == 0x1E || c == 0x1F || c == 0x85 ||
         c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F ||
         c == 0x205F || c == 0x3000;
}

bool LineBreak(char32_t c)
{
  return c == '\n' || c == '\r' || c == 0x0B || c == 0x0C || c == 0x1C || c == 0x1D || c == 0x1E || c == 0x85 ||
         c == 0x2028 || c == 0x2029;
}

// Python's isalnum / islower for the scripts a GPS line or a novel is likely to use: ASCII,
// Latin-1, Latin Extended-A, Greek and Cyrillic letters; everything else above Latin-1 that isn't
// punctuation or a symbol counts as a letter with no case (CJK, kana).
bool Alnum(char32_t c)
{
  if (c < 0x80)
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  if (c < 0xC0)
    return c == 0xAA || c == 0xB5 || c == 0xBA || c == 0xB2 || c == 0xB3 || c == 0xB9;
  if (c == 0xD7 || c == 0xF7)
    return false;
  if ((c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x303F) || (c >= 0xFE30 && c <= 0xFE4F) ||
      (c >= 0xFF00 && c <= 0xFF0F) || (c >= 0x1F000 && c <= 0x1FAFF))
    return false;
  return true;
}

bool Lower(char32_t c)
{
  if (c < 0x80)
    return c >= 'a' && c <= 'z';
  if (c >= 0xDF && c <= 0xFF)
    return c != 0xF7;
  if (c >= 0x100 && c <= 0x17F)  // Latin Extended-A: upper/lower pairs, parity flips at 0x138 and 0x149
  {
    if (c == 0x138 || c == 0x149 || c == 0x17F)
      return true;
    if (c == 0x178)
      return false;
    bool const odd = (c % 2) == 1;
    return (c < 0x138 || (c > 0x149 && c < 0x178)) ? odd : !odd;
  }
  return (c >= 0x3AC && c <= 0x3CE) || (c >= 0x430 && c <= 0x45F);
}

char32_t const kClosers[] = U"\"”’')]}»*_ \t";
char32_t const kSentenceEnd[] = U".!?…";
char32_t const kClauseEnd[] = U",;:";
char32_t const kDashes[] = U"—–-";
char32_t const kSceneChars[] = U"*#~•·◆◇⁂§=_—–-";

U32 StripClosers(U32 const & s)
{
  size_t end = s.size();
  while (end && In(s[end - 1], kClosers))
    --end;
  return s.substr(0, end);
}

U32 Strip(U32 const & s)
{
  size_t b = 0, e = s.size();
  while (b < e && Space(s[b]))
    ++b;
  while (e > b && Space(s[e - 1]))
    --e;
  return s.substr(b, e - b);
}

// Whole text is scene-break characters and space (the regex [\s*#~...]+ as a full match).
bool SceneChars(U32 const & s)
{
  if (s.empty())
    return false;
  for (char32_t c : s)
    if (!Space(c) && !In(c, kSceneChars))
      return false;
  return true;
}

bool SceneBreak(U32 const & s) { return !Strip(s).empty() && SceneChars(s); }

bool GapHasSceneBreak(U32 const & gap)
{
  size_t start = 0;
  for (size_t i = 0; i <= gap.size(); ++i)
  {
    if (i < gap.size() && !LineBreak(gap[i]))
      continue;
    U32 const line = gap.substr(start, i - start);
    if (!Strip(line).empty() && SceneChars(line))
      return true;
    if (i + 1 < gap.size() && gap[i] == '\r' && gap[i + 1] == '\n')
      ++i;
    start = i + 1;
  }
  return false;
}

bool HasNewline(U32 const & s) { return s.find(U'\n') != U32::npos || s.find(U'\r') != U32::npos; }

bool HeadingWord(U32 const & line)
{
  static char const * const kWords[] = {"chapter", "part", "book", "prologue", "epilogue", "interlude", "act", "volume"};
  for (char const * w : kWords)
  {
    size_t n = 0;
    for (; w[n]; ++n)
    {
      if (n >= line.size())
        break;
      char32_t c = line[n];
      if (c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
      if (c != static_cast<char32_t>(w[n]))
        break;
    }
    if (w[n])
      continue;
    if (n == line.size() || !(Alnum(line[n]) || line[n] == '_'))
      return true;
  }
  return false;
}

bool Heading(U32 const & text)
{
  U32 const line = Strip(text);
  if (line.empty() || line.size() > 80 || line.find(U'\n') != U32::npos)
    return false;
  if (HeadingWord(line))
    return true;
  U32 const stripped = StripClosers(line);
  if (stripped.empty())
    return false;
  char32_t const last = stripped.back();
  return !In(last, kSentenceEnd) && !In(last, kClauseEnd) && line[0] != '"' && line[0] != 0x201C;
}

struct Span
{
  size_t begin, end;
};

Boundary After(U32 const & content, Span r, Span const * next, bool speakerChanged, bool voiceChanged,
               Span const * previous)
{
  if (!next)
    return Boundary::End;
  U32 const text = content.substr(r.begin, r.end - r.begin);
  U32 const nextText = content.substr(next->begin, next->end - next->begin);
  U32 const gap = content.substr(r.end, next->begin - r.end);
  if (SceneBreak(text) || SceneBreak(nextText) || GapHasSceneBreak(gap))
    return Boundary::SceneBreak;
  if (HasNewline(gap))
  {
    U32 const before = previous ? content.substr(previous->end, r.begin - previous->end) : U32(U"\n");
    if (HasNewline(before) && Heading(text))
      return Boundary::Heading;
    return speakerChanged ? Boundary::DialogueTurn : Boundary::Paragraph;
  }
  U32 const stripped = StripClosers(text);
  if (stripped.empty())
    return Boundary::MidSentence;
  char32_t const last = stripped.back();
  std::optional<char32_t> nextFirst;
  for (char32_t c : nextText)
    if (Alnum(c))
    {
      nextFirst = c;
      break;
    }
  if (In(last, kSentenceEnd))
  {
    if (nextFirst && Lower(*nextFirst))  // "Run!" he said
      return last == 0x2026 ? Boundary::Interruption : Boundary::MidSentence;
    return Boundary::Sentence;
  }
  if (In(last, kDashes))
    return Boundary::Interruption;
  if (In(last, kClauseEnd))
    return voiceChanged ? Boundary::VoiceChange : Boundary::Clause;
  return voiceChanged ? Boundary::Clause : Boundary::MidSentence;
}

std::string const & At(std::vector<std::string> const & v, size_t i, std::string const & fallback)
{
  return i < v.size() ? v[i] : fallback;
}

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

std::vector<Boundary> Boundaries(std::string const & content, std::vector<std::string> const & pieces,
                                 std::vector<std::string> const & speakers, std::vector<std::string> const & voices)
{
  using pronunciation::detail::Decode;
  std::vector<U32> texts;
  texts.reserve(pieces.size());
  for (auto const & p : pieces)
    texts.push_back(Decode(p));
  U32 text = Decode(content);
  std::vector<Span> spans;
  size_t cursor = 0;
  for (auto const & t : texts)
  {
    size_t const at = t.empty() ? U32::npos : text.find(t, cursor);
    if (at == U32::npos)
      break;
    spans.push_back({at, at + t.size()});
    cursor = at + t.size();
  }
  if (spans.size() != texts.size())  // a piece was rewritten: lay them out on one line
  {
    text.clear();
    spans.clear();
    for (auto const & t : texts)
    {
      if (!text.empty())
        text += U' ';
      spans.push_back({text.size(), text.size() + t.size()});
      text += t;
    }
  }
  std::vector<std::string> sp(pieces.size()), vo(pieces.size());
  for (size_t i = 0; i < pieces.size(); ++i)
  {
    sp[i] = At(speakers, i, i ? sp[i - 1] : std::string("narrator"));
    vo[i] = At(voices, i, i < speakers.size() ? sp[i] : (i ? vo[i - 1] : sp[i]));
  }
  std::vector<Boundary> out;
  out.reserve(spans.size());
  for (size_t i = 0; i < spans.size(); ++i)
  {
    Span const * next = i + 1 < spans.size() ? &spans[i + 1] : nullptr;
    out.push_back(After(text, spans[i], next, next && sp[i] != sp[i + 1], next && vo[i] != vo[i + 1],
                        i ? &spans[i - 1] : nullptr));
  }
  return out;
}

Boundary BetweenLines(std::string const & line, std::string const & next, bool voiceChanged)
{
  return Boundaries(line + " " + next, {line, next}, {}, {"a", voiceChanged ? "b" : "a"}).front();
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
