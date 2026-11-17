// Direct tests for units.cpp: the JavaScript-compatible number formatting the line
// templates and trip report rely on, and distances that are always safe to say.
#include "check.hpp"

#include "backseat/units.hpp"

#include <cmath>
#include <limits>
#include <string>

using namespace backseat;

namespace
{
double const kNaN = std::numeric_limits<double>::quiet_NaN();
double const kInf = std::numeric_limits<double>::infinity();
}  // namespace

TEST(JsNumberMatchesJavaScriptLayout)
{
  CHECK_EQUAL(JsNumber(0), std::string("0"));
  CHECK_EQUAL(JsNumber(-0.0), std::string("0"));
  CHECK_EQUAL(JsNumber(42), std::string("42"));
  CHECK_EQUAL(JsNumber(-7), std::string("-7"));
  CHECK_EQUAL(JsNumber(1.5), std::string("1.5"));
  CHECK_EQUAL(JsNumber(0.1), std::string("0.1"));
  CHECK_EQUAL(JsNumber(0.1 + 0.2), std::string("0.30000000000000004"));
  CHECK_EQUAL(JsNumber(-2.25), std::string("-2.25"));
  CHECK_EQUAL(JsNumber(123.456), std::string("123.456"));
  // Plain notation all the way to 1e21 (printf %g would switch to "1e+16").
  CHECK_EQUAL(JsNumber(1e16), std::string("10000000000000000"));
  CHECK_EQUAL(JsNumber(123456789012345680000.0), std::string("123456789012345680000"));
  CHECK_EQUAL(JsNumber(1e21), std::string("1e+21"));
  CHECK_EQUAL(JsNumber(1.5e300), std::string("1.5e+300"));
  // Small numbers: plain down to 1e-6, then "1e-7" (not printf's "1e-07").
  CHECK_EQUAL(JsNumber(0.00001), std::string("0.00001"));
  CHECK_EQUAL(JsNumber(0.000001), std::string("0.000001"));
  CHECK_EQUAL(JsNumber(1e-7), std::string("1e-7"));
  CHECK_EQUAL(JsNumber(1.25e-10), std::string("1.25e-10"));
  CHECK_EQUAL(JsNumber(5e-324), std::string("5e-324"));
  CHECK_EQUAL(JsNumber(std::numeric_limits<double>::max()), std::string("1.7976931348623157e+308"));
  CHECK_EQUAL(JsNumber(kNaN), std::string("NaN"));
  CHECK_EQUAL(JsNumber(kInf), std::string("Infinity"));
  CHECK_EQUAL(JsNumber(-kInf), std::string("-Infinity"));
}

TEST(JsNumberRoundTripsEveryValue)
{
  double const values[] = {1.0 / 3, 2.0 / 3, 1e-5 / 3, 12345.678901, 9007199254740993.0, 3.14159, 1e20 / 7};
  for (double v : values)
  {
    CHECK(std::strtod(JsNumber(v).c_str(), nullptr) == v);
    CHECK(std::strtod(JsNumber(-v).c_str(), nullptr) == -v);
  }
}

TEST(ToFixedRoundsTiesLikeJavaScript)
{
  // Exact binary ties go up (JavaScript picks the larger n); printf would round to even.
  CHECK_EQUAL(ToFixed(0.5, 0), std::string("1"));
  CHECK_EQUAL(ToFixed(2.5, 0), std::string("3"));
  CHECK_EQUAL(ToFixed(-2.5, 0), std::string("-3"));
  CHECK_EQUAL(ToFixed(1.125, 2), std::string("1.13"));
  CHECK_EQUAL(ToFixed(0.125, 2), std::string("0.13"));
  CHECK_EQUAL(ToFixed(9.5, 0), std::string("10"));
  CHECK_EQUAL(ToFixed(99.875, 2), std::string("99.88"));
  CHECK_EQUAL(ToFixed(9.75, 1), std::string("9.8"));
  // Not ties: the double sits just below the half, as in JavaScript.
  CHECK_EQUAL(ToFixed(1.005, 2), std::string("1.00"));
  CHECK_EQUAL(ToFixed(2.675, 2), std::string("2.67"));
  CHECK_EQUAL(ToFixed(1.45, 1), std::string("1.4"));
  CHECK_EQUAL(ToFixed(3.14159, 3), std::string("3.142"));
  CHECK_EQUAL(ToFixed(1234.5678, 0), std::string("1235"));
}

TEST(ToFixedEdgesMatchJavaScript)
{
  CHECK_EQUAL(ToFixed(-0.0, 2), std::string("0.00"));
  CHECK_EQUAL(ToFixed(-0.001, 2), std::string("-0.00"));
  CHECK_EQUAL(ToFixed(0, 0), std::string("0"));
  CHECK_EQUAL(ToFixed(kNaN, 2), std::string("NaN"));
  CHECK_EQUAL(ToFixed(kInf, 2), std::string("Infinity"));
  CHECK_EQUAL(ToFixed(-kInf, 1), std::string("-Infinity"));
  CHECK_EQUAL(ToFixed(1e21, 2), std::string("1e+21"));
  CHECK_EQUAL(ToFixed(-1e300, 2), std::string("-1e+300"));
  CHECK_EQUAL(ToFixed(1e20, 1), std::string("100000000000000000000.0"));
  // Digits outside 0-100 are clamped instead of overflowing a buffer.
  CHECK_EQUAL(ToFixed(1.5, -3), std::string("2"));
  CHECK_EQUAL(ToFixed(0.1, 500).size(), std::string("0.").size() + 100);
}

TEST(RoundToKeepsHugeAndBrokenValues)
{
  CHECK(RoundTo(1e300, 2) == 1e300);
  CHECK(RoundTo(-1e25, 0) == -1e25);
  CHECK(std::isnan(RoundTo(kNaN, 1)));
  CHECK(RoundTo(kInf, 1) == kInf);
  CHECK(RoundTo(0.125, 2) == 0.13);
  CHECK(RoundTo(2.675, 2) == 2.67);
  CHECK(RoundTo(51.5074456, 6) == 51.507446);
  CHECK(RoundTo(-122.4194155, 6) == -122.419415);
}

TEST(FormatDistanceNeverSaysNaN)
{
  CHECK_EQUAL(FormatDistance(kNaN, Units::Mph), std::string("0 feet"));
  CHECK_EQUAL(FormatDistance(kInf, Units::Mph), std::string("0 feet"));
  CHECK_EQUAL(FormatDistance(-120, Units::Mph), std::string("0 feet"));
  CHECK_EQUAL(FormatDistance(kNaN, Units::Kmh), std::string("0 meters"));
  CHECK_EQUAL(FormatDistance(-5, Units::Kmh), std::string("0 meters"));
}

TEST(FormatDistanceBoundaries)
{
  // Feet in 50s below 1000 feet, then quarter / half / three quarters, then miles.
  CHECK_EQUAL(FormatDistance(7, Units::Mph), std::string("0 feet"));
  CHECK_EQUAL(FormatDistance(8, Units::Mph), std::string("50 feet"));
  CHECK_EQUAL(FormatDistance(304, Units::Mph), std::string("1000 feet"));
  CHECK_EQUAL(FormatDistance(305, Units::Mph), std::string("a quarter mile"));
  CHECK_EQUAL(FormatDistance(603, Units::Mph), std::string("a quarter mile"));
  CHECK_EQUAL(FormatDistance(604, Units::Mph), std::string("half a mile"));
  CHECK_EQUAL(FormatDistance(1005, Units::Mph), std::string("half a mile"));
  CHECK_EQUAL(FormatDistance(1006, Units::Mph), std::string("three quarters of a mile"));
  CHECK_EQUAL(FormatDistance(1408, Units::Mph), std::string("three quarters of a mile"));
  CHECK_EQUAL(FormatDistance(1409, Units::Mph), std::string("1 mile"));
  CHECK_EQUAL(FormatDistance(15900, Units::Mph), std::string("10 miles"));
  CHECK_EQUAL(FormatDistance(40000, Units::Mph), std::string("25 miles"));
  // Metric: 995 m rounds to a kilometer instead of "1000 meters".
  CHECK_EQUAL(FormatDistance(994, Units::Kmh), std::string("990 meters"));
  CHECK_EQUAL(FormatDistance(995, Units::Kmh), std::string("1 kilometer"));
  CHECK_EQUAL(FormatDistance(9800, Units::Kmh), std::string("10 kilometers"));
  CHECK_EQUAL(FormatDistance(2250, Units::Kmh), std::string("2.5 kilometers"));
}

TEST(SpeedConversionRoundTrips)
{
  CHECK(std::fabs(ToDisplay(FromDisplay(65, Units::Mph), Units::Mph) - 65) < 1e-9);
  CHECK(std::fabs(ToDisplay(FromDisplay(100, Units::Kmh), Units::Kmh) - 100) < 1e-9);
  CHECK(std::fabs(FromDisplay(36, Units::Kmh) - 10) < 1e-9);
  CHECK(std::fabs(ToDisplay(26.8224, Units::Mph) - 60) < 1e-9);
}
