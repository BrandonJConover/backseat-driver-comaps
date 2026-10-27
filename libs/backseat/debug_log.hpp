// The drive log: a record of what the GPS did on each drive, so a test drive can be troubleshot
// afterwards. Which lines were picked and spoken, by which voice, which remarks were held back and
// why, route changes, a GPS sample every few seconds, mileage detector changes and errors.
//
// It stays on the phone. Nothing is uploaded; the driver shares it by hand (Settings > Backseat
// Driver > Share drive log). Files are JSON Lines, one event per line, capped and rotated.
//
// Anything can write to it through Trace() without plumbing; it does nothing until the host
// installs a log with SetDebugLog(), so the engine and its tests don't change behavior.
#pragma once

#include <cstddef>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace backseat
{
/// Field name and value, written in order. Values are written as JSON strings.
using LogFields = std::vector<std::pair<std::string, std::string>>;

/// Numbers for log fields, without trailing zeros: LogNum(3.50, 2) is "3.5".
std::string LogNum(double value, int decimals = 1);

class DebugLog
{
public:
  struct Options
  {
    /// Folder for the files. Must exist.
    std::string dir;
    std::string baseName = "backseat_drive_log";
    /// The current file rolls over at this size. With keepFiles = 4 the log tops out near 5 MB.
    std::size_t maxFileBytes = 1024 * 1024;
    /// Rolled-over files kept: base.1.jsonl (newest) .. base.N.jsonl (oldest).
    int keepFiles = 4;
    /// The same throttled event (WriteThrottled) is written at most this often; the rest are counted.
    double repeatWindowMs = 30000;
    /// SampleDue() says yes this often.
    double sampleEveryMs = 5000;
  };

  /// `nowMs` is wall-clock milliseconds since the epoch; tests swap it.
  explicit DebugLog(Options options, std::function<double()> nowMs = {});

  /// Writes one event: {"t":"2026-10-27T12:42:04.123Z","dt":"12.3","ev":"say",...fields}.
  /// `dt` is seconds since BeginDrive(), or empty between drives.
  void Write(std::string const & ev, LogFields const & fields = {});
  /// Like Write, but the same `ev` + `key` is written at most once per repeatWindowMs. The next
  /// one that gets through carries "skipped":"N", the number held back since the last.
  void WriteThrottled(std::string const & ev, std::string const & key, LogFields const & fields = {});
  /// For periodic samples (GPS fixes): true at most once per sampleEveryMs of `tMs`.
  bool SampleDue(double tMs);

  /// Marks the start of a drive (a trip starting, or the mileage detector seeing a drive).
  void BeginDrive(std::string const & reason, LogFields fields = {});
  void EndDrive(std::string const & reason, LogFields fields = {});

  /// Off: nothing is written, and the files stay as they are.
  void SetEnabled(bool enabled);
  bool Enabled() const;

  /// Existing log files, oldest first.
  std::vector<std::string> Files() const;
  /// The whole log, oldest first, in one file at `path`, starting with a header event built
  /// from `header` (app version, device, settings). False if it couldn't be written.
  bool ExportTo(std::string const & path, LogFields const & header) const;
  /// Deletes every log file.
  void Clear();

private:
  std::string PathFor(int index) const;
  void WriteLocked(std::string const & ev, LogFields const & fields);
  void RotateLocked();
  void OpenLocked();

  Options m_options;
  std::function<double()> m_nowMs;
  mutable std::mutex m_mutex;
  std::ofstream m_out;
  std::size_t m_bytes = 0;
  bool m_enabled = true;
  double m_driveStartMs = -1;
  int m_drives = 0;
  double m_lastSampleT = -1e18;
  struct Throttle
  {
    double lastMs = -1e18;
    int skipped = 0;
  };
  std::map<std::string, Throttle> m_throttles;
};

/// The log Trace() writes to. Not owned; null (the default) turns tracing off.
void SetDebugLog(DebugLog * log);
DebugLog * GetDebugLog();

/// Write to the installed log, if any. Cheap when there's none.
void Trace(std::string const & ev, LogFields const & fields = {});
void TraceThrottled(std::string const & ev, std::string const & key, LogFields const & fields = {});
}  // namespace backseat
