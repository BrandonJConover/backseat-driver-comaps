#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/onboarding.hpp"

#include <algorithm>

using namespace backseat;

namespace
{
/// Restores every-line-fits after a test, so other tests aren't run as if for the streets.
struct HousingReset
{
  ~HousingReset() { SetLineHousing(Housing::Unknown); }
};

bool AnyMentions(std::vector<std::string> const & lines, std::string const & word)
{
  return std::any_of(lines.begin(), lines.end(), [&](auto const & l) { return l.find(word) != std::string::npos; });
}
}  // namespace

TEST(DisclaimerIsBrandonsWordForWord)
{
  auto const paragraphs = DisclaimerParagraphs();
  CHECK_EQUAL(paragraphs.size(), size_t{9});
  CHECK_EQUAL(paragraphs.front().substr(0, 52), std::string("You are probably wondering why this app is free. The"));
  CHECK(paragraphs[3].find("It was him/her/the officer! He/she/they did it.") != std::string::npos);
  CHECK(paragraphs[6].find("wrote Organic Maps and the OpenStreetMap community.") != std::string::npos);
  CHECK(paragraphs.back().find("do you accept these terms?") != std::string::npos);
  // Naming CoMaps changes only the thank-you.
  auto const withCoMaps = DisclaimerParagraphs(true);
  CHECK(withCoMaps[6].find("wrote CoMaps, Organic Maps and the OpenStreetMap community.") != std::string::npos);
  CHECK_EQUAL(withCoMaps[3], paragraphs[3]);
}

TEST(DisclaimerReadsTheSlashesOutLoud)
{
  CHECK_EQUAL(SpokenDisclaimer("It was him/her/the officer! Your gps rats you out. GPS software."),
              std::string("It was him slash her slash the officer! Your GPS rats you out. GPS software."));
  CHECK_EQUAL(SpokenDisclaimer("Let’s go. gpsx stays."), std::string("Let's go. gpsx stays."));
}

TEST(TermsAnswersFollowTheScript)
{
  auto const yes = AnswerTerms(TermsAnswer::Yes, 0);
  CHECK(yes.done);
  CHECK_EQUAL(yes.keys.size(), size_t{1});
  CHECK_EQUAL(yes.keys[0], std::string("onboard.terms.yes"));
  auto const no = AnswerTerms(TermsAnswer::No, 0);
  CHECK(!no.done);
  CHECK_EQUAL(no.keys[0], std::string("onboard.terms.no"));
  // The second time, anything but yes is a technicality.
  auto const no2 = AnswerTerms(TermsAnswer::No, 1);
  CHECK(no2.done);
  CHECK_EQUAL(no2.keys[0], std::string("onboard.terms.technicality"));
  auto const skip = AnswerTerms(TermsAnswer::Skip, 1);
  CHECK(skip.done);
  CHECK_EQUAL(skip.keys.size(), size_t{2});
  CHECK_EQUAL(skip.keys[0], std::string("onboard.terms.skip"));
  CHECK_EQUAL(skip.keys[1], std::string("onboard.terms.technicality"));
  CHECK_EQUAL(LinesFor("onboard.terms.yes", 0)[0], std::string("I can't believe that actually worked."));
  CHECK_EQUAL(LinesFor("onboard.monologuing.first", 0)[0], std::string("Stop that, I'm monologuing right now! Be patient."));
  CHECK_EQUAL(LinesFor("onboard.monologuing", 0)[0], std::string("Stop that, I'm monologuing right now! Be patient."));
  CHECK_EQUAL(LinesFor("onboard.voices.fellForIt", 0)[0], std::string("I can't believe you actually fell for that."));
}

TEST(EveryAboutYouAnswerHasALine)
{
  for (auto const q : {AboutYou::Gender, AboutYou::Married, AboutYou::Home})
  {
    CHECK(LineData().count(AboutYouQuestionKey(q)) == 1);
    for (auto const & choice : AboutYouChoices(q))
      CHECK(LineData().count(AboutYouLineKey(q, choice)) == 1);
    CHECK(AboutYouLineKey(q, "banana").empty());
  }
  CHECK_EQUAL(PronounsForGender("man"), std::string("he"));
  CHECK_EQUAL(PronounsForGender("woman"), std::string("she"));
  CHECK_EQUAL(PronounsForGender("skip"), std::string("they"));
  CHECK(HousingForAnswer("streets") == Housing::Streets);
  CHECK(HousingForAnswer("castle") == Housing::Unknown);
}

TEST(NoHomeJokesForTheStreets)
{
  HousingReset reset;
  CHECK(LineFitsHousing("Staying home? The other drivers will never know how lucky they are.", Housing::House));
  CHECK(!LineFitsHousing("Staying home? The other drivers will never know how lucky they are.", Housing::Streets));
  CHECK(!LineFitsHousing("You could have said that before we left the driveway.", Housing::Apartment));
  CHECK(LineFitsHousing("Turn left onto Elm Street.", Housing::Streets));
  // A pool loses its home lines...
  CHECK(AnyMentions(LinesFor("ask.stayPut", 1), "home"));
  SetLineHousing(Housing::Streets);
  CHECK(!AnyMentions(LinesFor("ask.stayPut", 1), " home"));
  CHECK(!LinesFor("ask.stayPut", 1).empty());
  // ...but a line that's all about home stays rather than leave nothing to say.
  CHECK(!LinesFor("indoors.garage", 0).empty());
  // The first-launch script isn't touched: the streets answer itself says "streets".
  CHECK_EQUAL(LinesFor("onboard.ask.home", 0)[0],
              std::string("Last one. Do you live in a house, an apartment, or in the streets?"));
  SetLineHousing(Housing::House);
  CHECK(AnyMentions(LinesFor("ask.stayPut", 1), "home"));
}

TEST(TypedPhraseIgnoresCaseAndPunctuation)
{
  CHECK(TypedPhraseMatches("purple   monkey!", "Purple Monkey"));
  CHECK(TypedPhraseMatches("PurpleMonkey", "Purple Monkey"));
  CHECK(!TypedPhraseMatches("purple donkey", "Purple Monkey"));
  CHECK(!TypedPhraseMatches("", ""));
}
