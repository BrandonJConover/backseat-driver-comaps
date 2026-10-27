// Mileage log: notices when you start driving (no navigation needed), records the drive, and sorts
// it into Business, Personal or Unknown from the places it has learned. At the end of the year the
// driver only has to move the unknowns.
//
// Everything stays on the phone. The host feeds location and motion updates and persists the log
// (one SerializeDrive() line per drive, never capped).
#pragma once

#include "backseat/types.hpp"

#include <ctime>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace backseat
{
/// What the phone's motion coprocessor thinks we're doing (CMMotionActivity / Activity Recognition).
enum class Activity
{
  Unknown,
  Stationary,
  Walking,
  Running,
  Cycling,
  Automotive
};

struct DriveSample
{
  /// ms since the epoch.
  double t = 0;
  LatLon at;
  /// From GPS; negative if the OS didn't give one (it's then worked out from the positions).
  double speedMps = -1;
  /// Horizontal accuracy in meters; 0 if unknown.
  double accuracyM = 0;
  Activity activity = Activity::Unknown;
  /// Map region under the car, e.g. "US_Colorado_Denver" (storage::CountryInfoGetter). Optional.
  std::string region;
};

/// One drive, from pulling away to parking.
struct Drive
{
  double startedAt = 0;
  double endedAt = 0;
  LatLon from;
  LatLon to;
  double meters = 0;
  /// States/countries driven through, in order, as StateOf() names them.
  std::vector<std::string> regions;
  /// A point about every 500 m. Kept in memory for the flags; not persisted.
  std::vector<LatLon> path;

  double Miles() const { return meters / 1609.344; }
};

/// Turns a stream of location/motion updates into drives.
///
/// A drive starts once the car has gone 400 m in at least 30 s at driving speed (or 200 m while the
/// phone says Automotive). It ends after 4 minutes without moving (long enough for a bad light), as
/// soon as the driver has walked for 30 s, or when updates stop for 10 minutes. Drives under half a
/// mile are thrown away.
class DriveDetector
{
public:
  /// Feed every update, in order. Returns the drive that just ended, if one did.
  std::optional<Drive> Feed(DriveSample const & s);
  /// The OS says we've arrived somewhere (CLVisit) or the app is going away.
  std::optional<Drive> Finish();

  bool Driving() const { return m_state == State::Driving; }
  /// True once, right after a drive is confirmed.
  bool TakeStarted() { return std::exchange(m_justStarted, false); }

private:
  enum class State
  {
    Idle,
    Maybe,
    Driving
  };

  void Begin(DriveSample const & anchor);
  void Add(DriveSample const & s);
  std::optional<Drive> End();

  State m_state = State::Idle;
  bool m_justStarted = false;
  Drive m_drive;
  std::optional<DriveSample> m_last;
  /// Last time and place the car was clearly moving.
  DriveSample m_lastMoving;
  double m_metersAtLastMoving = 0;
  /// Since when the phone has said Walking/Running, 0 if it hasn't.
  double m_walkingSince = 0;
  LatLon m_pathTail;
  double m_pathMeters = 0;
};

/// "US_Colorado_Denver" -> "US_Colorado"; "France_Paris" -> "France". Empty stays empty.
std::string StateOf(std::string const & region);
/// "US_Colorado" -> "Colorado"; "France" -> "France".
std::string RegionName(std::string const & state);

enum class Bucket
{
  Unknown,
  Business,
  Personal
};

enum class PlaceRole
{
  Home,
  /// The regular workplace.
  Office,
  /// A client, job site, supplier: anywhere driving to is business.
  Client,
  /// Gym, Mom's house, the store.
  Personal
};

struct MileagePlace
{
  std::string name;
  LatLon at;
  PlaceRole role = PlaceRole::Personal;
  double radiusM = 200;
};

struct MileageSettings
{
  /// IRS: if home is the principal place of business, driving from home to other work places is
  /// business, not commuting.
  bool homeIsMainWorkplace = false;
  /// StateOf() the driver lives in, e.g. "US_Colorado". Drives that leave it get flagged.
  std::string homeState;
};

/// Why a drive landed in its bucket. Persisted by name, so only append.
enum class Why
{
  /// Not sorted yet.
  NoClue,
  /// The driver sorted it.
  DriverSaid,
  /// The driver sorted the same trip the same way before.
  Learned,
  /// Home to the regular workplace (or back): personal under IRS rules, shown on its own line.
  Commute,
  /// Between two work places (office, clients).
  BetweenWorkplaces,
  /// Home to a client or job site while there's a regular workplace (a temporary work location).
  ToClient,
  /// To or from a place marked personal.
  PersonalPlace,
  /// A day the driver never goes to work.
  DayOff
};

/// Out-of-habit markers on a drive. Flagged drives with no clear place rule stay Unknown.
enum Flag : unsigned
{
  kOutOfState = 1,
  /// Went much farther from home than usual (and at least 50 miles).
  kFarFromHome = 2,
  /// Ended somewhere it's never been.
  kNewPlace = 4,
};

struct LoggedDrive
{
  Drive drive;
  std::string fromName;
  std::string toName;
  Bucket bucket = Bucket::Unknown;
  Why why = Why::NoClue;
  unsigned flags = 0;
  /// What the driver said it was for ("client meeting, Acme"). The IRS wants this for business.
  std::string purpose;
};

using LocalTime = std::function<std::tm(double)>;

/// Sort a drive that just ended. `log` is every drive so far (to learn from what the driver sorted
/// before and to know what's usual).
LoggedDrive Classify(Drive const & drive, std::vector<MileagePlace> const & places,
                     std::vector<LoggedDrive> const & log, MileageSettings const & settings,
                     LocalTime const & localTime);

/// Put drive `index` in `bucket`. With `similarToo`, every other Unknown drive between the same two
/// places (~250 m) goes there as well. Returns how many drives changed.
size_t Reassign(std::vector<LoggedDrive> & log, size_t index, Bucket bucket, bool similarToo,
                std::string const & purpose = {});

struct MileageYear
{
  int year = 0;
  double businessMiles = 0;
  /// Includes commuteMiles.
  double personalMiles = 0;
  double commuteMiles = 0;
  double unknownMiles = 0;
  int businessDrives = 0;
  int personalDrives = 0;
  int unknownDrives = 0;
  /// IRS standard rate for the year in dollars per mile, 0 if we don't know it.
  double ratePerMile = 0;
  /// businessMiles * ratePerMile.
  double estimatedDeduction = 0;
  /// Indexes into the log of this year's Unknown drives, the most miles first.
  std::vector<size_t> unknown;
  /// Indexes of this year's flagged drives (out of state, far from home, new place).
  std::vector<size_t> flagged;
};

/// IRS business standard mileage rate, dollars per mile (2024: 0.67, 2025: 0.70, 2026: 0.725).
/// 0 for years we don't know; the app should then ask.
double IrsRatePerMile(int year);

MileageYear SummarizeYear(std::vector<LoggedDrive> const & log, int year, LocalTime const & localTime);

/// The year's drives as CSV, oldest first: Date, Start, End, From, To, Miles, Category, Purpose,
/// Reason, Notes. Totals at the bottom.
std::string MileageCsv(std::vector<LoggedDrive> const & log, int year, LocalTime const & localTime);

/// One tab-separated line, no newline.
std::string SerializeDrive(LoggedDrive const & d);
std::optional<LoggedDrive> ParseDrive(std::string const & line);

char const * BucketName(Bucket b);
char const * WhyName(Why w);
}  // namespace backseat
