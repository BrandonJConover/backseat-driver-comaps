// Corrupt saved-state soak: the files the hosts keep between drives (mileage log, trip history,
// driver memory, game logs, badges, said-lines log) can come back truncated, hand-edited or
// half-written after a crash. Nothing may throw, and no NaN, infinity, negative count or
// overflowing counter may get past the parser into sorting, local-time math, a spoken line or the
// mileage CSV. Loop fallback:security.
#include "check.hpp"

#include "backseat/achievements.hpp"
#include "backseat/history.hpp"
#include "backseat/lines.hpp"
#include "backseat/mileage.hpp"
#include "backseat/plate_game.hpp"
#include "backseat/twenty_questions.hpp"

#include <climits>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using namespace backseat;

struct StateRng
{
  uint64_t s;
  uint32_t Next()
  {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return static_cast<uint32_t>(s >> 11);
  }
  size_t Below(size_t n) { return n == 0 ? 0 : Next() % n; }
};

std::vector<std::string> const kStatePieces = {
  "\t", "\t", "\t", "\n", "\r", ",", ";", " ", "-", "nan", "NaN", "inf", "-inf", "1e308", "1e999", "-1",
  "0", "12", "3.5", "1730000000000", "99999999999999999999", "2147483647", "-2147483648", "45.5,-122.6",
  "999,999", "nan,nan", "Business", "Personal", "Unknown", "Commute", "DriverSaid", "TX", "CA;OR",
  "games", "gpsWins", "driverWins", "grudges", "played", "lifetime", "route", "recent", "note", "name",
  "home", "turnLeft.now", "\xFF", "\xC3", "😀", "x",
};

std::string RandomState(StateRng & r, size_t pieces)
{
  std::string s;
  for (size_t i = 0; i < pieces; ++i)
    s += r.Below(10) == 0 ? std::string(1, static_cast<char>(r.Below(256))) : kStatePieces[r.Below(kStatePieces.size())];
  return s;
}

std::string GoodDriveLine()
{
  LoggedDrive d;
  d.drive.startedAt = 1767268800000.0;  // 2026-01-01 12:00 UTC
  d.drive.endedAt = d.drive.startedAt + 1800000;
  d.drive.from = {45.5, -122.6};
  d.drive.to = {45.6, -122.7};
  d.drive.meters = 16093.4;
  d.bucket = Bucket::Business;
  d.purpose = "client";
  return SerializeDrive(d);
}

std::string WithField(std::string line, size_t field, std::string const & value)
{
  size_t start = 0;
  for (size_t i = 0; i < field; ++i)
    start = line.find('\t', start) + 1;
  auto const end = line.find('\t', start);
  return line.replace(start, end == std::string::npos ? std::string::npos : end - start, value);
}

std::tm Utc(double ms)
{
  CHECK(std::isfinite(ms));
  std::time_t const s = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  gmtime_r(&s, &tm);
  return tm;
}

bool Sane(LoggedDrive const & d)
{
  return std::isfinite(d.drive.startedAt) && std::isfinite(d.drive.endedAt) && std::isfinite(d.drive.meters) &&
         d.drive.meters >= 0 && std::abs(d.drive.from.lat) <= 90 && std::abs(d.drive.from.lon) <= 180 &&
         std::abs(d.drive.to.lat) <= 90 && std::abs(d.drive.to.lon) <= 180;
}
}  // namespace

TEST(CorruptMileageLinesAreDropped)
{
  auto const good = ParseDrive(GoodDriveLine());
  CHECK(good.has_value());
  CHECK(Sane(*good));
  // Each of these used to load: NaN reached std::sort and time_t, inf/negative miles reached the CSV,
  // "-1" flags wrapped to 4294967295, and a point off the planet reached SamePlace.
  struct Bad
  {
    size_t field;
    char const * value;
  };
  for (Bad const & b : std::vector<Bad>{{0, "nan"}, {0, "inf"}, {1, "-inf"}, {1, "1e999"}, {2, "nan,1"},
                                        {2, "999,999"}, {3, "45,inf"}, {3, "0,-181"}, {4, "nan"}, {4, "inf"},
                                        {4, "-5"}, {4, "1e300"}, {10, "-1"}, {10, "99999999999999999999"}})
  {
    auto const d = ParseDrive(WithField(GoodDriveLine(), b.field, b.value));
    if (d)
      std::cerr << "    loaded field " << b.field << " = " << b.value << "\n";
    CHECK(!d.has_value());
  }
}

TEST(CorruptMileageLogStillSummarizes)
{
  StateRng r{21};
  std::vector<LoggedDrive> log;
  auto const good = GoodDriveLine();
  for (int i = 0; i < 3000; ++i)
  {
    std::string line = r.Below(3) == 0 ? good : RandomState(r, 3 + r.Below(20));
    if (r.Below(2) == 0)
      line = WithField(good, r.Below(12), RandomState(r, 1 + r.Below(3)));
    std::optional<LoggedDrive> d;
    try
    {
      d = ParseDrive(line);
    }
    catch (...)
    {
      CHECK(!"ParseDrive threw on a corrupt line");
    }
    if (d)
    {
      CHECK(Sane(*d));
      // Free text may well say "nan"; only the numbers are under test in the CSV below.
      d->fromName = d->toName = d->purpose = "x";
      d->drive.regions.clear();
      log.push_back(*d);
    }
  }
  CHECK(log.size() > 100);
  auto const year = SummarizeYear(log, 2026, Utc);
  CHECK(std::isfinite(year.businessMiles) && std::isfinite(year.unknownMiles) && year.businessMiles >= 0);
  CHECK(std::isfinite(year.estimatedDeduction));
  auto const csv = MileageCsv(log, 2026, Utc);
  CHECK(csv.find("nan") == std::string::npos && csv.find("inf") == std::string::npos);
  CHECK(csv.find("4294967295") == std::string::npos);
}

TEST(CorruptGameLogsKeepCountsInRange)
{
  auto const twenty =
    TwentyLog::Parse("games 99999999999\ngpsWins -4\ndriverWins 2147483647\ngrudges -2147483648\n");
  for (int n : {twenty.games, twenty.gpsWins, twenty.driverWins, twenty.grudges})
  {
    CHECK(n >= 0);
    CHECK(n < INT_MAX);  // the next game's ++ must not overflow
  }
  std::string many;
  for (int i = 0; i < 5000; ++i)
    many += "recent thing " + std::to_string(i) + "\n";
  auto const capped = TwentyLog::Parse(many);
  CHECK(capped.recent.size() <= TwentyQuestions::kRecentKeep);
  CHECK_EQUAL(capped.recent.back(), std::string("thing 4999"));  // the newest are kept

  auto const plates = PlateLog::Parse("grudges -7\nplayed inf\n");
  CHECK(plates.grudges >= 0);
  CHECK(std::isfinite(plates.lastPlayedMs) && plates.lastPlayedMs >= 0);
  auto const plates2 = PlateLog::Parse("grudges 99999999999\nplayed -1e300\n");
  CHECK(plates2.grudges >= 0 && plates2.grudges < INT_MAX);
  CHECK(plates2.lastPlayedMs >= 0);

  // Round trips of honest logs are unchanged.
  auto const honest = TwentyLog::Parse("games 12\ngpsWins 7\ndriverWins 5\ngrudges 1\nrecent a cat\n");
  CHECK_EQUAL(TwentyLog::Parse(honest.Serialize()).Serialize(), honest.Serialize());
  CHECK_EQUAL(honest.games, 12);
}

TEST(CorruptDriverMemoryTimesAreFinite)
{
  auto const memory = DriverMemory::Parse("nan\tname\tSam\ninf\tnote\tlikes jazz\n-inf\thome\tPortland\n");
  CHECK_EQUAL(memory.All().size(), size_t{3});  // the facts survive, only the times are dropped
  for (auto const & m : memory.All())
    CHECK(std::isfinite(m.learnedAt) && m.learnedAt == 0);
  CHECK_EQUAL(memory.Recall("name").value_or(""), std::string("Sam"));
}

TEST(CorruptStateSoakNeverThrows)
{
  StateRng r{33};
  for (int i = 0; i < 2000; ++i)
  {
    std::string text;
    for (size_t j = 0, n = 1 + r.Below(8); j < n; ++j)
      text += RandomState(r, 2 + r.Below(10)) + "\n";
    try
    {
      auto const twenty = TwentyLog::Parse(text);
      CHECK(twenty.games >= 0 && twenty.games < INT_MAX && twenty.grudges >= 0 && twenty.grudges < INT_MAX);
      CHECK(twenty.recent.size() <= TwentyQuestions::kRecentKeep);
      auto const plates = PlateLog::Parse(text);
      CHECK(plates.grudges >= 0 && plates.grudges < INT_MAX && std::isfinite(plates.lastPlayedMs));
      auto const memory = DriverMemory::Parse(text);
      for (auto const & m : memory.All())
        CHECK(std::isfinite(m.learnedAt));
      (void)Achievements::Parse(text).Serialize();
      (void)SaidLog::Parse(text).Serialize();
      size_t lines = 0;
      for (auto const & t : ParseTrips(text, 400, &lines))
        CHECK(std::isfinite(t.endedAt) && std::isfinite(t.actualMinutes) && t.missedTurns >= 0);
    }
    catch (::backseat_tests::Failure const &)
    {
      throw;
    }
    catch (...)
    {
      CHECK(!"a saved-state parser threw on corrupt text");
    }
  }
}
