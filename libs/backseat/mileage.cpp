#include "backseat/mileage.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace backseat
{
namespace
{
double constexpr kStartSpeedMps = 6.7;      // 15 mph
double constexpr kMovingSpeedMps = 2.0;     // crawling in traffic still counts
double constexpr kWalkingMaxSpeedMps = 3.0;
double constexpr kWorstAccuracyM = 65;
double constexpr kFastestMps = 75;          // anything faster is a GPS jump
double constexpr kMaybeLookbackMs = 120'000;
double constexpr kConfirmMeters = 400;
double constexpr kConfirmMetersAutomotive = 200;
double constexpr kConfirmMs = 30'000;
double constexpr kGiveUpMaybeMs = 120'000;
double constexpr kParkedMs = 240'000;
double constexpr kWalkedAwayMs = 30'000;
double constexpr kSilenceMs = 600'000;
double constexpr kMinDriveMeters = 805;     // half a mile
double constexpr kPathStepMeters = 500;
double constexpr kSameSpotMeters = 250;
double constexpr kFarFromHomeMeters = 80'467;  // 50 miles

double Distance(LatLon a, LatLon b)
{
  double constexpr kEarthM = 6'371'000;
  double constexpr kRad = M_PI / 180;
  double const dLat = (b.lat - a.lat) * kRad;
  double const dLon = (b.lon - a.lon) * kRad;
  double const h = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(a.lat * kRad) * std::cos(b.lat * kRad) * std::sin(dLon / 2) * std::sin(dLon / 2);
  return 2 * kEarthM * std::asin(std::min(1.0, std::sqrt(h)));
}

bool SameSpot(LatLon a, LatLon b) { return Distance(a, b) <= kSameSpotMeters; }

/// The same two places, either direction.
bool SameTrip(Drive const & a, Drive const & b)
{
  return (SameSpot(a.from, b.from) && SameSpot(a.to, b.to)) || (SameSpot(a.from, b.to) && SameSpot(a.to, b.from));
}

bool IsWalking(Activity a) { return a == Activity::Walking || a == Activity::Running; }

MileagePlace const * PlaceAt(std::vector<MileagePlace> const & places, LatLon at)
{
  MileagePlace const * best = nullptr;
  double bestM = 0;
  for (auto const & p : places)
  {
    double const m = Distance(p.at, at);
    if (m <= p.radiusM && (!best || m < bestM))
    {
      best = &p;
      bestM = m;
    }
  }
  return best;
}

bool IsWork(MileagePlace const * p) { return p && (p->role == PlaceRole::Office || p->role == PlaceRole::Client); }
bool Is(MileagePlace const * p, PlaceRole role) { return p && p->role == role; }

std::string Clean(std::string s)
{
  std::replace_if(s.begin(), s.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
  return s;
}

std::vector<std::string> Split(std::string const & s, char sep)
{
  std::vector<std::string> out(1);
  for (char c : s)
  {
    if (c == sep)
      out.emplace_back();
    else
      out.back() += c;
  }
  return out;
}

std::string Format(char const * fmt, double v)
{
  char buf[64];
  std::snprintf(buf, sizeof buf, fmt, v);
  return buf;
}

std::string LatLonText(LatLon p) { return Format("%.6f", p.lat) + "," + Format("%.6f", p.lon); }

std::optional<LatLon> ParseLatLon(std::string const & s)
{
  auto const parts = Split(s, ',');
  if (parts.size() != 2)
    return {};
  return LatLon{std::stod(parts[0]), std::stod(parts[1])};
}

int Year(LocalTime const & localTime, double t) { return localTime(t).tm_year + 1900; }
}  // namespace

// DriveDetector ---------------------------------------------------------------------------------

std::optional<Drive> DriveDetector::Feed(DriveSample const & s)
{
  std::optional<Drive> ended;
  if (m_last && s.t - m_last->t >= kSilenceMs)
  {
    // The app was asleep. Whatever was going on is over.
    ended = Finish();
  }

  if (IsWalking(s.activity))
  {
    if (m_walkingSince == 0)
      m_walkingSince = s.t;
  }
  else if (s.activity != Activity::Unknown)
  {
    m_walkingSince = 0;
  }

  if (s.accuracyM > kWorstAccuracyM)
  {
    // Too fuzzy to place, but it still tells us time is passing.
    if (m_state == State::Driving && m_walkingSince && s.t - m_walkingSince >= kWalkedAwayMs)
      return End();
    return ended;
  }

  double speed = s.speedMps;
  if (speed < 0)
    speed = m_last && s.t > m_last->t ? Distance(m_last->at, s.at) / ((s.t - m_last->t) / 1000) : 0;
  bool const moving = speed >= kMovingSpeedMps;

  switch (m_state)
  {
  case State::Idle:
    if (speed >= kStartSpeedMps || (s.activity == Activity::Automotive && moving))
    {
      // Start from where the car was sitting, if we saw it there a moment ago.
      Begin(m_last && s.t - m_last->t <= kMaybeLookbackMs ? *m_last : s);
      Add(s);
      m_state = State::Maybe;
    }
    break;
  case State::Maybe:
  {
    Add(s);
    if (moving)
      m_lastMoving = s;
    double const elapsed = s.t - m_drive.startedAt;
    if ((m_drive.meters >= kConfirmMeters && elapsed >= kConfirmMs) ||
        (s.activity == Activity::Automotive && m_drive.meters >= kConfirmMetersAutomotive))
    {
      m_state = State::Driving;
      m_justStarted = true;
    }
    else if (s.t - m_lastMoving.t >= kGiveUpMaybeMs || (m_walkingSince && s.t - m_walkingSince >= kWalkedAwayMs))
    {
      m_state = State::Idle;
    }
    break;
  }
  case State::Driving:
    Add(s);
    if (moving)
    {
      m_lastMoving = s;
      m_metersAtLastMoving = m_drive.meters;
    }
    if (s.t - m_lastMoving.t >= kParkedMs ||
        (m_walkingSince && s.t - m_walkingSince >= kWalkedAwayMs && speed < kWalkingMaxSpeedMps))
    {
      m_last = s;
      return End();
    }
    break;
  }
  m_last = s;
  return ended;
}

std::optional<Drive> DriveDetector::Finish()
{
  if (m_state == State::Driving)
    return End();
  m_state = State::Idle;
  return {};
}

void DriveDetector::Begin(DriveSample const & anchor)
{
  m_drive = {};
  m_drive.startedAt = m_drive.endedAt = anchor.t;
  m_drive.from = m_drive.to = anchor.at;
  m_drive.path = {anchor.at};
  m_pathMeters = 0;
  m_lastMoving = anchor;
  m_metersAtLastMoving = 0;
  m_walkingSince = 0;
  if (!anchor.region.empty())
    m_drive.regions = {StateOf(anchor.region)};
}

void DriveDetector::Add(DriveSample const & s)
{
  double const step = Distance(m_drive.to, s.at);
  double const dt = (s.t - m_drive.endedAt) / 1000;
  if (dt <= 0 || step / dt > kFastestMps)
    return;
  m_drive.meters += step;
  m_drive.to = s.at;
  m_drive.endedAt = s.t;
  m_pathMeters += step;
  if (m_pathMeters >= kPathStepMeters)
  {
    m_drive.path.push_back(s.at);
    m_pathMeters = 0;
  }
  if (!s.region.empty())
  {
    std::string const state = StateOf(s.region);
    if (m_drive.regions.empty() || m_drive.regions.back() != state)
      m_drive.regions.push_back(state);
  }
}

std::optional<Drive> DriveDetector::End()
{
  m_state = State::Idle;
  m_walkingSince = 0;
  Drive d = std::move(m_drive);
  m_drive = {};
  // It ended where the car stopped, not where the driver wandered off to.
  d.to = m_lastMoving.at;
  d.endedAt = m_lastMoving.t;
  d.meters = m_metersAtLastMoving;
  d.path.push_back(d.to);
  if (d.meters < kMinDriveMeters)
    return {};
  return d;
}

// Regions ---------------------------------------------------------------------------------------

std::string StateOf(std::string const & region)
{
  auto const first = region.find('_');
  if (first == std::string::npos)
    return region;
  // Big countries are split by state or province in the map files: "US_Colorado_Denver".
  for (char const * split : {"US", "Canada", "Australia", "Mexico", "Brazil", "Germany", "India"})
  {
    if (region.compare(0, first, split) == 0)
      return region.substr(0, region.find('_', first + 1));
  }
  return region.substr(0, first);
}

std::string RegionName(std::string const & state)
{
  auto const first = state.find('_');
  std::string name = first == std::string::npos ? state : state.substr(first + 1);
  std::replace(name.begin(), name.end(), '_', ' ');
  return name;
}

// Sorting ---------------------------------------------------------------------------------------

LoggedDrive Classify(Drive const & drive, std::vector<MileagePlace> const & places,
                     std::vector<LoggedDrive> const & log, MileageSettings const & settings,
                     LocalTime const & localTime)
{
  LoggedDrive out;
  out.drive = drive;
  auto const * from = PlaceAt(places, drive.from);
  auto const * to = PlaceAt(places, drive.to);
  if (from)
    out.fromName = from->name;
  if (to)
    out.toName = to->name;

  // Out of habit?
  if (!settings.homeState.empty())
  {
    for (auto const & r : drive.regions)
    {
      if (!r.empty() && r != settings.homeState)
        out.flags |= kOutOfState;
    }
  }
  auto const home = std::find_if(places.begin(), places.end(), [](auto const & p) { return p.role == PlaceRole::Home; });
  if (home != places.end())
  {
    double far = std::max(Distance(home->at, drive.from), Distance(home->at, drive.to));
    for (auto const & p : drive.path)
      far = std::max(far, Distance(home->at, p));
    std::vector<double> usual;
    for (auto const & d : log)
      usual.push_back(std::max(Distance(home->at, d.drive.from), Distance(home->at, d.drive.to)));
    double limit = kFarFromHomeMeters;
    if (usual.size() >= 10)
    {
      std::sort(usual.begin(), usual.end());
      limit = std::max(limit, 1.5 * usual[usual.size() * 95 / 100]);
    }
    if (far > limit)
      out.flags |= kFarFromHome;
  }
  if (!to && log.size() >= 10 &&
      std::none_of(log.begin(), log.end(), [&](auto const & d) {
        return SameSpot(d.drive.to, drive.to) || SameSpot(d.drive.from, drive.to);
      }))
  {
    out.flags |= kNewPlace;
  }

  auto const sorted = [&](Bucket b, Why w) {
    out.bucket = b;
    out.why = w;
    return out;
  };

  // 1. The driver's word wins: they sorted this trip before, and always the same way.
  std::optional<Bucket> said;
  bool agree = true;
  for (auto const & d : log)
  {
    if (d.why != Why::DriverSaid || !SameTrip(d.drive, drive))
      continue;
    agree = agree && (!said || *said == d.bucket);
    said = d.bucket;
  }
  if (said && agree && *said != Bucket::Unknown)
    return sorted(*said, Why::Learned);

  // 2. Known places, IRS style (Publication 463, "Transportation").
  if (Is(from, PlaceRole::Personal) || Is(to, PlaceRole::Personal))
    return sorted(Bucket::Personal, Why::PersonalPlace);
  bool const hasOffice =
      std::any_of(places.begin(), places.end(), [](auto const & p) { return p.role == PlaceRole::Office; });
  bool const fromHome = Is(from, PlaceRole::Home);
  bool const toHome = Is(to, PlaceRole::Home);
  if ((fromHome && Is(to, PlaceRole::Office)) || (toHome && Is(from, PlaceRole::Office)))
  {
    return settings.homeIsMainWorkplace ? sorted(Bucket::Business, Why::BetweenWorkplaces)
                                        : sorted(Bucket::Personal, Why::Commute);
  }
  if ((fromHome && Is(to, PlaceRole::Client)) || (toHome && Is(from, PlaceRole::Client)))
  {
    if (settings.homeIsMainWorkplace || hasOffice)
      return sorted(Bucket::Business, Why::ToClient);
    return sorted(Bucket::Unknown, Why::NoClue);
  }
  if (IsWork(from) && IsWork(to) && from != to)
    return sorted(Bucket::Business, Why::BetweenWorkplaces);

  // 3. Anything out of the ordinary waits for the driver.
  if (out.flags != 0)
    return sorted(Bucket::Unknown, Why::NoClue);

  // 4. A day the driver never goes to the office, and nothing about it looks like work.
  if (hasOffice && !IsWork(from) && !IsWork(to))
  {
    std::array<int, 7> officeDays{};
    int officeDrives = 0;
    for (auto const & d : log)
    {
      if (Is(PlaceAt(places, d.drive.to), PlaceRole::Office) || Is(PlaceAt(places, d.drive.from), PlaceRole::Office))
      {
        ++officeDays[localTime(d.drive.startedAt).tm_wday];
        ++officeDrives;
      }
    }
    if (officeDrives >= 8 && officeDays[localTime(drive.startedAt).tm_wday] == 0)
      return sorted(Bucket::Personal, Why::DayOff);
  }
  return sorted(Bucket::Unknown, Why::NoClue);
}

size_t Reassign(std::vector<LoggedDrive> & log, size_t index, Bucket bucket, bool similarToo,
                std::string const & purpose)
{
  if (index >= log.size())
    return 0;
  auto & target = log[index];
  target.bucket = bucket;
  target.why = bucket == Bucket::Unknown ? Why::NoClue : Why::DriverSaid;
  if (!purpose.empty())
    target.purpose = purpose;
  size_t changed = 1;
  if (!similarToo || bucket == Bucket::Unknown)
    return changed;
  for (size_t i = 0; i < log.size(); ++i)
  {
    auto & d = log[i];
    if (i == index || d.bucket != Bucket::Unknown || !SameTrip(d.drive, target.drive))
      continue;
    d.bucket = bucket;
    d.why = Why::Learned;
    if (!purpose.empty() && d.purpose.empty())
      d.purpose = purpose;
    ++changed;
  }
  return changed;
}

// The year --------------------------------------------------------------------------------------

double IrsRatePerMile(int year)
{
  switch (year)
  {
  case 2023: return 0.655;
  case 2024: return 0.67;
  case 2025: return 0.70;
  case 2026: return 0.725;
  default: return 0;
  }
}

MileageYear SummarizeYear(std::vector<LoggedDrive> const & log, int year, LocalTime const & localTime)
{
  MileageYear y;
  y.year = year;
  y.ratePerMile = IrsRatePerMile(year);
  for (size_t i = 0; i < log.size(); ++i)
  {
    auto const & d = log[i];
    if (Year(localTime, d.drive.startedAt) != year)
      continue;
    double const miles = d.drive.Miles();
    switch (d.bucket)
    {
    case Bucket::Business:
      y.businessMiles += miles;
      ++y.businessDrives;
      break;
    case Bucket::Personal:
      y.personalMiles += miles;
      ++y.personalDrives;
      if (d.why == Why::Commute)
        y.commuteMiles += miles;
      break;
    case Bucket::Unknown:
      y.unknownMiles += miles;
      ++y.unknownDrives;
      y.unknown.push_back(i);
      break;
    }
    if (d.flags != 0)
      y.flagged.push_back(i);
  }
  std::stable_sort(y.unknown.begin(), y.unknown.end(),
                   [&](size_t a, size_t b) { return log[a].drive.meters > log[b].drive.meters; });
  y.estimatedDeduction = y.businessMiles * y.ratePerMile;
  return y;
}

char const * BucketName(Bucket b)
{
  switch (b)
  {
  case Bucket::Business: return "Business";
  case Bucket::Personal: return "Personal";
  case Bucket::Unknown: return "Unknown";
  }
  return "Unknown";
}

char const * WhyName(Why w)
{
  switch (w)
  {
  case Why::NoClue: return "NoClue";
  case Why::DriverSaid: return "DriverSaid";
  case Why::Learned: return "Learned";
  case Why::Commute: return "Commute";
  case Why::BetweenWorkplaces: return "BetweenWorkplaces";
  case Why::ToClient: return "ToClient";
  case Why::PersonalPlace: return "PersonalPlace";
  case Why::DayOff: return "DayOff";
  }
  return "NoClue";
}

namespace
{
char const * WhyText(Why w)
{
  switch (w)
  {
  case Why::NoClue: return "Not sorted yet";
  case Why::DriverSaid: return "Sorted by you";
  case Why::Learned: return "Same as a trip you sorted";
  case Why::Commute: return "Commute (home to regular workplace)";
  case Why::BetweenWorkplaces: return "Between work locations";
  case Why::ToClient: return "To a client or job site";
  case Why::PersonalPlace: return "Personal place";
  case Why::DayOff: return "Day off";
  }
  return "";
}

std::string Csv(std::string const & s)
{
  if (s.find_first_of(",\"\n") == std::string::npos)
    return s;
  std::string out = "\"";
  for (char c : s)
    out += c == '"' ? std::string("\"\"") : std::string(1, c);
  return out + "\"";
}

std::string Where(std::string const & name, LatLon at)
{
  return name.empty() ? Format("%.5f", at.lat) + " " + Format("%.5f", at.lon) : name;
}

std::string Clock(std::tm const & t)
{
  char buf[40];
  std::snprintf(buf, sizeof buf, "%02d:%02d", t.tm_hour, t.tm_min);
  return buf;
}
}  // namespace

std::string MileageCsv(std::vector<LoggedDrive> const & log, int year, LocalTime const & localTime)
{
  std::vector<LoggedDrive const *> drives;
  for (auto const & d : log)
  {
    if (Year(localTime, d.drive.startedAt) == year)
      drives.push_back(&d);
  }
  std::stable_sort(drives.begin(), drives.end(),
                   [](auto const * a, auto const * b) { return a->drive.startedAt < b->drive.startedAt; });

  std::ostringstream out;
  out << "Date,Start,End,From,To,Miles,Category,Purpose,Reason,Notes\n";
  for (auto const * d : drives)
  {
    std::tm const start = localTime(d->drive.startedAt);
    char date[40];
    std::snprintf(date, sizeof date, "%04d-%02d-%02d", start.tm_year + 1900, start.tm_mon + 1, start.tm_mday);
    std::string category = BucketName(d->bucket);
    if (d->why == Why::Commute)
      category += " (commute)";
    std::string notes;
    auto const note = [&](std::string const & n) { notes += (notes.empty() ? "" : "; ") + n; };
    if (d->flags & kOutOfState)
    {
      std::string states;
      for (auto const & r : d->drive.regions)
        states += (states.empty() ? "" : ", ") + RegionName(r);
      note("Out of state: " + states);
    }
    if (d->flags & kFarFromHome)
      note("Far from home");
    if (d->flags & kNewPlace)
      note("New place");
    out << date << ',' << Clock(start) << ',' << Clock(localTime(d->drive.endedAt)) << ','
        << Csv(Where(d->fromName, d->drive.from)) << ',' << Csv(Where(d->toName, d->drive.to)) << ','
        << Format("%.1f", d->drive.Miles()) << ',' << Csv(category) << ',' << Csv(d->purpose) << ','
        << Csv(WhyText(d->why)) << ',' << Csv(notes) << '\n';
  }

  auto const y = SummarizeYear(log, year, localTime);
  out << '\n'
      << "Business miles," << Format("%.1f", y.businessMiles) << '\n'
      << "Personal miles," << Format("%.1f", y.personalMiles) << '\n'
      << "Of which commuting," << Format("%.1f", y.commuteMiles) << '\n'
      << "Unsorted miles," << Format("%.1f", y.unknownMiles) << '\n';
  if (y.ratePerMile > 0)
  {
    out << Csv("Estimate at IRS standard rate ($" + Format("%.3f", y.ratePerMile) + "/mile)") << ','
        << Format("%.2f", y.estimatedDeduction) << '\n';
  }
  out << "Miles are from GPS. Note your odometer on January 1 and December 31.\n";
  return out.str();
}

// Storage ---------------------------------------------------------------------------------------

std::string SerializeDrive(LoggedDrive const & d)
{
  std::string regions;
  for (auto const & r : d.drive.regions)
    regions += (regions.empty() ? "" : ";") + Clean(r);
  std::ostringstream out;
  out << Format("%.0f", d.drive.startedAt) << '\t' << Format("%.0f", d.drive.endedAt) << '\t'
      << LatLonText(d.drive.from) << '\t' << LatLonText(d.drive.to) << '\t' << Format("%.1f", d.drive.meters) << '\t'
      << regions << '\t' << Clean(d.fromName) << '\t' << Clean(d.toName) << '\t' << BucketName(d.bucket) << '\t'
      << WhyName(d.why) << '\t' << d.flags << '\t' << Clean(d.purpose);
  return out.str();
}

std::optional<LoggedDrive> ParseDrive(std::string const & line)
{
  auto const f = Split(line, '\t');
  if (f.size() < 12)
    return {};
  try
  {
    LoggedDrive d;
    d.drive.startedAt = std::stod(f[0]);
    d.drive.endedAt = std::stod(f[1]);
    auto const from = ParseLatLon(f[2]);
    auto const to = ParseLatLon(f[3]);
    if (!from || !to)
      return {};
    d.drive.from = *from;
    d.drive.to = *to;
    d.drive.meters = std::stod(f[4]);
    if (!f[5].empty())
      d.drive.regions = Split(f[5], ';');
    d.fromName = f[6];
    d.toName = f[7];
    for (Bucket b : {Bucket::Unknown, Bucket::Business, Bucket::Personal})
    {
      if (f[8] == BucketName(b))
        d.bucket = b;
    }
    for (Why w : {Why::NoClue, Why::DriverSaid, Why::Learned, Why::Commute, Why::BetweenWorkplaces, Why::ToClient,
                  Why::PersonalPlace, Why::DayOff})
    {
      if (f[9] == WhyName(w))
        d.why = w;
    }
    d.flags = static_cast<unsigned>(std::stoul(f[10]));
    d.purpose = f[11];
    return d;
  }
  catch (std::exception const &)
  {
    return {};
  }
}
}  // namespace backseat
