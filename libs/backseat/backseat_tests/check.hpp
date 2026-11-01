// A tiny test harness, so these tests build with nothing but the engine (and run in
// seconds without the rest of the CoMaps tree).
#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace backseat_tests
{
struct TestCase
{
  char const * name;
  std::function<void()> fn;
};

inline std::vector<TestCase> & Registry()
{
  static std::vector<TestCase> tests;
  return tests;
}

struct Failure
{
  std::string message;
};

struct Registrar
{
  Registrar(char const * name, std::function<void()> fn) { Registry().push_back({name, std::move(fn)}); }
};

template <typename A, typename B>
void CheckEqual(A const & a, B const & b, char const * expr, char const * file, int line)
{
  if (a == b)
    return;
  std::ostringstream os;
  os << file << ":" << line << ": " << expr << "\n    got:      " << a << "\n    expected: " << b;
  throw Failure{os.str()};
}
}  // namespace backseat_tests

#define BS_CONCAT2(a, b) a##b
#define BS_CONCAT(a, b) BS_CONCAT2(a, b)
#define TEST(name)                                                                                     \
  static void name();                                                                                  \
  static ::backseat_tests::Registrar BS_CONCAT(registrar_, name)(#name, &name);                        \
  static void name()
#define CHECK(cond)                                                                                    \
  do                                                                                                   \
  {                                                                                                    \
    if (!(cond))                                                                                       \
      throw ::backseat_tests::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #cond}; \
  } while (false)
#define CHECK_EQUAL(a, b) ::backseat_tests::CheckEqual((a), (b), #a " == " #b, __FILE__, __LINE__)
