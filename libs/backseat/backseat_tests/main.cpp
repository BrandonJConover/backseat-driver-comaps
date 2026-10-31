#include "check.hpp"

int main()
{
  int failed = 0;
  for (auto const & test : backseat_tests::Registry())
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
