#pragma once

// Internal to libs/backseat: UTF-8 helpers shared by pronunciation.cpp and pronunciation_scripts.cpp.
// Not public API; hosts include pronunciation.hpp.

#include <string>

namespace backseat::pronunciation::detail
{
/// UTF-8 to code points (no validation; a truncated tail decodes what is there).
std::u32string Decode(std::string const & s);
/// Code points back to UTF-8.
std::string Encode(std::u32string const & s);
/// ASCII letter a-z or A-Z.
bool Letter(char32_t c);
}  // namespace backseat::pronunciation::detail
