#include "check.hpp"
#include "timing.hpp"

using backseat_tests::AllTimesReport;
using backseat_tests::SlowestReport;
using backseat_tests::Timing;

TEST(SlowestReportListsTheSlowestFirstAndCapsTheCount)
{
  std::vector<Timing> t{{"a", 5}, {"b", 900}, {"c", 40}, {"d", 900}, {"e", 1}, {"f", 300}, {"g", 2}};
  CHECK_EQUAL(SlowestReport(t, 5), std::string("slowest:\n"
                                               "  900 ms  b\n"
                                               "  900 ms  d\n"
                                               "  300 ms  f\n"
                                               "  40 ms  c\n"
                                               "  5 ms  a\n"));
}

TEST(SlowestReportHandlesFewTestsAndNone)
{
  CHECK_EQUAL(SlowestReport({{"only", 7}}, 5), std::string("slowest:\n  7 ms  only\n"));
  CHECK_EQUAL(SlowestReport({}, 5), std::string());
  CHECK_EQUAL(SlowestReport({{"x", 1}}, 0), std::string());
}

TEST(AllTimesReportKeepsRunOrder)
{
  CHECK_EQUAL(AllTimesReport({{"z", 3}, {"a", 10}}), std::string("time 3 ms  z\ntime 10 ms  a\n"));
}
