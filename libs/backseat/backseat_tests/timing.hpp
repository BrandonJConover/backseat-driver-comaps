// Per-test timings for the harness (loop B102): main.cpp times each test and prints the slowest
// few before the final count line, so a scout can see where the ~2 minute suite goes.
#pragma once

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace backseat_tests
{
// Wall-clock ceilings in tests are there to catch a pass gone quadratic, not to time a sanitizer. CI's
// engine job is a Debug build with ASan and UBSan, where the text and listening passes run far slower than release
// and the speed tests sit right at their ceilings (they moved past them as the
// pronunciation and command passes grew). Those builds get twice the room; plain builds keep the tight bound.
#if defined(__SANITIZE_ADDRESS__)
inline constexpr double kSanitizerSlack = 2.0;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
inline constexpr double kSanitizerSlack = 2.0;
#else
inline constexpr double kSanitizerSlack = 1.0;
#endif
#else
inline constexpr double kSanitizerSlack = 1.0;
#endif

struct Timing
{
  std::string name;
  long long ms = 0;
};

// "slowest:" then up to `count` tests, slowest first (ties keep run order), one per line as
// "  <ms> ms  <name>". Empty when there are no timings or count is 0.
inline std::string SlowestReport(std::vector<Timing> timings, std::size_t count)
{
  if (timings.empty() || count == 0)
    return {};
  std::stable_sort(timings.begin(), timings.end(), [](Timing const & a, Timing const & b) { return a.ms > b.ms; });
  if (timings.size() > count)
    timings.resize(count);
  std::ostringstream os;
  os << "slowest:\n";
  for (auto const & t : timings)
    os << "  " << t.ms << " ms  " << t.name << "\n";
  return os.str();
}

// Every test's time in run order, for BACKSEAT_TEST_TIMES=all.
inline std::string AllTimesReport(std::vector<Timing> const & timings)
{
  std::ostringstream os;
  for (auto const & t : timings)
    os << "time " << t.ms << " ms  " << t.name << "\n";
  return os.str();
}
}  // namespace backseat_tests
