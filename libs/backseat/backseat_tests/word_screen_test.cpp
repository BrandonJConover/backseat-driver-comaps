// ForSpeech's street, title, place, clock and weekday passes screen each line for a word their regex could
// change (a whole letter run like "St", "Ste", "Mon"; a digit, "a."/"p."/"AM"/"PM" for clock times) and skip
// the regex when there is none (loop B122). Checked against the unscreened passes on every line-bank line,
// raw and filled twice with street-heavy values (22,383 outputs): byte-identical, ForSpeech ~126 -> ~35 us a
// line in a release build. These pin that every screened pass still fires on its own trigger, and that a
// near miss (a type word inside a longer word, a digit glued on) still comes back as it went in.
#include "check.hpp"

#include "backseat/lines_data.hpp"
#include "backseat/pronunciation.hpp"

#include <chrono>
#include <string>

using namespace backseat;
using pronunciation::ForSpeech;

namespace
{
struct Case
{
  char const * in;
  char const * said;
};

Case const kCases[] = {
    {"Open 9am-5pm, closes at 6 PM.", "Open nine ayem to five peeem, closes at six peeem."},
    {"Up at five A.M. sharp.", "Up at five ayem sharp."},
    {"Up at five am, I am.", "Up at five am, I am."},
    {"It's noon to 1 p.m.", "It's noon to one peeem."},
    {"Open Mon-Fri and Sat.", "Open Monday to Friday and Saturday."},
    {"Sunday's Friar stays.", "Sunday's Friar stays."},
    {"Turn onto St. Johns Rd NW.", "Turn onto Saint Johns Road Northwest."},
    {"Take Main St. and then Elm Dr.", "Take Main Street and then Elm Drive."},
    {"Mt Hood via Pt Reyes.", "Mount Hood via Point Reyes."},
    {"Take the Frontage Svc Rd to Ave B.", "Take the Frontage Service Road to Avenue B."},
    {"Left on Forest Hts Dr.", "Left on Forest Heights Drive."},
    {"Lamar Blvd Ste 200, Apt 4B.", "Lamar Boulevard Suite two hundred, Apartment 4B."},
    {"Austin-Bergstrom Intl Airport", "Austin-Bergstrom International Airport"},
    {"Dr Martin Luther King Jr. Blvd", "Doctor Martin Luther King Junior Boulevard"},
    {"Hwy 290 to the 101 Fwy.", "Highway two ninety to the one oh one Freeway."},
    {"N Main St, Avenue N.", "North Main Street, Avenue N."},
    {"Stop at the Stand, Steer and Drive.", "Stop at the Stand, Steer and Drive."},
    {"Mtn Rd 1St", "Mtn Road 1St"},
    {"The MT and PT are fine.", "The MT and PT are fine."},
};
}  // namespace

TEST(WordScreenEveryScreenedPassStillFires)
{
  for (auto const & c : kCases)
    CHECK_EQUAL(ForSpeech(c.in), std::string(c.said));
}

TEST(WordScreenLineBankStaysFast)
{
  // The bank is ~7,500 lines: ~0.26 s in a release build and ~1.6 s in the default debug build (unscreened,
  // ~0.95 s in release). A loose bound for sanitizer builds that still catches a pass gone badly slow.
  size_t lines = 0;
  auto const start = std::chrono::steady_clock::now();
  for (auto const & [key, tiers] : LineData())
    for (auto const & tier : tiers)
      for (auto const & line : tier)
      {
        CHECK(!ForSpeech(line).empty() || line.empty());
        ++lines;
      }
  double const seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  CHECK(lines > 5000);
  CHECK(seconds < 8.0);
}
