#include "backseat/units.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace backseat
{
namespace
{
double constexpr kMpsPerMph = 0.44704;
double constexpr kMpsPerKmh = 1 / 3.6;

std::string StripPointZero(std::string s)
{
  if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0)
    s.resize(s.size() - 2);
  return s;
}
}  // namespace

double ToDisplay(double mps, Units units) { return mps / (units == Units::Mph ? kMpsPerMph : kMpsPerKmh); }

double FromDisplay(double value, Units units) { return value * (units == Units::Mph ? kMpsPerMph : kMpsPerKmh); }

std::string FormatDistance(double meters, Units units)
{
  if (units == Units::Mph)
  {
    double const feet = meters * 3.28084;
    if (feet >= 1000)
      return StripPointZero(ToFixed(feet / 5280, 1)) + " miles";
    return JsNumber(JsRound(feet / 50) * 50) + " feet";
  }
  if (meters >= 1000)
    return StripPointZero(ToFixed(meters / 1000, 1)) + " kilometers";
  return JsNumber(JsRound(meters / 10) * 10) + " meters";
}

double JsRound(double x) { return std::floor(x + 0.5); }

std::string JsNumber(double x)
{
  if (x == 0)
    return "0";  // also -0
  if (std::isinf(x))
    return x > 0 ? "Infinity" : "-Infinity";
  if (std::isnan(x))
    return "NaN";
  if (x == std::floor(x) && std::fabs(x) < 1e15)
    return std::to_string(static_cast<long long>(x));
  // Shortest representation that reads back as the same double, like JavaScript.
  // (snprintf rather than std::to_chars, which older Android NDKs lack for doubles.)
  char buf[32];
  for (int precision = 1; precision <= 17; ++precision)
  {
    std::snprintf(buf, sizeof(buf), "%.*g", precision, x);
    if (std::strtod(buf, nullptr) == x)
      break;
  }
  return buf;
}

std::string ToFixed(double x, int digits)
{
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", digits, x);
  return buf;
}

double RoundTo(double x, int digits) { return std::stod(ToFixed(x, digits)); }
}  // namespace backseat
