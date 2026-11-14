// Clip pacing (CROSS X-031, BookLyre's ClipPacing): boundary rules against BookLyre's own golden
// file (clip_pacing_boundaries.json, copied unchanged), GPS line pairs, and the trim / level /
// pause maths on synthetic clips. One-off parity check at build time (loop B70): PacePcm output
// was byte-identical to BookLyre's audio/clip_pacing.py pace_pcm on the clips below.
#include "check.hpp"

#include "backseat/clip_pacing.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace backseat::pacing;

namespace
{
// Just enough JSON for the golden file: objects, arrays, strings (with \n \" \\ \uXXXX), numbers.
struct Json
{
  enum Kind { Null, Num, Str, Arr, Obj } kind = Null;
  double num = 0;
  std::string str;
  std::vector<Json> arr;
  std::vector<std::pair<std::string, Json>> obj;

  Json const * Get(std::string const & key) const
  {
    for (auto const & [k, v] : obj)
      if (k == key)
        return &v;
    return nullptr;
  }
  std::vector<std::string> Strings() const
  {
    std::vector<std::string> out;
    for (auto const & v : arr)
      out.push_back(v.str);
    return out;
  }
};

struct Parser
{
  std::string const & s;
  size_t i = 0;

  void Ws()
  {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t'))
      ++i;
  }
  static void Utf8(std::string & out, unsigned cp)
  {
    if (cp < 0x80)
      out += static_cast<char>(cp);
    else if (cp < 0x800)
      out += static_cast<char>(0xC0 | (cp >> 6)), out += static_cast<char>(0x80 | (cp & 0x3F));
    else
      out += static_cast<char>(0xE0 | (cp >> 12)), out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)),
          out += static_cast<char>(0x80 | (cp & 0x3F));
  }
  std::string String()
  {
    std::string out;
    for (++i; i < s.size() && s[i] != '"'; ++i)
    {
      if (s[i] != '\\')
      {
        out += s[i];
        continue;
      }
      char const e = s[++i];
      if (e == 'n')
        out += '\n';
      else if (e == 't')
        out += '\t';
      else if (e == 'r')
        out += '\r';
      else if (e == 'u')
      {
        Utf8(out, static_cast<unsigned>(std::stoul(s.substr(i + 1, 4), nullptr, 16)));
        i += 4;
      }
      else
        out += e;
    }
    ++i;
    return out;
  }
  Json Value()
  {
    Ws();
    Json v;
    if (s[i] == '{')
    {
      v.kind = Json::Obj;
      ++i;
      for (Ws(); s[i] != '}'; Ws())
      {
        std::string key = String();
        Ws();
        ++i;  // :
        v.obj.emplace_back(key, Value());
        Ws();
        if (s[i] == ',')
          ++i;
      }
      ++i;
    }
    else if (s[i] == '[')
    {
      v.kind = Json::Arr;
      ++i;
      for (Ws(); s[i] != ']'; Ws())
      {
        v.arr.push_back(Value());
        Ws();
        if (s[i] == ',')
          ++i;
      }
      ++i;
    }
    else if (s[i] == '"')
    {
      v.kind = Json::Str;
      v.str = String();
    }
    else
    {
      v.kind = Json::Num;
      size_t used = 0;
      v.num = std::stod(s.substr(i, 32), &used);
      i += used;
    }
    return v;
  }
};

Json Golden()
{
  std::ifstream in(BACKSEAT_CLIP_PACING_PATH);
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string const text = buffer.str();
  CHECK(!text.empty());
  Parser p{text};
  return p.Value();
}

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

TEST(ClipPacingBoundariesMatchBookLyresGoldenCases)
{
  Json const golden = Golden();
  Json const * pauses = golden.Get("pause_ms");
  CHECK(pauses);
  for (auto b : {Boundary::MidSentence, Boundary::Interruption, Boundary::Clause, Boundary::VoiceChange,
                 Boundary::Sentence, Boundary::DialogueTurn, Boundary::Paragraph, Boundary::Heading,
                 Boundary::SceneBreak, Boundary::End})
  {
    Json const * ms = pauses->Get(Name(b));
    CHECK(ms);
    CHECK_EQUAL(PauseMs(b), static_cast<int>(ms->num));
  }
  Json const * cases = golden.Get("cases");
  CHECK(cases && cases->arr.size() >= 14);
  for (auto const & c : cases->arr)
  {
    auto const pieces = c.Get("pieces")->Strings();
    std::vector<std::string> speakers;
    if (Json const * sp = c.Get("speakers"))
      speakers = sp->Strings();
    auto const expected = c.Get("expected")->Strings();
    auto const got = Boundaries(c.Get("text")->str, pieces, speakers, speakers);  // speakers double as voices
    CHECK_EQUAL(got.size(), expected.size());
    for (size_t i = 0; i < got.size(); ++i)
      CHECK_EQUAL(c.Get("name")->str + " #" + std::to_string(i) + " " + Name(got[i]),
                  c.Get("name")->str + " #" + std::to_string(i) + " " + expected[i]);
  }
}

TEST(ClipPacingGapsBetweenGpsLines)
{
  CHECK(BetweenLines("Recalculating. Again.", "Turn left onto Main St.") == Boundary::Sentence);
  CHECK(BetweenLines("In 500 feet,", "turn left.") == Boundary::Clause);
  CHECK(BetweenLines("In 500 feet,", "turn left.", true) == Boundary::VoiceChange);
  CHECK(BetweenLines("I was going to say something nice—", "Turn right.") == Boundary::Interruption);
  CHECK(BetweenLines("Take the exit toward Elm St.", "then keep left.") == Boundary::MidSentence);
  CHECK(BetweenLines("Wow…", "okay.") == Boundary::Interruption);
  CHECK(BetweenLines("You have arrived!", "") == Boundary::Sentence);  // nothing after the "!" to make it a tag
  CHECK(PauseMs(BetweenLines("Recalculating.", "Turn left.")) == 550);
  // Accented and Cyrillic lowercase starts count as a tag after "?".
  CHECK(BetweenLines("¿Qué?", "él dijo.") == Boundary::MidSentence);
  CHECK(BetweenLines("Что?", "сказал он.") == Boundary::MidSentence);
  CHECK(BetweenLines("Что?", "Сказал он.") == Boundary::Sentence);
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

TEST(ClipPacingBoundariesNeverThrowOnJunk)
{
  std::mt19937 rng(70);
  std::uniform_int_distribution<int> byte(0, 255), len(0, 40);
  for (int round = 0; round < 2000; ++round)
  {
    auto junk = [&] {
      std::string s(static_cast<size_t>(len(rng)), ' ');
      for (auto & c : s)
        c = static_cast<char>(byte(rng));
      return s;
    };
    std::vector<std::string> pieces = {junk(), junk(), junk()};
    std::string content = round % 3 ? pieces[0] + "\n\n" + pieces[1] + " " + pieces[2] : junk();
    auto const got = Boundaries(content, pieces, {junk()}, {});
    CHECK_EQUAL(got.size(), pieces.size());
    CHECK(got.back() == Boundary::End);
  }
  CHECK(Boundaries("", {}).empty());
}
