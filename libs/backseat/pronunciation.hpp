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

/// Symbols the voice would skip or spell out: "½ mile" is "1/2 mile" ("1½" "1 and 1/2"), "#5"
/// "number 5", "$3.49/gal" "$3.49 per gallon", "60 mi/hr" "60 mi per hour". "#blessed", "and/or"
/// and "24/7" are left. Part of ForSpeech; golden cases in backseat_tests/spoken_symbols.json.
std::string Symbols(std::string const & text);

/// Web addresses and e-mail said the way people say them: "https://www.gutenberg.org/ebooks/1342" is
/// "gutenberg dot org", "www.heb.com." "heb dot com.", "jo@example.com" "jo at example dot com". The
/// scheme, "www.", port and path go; hosts are lowercased; punctuation after the address stays.
/// Without a scheme or "www." the host must end in a common top-level domain, so "e.g.", "U.S.",
/// "file.txt" and "3.14" are left. Part of ForSpeech (first); golden cases shared with BookLyre in
/// backseat_tests/spoken_web.json.
std::string WebAddresses(std::string const & text);

/// Two-digit decades said as decades: "the 90s", "the '90s" and "the 80's" read "the nineties",
/// "mid-70s" "mid-seventies". Twenties to nineties only; "10s", "$90s" and "A90s" are left. Part
/// of ForSpeech; golden cases in backseat_tests/spoken_symbols.json.
std::string Decades(std::string const & text);

/// Fractions read with their unit: "1/2 mi" is "a half mile", "3/4 mi" "three quarters of a mile",
/// "2/3 hr" "two thirds of an hour", "1 1/2 mi" "1 and a half miles", "2 1/2 years" "2 and a half
/// years". Dates and other fractions are left. Part of ForSpeech; golden cases in
/// backseat_tests/spoken_symbols.json.
std::string FractionsWithUnits(std::string const & text);
/// Dates said as dates: "11/6/2026" and "2026-11-06" are "November sixth, twenty twenty six", "Nov. 6"
/// "November sixth", "31/12/1999" "the thirty first of December, ...", "on 12/25" "on December twenty
/// fifth". Fractions, versions, counts ("May 3 cars") and impossible days are left alone.
std::string Dates(std::string const & text);
/// Day abbreviations said as days: "Fri" is "Friday", "Mon-Fri" "Monday to Friday", "Sat/Sun"
/// "Saturday and Sunday". A lone "Sat" or "Sun" only before a date ("Sun November ninth"), since
/// "Sat on it" and "Sun's down" are words. Capital only. Part of ForSpeech, after Dates.
std::string Weekdays(std::string const & text);

/// Just the words spelled the same but said differently, swapped for the sound the sentence needs
/// ("Where do you live?" reads "liv"). Part of ForSpeech.
std::string PickSenses(std::string const & text);

/// Number ranges said as ranges, digits left as digits ("3–5" and "pages 3-5" read "3 to 5",
/// "between 3-5" reads "3 and 5", "1914–18" reads "1914 to 1918", "3-1/2" reads "3 and a half").
/// A hyphen only counts next to a counting word or between years, so phone numbers, scores,
/// dates and "I-35" stay as written. Part of ForSpeech; golden cases in
/// backseat_tests/spoken_ranges.json, shared with EpubToM4B's tests/fixtures/spoken_ranges.json.
std::string Ranges(std::string const & text);

/// Foreign phrases the lines quote ("Gire a la izquierda", "Tournez à gauche", "左に曲がってください")
/// swapped for stand-ins the dictionary says in that language's sounds. Latin-script phrases match
/// ignoring case on word boundaries. Part of ForSpeech.
std::string Foreign(std::string const & text);

/// Street types abroad said in their own language: "Calle Ocho" reads "KAH-yeh Ocho", "Paseo de la
/// Reforma" keeps a Spanish "de la", "Hauptstraße" "Haupt SHTRAH-seh" (as dictionary stand-ins).
/// Only before a capitalised word, a number or a name particle, so "via I-35" and "rue the day"
/// stay English. Part of ForSpeech; golden cases in backseat_tests/spoken_street_types.json.
std::string ForeignStreets(std::string const & text);

/// Regnal numbers after a monarch's or pope's name read as ordinals: "King George VI Ave" is "King
/// George the sixth Ave", "Pope John Paul II Blvd" "Pope John Paul the second Blvd", "Rue Louis XVI"
/// "Rue Louis the sixteenth". Only after a known given name or any name after a title (King, Pope,
/// ...), I..XXXIX only; "World War II", "Phase II", "Malcolm X", "John V. Smith" and "Henry I think"
/// are left. Part of ForSpeech; golden cases in backseat_tests/spoken_regnal.json.
std::string Regnal(std::string const & text);

/// Place names spelled like an English word but said another way there, swapped for a stand-in
/// only where the sentence says it's the place: "Turn right onto Burnet Rd" (BURN-it), "Humble, TX"
/// (UM-bul), "toward Manor" (MAY-nur), "Palestine, TX" (PAL-uh-steen; never after "in", that's the
/// country). "Be humble" and "the manor" are left. Part of ForSpeech; golden cases in
/// backseat_tests/spoken_places.json (with the unambiguous place names Lexicon() holds).
std::string LocalPlaces(std::string const & text);

/// Every respelling LocalPlaces() swaps in (their stand-ins are in Lexicon()).
std::vector<std::string> LocalPlaceRespellings();

/// Every respelling ForeignStreets() swaps in, one word each (their stand-ins are in Lexicon()).
std::vector<std::string> ForeignStreetRespellings();

/// Cyrillic romanised the way a reader says it, for street names and quotes the English voice
/// would skip: "улица Ленина" is "ulitsa Lenina", "Київ" "Kiyiv", "Ёлка" "Yolka"; a vowelless
/// acronym in capitals is spelled ("СССР" "S-S-S-R"); a token mixing Latin letters ("Мoscow") is left.
std::string Cyrillic(std::string const & text);

/// Japanese kana romanised by Hepburn for street names and quotes the English voice would skip:
/// small tsu doubles the next consonant ("きって" "kitte"), yoon ("キャ" "kya"), katakana loan sounds
/// ("ティ" "ti"), long marks and ou/oo collapsed ("ありがとう" "arigato"). Kanji and hangul are left.
std::string Kana(std::string const & text);

/// Every foreign phrase Foreign() knows, with the stand-ins it swaps in (space-separated).
std::vector<std::pair<std::string, std::string>> const & ForeignPhrases();

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

/// A route or exit number the way drivers say it: up to two digits as a count ("sixty six"), three
/// as one and a pair ("four oh five", "one eighty three", "two hundred"), four as pairs ("fourteen
/// thirty one", "thirty five oh two", "fourteen hundred"). Longer ones come back as written.
std::string RouteNumber(std::string const & digits);

/// Highways, routes and exits written out: "I-405" reads "I four oh five", "US-183" "yooess one
/// eighty three", "FM 1431" "F M fourteen thirty one", "SH 71" "State Highway seventy one",
/// "TX-71" "Texas seventy one", "Exit 238B" "Exit two thirty eight B", "the 101 Freeway" "the one
/// oh one Freeway". Plain counts ("405 miles") are left for the number pass. Part of ForSpeech;
/// golden cases in backseat_tests/spoken_roads.json.
std::string Roads(std::string const & text);

/// House numbers in pairs, the way people say an address: "1600 Pennsylvania Avenue" is "sixteen
/// hundred Pennsylvania Avenue", "4521 Oak Street" "forty five twenty one Oak Street", "105 N Main
/// Street" "one oh five North Main Street". Only a 3-4 digit number right before capitalised words
/// ending in a spelled-out street type; counts ("500 feet", "1,600 Main") are left alone. Runs
/// after the street pass in ForSpeech; golden cases in backseat_tests/spoken_addresses.json.
std::string HouseNumbers(std::string const & text);

/// Clock times with am/pm the way people say them: "5pm" is "five peeem", "9:05 a.m." "nine oh
/// five ayem", "12 pm" "noon", "12am" "midnight", "9am-5pm" "nine ayem to five peeem" (ayem and
/// peeem are Lexicon() stand-ins). Hours outside 1-12 and decimals ("1.5 am") are left alone; a
/// dotted "p.m." that ended the sentence keeps its period. Part of ForSpeech; golden cases in
/// backseat_tests/spoken_times.json.
std::string Clock(std::string const & text);

/// Years said as years: "in 1868" is "in eighteen sixty eight", "1905" "nineteen oh five", "1066" "ten sixty six", "2005"
/// "two thousand five", "2026" "twenty twenty six", "the 1990s" "the nineteen nineties". Only a
/// number 1000-2099 with a year cue: a decade "s", AD/BC/BCE/CE, a word before it (in, since, from,
/// until, by, before, after, during, between, circa, a month, a season, "summer of"), a month and
/// day ("May 5, 1868"), or next to another year ("1914 to 1918"). Counts and distances ("in 1500
/// feet", "1868 people") stay numbers. Part of ForSpeech; golden cases in
/// backseat_tests/spoken_years.json.
std::string Years(std::string const & text);

/// A whole number in words, without hyphens ("one hundred twenty one").
std::string SpellNumber(long long n);
}  // namespace backseat::pronunciation
