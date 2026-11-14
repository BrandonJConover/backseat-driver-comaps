// The driver's pronunciation list as a file BookLyre can read and write, so one list of words
// (character names, a hometown, a street) teaches both apps' Kokoro voice the same sounds.
// The file is BookLyre's PronunciationBackup JSON (iOS PronunciationDictionaryExport, Android
// core/backup/BackupFiles.kt): {name, exportDate, entries:[{id, word, pronunciation, isPattern,
// source, description?}], bookTitles}. Our side is SetUserWords' "word<TAB>say it as" lines.
#pragma once

#include <string>

namespace backseat::pronunciation
{
/// A BookLyre pronunciation file as SetUserWords lines. Pattern rows (regular expressions) are
/// skipped, rows missing a word or pronunciation are dropped, the first of a word repeated in any
/// case wins, descriptions are left out, and tabs or line breaks inside a field become spaces.
/// IPA stays between its slashes. Anything that isn't a pronunciation file comes back "" (never throws).
std::string FromBackupJson(std::string const & json);

/// SetUserWords lines as a BookLyre pronunciation file both its apps import: every entry a manual,
/// non-pattern row with a stable id, exportDate in Apple's seconds since 2001 from unixSeconds,
/// bookTitles empty. Lines without a tab or with an empty side are left out.
std::string ToBackupJson(std::string const & lines, std::string const & name, long long unixSeconds);
}  // namespace backseat::pronunciation
