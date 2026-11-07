#include "check.hpp"

#include <algorithm>
#include <cstdlib>
#include <random>
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
  for (auto const & test : tests)
  {
    try
    {
      test.fn();
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
  std::cout << backseat_tests::Registry().size() - failed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
