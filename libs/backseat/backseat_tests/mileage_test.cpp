#include "check.hpp"

#include "backseat/mileage.hpp"

#include <cmath>
#include <ctime>

using namespace backseat;

namespace
{
LatLon const kHome{39.7392, -104.9903};
LatLon const kOffice{39.7800, -104.9903};  // ~4.5 km north
LatLon const kClient{39.7800, -104.9400};
LatLon const kStore{39.7392, -104.9500};

// 2026-03-02, a Monday, 08:00 UTC.
double constexpr kMonday8am = 1772438400000.0;
double constexpr kDayMs = 86'400'000.0;

std::tm MileageUtc(double ms)
{
  std::time_t const s = static_cast<std::time_t>(ms / 1000);
  std::tm t{};
  gmtime_r(&s, &t);
  return t;
}

double Meters(LatLon a, LatLon b)
{
  double const k = M_PI / 180;
  double const x = (b.lon - a.lon) * k * std::cos((a.lat + b.lat) / 2 * k);
  double const y = (b.lat - a.lat) * k;
  return std::sqrt(x * x + y * y) * 6'371'000;
}

struct Sim
{
  DriveDetector detector;
  double t = kMonday8am;
  LatLon at = kHome;
  std::vector<Drive> drives;

  void Feed(double speed, Activity activity = Activity::Unknown, std::string region = {})
  {
    DriveSample s;
    s.t = t;
    s.at = at;
    s.speedMps = speed;
    s.accuracyM = 10;
    s.activity = activity;
    s.region = region;
    if (auto d = detector.Feed(s))
      drives.push_back(*d);
  }
  void Park(double seconds, Activity activity = Activity::Stationary)
  {
    for (double i = 0; i < seconds; i += 5, t += 5000)
      Feed(0, activity);
  }
  void DriveTo(LatLon to, double speed = 15, std::string region = {})
  {
    double const total = Meters(at, to);
    LatLon const start = at;
    int const steps = static_cast<int>(total / speed);
    for (int i = 1; i <= steps; ++i)
    {
      t += 1000;
      double const f = static_cast<double>(i) / steps;
      at = {start.lat + (to.lat - start.lat) * f, start.lon + (to.lon - start.lon) * f};
      Feed(speed, Activity::Automotive, region);
    }
  }
};

Drive LoggedTrip(LatLon from, LatLon to, double startedAt, std::vector<std::string> regions = {})
{
  Drive d;
  d.from = from;
  d.to = to;
  d.startedAt = startedAt;
  d.endedAt = startedAt + 600'000;
  d.meters = Meters(from, to);
  d.regions = regions;
  d.path = {from, to};
  return d;
}

std::vector<MileagePlace> Places()
{
  return {{"Home", kHome, PlaceRole::Home},
          {"Office", kOffice, PlaceRole::Office},
          {"Acme", kClient, PlaceRole::Client},
          {"King Soopers", kStore, PlaceRole::Personal}};
}

LoggedDrive Sort(Drive const & d, std::vector<LoggedDrive> const & log = {}, MileageSettings settings = {},
                 std::vector<MileagePlace> const & places = Places())
{
  return Classify(d, places, log, settings, MileageUtc);
}
}  // namespace

TEST(DetectsADriveFromParkedToParked)
{
  Sim sim;
  sim.Park(60);
  sim.DriveTo(kOffice);
  CHECK(sim.detector.Driving());
  CHECK(sim.detector.TakeStarted());
  CHECK(!sim.detector.TakeStarted());
  sim.Park(300);
  CHECK(!sim.detector.Driving());
  CHECK_EQUAL(sim.drives.size(), 1u);
  auto const & d = sim.drives.front();
  CHECK(Meters(d.from, kHome) < 50);
  CHECK(Meters(d.to, kOffice) < 50);
  CHECK(std::abs(d.meters - Meters(kHome, kOffice)) < 100);
  // It started where the car was parked and ended when it stopped, not 4 minutes later.
  CHECK(d.endedAt - d.startedAt < 320'000);  // ~5 min of driving
}

TEST(ShortHopsAreNotDrives)
{
  Sim sim;
  sim.Park(30);
  sim.DriveTo({kHome.lat + 0.003, kHome.lon});  // ~330 m
  sim.Park(300);
  CHECK(sim.drives.empty());
}

TEST(ARedLightIsNotTheEnd)
{
  Sim sim;
  sim.Park(30);
  sim.DriveTo({(kHome.lat + kOffice.lat) / 2, kHome.lon});
  sim.Park(120);
  sim.DriveTo(kOffice);
  sim.Park(300);
  CHECK_EQUAL(sim.drives.size(), 1u);
}

TEST(WalkingAwayEndsTheDriveRightAway)
{
  Sim sim;
  sim.Park(30);
  sim.DriveTo(kOffice);
  sim.Park(10);
  sim.Park(40, Activity::Walking);
  CHECK_EQUAL(sim.drives.size(), 1u);
}

TEST(GpsJumpsDontAddMiles)
{
  Sim sim;
  sim.Park(30);
  sim.DriveTo({(kHome.lat + kOffice.lat) / 2, kHome.lon});
  LatLon const real = sim.at;
  sim.t += 1000;
  sim.at = {real.lat + 0.1, real.lon};  // 11 km away, one second later
  sim.Feed(15, Activity::Automotive);
  sim.at = real;
  sim.DriveTo(kOffice);
  sim.Park(300);
  CHECK_EQUAL(sim.drives.size(), 1u);
  CHECK(std::abs(sim.drives.front().meters - Meters(kHome, kOffice)) < 100);
}

TEST(RecordsStatesDrivenThrough)
{
  Sim sim;
  sim.Park(30);
  sim.DriveTo({kHome.lat + 0.02, kHome.lon}, 15, "US_Colorado_Denver");
  sim.DriveTo({kHome.lat + 0.04, kHome.lon}, 15, "US_Wyoming");
  sim.Park(300);
  CHECK_EQUAL(sim.drives.size(), 1u);
  CHECK_EQUAL(sim.drives.front().regions.size(), 2u);
  CHECK_EQUAL(sim.drives.front().regions.back(), "US_Wyoming");
}

TEST(StatesFromMapRegions)
{
  CHECK_EQUAL(StateOf("US_Colorado_Denver"), "US_Colorado");
  CHECK_EQUAL(StateOf("US_New York_New York"), "US_New York");
  CHECK_EQUAL(StateOf("France_Paris & Ile-de-France"), "France");
  CHECK_EQUAL(StateOf("Luxembourg"), "Luxembourg");
  CHECK_EQUAL(RegionName("US_New York"), "New York");
}

TEST(TheCommuteIsPersonal)
{
  auto const d = Sort(LoggedTrip(kHome, kOffice, kMonday8am));
  CHECK(d.bucket == Bucket::Personal);
  CHECK(d.why == Why::Commute);
  CHECK_EQUAL(d.toName, "Office");
  auto const back = Sort(LoggedTrip(kOffice, kHome, kMonday8am));
  CHECK(back.why == Why::Commute);

  MileageSettings homeOffice;
  homeOffice.homeIsMainWorkplace = true;
  CHECK(Sort(LoggedTrip(kHome, kOffice, kMonday8am), {}, homeOffice).bucket == Bucket::Business);
}

TEST(WorkTripsAreBusiness)
{
  CHECK(Sort(LoggedTrip(kOffice, kClient, kMonday8am)).why == Why::BetweenWorkplaces);
  CHECK(Sort(LoggedTrip(kHome, kClient, kMonday8am)).why == Why::ToClient);
  CHECK(Sort(LoggedTrip(kHome, kClient, kMonday8am)).bucket == Bucket::Business);
  // No regular workplace: home to a client can't be sorted by the rules alone.
  std::vector<MileagePlace> noOffice = {{"Home", kHome, PlaceRole::Home}, {"Acme", kClient, PlaceRole::Client}};
  CHECK(Sort(LoggedTrip(kHome, kClient, kMonday8am), {}, {}, noOffice).bucket == Bucket::Unknown);
  CHECK(Sort(LoggedTrip(kOffice, kStore, kMonday8am)).bucket == Bucket::Personal);
  CHECK(Sort(LoggedTrip(kOffice, {39.80, -105.10}, kMonday8am)).bucket == Bucket::Unknown);
}

TEST(OutOfTheOrdinaryWaitsForTheDriver)
{
  MileageSettings settings;
  settings.homeState = "US_Colorado";
  auto const wyoming = Sort(LoggedTrip(kHome, {41.14, -104.82}, kMonday8am, {"US_Colorado", "US_Wyoming"}), {}, settings);
  CHECK(wyoming.bucket == Bucket::Unknown);
  CHECK(wyoming.flags & kOutOfState);
  CHECK(wyoming.flags & kFarFromHome);
  // Out of state but to a client is still business, just flagged.
  auto const client = Sort(LoggedTrip(kOffice, kClient, kMonday8am, {"US_Wyoming"}), {}, settings);
  CHECK(client.bucket == Bucket::Business);
  CHECK(client.flags & kOutOfState);
}

TEST(NewPlacesAreFlagged)
{
  std::vector<LoggedDrive> log;
  for (int i = 0; i < 10; ++i)
    log.push_back(Sort(LoggedTrip(kHome, kOffice, kMonday8am + i * kDayMs)));
  CHECK(Sort(LoggedTrip(kHome, {39.70, -105.05}, kMonday8am), log).flags & kNewPlace);
  CHECK(!(Sort(LoggedTrip(kHome, kOffice, kMonday8am), log).flags & kNewPlace));
}

TEST(LearnsFromWhatTheDriverSorted)
{
  LatLon const site{39.70, -105.05};
  std::vector<LoggedDrive> log = {Sort(LoggedTrip(kHome, site, kMonday8am))};
  CHECK(log[0].bucket == Bucket::Unknown);
  CHECK_EQUAL(Reassign(log, 0, Bucket::Business, false, "Job site"), 1u);
  CHECK(log[0].why == Why::DriverSaid);
  auto const again = Sort(LoggedTrip(kHome, site, kMonday8am + kDayMs), log);
  CHECK(again.bucket == Bucket::Business);
  CHECK(again.why == Why::Learned);
  // The drive back counts as the same trip.
  CHECK(Sort(LoggedTrip(site, kHome, kMonday8am + kDayMs), log).bucket == Bucket::Business);
  // Driver's word beats the rules too.
  log.push_back(Sort(LoggedTrip(kHome, kOffice, kMonday8am)));
  Reassign(log, 1, Bucket::Business, false);
  CHECK(Sort(LoggedTrip(kHome, kOffice, kMonday8am + kDayMs), log).bucket == Bucket::Business);
}

TEST(SortingOneSortsTheSimilarUnknowns)
{
  LatLon const site{39.70, -105.05};
  std::vector<LoggedDrive> log = {Sort(LoggedTrip(kHome, site, kMonday8am)), Sort(LoggedTrip(site, kHome, kMonday8am + 3600'000)),
                                  Sort(LoggedTrip(kHome, {39.60, -105.0}, kMonday8am)), Sort(LoggedTrip(kHome, kOffice, kMonday8am))};
  CHECK_EQUAL(Reassign(log, 0, Bucket::Business, true, "Client visit"), 2u);
  CHECK(log[1].bucket == Bucket::Business);
  CHECK(log[1].why == Why::Learned);
  CHECK_EQUAL(log[1].purpose, "Client visit");
  CHECK(log[2].bucket == Bucket::Unknown);
  CHECK(log[3].why == Why::Commute);  // already sorted, left alone
}

TEST(DaysOffArePersonal)
{
  std::vector<LoggedDrive> log;
  for (int week = 0; week < 2; ++week)
  {
    for (int day = 0; day < 5; ++day)
      log.push_back(Sort(LoggedTrip(kHome, kOffice, kMonday8am + (week * 7 + day) * kDayMs)));
  }
  double const saturday = kMonday8am + 5 * kDayMs;
  LatLon const park{39.75, -104.96};
  log.push_back(Sort(LoggedTrip(kHome, park, kMonday8am)));  // been there before, not new
  auto const d = Sort(LoggedTrip(kHome, park, saturday), log);
  CHECK(d.bucket == Bucket::Personal);
  CHECK(d.why == Why::DayOff);
  CHECK(Sort(LoggedTrip(kHome, park, kMonday8am + kDayMs), log).bucket == Bucket::Unknown);
}

TEST(TheYearInMiles)
{
  std::vector<LoggedDrive> log = {Sort(LoggedTrip(kHome, kOffice, kMonday8am)), Sort(LoggedTrip(kOffice, kClient, kMonday8am)),
                                  Sort(LoggedTrip(kHome, {39.70, -105.05}, kMonday8am)),
                                  Sort(LoggedTrip(kHome, kOffice, kMonday8am - 400 * kDayMs))};  // 2025
  auto const y = SummarizeYear(log, 2026, MileageUtc);
  double const commute = Meters(kHome, kOffice) / 1609.344;
  double const client = Meters(kOffice, kClient) / 1609.344;
  CHECK(std::abs(y.commuteMiles - commute) < 0.01);
  CHECK(std::abs(y.personalMiles - commute) < 0.01);
  CHECK(std::abs(y.businessMiles - client) < 0.01);
  CHECK_EQUAL(y.unknownDrives, 1);
  CHECK_EQUAL(y.unknown.size(), 1u);
  CHECK_EQUAL(y.unknown.front(), 2u);
  CHECK(std::abs(y.estimatedDeduction - client * 0.725) < 0.01);
  CHECK_EQUAL(IrsRatePerMile(2025), 0.70);
  CHECK_EQUAL(IrsRatePerMile(2031), 0.0);

  log[1].purpose = "Meeting, \"Acme\"";
  std::string const csv = MileageCsv(log, 2026, MileageUtc);
  CHECK(csv.rfind("Date,Start,End,From,To,Miles,Category,Purpose,Reason,Notes\n", 0) == 0);
  CHECK(csv.find("2026-03-02,08:00,08:10,Home,Office,2.8,Personal (commute),") != std::string::npos);
  CHECK(csv.find("\"Meeting, \"\"Acme\"\"\"") != std::string::npos);
  CHECK(csv.find("2025-") == std::string::npos);
  CHECK(csv.find("($0.725/mile)") != std::string::npos);
}

TEST(DrivesSurviveARestart)
{
  auto d = Sort(LoggedTrip(kHome, kClient, kMonday8am, {"US_Colorado", "US_Wyoming"}));
  d.purpose = "Site\tvisit";
  d.flags = kOutOfState | kNewPlace;
  auto const back = ParseDrive(SerializeDrive(d));
  CHECK(back.has_value());
  CHECK_EQUAL(back->drive.startedAt, d.drive.startedAt);
  CHECK(std::abs(back->drive.to.lon - kClient.lon) < 1e-6);
  CHECK(std::abs(back->drive.meters - d.drive.meters) < 0.1);
  CHECK_EQUAL(back->drive.regions.size(), 2u);
  CHECK_EQUAL(back->toName, "Acme");
  CHECK(back->bucket == Bucket::Business);
  CHECK(back->why == Why::ToClient);
  CHECK_EQUAL(back->flags, d.flags);
  CHECK_EQUAL(back->purpose, "Site visit");
  CHECK(!ParseDrive("garbage").has_value());
  CHECK(!ParseDrive("x\ty\t1,2\t3,4\t5\t\t\t\tBusiness\tNoClue\t0\t").has_value());
}

TEST(FindsWhereThePhoneSleeps)
{
  double constexpr kHourMs = 3600'000.0;
  std::vector<LoggedDrive> log;
  auto const add = [&](LatLon from, LatLon to, double startedAt) {
    LoggedDrive d;
    d.drive = LoggedTrip(from, to, startedAt);
    log.push_back(d);
  };
  for (int day = 0; day < 2; ++day)
  {
    add(kHome, kOffice, kMonday8am + day * kDayMs);                // 08:00
    add(kOffice, kHome, kMonday8am + day * kDayMs + 9 * kHourMs);  // 17:00, home 17:10
  }
  CHECK(!LikelyHome(log, MileageUtc).has_value());  // one night so far
  for (int day = 2; day < 4; ++day)
  {
    add(kHome, kOffice, kMonday8am + day * kDayMs);
    add(kOffice, kHome, kMonday8am + day * kDayMs + 9 * kHourMs);
  }
  auto const home = LikelyHome(log, MileageUtc);
  CHECK(home.has_value());
  CHECK(SamePlace(*home, kHome));
  // A lunch run back to the office doesn't count as a night anywhere.
  CHECK(!SamePlace(*home, kOffice));
}

TEST(BusinessForSomeoneElse)
{
  std::vector<LoggedDrive> log = {Sort(LoggedTrip(kOffice, kClient, kMonday8am))};
  CHECK(MileageCsv(log, 2026, MileageUtc, "Sarah").find(",Business (Sarah's business),") != std::string::npos);
  CHECK(MileageCsv(log, 2026, MileageUtc).find(",Business,") != std::string::npos);
}
