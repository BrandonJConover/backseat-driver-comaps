// Direct tests for achievements.cpp: the badge list, unlocking once, and the saved file
// (fallback:tests, loop B32). Navigator tests only reach "nascar" through a drive.
#include "check.hpp"

#include "backseat/achievements.hpp"

#include <cstring>
#include <set>
#include <string>

using namespace backseat;

TEST(AchievementsBadgeListIsWellFormed)
{
  std::set<std::string> ids;
  for (auto const & b : Badges())
  {
    CHECK(b.id && b.name && b.description);
    CHECK(std::strlen(b.id) > 0 && std::strlen(b.name) > 0 && std::strlen(b.description) > 0);
    // Ids go in a tab-separated file: no tabs, newlines or spaces.
    CHECK(std::string(b.id).find_first_of("\t\r\n ") == std::string::npos);
    CHECK(ids.insert(b.id).second);
    CHECK(FindBadge(b.id) == &b);
  }
  CHECK(ids.count("nascar") == 1);
  CHECK(FindBadge("no-such-badge") == nullptr);
  CHECK(FindBadge("") == nullptr);
}

TEST(AchievementsUnlockOnlyOnce)
{
  Achievements a;
  CHECK(!a.Dirty());
  CHECK(a.Unlock("nascar", 1000));
  CHECK(a.Dirty());
  CHECK(a.Has("nascar"));
  a.ClearDirty();
  CHECK(!a.Unlock("nascar", 2000));  // already have it: keeps the first time, stays clean
  CHECK(!a.Dirty());
  CHECK_EQUAL(a.Unlocked().at("nascar"), 1000.0);
  CHECK(!a.Unlock("made-up", 3000));  // unknown badges never unlock
  CHECK(!a.Has("made-up"));
  CHECK(!a.Dirty());
}

TEST(AchievementsRoundTripKeepsExactTimes)
{
  Achievements a;
  CHECK(a.Unlock("nascar", 1762437612345.0));
  CHECK(a.Unlock("teacherspet", 1762437699999.0));
  auto const text = a.Serialize();
  CHECK_EQUAL(text, std::string("nascar\t1762437612345\nteacherspet\t1762437699999\n"));
  auto const b = Achievements::Parse(text);
  CHECK(b.Unlocked() == a.Unlocked());
  CHECK(!b.Dirty());  // loading isn't a change to save
  CHECK_EQUAL(b.Serialize(), text);
}

TEST(AchievementsParseSkipsJunkAndKeepsOldBadges)
{
  auto const a = Achievements::Parse(
      "nascar\t1000\r\n"   // Windows line ending
      "\n"                 // blank
      "no tab here\n"
      "\t2000\n"           // no id
      "teacherspet\tsoon\n"  // no time
      "retired\t3000\n"    // renamed/removed badge: kept in the file, can't be unlocked again
      "nascar\t9000\n");   // duplicate: first one wins
  CHECK_EQUAL(a.Unlocked().size(), size_t{2});
  CHECK_EQUAL(a.Unlocked().at("nascar"), 1000.0);
  CHECK(!a.Has("teacherspet"));
  CHECK(a.Has("retired"));
  CHECK(a.Serialize().find("retired\t3000\n") != std::string::npos);
  CHECK(Achievements::Parse("").Unlocked().empty());
}

TEST(AchievementsParseNonFiniteTimeKeepsBadge)
{
  auto const a = Achievements::Parse("nascar\tnan\nteacherspet\tinf\n");
  CHECK(a.Has("nascar") && a.Has("teacherspet"));
  CHECK_EQUAL(a.Unlocked().at("nascar"), 0.0);
  CHECK_EQUAL(a.Unlocked().at("teacherspet"), 0.0);
  CHECK(a.Serialize().find("nan") == std::string::npos);
  CHECK(a.Serialize().find("inf") == std::string::npos);
}
