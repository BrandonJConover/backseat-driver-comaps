// Delivery tags (loop B90, BI-001 core half, CROSS X-005): the GPS muttering to itself is tagged Aside so
// the host can say it in BookLyre's whisper preset; everything else, and every direction, is Normal.
#include "check.hpp"
#include "sim_drive.hpp"

#include "backseat/lines.hpp"
#include "backseat/navigator.hpp"

#include <string>

using namespace backseat;
using namespace backseat_tests;

TEST(DeliveryAsideKeysAreAsides)
{
  for (char const * key : {"quiet.aside", "partner.aside", "outdoors.aside"})
    for (int tier = 0; tier < 2; ++tier)
      for (auto const & line : LinesFor(key, tier))
        if (line.find('!') == std::string::npos)
          CHECK(DeliveryFor(key, line) == Delivery::Aside);
  CHECK(DeliveryFor("quiet.aside", LinesFor("quiet.aside", 0).front()) == Delivery::Aside);
  CHECK(DeliveryFor("outdoors.aside.walk", "A bird just flew by. Show-off.") == Delivery::Aside);
}

TEST(DeliveryOtherKeysAreNormal)
{
  for (char const * key : {"turnCompleted.onTime", "speeding", "road.farm", "weather.fog", "asides", "quiet.asideways",
                           "fact.frame", ""})
    CHECK(DeliveryFor(key, "Nothing to report. I'm reporting it anyway.") == Delivery::Normal);
  CHECK(DeliveryFor("quiet.aside", "") == Delivery::Normal);
}

TEST(DeliveryShoutedAsidesAreNormal)
{
  // The unhinged tier yells its "asides": no whisper for those.
  CHECK(DeliveryFor("quiet.aside", "I'M BEING QUIET! THIS IS ME BEING QUIET! It's VERY hard for me!") ==
        Delivery::Normal);
  CHECK(DeliveryFor("outdoors.aside", "I HEARD A STICK BREAK! Was that you? Tell me that was you!") ==
        Delivery::Normal);
  for (auto const & line : LinesFor("quiet.aside", 2))
    if (line.find('!') != std::string::npos)
      CHECK(DeliveryFor("quiet.aside", line) == Delivery::Normal);
}

TEST(DeliveryNamesForHosts)
{
  CHECK_EQUAL(std::string(ToString(Delivery::Normal)), std::string("normal"));
  CHECK_EQUAL(std::string(ToString(Delivery::Aside)), std::string("aside"));
  CHECK(Commentary{}.delivery == Delivery::Normal);
}

TEST(DeliveryDirectionsAreNeverAsides)
{
  // Whole simulated drives: every direction comes out Normal, and an Aside only ever comes from an aside pool.
  int nav = 0;
  for (uint32_t seed = 1; seed <= 12; ++seed)
  {
    EngineConfig c;
    c.rng = SeededRng(seed);
    Navigator n(c);
    auto const r = SimulateTrip(n, {seed % 2 ? TripShape::Town : TripShape::RoadTrip, seed});
    for (auto const & s : r.spoken)
    {
      if (s.line.priority == Priority::Nav)
      {
        ++nav;
        CHECK(s.line.delivery == Delivery::Normal);
      }
      if (s.line.delivery == Delivery::Aside)
      {
        CHECK(s.line.key.find("aside") != std::string::npos);
        CHECK(s.line.text.find('!') == std::string::npos);
      }
    }
  }
  CHECK(nav > 0);
}
