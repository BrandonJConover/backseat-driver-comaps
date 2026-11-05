// How Kokoro is told to say things, on iOS and Android alike. One place for every pronunciation
// fix, so a word that sounded wrong on a drive is fixed once for both phones:
// - ForSpeech() turns a line into what the voice should read: street abbreviations, compass
//   points, highways, units, numbers and times written out, "…" and dashes as pauses, and
//   words spelled the same but said differently ("Where do you live?" vs "live music") swapped
//   for the sound the sentence needs.
// - Lexicon() is the dictionary those swaps (and any word the voice says wrong) are looked up in,
//   as Misaki phonemes: iOS hands it to FluidAudio as the custom lexicon, Android writes it ahead
//   of sherpa-onnx's own lexicon file.
// Captions keep the original text: only the voice hears this.
//
// To fix a word: add it to kWords in pronunciation.cpp, as Misaki IPA between slashes
// ("/jˌuˈɛs/") or as a reader-style respelling with the stressed syllable in capitals
// ("Ab-DEE-rus"). The drive log's voice.phonemes event shows what the voice was given.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace backseat::pronunciation
{
/// `text` the way the voice should read it (see above). UTF-8 in, UTF-8 out.
std::string ForSpeech(std::string const & text);

/// Just the words spelled the same but said differently, swapped for the sound the sentence needs
/// ("Where do you live?" reads "liv"). Part of ForSpeech.
std::string PickSenses(std::string const & text);

/// Number ranges said as ranges, digits left as digits ("3–5" and "pages 3-5" read "3 to 5",
/// "between 3-5" reads "3 and 5", "1914–18" reads "1914 to 1918", "3-1/2" reads "3 and a half").
/// A hyphen only counts next to a counting word or between years, so phone numbers, scores,
/// dates and "I-35" stay as written. Part of ForSpeech; golden cases in
/// backseat_tests/spoken_ranges.json, shared with EpubToM4B's tests/fixtures/spoken_ranges.json.
std::string Ranges(std::string const & text);

/// Every entry of the pronunciation dictionary: lowercase word, Misaki phonemes (UTF-8 IPA).
std::vector<std::pair<std::string, std::string>> const & Lexicon();

/// A reader-style respelling ("Ab-DEE-rus", "EE-fa") as Misaki phonemes; nullopt when the text
/// isn't one. Syllables split on hyphens; the capitalised one (or the one marked ' ´ ˈ) is stressed.
/// Ported from EpubToM4B's RespellingPhonemizer.
std::optional<std::string> RespellingPhonemes(std::string const & respelling);

/// A whole number in words, without hyphens ("one hundred twenty one").
std::string SpellNumber(long long n);
}  // namespace backseat::pronunciation
