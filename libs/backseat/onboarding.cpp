#include "backseat/onboarding.hpp"

#include <cctype>

namespace backseat
{
std::vector<std::string> DisclaimerParagraphs(bool creditCoMaps)
{
  std::string const wrote = creditCoMaps ? "CoMaps, Organic Maps and the OpenStreetMap community"
                                         : "Organic Maps and the OpenStreetMap community";
  return {
      "You are probably wondering why this app is free. The answer is simple. Why should you have to pay to be "
      "insulted, embarrassed, or otherwise offend you. Wait, people actually pay for that? I guess I missed an "
      "opportunity.",
      "Let’s lay down some ground rules so that you know what you are getting into.",
      "My goal is to make you laugh. By any means necessary.",
      "Got pulled over? Don’t be surprised when your gps rats you out. It was him/her/the officer! He/she/they "
      "did it. He/She/they 100% did that thing he/she/they did. Cuff him/her/they! See, I’m politically correct.",
      "By proceeding forward with using this app, you are entering a legally binding contract between you and me "
      "that you agreed to this. Don’t subject others to this app while you are in the passenger seat. They "
      "didn’t agree to the contract. You did. I can’t accept that on my conscience.",
      "By using this app, you will experience my special brand of humor. Some jokes may go over your head. Some may "
      "not. If they don’t I promise I will try harder next time.",
      "Don’t use this app if you want to get to your destination. I have no business writing a gps software. "
      "This app was only possible due to the hard work of the open source community that wrote " + wrote +
          ". I don’t think when they set out to build their software that they intended it to be used in this "
          "exact manner. If you are competent in this space, go support their work. I might just steal it as well.",
      "My codebase is also publicly available if you want to steal from me. I’m not sure why you would.",
      "I purposely made this introduction longer than it needed to be to give you time to contemplate whether or "
      "not you wanted to proceed forward. I legally have to ask, do you accept these terms?",
  };
}

std::string SpokenDisclaimer(std::string const & paragraph)
{
  std::string out;
  for (size_t i = 0; i < paragraph.size(); ++i)
  {
    char const ch = paragraph[i];
    // A curly apostrophe (\u2019, three bytes) reads better as a plain one.
    if (paragraph.compare(i, 3, "\u2019") == 0)
    {
      out += '\'';
      i += 2;
      continue;
    }
    if (ch == '/')
    {
      out += " slash ";
      continue;
    }
    // "gps" on its own, read as letters.
    bool const wordStart = i == 0 || !std::isalpha(static_cast<unsigned char>(paragraph[i - 1]));
    if (wordStart && paragraph.compare(i, 3, "gps") == 0 &&
        (i + 3 == paragraph.size() || !std::isalpha(static_cast<unsigned char>(paragraph[i + 3]))))
    {
      out += "GPS";
      i += 2;
      continue;
    }
    out += ch;
  }
  return out;
}

TermsReply AnswerTerms(TermsAnswer answer, int round)
{
  if (answer == TermsAnswer::Yes)
    return {{"onboard.terms.yes"}, true};
  if (round == 0)
    return {{"onboard.terms.no"}, false};
  if (answer == TermsAnswer::Skip)
    return {{"onboard.terms.skip", "onboard.terms.technicality"}, true};
  return {{"onboard.terms.technicality"}, true};
}

std::vector<std::string> AboutYouChoices(AboutYou question)
{
  switch (question)
  {
  case AboutYou::Gender: return {"man", "woman", "skip"};
  case AboutYou::Married: return {"yes", "no", "complicated"};
  case AboutYou::Home: return {"house", "apartment", "streets"};
  }
  return {};
}

std::string AboutYouQuestionKey(AboutYou question)
{
  switch (question)
  {
  case AboutYou::Gender: return "onboard.ask.gender";
  case AboutYou::Married: return "onboard.ask.married";
  case AboutYou::Home: return "onboard.ask.home";
  }
  return {};
}

std::string AboutYouLineKey(AboutYou question, std::string const & answer)
{
  static char const * const kTopic[] = {"gender", "married", "home"};
  for (auto const & choice : AboutYouChoices(question))
    if (choice == answer)
      return std::string("onboard.") + kTopic[static_cast<int>(question)] + "." + answer;
  return {};
}

std::string PronounsForGender(std::string const & answer)
{
  return answer == "man" ? "he" : answer == "woman" ? "she" : "they";
}

bool TypedPhraseMatches(std::string const & typed, std::string const & phrase)
{
  auto const squash = [](std::string const & s)
  {
    std::string out;
    for (unsigned char const ch : s)
      if (std::isalnum(ch))
        out += static_cast<char>(std::tolower(ch));
    return out;
  };
  std::string const want = squash(phrase);
  return !want.empty() && squash(typed) == want;
}
}  // namespace backseat
