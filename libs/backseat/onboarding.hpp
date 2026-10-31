// The first launch, Brandon's script: a disclaimer the GPS reads out loud (and the driver has to
// accept, one way or another), picking a voice with the code-phrase gag, then the getting-to-know-you
// questions whose answers decide which jokes fit. The platforms draw the screens; this holds the
// words and what the answers mean, so iOS and Android say the same thing.
#pragma once

#include "backseat/lines.hpp"

#include <optional>
#include <string>
#include <vector>

namespace backseat
{
/// The disclaimer, paragraph by paragraph, word for word as Brandon wrote it (his typos included).
/// The last paragraph asks "do you accept these terms?". `creditCoMaps` names CoMaps next to
/// Organic Maps in the thank-you.
std::vector<std::string> DisclaimerParagraphs(bool creditCoMaps = false);

/// A paragraph as the voice should read it: the politically correct slashes are said out loud
/// ("him slash her slash the officer") and "gps" is spelled out as GPS.
std::string SpokenDisclaimer(std::string const & paragraph);

/// The disclaimer's answer buttons. The first time it's Yes or No; after a No, Yes, No or Skip.
enum class TermsAnswer
{
  Yes,
  No,
  Skip
};

/// What the GPS says to an answer, as line keys in order, and whether the menu is over. `round` is 0
/// for the first ask, 1 for the ask after a No.
///   round 0: Yes -> "onboard.terms.yes" (done). No -> "onboard.terms.no" (ask again, with Skip).
///   round 1: Yes -> "onboard.terms.yes". No -> "onboard.terms.technicality". Skip ->
///            "onboard.terms.skip" then "onboard.terms.technicality". Always done.
struct TermsReply
{
  std::vector<std::string> keys;
  bool done = false;
};
TermsReply AnswerTerms(TermsAnswer answer, int round);

/// The getting-to-know-you questions, in order.
enum class AboutYou
{
  Gender,
  Married,
  Home
};

/// The answer buttons for `question`, as the values AboutYouLineKey() and the platform's save take:
///   Gender: "man", "woman", "skip"      Married: "yes", "no", "complicated"
///   Home: "house", "apartment", "streets"
std::vector<std::string> AboutYouChoices(AboutYou question);
/// The question's own line key ("onboard.ask.gender") and the comment on an answer
/// ("onboard.gender.woman"). Empty for an answer that isn't one of the choices.
std::string AboutYouQuestionKey(AboutYou question);
std::string AboutYouLineKey(AboutYou question, std::string const & answer);

/// The pronouns a gender answer means: "he", "she", or "they" for rather not say.
std::string PronounsForGender(std::string const & answer);
/// "house" -> House and so on; Unknown for anything else.
inline Housing HousingForAnswer(std::string const & answer) { return HousingFrom(answer); }

/// Whether what the driver typed is the code phrase: case, spacing and punctuation don't matter
/// ("purple   monkey!" is "Purple Monkey").
bool TypedPhraseMatches(std::string const & typed, std::string const & phrase);
}  // namespace backseat
