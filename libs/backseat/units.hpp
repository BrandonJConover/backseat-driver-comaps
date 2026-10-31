// Unit conversion and the number formatting the line templates need.
// Port of engine/src/units.ts, plus helpers that reproduce JavaScript's rounding and
// number-to-string rules so both engines say exactly the same thing.
#pragma once

#include "backseat/types.hpp"

#include <string>

namespace backseat
{
double ToDisplay(double mps, Units units);
double FromDisplay(double value, Units units);

/// Feet for mph users, meters otherwise, rounded the way a person says it: "300 feet",
/// "a quarter mile", "half a mile", "1.5 miles", "12 miles".
std::string FormatDistance(double meters, Units units);

/// JavaScript Math.round: halves round up (towards +infinity).
double JsRound(double x);

/// JavaScript String(number): integers without a decimal point, others shortest round-trip.
std::string JsNumber(double x);

/// JavaScript Number.prototype.toFixed.
std::string ToFixed(double x, int digits);

/// +x.toFixed(digits): the number rounded the same way.
double RoundTo(double x, int digits);
}  // namespace backseat
