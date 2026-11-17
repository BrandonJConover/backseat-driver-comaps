// The trip log format (loop BI-031): what the host writes to backseat_trips.tsv and reads back.
// It used to live in backseat_adapter.cpp with no tests, wrote endedAt with six significant digits
// (hours lost per trip) and let a tab or newline in a destination split one trip into junk.
#include "check.hpp"

#include "backseat/history.hpp"

#include <string>
#include <vector>

using namespace backseat;

namespace
{
TripRecord SampleTrip()
{
  TripRecord r;
  r.routeKey = "39.7000,-104.9000>39.7500,-104.9900";
  r.destination = "Work";
  r.endedAt = 1762430123456;
  r.expectedMinutes = 18;
  r.actualMinutes = 21.4;
  r.speedingShare = 0.125;
  r.missedTurns = 2;
  r.hardBrakes = 1;
  r.grade = "B-";
  r.to = LatLon{39.7512345, -104.9876543};
  return r;
}

void CheckSameTrip(TripRecord const & a, TripRecord const & b)
{
  CHECK_EQUAL(a.routeKey, b.routeKey);
  CHECK_EQUAL(a.destination, b.destination);
  CHECK_EQUAL(a.endedAt, b.endedAt);
  CHECK_EQUAL(a.expectedMinutes, b.expectedMinutes);
  CHECK_EQUAL(a.actualMinutes, b.actualMinutes);
  CHECK_EQUAL(a.speedingShare, b.speedingShare);
  CHECK_EQUAL(a.missedTurns, b.missedTurns);
  CHECK_EQUAL(a.hardBrakes, b.hardBrakes);
  CHECK_EQUAL(a.grade, b.grade);
  CHECK_EQUAL(a.to.has_value(), b.to.has_value());
  if (a.to && b.to)
  {
    CHECK_EQUAL(a.to->lat, b.to->lat);
    CHECK_EQUAL(a.to->lon, b.to->lon);
  }
}
}  // namespace

TEST(TripLogGoldenLine)
{
  CHECK_EQUAL(TripToLine(SampleTrip()),
              std::string("39.7000,-104.9000>39.7500,-104.9900\tWork\t1762430123456\t18\t21.4\t0.125\t2\t1\tB-\t"
                          "39.7512345,-104.9876543"));
  auto r = SampleTrip();
  r.to.reset();
  CHECK_EQUAL(TripToLine(r),
              std::string("39.7000,-104.9000>39.7500,-104.9900\tWork\t1762430123456\t18\t21.4\t0.125\t2\t1\tB-"));
}

TEST(TripLogRoundTripsWithAndWithoutDestinationPoint)
{
  auto const a = SampleTrip();
  auto const back = TripFromLine(TripToLine(a));
  CHECK(back.has_value());
  CheckSameTrip(*back, a);
  auto b = SampleTrip();
  b.to.reset();
  b.grade.clear();
  auto const back2 = TripFromLine(TripToLine(b));
  CHECK(back2.has_value());
  CheckSameTrip(*back2, b);
}

TEST(TripLogKeepsTheExactEndTime)
{
  // The old writer used the stream's default six digits: 1762430123456 became 1.76243e+12, about
  // 2 minutes off here and up to ~1.4 hours in general, which moved routines and "last time" grades.
  auto const back = TripFromLine(TripToLine(SampleTrip()));
  CHECK(back.has_value());
  CHECK_EQUAL(back->endedAt, 1762430123456.0);
}

TEST(TripLogOldLinesStillLoad)
{
  // Exactly what the old adapter wrote: 9 fields, short numbers, and the later 10th "lat,lon" field.
  auto const old9 = TripFromLine("k>k\tMom's house\t1.76243e+12\t25\t31.5\t0\t0\t3\tC");
  CHECK(old9.has_value());
  CHECK_EQUAL(old9->destination, std::string("Mom's house"));
  CHECK_EQUAL(old9->endedAt, 1.76243e12);
  CHECK_EQUAL(old9->hardBrakes, 3);
  CHECK(!old9->to);
  auto const old10 = TripFromLine("k>k\tGym\t1.76243e+12\t10\t9\t0.2\t1\t0\tA\t39.75,-104.99\r");
  CHECK(old10.has_value());
  CHECK(old10->to.has_value());
  CHECK_EQUAL(old10->to->lon, -104.99);
}

TEST(TripLogTabsAndNewlinesInTextStayOneRecord)
{
  auto r = SampleTrip();
  r.destination = "Joe's\tBar\nand Grill\r";
  r.grade = "A\n";
  std::string const log = SerializeTrips({r, SampleTrip()});
  size_t lines = 0;
  auto const trips = ParseTrips(log, 200, &lines);
  CHECK_EQUAL(lines, size_t(2));
  CHECK_EQUAL(trips.size(), size_t(2));
  CHECK_EQUAL(trips[0].destination, std::string("Joe's Bar and Grill"));
  CHECK_EQUAL(trips[0].grade, std::string("A"));
  CHECK_EQUAL(trips[1].destination, std::string("Work"));
}

TEST(TripLogSkipsDamagedLines)
{
  std::string const good = TripToLine(SampleTrip());
  std::string const log = good + "\n" +
                          "garbage\n" +
                          "k>k\tX\tnan\t1\t1\t0\t0\t0\tA\n" +       // not finite
                          "k>k\tX\t100\t1\t1\t0\t-2\t0\tA\n" +      // negative count
                          "k>k\tX\t100\t1\t1\t0\t1.5\t0\tA\n" +     // half a turn
                          "k>k\tX\t100abc\t1\t1\t0\t0\t0\tA\n" +    // trailing junk
                          "k>k\tX\t100\t1\t1\t0\t0\t0\n" +           // 8 fields
                          "\tX\t100\t1\t1\t0\t0\t0\tA\n" +           // no route
                          "\n\r\n" + good.substr(0, good.size() / 2);  // cut off by a crash mid-write
  size_t lines = 0;
  auto const trips = ParseTrips(log, 200, &lines);
  CHECK_EQUAL(trips.size(), size_t(1));
  CHECK_EQUAL(lines, size_t(9));
  CheckSameTrip(trips[0], SampleTrip());
}

TEST(TripLogBadDestinationPointKeepsTheTrip)
{
  auto const r = TripFromLine("k>k\tX\t100\t1\t1\t0\t0\t0\tA\t999,inf");
  CHECK(r.has_value());
  CHECK(!r->to);
  auto const s = TripFromLine("k>k\tX\t100\t1\t1\t0\t0\t0\tA\tnot a point");
  CHECK(s.has_value());
  CHECK(!s->to);
}

TEST(TripLogKeepsTheLastTrips)
{
  std::vector<TripRecord> all;
  for (int i = 0; i < 450; ++i)
  {
    auto r = SampleTrip();
    r.endedAt = 1000.0 * i;
    all.push_back(r);
  }
  size_t lines = 0;
  auto const kept = ParseTrips(SerializeTrips(all), 200, &lines);
  CHECK_EQUAL(lines, size_t(450));
  CHECK_EQUAL(kept.size(), size_t(200));
  CHECK_EQUAL(kept.front().endedAt, 250000.0);
  CHECK_EQUAL(kept.back().endedAt, 449000.0);
  // The compacted log reads back the same.
  auto const again = ParseTrips(SerializeTrips(kept), 200);
  CHECK_EQUAL(again.size(), size_t(200));
  CHECK_EQUAL(again.front().endedAt, 250000.0);
  CHECK(ParseTrips("", 200).empty());
}
