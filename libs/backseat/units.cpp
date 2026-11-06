#include "backseat/units.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace backseat
{
namespace
{
double constexpr kMpsPerMph = 0.44704;
double constexpr kMpsPerKmh = 1 / 3.6;
}  // namespace

double ToDisplay(double mps, Units units) { return mps / (units == Units::Mph ? kMpsPerMph : kMpsPerKmh); }

double FromDisplay(double value, Units units) { return value * (units == Units::Mph ? kMpsPerMph : kMpsPerKmh); }

std::string FormatDistance(double meters, Units units)
{
  // A broken distance (NaN, infinite, negative) must never reach the voice as "NaN feet".
  if (!std::isfinite(meters) || meters < 0)
    meters = 0;
  auto const plural = [](double n, char const * unit)
  { return JsNumber(n) + " " + unit + (n == 1 ? "" : "s"); };
  if (units == Units::Mph)
  {
    double const feet = meters * 3.28084;
    if (feet < 1000)
      return JsNumber(JsRound(feet / 50) * 50) + " feet";
    double const miles = feet / 5280;
    if (miles < 0.375)
      return "a quarter mile";
    if (miles < 0.625)
      return "half a mile";
    if (miles < 0.875)
      return "three quarters of a mile";
    return plural(miles < 10 ? JsRound(miles * 2) / 2 : JsRound(miles), "mile");
  }
  // 995 m and up rounds to "1000 meters"; people say "1 kilometer".
  if (JsRound(meters / 10) * 10 < 1000)
    return JsNumber(JsRound(meters / 10) * 10) + " meters";
  double const km = meters / 1000;
  return plural(km < 10 ? JsRound(km * 2) / 2 : JsRound(km), "kilometer");
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
  if (x < 0)
    return "-" + JsNumber(-x);
  // Shortest digits that read back as the same double, then JavaScript's layout rules
  // (Number::toString): plain notation for 1e-7 < x < 1e21, "1e-7" / "1.5e+300" outside.
  // (snprintf rather than std::to_chars, which older Android NDKs lack for doubles.)
  char buf[40];
  for (int precision = 0; precision <= 16; ++precision)
  {
    std::snprintf(buf, sizeof(buf), "%.*e", precision, x);
    if (std::strtod(buf, nullptr) == x)
      break;
  }
  std::string const sci = buf;
  std::size_t const ePos = sci.find('e');
  std::string digits;
  for (std::size_t i = 0; i < ePos; ++i)
    if (sci[i] != '.')
      digits += sci[i];
  while (digits.size() > 1 && digits.back() == '0')
    digits.pop_back();
  int const k = static_cast<int>(digits.size());
  int const n = std::atoi(sci.c_str() + ePos + 1) + 1;  // x = 0.digits * 10^n
  if (k <= n && n <= 21)
    return digits + std::string(n - k, '0');
  if (0 < n && n <= 21)
    return digits.substr(0, n) + "." + digits.substr(n);
  if (-6 < n && n <= 0)
    return "0." + std::string(-n, '0') + digits;
  int const e = n - 1;
  std::string const mantissa = k == 1 ? digits : digits.substr(0, 1) + "." + digits.substr(1);
  return mantissa + "e" + (e < 0 ? "-" : "+") + std::to_string(e < 0 ? -e : e);
}

std::string ToFixed(double x, int digits)
{
  // JavaScript: toFixed takes 0-100 digits, and numbers past 1e21 (or not finite) print as String(x).
  digits = digits < 0 ? 0 : (digits > 100 ? 100 : digits);
  if (!std::isfinite(x) || std::fabs(x) >= 1e21)
    return JsNumber(x);
  if (x == 0)
    x = 0;  // (-0).toFixed(2) is "0.00", not "-0.00"
  bool const negative = x < 0;
  double const a = std::fabs(x);
  // Exact decimal value of the double (it always terminates), so a true tie is visible.
  std::vector<char> exact(1200);
  std::snprintf(exact.data(), exact.size(), "%.1100f", a);
  std::string const e = exact.data();
  std::size_t const dot = e.find('.');
  std::string out;
  if (dot != std::string::npos && e[dot + 1 + digits] == '5' &&
      e.find_first_not_of('0', dot + 2 + digits) == std::string::npos)
  {
    // Exact tie: JavaScript picks the larger n (printf would round half to even).
    out = e.substr(0, dot + 1 + digits);
    if (!out.empty() && out.back() == '.')
      out.pop_back();
    int i = static_cast<int>(out.size()) - 1;
    for (; i >= 0; --i)
    {
      if (out[i] == '.')
        continue;
      if (out[i] != '9')
      {
        ++out[i];
        break;
      }
      out[i] = '0';
    }
    if (i < 0)
      out.insert(out.begin(), '1');
  }
  else
  {
    std::vector<char> buf(160);
    std::snprintf(buf.data(), buf.size(), "%.*f", digits, a);
    out = buf.data();
  }
  return negative ? "-" + out : out;
}

double RoundTo(double x, int digits)
{
  if (!std::isfinite(x) || std::fabs(x) >= 1e21)
    return x;
  return std::strtod(ToFixed(x, digits).c_str(), nullptr);
}
}  // namespace backseat
