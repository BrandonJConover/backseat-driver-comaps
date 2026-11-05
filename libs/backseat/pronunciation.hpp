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

/// Every entry of the pronunciation dictionary: lowercase word, Misaki phonemes (UTF-8 IPA). Holds
/// the stand-ins for the driver's own words too, so hand it to the voice again after SetUserWords.
std::vector<std::pair<std::string, std::string>> Lexicon();

/// The driver's own pronunciations, one "word<TAB>say it as" per line; they win over everything
/// else here. "Say it as" is plain words ("wine sap"), a respelling with the loud syllable in
/// capitals ("WINE-sap"), or Misaki IPA between slashes. A word can be a phrase ("Mt Hood").
/// Replaces the previous list. Any thread.
void SetUserWords(std::string const & lines);

/// A reader-style respelling ("Ab-DEE-rus", "EE-fa") as Misaki phonemes; nullopt when the text
/// isn't one. Syllables split on hyphens; the capitalised one (or the one marked ' ´ ˈ) is stressed.
/// Ported from EpubToM4B's RespellingPhonemizer.
std::optional<std::string> RespellingPhonemes(std::string const & respelling);

/// A whole number in words, without hyphens ("one hundred twenty one").
std::string SpellNumber(long long n);
}  // namespace backseat::pronunciation
