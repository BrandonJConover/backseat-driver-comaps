#include "backseat/units.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
  // Cheap screen first: an exact tie prints as 5 then only 0s in the 30 digits after the cut, so
  // anything else is not a tie and skips the 1,100-digit print below (~90k instructions a call; it was
  // most of a simulated drive via RoundTo in the trip report's heat map). Near-ties still go exact.
  char quick[200];
  std::snprintf(quick, sizeof quick, "%.*f", digits + 30, a);
  char const * const cut = std::strchr(quick, '.') + 1 + digits;
  if (*cut != '5' || cut[1 + std::strspn(cut + 1, "0")] != '\0')
  {
    std::vector<char> buf(160);
    std::snprintf(buf.data(), buf.size(), "%.*f", digits, a);
    return negative ? "-" + std::string(buf.data()) : std::string(buf.data());
  }
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
  // Fast path (loop B163): no printing. y = |x| * 10^d is within half an ulp of the exact product, so
  // unless y sits within that error of a .5 tie, rounding y half-up picks the same integer n as
  // ToFixed's exact decimal print. n and 10^d are exact doubles, so n / 10^d is correctly rounded:
  // the same double strtod makes of ToFixed's "n with d decimals". Near-ties and big values print.
  if (digits >= 0 && digits <= 15)
  {
    static constexpr double kPow10[] = {1e0, 1e1, 1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                        1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15};
    double const p = kPow10[digits];
    bool const negative = x < 0;  // -0 is not: (-0).toFixed is "0.00", +0 back
    double const y = std::fabs(x) * p;
    if (y < 4503599627370496.0)  // 2^52: y - floor(y) is exact below this
    {
      double const whole = std::floor(y);
      double const frac = y - whole;
      double const slack = y * 0x1p-50 + 1e-300;  // > the product's rounding error, with margin
      if (std::fabs(frac - 0.5) > slack)
      {
        double const r = (whole + (frac > 0.5 ? 1 : 0)) / p;
        return negative ? -r : r;
      }
    }
  }
  return std::strtod(ToFixed(x, digits).c_str(), nullptr);
}
}  // namespace backseat
