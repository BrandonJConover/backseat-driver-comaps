#include "check.hpp"

#include "backseat/plate_game.hpp"
#include "backseat/voice_command.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace backseat;

namespace
{
struct YesNoCase
{
  std::string input, plate, said, walk;
};

std::string Field(std::string const & json, std::string const & key, size_t open, size_t close)
{
  auto const k = json.find("\"" + key + "\"", open);
  if (k == std::string::npos || k > close)
    return "";
  auto i = json.find('"', json.find(':', k) + 1) + 1;
  std::string out;
  for (; i < json.size() && json[i] != '"'; ++i)
  {
    if (json[i] == '\\')
      ++i;
    out += json[i];
  }
  return out;
}

std::vector<YesNoCase> YesNoCases()
{
  std::ifstream in(BACKSEAT_YES_NO_PATH);
  std::stringstream buffer;
  buffer << in.rdbuf();
  auto const json = buffer.str();
  std::vector<YesNoCase> cases;
  for (size_t open = json.find('{', json.find("\"cases\"")); open != std::string::npos; open = json.find('{', open + 1))
  {
    auto const close = json.find('}', open);
    cases.push_back({Field(json, "input", open, close), Field(json, "plate", open, close),
                     Field(json, "said", open, close), Field(json, "walk", open, close)});
    open = close;
  }
  return cases;
}

std::string Name(YesNo yn) { return yn == YesNo::Yes ? "yes" : yn == YesNo::No ? "no" : "unclear"; }
}  // namespace

TEST(YesNoGoldenCases)
{
  auto const cases = YesNoCases();
  CHECK(cases.size() >= 30);
  // Every case is checked and printed before the first CHECK stops the test.
  int wrong = 0;
  for (auto const & c : cases)
  {
    std::string const said = SaidYes(c.input) ? "yes" : SaidNo(c.input) ? "no" : "none";
    auto const walk = WalkAskAnswer(c.input);
    std::string const walkName = !walk ? "none" : *walk ? "walk" : "drive";
    if (Name(ParseYesNo(c.input)) != c.plate || said != c.said || walkName != c.walk)
      std::printf("  yes/no \"%s\": plate %s (want %s), said %s (want %s), walk %s (want %s)\n", c.input.c_str(),
                  Name(ParseYesNo(c.input)).c_str(), c.plate.c_str(), said.c_str(), c.said.c_str(),
                  walkName.c_str(), c.walk.c_str());
    wrong += Name(ParseYesNo(c.input)) != c.plate || said != c.said || walkName != c.walk;
  }
  CHECK_EQUAL(wrong, 0);
}

