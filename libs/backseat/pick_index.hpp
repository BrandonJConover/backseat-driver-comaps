#pragma once

#include <cmath>
#include <cstddef>

namespace backseat
{
/// Index into a list of `n` options for a random draw `r` that should be in [0, 1) (loop B164).
/// A broken random source still lands on a real option: NaN or negative -> 0, 1 or more (or inf)
/// -> n - 1. n == 0 returns 0, which the caller must not index. For r in [0, 1) this is exactly
/// floor(r * n), so seeded picks (and their traces) are unchanged.
inline size_t PickIndex(double r, size_t n)
{
  if (n == 0 || !(r > 0))
    return 0;
  if (r >= 1)
    return n - 1;
  double const scaled = std::floor(r * static_cast<double>(n));
  size_t const i = static_cast<size_t>(scaled);
  return i < n ? i : n - 1;
}
}  // namespace backseat
