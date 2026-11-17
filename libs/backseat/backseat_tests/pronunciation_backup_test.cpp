#include "check.hpp"

#include "backseat/pronunciation.hpp"
#include "backseat/pronunciation_backup.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace backseat::pronunciation;

namespace
{
std::string ReadFile(char const * path)
{
  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// What the golden BookLyre export becomes: no pattern row, no blank word, no second "hermione",
// no row without a pronunciation, no description; escapes decoded, the tab flattened, IPA kept.
std::string const kGoldenLines =
    "Hermione\ther-MY-oh-nee\n"
    "Mt Hood\tmount hood\n"
    "Quasi\t/kwˈɑzi/\n"
    "Café Renée\tka-FAY ruh-NAY\n"
    "Kvothe\tKVOHTH with a tab\n"
    "Sirius \"Padfoot\" Black\tSIR-ee-us\n"
    "Zoë 🐦\tZOH-ee\n";
}  // namespace

TEST(BookLyrePronunciationFileBecomesDriverWords)
{
  auto const json = ReadFile(BACKSEAT_PRONUNCIATION_BACKUP_PATH);
  CHECK(!json.empty());
  CHECK_EQUAL(FromBackupJson(json), kGoldenLines);
}

TEST(DriverWordsRoundTripThroughABookLyreFile)
{
  auto const json = ToBackupJson(kGoldenLines, "Backseat \"Driver\"", 1762412400);
  CHECK_EQUAL(FromBackupJson(json), kGoldenLines);
  // The fields both BookLyre readers require, Apple's date, and a stable UUID-shaped id.
  CHECK(json.find("\"name\":\"Backseat \\\"Driver\\\"\"") != std::string::npos);
  CHECK(json.find("\"exportDate\":784105200,") != std::string::npos);
  CHECK(json.find("\"isPattern\":false,\"source\":\"manual\"") != std::string::npos);
  CHECK(json.find("\"bookTitles\":[]") != std::string::npos);
  CHECK(json.find("description") == std::string::npos);
  CHECK_EQUAL(json, ToBackupJson(kGoldenLines, "Backseat \"Driver\"", 1762412400));
  auto const id = json.find("\"id\":\"");
  CHECK(id != std::string::npos);
  auto const uuid = json.substr(id + 6, 36);
  CHECK_EQUAL(uuid.size(), size_t{36});
  CHECK_EQUAL(uuid[8], '-');
  CHECK_EQUAL(uuid[14], '4');
  CHECK_EQUAL(uuid[23], '-');
  // Lines the editor can't use are left out; an empty list is still a valid file.
  CHECK_EQUAL(FromBackupJson(ToBackupJson("no tab here\n\tno word\nword\t\nGIF\tJIF", "x", 978307200)),
              std::string("GIF\tJIF\n"));
  CHECK_EQUAL(ToBackupJson("", "x", 978307200), std::string("{\"name\":\"x\",\"exportDate\":0,\"entries\":[],\"bookTitles\":[]}"));
}

TEST(ImportedWordsReachTheVoice)
{
  SetUserWords(FromBackupJson(ReadFile(BACKSEAT_PRONUNCIATION_BACKUP_PATH)));
  CHECK_EQUAL(ForSpeech("Off to Mt Hood!"), std::string("Off to mount hood!"));
  CHECK(ForSpeech("Quasi-legal.").find("Quasi") == std::string::npos);
  SetUserWords("");
}

TEST(BrokenPronunciationFilesImportNothing)
{
  char const * const bad[] = {
      "",
      "not json",
      "{",
      "[]",
      "{\"name\":\"settings\",\"narratorVoiceId\":\"bf_emma\"}",
      "{\"entries\":{}}",
      "{\"entries\":[{\"word\":\"a\",\"pronunciation\":\"b\"}]",
      "{\"entries\":[{\"word\":\"a\",\"pronunciation\":\"b\"}]} trailing",
      "{\"entries\":[{\"word\":\"a\\x\",\"pronunciation\":\"b\"}]}",
      "{\"entries\":[{\"word\":\"a\\u12\",\"pronunciation\":\"b\"}]}",
      "{\"entries\":[{\"word\":\"a\",\"pronunciation\":\"b\"},]}",
      "{\"entries\":[{\"word\":\"a\n\",\"pronunciation\":\"b\"}]}",
  };
  for (auto const * text : bad)
    CHECK_EQUAL(FromBackupJson(text), std::string());
  // Deep nesting is refused instead of overflowing the stack.
  CHECK_EQUAL(FromBackupJson(std::string(100000, '[')), std::string());
  // Rows of the wrong shape are skipped, the rest still import.
  CHECK_EQUAL(FromBackupJson("{\"entries\":[1,\"x\",{\"word\":3,\"pronunciation\":\"b\"},{\"word\":\"ok\","
                             "\"pronunciation\":\"fine\",\"isPattern\":null}]}"),
              std::string("ok\tfine\n"));
}
