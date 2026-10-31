#include "backseat/debug_log.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace backseat
{
namespace
{
std::atomic<DebugLog *> g_log{nullptr};

double SystemNowMs()
{
  using namespace std::chrono;
  return static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

/// 2026-10-27T12:42:04.123Z
std::string IsoUtc(double ms)
{
  auto const whole = static_cast<std::time_t>(std::floor(ms / 1000));
  int const millis = static_cast<int>(ms - static_cast<double>(whole) * 1000);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &whole);
#else
  gmtime_r(&whole, &tm);
#endif
  char buf[96];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec, millis);
  return buf;
}

void AppendJsonString(std::string & out, std::string const & s)
{
  out += '"';
  for (unsigned char c : s)
  {
    switch (c)
    {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 0x20)
      {
        char buf[8];
        std::snprintf(buf, sizeof buf, "\\u%04x", c);
        out += buf;
      }
      else
      {
        out += static_cast<char>(c);
      }
    }
  }
  out += '"';
}

bool FileExists(std::string const & path) { return std::ifstream(path).good(); }
}  // namespace

std::string LogNum(double value, int decimals)
{
  if (!std::isfinite(value))
    return "";
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, value);
  std::string s = buf;
  if (s.find('.') != std::string::npos)
  {
    while (s.back() == '0')
      s.pop_back();
    if (s.back() == '.')
      s.pop_back();
  }
  return s == "-0" ? "0" : s;
}

DebugLog::DebugLog(Options options, std::function<double()> nowMs)
  : m_options(std::move(options)), m_nowMs(nowMs ? std::move(nowMs) : SystemNowMs)
{
  if (!m_options.dir.empty() && m_options.dir.back() != '/')
    m_options.dir += '/';
}

std::string DebugLog::PathFor(int index) const
{
  return m_options.dir + m_options.baseName + (index == 0 ? "" : "." + std::to_string(index)) + ".jsonl";
}

void DebugLog::OpenLocked()
{
  if (m_out.is_open())
    return;
  m_out.open(PathFor(0), std::ios::app | std::ios::binary);
  m_out.seekp(0, std::ios::end);
  auto const pos = m_out.tellp();
  m_bytes = pos > 0 ? static_cast<std::size_t>(pos) : 0;
}

void DebugLog::RotateLocked()
{
  m_out.close();
  std::remove(PathFor(m_options.keepFiles).c_str());
  for (int i = m_options.keepFiles - 1; i >= 0; --i)
    std::rename(PathFor(i).c_str(), PathFor(i + 1).c_str());
  m_bytes = 0;
}

void DebugLog::WriteLocked(std::string const & ev, LogFields const & fields)
{
  if (!m_enabled)
    return;
  double const now = m_nowMs();
  std::string line = "{\"t\":";
  AppendJsonString(line, IsoUtc(now));
  line += ",\"dt\":";
  AppendJsonString(line, m_driveStartMs < 0 ? "" : LogNum((now - m_driveStartMs) / 1000, 1));
  line += ",\"ev\":";
  AppendJsonString(line, ev);
  for (auto const & [name, value] : fields)
  {
    line += ',';
    AppendJsonString(line, name);
    line += ':';
    AppendJsonString(line, value);
  }
  line += "}\n";

  OpenLocked();
  if (m_bytes > 0 && m_bytes + line.size() > m_options.maxFileBytes)
  {
    RotateLocked();
    OpenLocked();
  }
  if (!m_out)
    return;
  m_out << line;
  // Flushed every time: the app can be killed mid-drive, and the end of the drive is what matters.
  m_out.flush();
  m_bytes += line.size();
}

void DebugLog::Write(std::string const & ev, LogFields const & fields)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  WriteLocked(ev, fields);
}

void DebugLog::WriteThrottled(std::string const & ev, std::string const & key, LogFields const & fields)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_enabled)
    return;
  auto & throttle = m_throttles[ev + '\x1f' + key];
  double const now = m_nowMs();
  if (now - throttle.lastMs < m_options.repeatWindowMs)
  {
    ++throttle.skipped;
    return;
  }
  throttle.lastMs = now;
  if (throttle.skipped == 0)
  {
    WriteLocked(ev, fields);
    return;
  }
  LogFields withCount = fields;
  withCount.emplace_back("skipped", std::to_string(throttle.skipped));
  throttle.skipped = 0;
  WriteLocked(ev, withCount);
}

bool DebugLog::SampleDue(double tMs)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_enabled || tMs - m_lastSampleT < m_options.sampleEveryMs)
    return false;
  m_lastSampleT = tMs;
  return true;
}

void DebugLog::BeginDrive(std::string const & reason, LogFields fields)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  // A trip starting during a drive the mileage detector already saw is the same drive.
  if (m_driveStartMs >= 0)
  {
    fields.insert(fields.begin(), {"reason", reason});
    WriteLocked("drive.also", fields);
    return;
  }
  m_driveStartMs = m_nowMs();
  m_throttles.clear();
  fields.insert(fields.begin(), {"reason", reason});
  fields.insert(fields.begin(), {"drive", std::to_string(++m_drives)});
  WriteLocked("drive.start", fields);
}

void DebugLog::EndDrive(std::string const & reason, LogFields fields)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_driveStartMs < 0)
    return;
  fields.insert(fields.begin(), {"reason", reason});
  WriteLocked("drive.end", fields);
  m_driveStartMs = -1;
}

void DebugLog::SetEnabled(bool enabled)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_enabled == enabled)
    return;
  if (!enabled)
    WriteLocked("log.off", {});
  m_enabled = enabled;
  if (enabled)
    WriteLocked("log.on", {});
  else
    m_out.close();
}

bool DebugLog::Enabled() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_enabled;
}

std::vector<std::string> DebugLog::Files() const
{
  std::vector<std::string> files;
  for (int i = m_options.keepFiles; i >= 0; --i)
    if (auto const path = PathFor(i); FileExists(path))
      files.push_back(path);
  return files;
}

bool DebugLog::ExportTo(std::string const & path, LogFields const & header) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  std::ofstream out(path, std::ios::trunc | std::ios::binary);
  if (!out)
    return false;
  std::string first = "{\"t\":";
  AppendJsonString(first, IsoUtc(m_nowMs()));
  first += ",\"ev\":\"export\"";
  for (auto const & [name, value] : header)
  {
    first += ',';
    AppendJsonString(first, name);
    first += ':';
    AppendJsonString(first, value);
  }
  out << first << "}\n";
  for (int i = m_options.keepFiles; i >= 0; --i)
  {
    std::ifstream in(PathFor(i), std::ios::binary);
    // Streaming an empty file would mark `out` as failed.
    if (in && in.peek() != std::ifstream::traits_type::eof())
      out << in.rdbuf();
  }
  return static_cast<bool>(out);
}

void DebugLog::Clear()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_out.close();
  for (int i = 0; i <= m_options.keepFiles; ++i)
    std::remove(PathFor(i).c_str());
  m_bytes = 0;
}

void SetDebugLog(DebugLog * log) { g_log.store(log); }
DebugLog * GetDebugLog() { return g_log.load(); }

void Trace(std::string const & ev, LogFields const & fields)
{
  if (auto * log = g_log.load())
    log->Write(ev, fields);
}

void TraceThrottled(std::string const & ev, std::string const & key, LogFields const & fields)
{
  if (auto * log = g_log.load())
    log->WriteThrottled(ev, key, fields);
}
}  // namespace backseat
