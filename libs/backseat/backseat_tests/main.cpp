#include "check.hpp"

#include "backseat/lines.hpp"

#include <algorithm>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

int main()
{
  int failed = 0;
  // BACKSEAT_TEST_SHUFFLE=<seed> runs the tests in a shuffled order, to catch one test leaning on
  // state another left behind (loop B47: the line filters used to leak between navigators).
  std::vector<backseat_tests::TestCase> tests(backseat_tests::Registry().begin(), backseat_tests::Registry().end());
  if (char const * seed = std::getenv("BACKSEAT_TEST_SHUFFLE"))
  {
    std::shuffle(tests.begin(), tests.end(), std::mt19937(static_cast<unsigned>(std::strtoul(seed, nullptr, 10))));
    std::cout << "shuffled with seed " << seed << "\n";
  }
  // BACKSEAT_TEST_ONLY=<text> runs only the tests whose name contains it (quick probes).
  if (char const * only = std::getenv("BACKSEAT_TEST_ONLY"))
    tests.erase(std::remove_if(tests.begin(), tests.end(),
                               [only](auto const & t) { return std::string(t.name).find(only) == std::string::npos; }),
                tests.end());
  for (auto const & test : tests)
  {
    // Every test starts from the default line filters, and one that leaves them changed fails by
    // name: in a shuffled order a leaked walk/housing/units setting used to fail some later,
    // innocent test instead (loop B83: NoHomeJokesForTheStreets under seed 2).
    backseat::SetLineFilters({});
    try
    {
      test.fn();
      auto const left = backseat::CurrentLineFilters();
      if (left != backseat::LineFilters{})
        throw backseat_tests::Failure{"left the line filters at " + backseat::DescribeLineFilters(left) +
                                      " (reset them before returning)"};
      std::cout << "ok   " << test.name << "\n";
    }
    catch (backseat_tests::Failure const & f)
    {
      ++failed;
      std::cout << "FAIL " << test.name << "\n  " << f.message << "\n";
    }
    catch (std::exception const & e)
    {
      ++failed;
      std::cout << "FAIL " << test.name << "\n  exception: " << e.what() << "\n";
    }
  }
  std::cout << tests.size() - failed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
