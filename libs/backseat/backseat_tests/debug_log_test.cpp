#include "check.hpp"

#include "backseat/debug_log.hpp"

#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
/// A fresh empty folder for one test's log files.
std::string DebugLogTestDir()
{
  char tmpl[] = "/tmp/backseat_debug_log_XXXXXX";
  char * dir = mkdtemp(tmpl);
  return dir ? std::string(dir) : std::string("/tmp");
}

std::vector<std::string> DebugLogLines(std::string const & path)
{
  std::ifstream in(path);
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);)
    lines.push_back(line);
  return lines;
}

struct DebugLogClock
{
  double ms = 1793104924123;  // 2026-10-27T12:42:04.123Z
  std::function<double()> Fn()
  {
    return [this] { return ms; };
  }
};
}  // namespace

TEST(DebugLogWritesOneJsonLinePerEvent)
{
  DebugLogClock clock;
  DebugLog::Options o;
  o.dir = DebugLogTestDir();
  DebugLog log(o, clock.Fn());
  log.Write("say", {{"text", "Turn \"left\"\nnow"}, {"voice", "kokoro"}});
  auto const lines = DebugLogLines(o.dir + "/backseat_drive_log.jsonl");
  CHECK_EQUAL(lines.size(), size_t{1});
  CHECK_EQUAL(lines[0], std::string("{\"t\":\"2026-10-27T12:42:04.123Z\",\"dt\":\"\",\"ev\":\"say\","
                                    "\"text\":\"Turn \\\"left\\\"\\nnow\",\"voice\":\"kokoro\"}"));
}

TEST(DebugLogCountsSecondsFromTheDriveStart)
{
  DebugLogClock clock;
  DebugLog::Options o;
  o.dir = DebugLogTestDir();
  DebugLog log(o, clock.Fn());
  log.BeginDrive("trip", {{"to", "Starbucks"}});
  clock.ms += 12345;
  log.Write("say");
  log.BeginDrive("mileage");
  log.EndDrive("arrived");
  log.Write("idle");
  auto const lines = DebugLogLines(o.dir + "/backseat_drive_log.jsonl");
  CHECK_EQUAL(lines.size(), size_t{5});
  CHECK(lines[0].find("\"dt\":\"0\",\"ev\":\"drive.start\",\"drive\":\"1\",\"reason\":\"trip\",\"to\":\"Starbucks\"") !=
        std::string::npos);
  CHECK(lines[1].find("\"dt\":\"12.3\",\"ev\":\"say\"") != std::string::npos);
  // A second start during a drive is noted, not a new drive.
  CHECK(lines[2].find("\"ev\":\"drive.also\",\"reason\":\"mileage\"") != std::string::npos);
  CHECK(lines[3].find("\"ev\":\"drive.end\"") != std::string::npos);
  CHECK(lines[4].find("\"dt\":\"\",\"ev\":\"idle\"") != std::string::npos);
}

TEST(DebugLogThrottlesRepeatsAndCountsThem)
{
  DebugLogClock clock;
  DebugLog::Options o;
  o.dir = DebugLogTestDir();
  DebugLog log(o, clock.Fn());
  for (int i = 0; i < 10; ++i, clock.ms += 1000)
    log.WriteThrottled("roast.held", "speeding/cooldown", {{"key", "speeding"}});
  log.WriteThrottled("roast.held", "tooSlow/gap", {{"key", "tooSlow"}});
  clock.ms += 30000;
  log.WriteThrottled("roast.held", "speeding/cooldown", {{"key", "speeding"}});
  auto const lines = DebugLogLines(o.dir + "/backseat_drive_log.jsonl");
  CHECK_EQUAL(lines.size(), size_t{3});
  CHECK(lines[0].find("skipped") == std::string::npos);
  CHECK(lines[1].find("tooSlow") != std::string::npos);
  CHECK(lines[2].find("\"key\":\"speeding\",\"skipped\":\"9\"") != std::string::npos);
}

TEST(DebugLogSamplesAtMostEveryFewSeconds)
{
  DebugLog::Options o;
  o.dir = DebugLogTestDir();
  DebugLog log(o);
  int due = 0;
  for (double t = 0; t < 60000; t += 1000)
    due += log.SampleDue(t) ? 1 : 0;
  CHECK_EQUAL(due, 12);
}

TEST(DebugLogRotatesAndKeepsAFewFiles)
{
  DebugLog::Options o;
  o.dir = DebugLogTestDir();
  o.maxFileBytes = 1000;
  o.keepFiles = 2;
  DebugLog log(o);
  for (int i = 0; i < 100; ++i)
    log.Write("fix", {{"i", std::to_string(i)}, {"pad", std::string(60, 'x')}});
  auto const files = log.Files();
  CHECK_EQUAL(files.size(), size_t{3});
  for (auto const & f : files)
  {
    std::ifstream in(f, std::ios::binary | std::ios::ate);
    CHECK(static_cast<long>(in.tellg()) <= 1000);
  }
  // Oldest first, and the newest event is last.
  CHECK(files.back() == o.dir + "/backseat_drive_log.jsonl");
  CHECK(DebugLogLines(files.back()).back().find("\"i\":\"99\"") != std::string::npos);
}

TEST(DebugLogExportsEverythingOldestFirstWithAHeader)
{
  DebugLog::Options o;
  o.dir = DebugLogTestDir();
  o.maxFileBytes = 300;
  DebugLog log(o);
  for (int i = 0; i < 20; ++i)
    log.Write("n", {{"i", std::to_string(i)}});
  std::string const out = o.dir + "/export.jsonl";
  CHECK(log.ExportTo(out, {{"app", "1.0 (42)"}}));
  auto const lines = DebugLogLines(out);
  CHECK(lines.size() >= 2);
  CHECK(lines[0].find("\"ev\":\"export\",\"app\":\"1.0 (42)\"") != std::string::npos);
  // Whatever survived rotation is in order.
  int last = -1;
  for (size_t i = 1; i < lines.size(); ++i)
  {
    auto const at = lines[i].find("\"i\":\"");
    int const n = std::atoi(lines[i].c_str() + at + 5);
    CHECK(n > last);
    last = n;
  }
  CHECK_EQUAL(last, 19);
}

TEST(DebugLogOffWritesNothingAndClearDeletes)
{
  DebugLog::Options o;
  o.dir = DebugLogTestDir();
  DebugLog log(o);
  log.Write("a");
  log.SetEnabled(false);
  log.Write("b");
  CHECK(!log.SampleDue(1e9));
  auto lines = DebugLogLines(o.dir + "/backseat_drive_log.jsonl");
  CHECK_EQUAL(lines.size(), size_t{2});
  CHECK(lines[1].find("log.off") != std::string::npos);
  log.Clear();
  CHECK(log.Files().empty());
}

TEST(DebugLogTraceIsANoOpUntilInstalled)
{
  DebugLog::Options o;
  o.dir = DebugLogTestDir();
  DebugLog log(o);
  Trace("before");
  SetDebugLog(&log);
  Trace("after", {{"k", "v"}});
  TraceThrottled("held", "x");
  TraceThrottled("held", "x");
  SetDebugLog(nullptr);
  Trace("gone");
  auto const lines = DebugLogLines(o.dir + "/backseat_drive_log.jsonl");
  CHECK_EQUAL(lines.size(), size_t{2});
  CHECK(lines[0].find("\"ev\":\"after\"") != std::string::npos);
}

TEST(DebugLogNumbersDropTrailingZeros)
{
  CHECK_EQUAL(LogNum(3.50, 2), std::string("3.5"));
  CHECK_EQUAL(LogNum(40.0, 1), std::string("40"));
  CHECK_EQUAL(LogNum(-0.01, 1), std::string("0"));
  CHECK_EQUAL(LogNum(39.7392358, 5), std::string("39.73924"));
}
