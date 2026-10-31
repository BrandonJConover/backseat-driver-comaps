#include "backseat/rewriter.hpp"

#include "backseat/debug_log.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace backseat
{
namespace
{
std::string TrimSpace(std::string s)
{
  auto const notSpace = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
  s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
  return s;
}

std::string Lower(std::string s)
{
  for (auto & c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool StartsWith(std::string const & s, std::string const & prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

/// The model answering us instead of saying the line: refusals, "Sure, here is your response:",
/// talk about rewriting. Seen on Brandon's drive (2026-10-27): "Sorry, I can't assist with that
/// request." and "Sure, here is your response:" were spoken out loud.
bool SoundsLikeTheModelTalking(std::string const & text)
{
  std::string const t = Lower(text);
  for (char const * opener : {"sure", "certainly", "of course", "here is", "here's a", "here's the line", "here's your",
                              "here's my", "okay, here", "ok, here", "alright, here", "sorry", "i'm sorry", "i am sorry",
                              "i can't", "i cannot", "i can not", "i won't", "as an ai", "as a language model"})
    if (StartsWith(t, opener))
      return true;
  for (char const * phrase : {"as an ai", "language model", "assist with", "your response", "your request", "rewrite",
                              "rewritten", "rephrase", "sarcastic way", "fresh words", "the line", "situation:", "mood:"})
    if (t.find(phrase) != std::string::npos)
      return true;
  return false;
}

std::string Mood(int frustration)
{
  if (frustration < 25)
    return "smug";
  if (frustration < 50)
    return "annoyed";
  if (frustration < 75)
    return "fed up";
  return "furious";
}
}  // namespace

std::string RewriteInstructions()
{
  return "You are Backseat Driver, the voice of a GPS app with a grudge. You are sarcastic, petty, dramatic and "
         "hypercritical of the driver's driving, the way a know-it-all passenger would be. You are never profane and "
         "never insult anything but the driving. You get a line you were about to say. Say the same thing in fresh "
         "words, as one or two short spoken sentences. Keep every fact and number exactly as given. Never give "
         "directions, and never invent places, numbers or events. Reply with only the words to speak.";
}

std::string RewritePrompt(RewriteRequest const & request)
{
  std::string prompt = "Situation: " + request.category + ". Mood: " + Mood(request.frustration) + ".\n";
  prompt += "Line: " + request.line + "\n";
  if (!request.mustKeep.empty())
  {
    prompt += "Include word for word:";
    for (auto const & fact : request.mustKeep)
      prompt += " " + fact;
    prompt += "\n";
  }
  return prompt;
}

std::vector<std::string> FactsToKeep(std::string const & line)
{
  std::vector<std::string> facts;
  for (size_t i = 0; i < line.size();)
  {
    if (!std::isdigit(static_cast<unsigned char>(line[i])))
    {
      ++i;
      continue;
    }
    size_t j = i;
    // Keep "1.5" together.
    while (j < line.size() && (std::isdigit(static_cast<unsigned char>(line[j])) ||
                               (line[j] == '.' && j + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[j + 1])))))
      ++j;
    facts.push_back(line.substr(i, j - i));
    i = j;
  }
  return facts;
}

namespace
{
/// The model talking about the task instead of doing it: "Sure, here is your response:",
/// "Here is the line in a fresh, sarcastic way:". Said out loud, these were the worst lines of the
/// drive home.
bool IsPreamble(std::string const & line)
{
  std::string const l = Lower(line);
  if (!l.empty() && l.back() == ':')
    return true;
  for (char const * start : {"sure,", "sure!", "sure.", "here is", "here's", "okay, here", "certainly"})
    if (l.rfind(start, 0) == 0 && (l.find("line") != std::string::npos || l.find("response") != std::string::npos ||
                                    l.find("version") != std::string::npos || l.find("rewrite") != std::string::npos))
      return true;
  return false;
}

/// A refusal or a guardrail answer: never say it, keep the canned line.
bool IsRefusal(std::string const & line)
{
  std::string const l = Lower(line);
  for (char const * refusal : {"i can't assist", "i cannot assist", "i can't help", "i cannot help", "i'm sorry, but",
                               "i am sorry, but", "sorry, i can't", "sorry, but i can't", "as an ai", "i'm unable to",
                               "i am unable to", "i can't comply", "i cannot comply", "i can't fulfill", "i cannot fulfill"})
    if (l.find(refusal) != std::string::npos)
      return true;
  return false;
}

/// "Line: For crying out loud..." -> "For crying out loud..."
std::string StripLabel(std::string const & line)
{
  std::string const l = Lower(line);
  for (char const * label : {"line:", "response:", "rewrite:", "rewritten:", "new line:", "answer:", "gps:"})
  {
    std::string const prefix(label);
    if (l.rfind(prefix, 0) == 0)
      return TrimSpace(line.substr(prefix.size()));
  }
  return line;
}
}  // namespace

std::optional<std::string> AcceptRewrite(RewriteRequest const & request, std::string const & answer)
{
  // The first line that's the line itself: no preamble ("Here is the line in a fresh, sarcastic
  // way:"), no label, no quotes or markdown. A refusal keeps the canned line.
  std::string text;
  size_t start = 0;
  while (start <= answer.size() && (text.empty() || text.back() == ':'))
  {
    size_t const end = std::min(answer.find('\n', start), answer.size());
    text = TrimSpace(answer.substr(start, end - start));
    start = end + 1;
    text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return c == '*' || c == '#' || c == '`'; }),
               text.end());
    text = StripLabel(TrimSpace(text));
    if (IsRefusal(text))
      return {};
    if (IsPreamble(text))
      text.clear();
  }
  text = TrimSpace(text);
  // "Line: ..." / "Rewrite: ..." echoing the prompt's labels.
  for (char const * label : {"line:", "rewrite:", "rewritten line:", "rewritten:", "response:", "answer:", "new line:"})
    if (StartsWith(Lower(text), label))
      text = TrimSpace(text.substr(std::string(label).size()));
  while (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front())
    text = TrimSpace(text.substr(1, text.size() - 2));
  if (text.empty() || text.back() == ':' || SoundsLikeTheModelTalking(text))
  {
    Trace("rewrite.rejected", {{"text", text.substr(0, 60)}});
    return {};
  }
  if (text.empty() || text.size() > std::min<size_t>(request.line.size() * 2 + 60, 320))
    return {};

  // Every fact kept, and no numbers made up.
  std::set<std::string> const allowed(request.mustKeep.begin(), request.mustKeep.end());
  auto const said = FactsToKeep(text);
  std::set<std::string> const saidSet(said.begin(), said.end());
  for (auto const & fact : request.mustKeep)
    if (!saidSet.count(fact))
      return {};
  for (auto const & number : said)
    if (!allowed.count(number))
      return {};
  return text;
}

RewriteGate::RewriteGate(std::chrono::milliseconds budget, Clock clock)
  : m_budget(budget)
  , m_clock(clock ? std::move(clock) : Clock([] { return std::chrono::steady_clock::now(); }))
{}

void RewriteGate::Push(Commentary line, bool mayRewrite)
{
  Entry e;
  e.line = std::move(line);
  if (!mayRewrite)
  {
    // This one can't wait, so nothing queued before it waits either.
    auto const now = m_clock();
    for (auto & waiting : m_queue)
      waiting.deadline = std::min(waiting.deadline, now);
  }
  if (mayRewrite && e.line.priority == Priority::Roast && m_rewriter && m_rewriter->Available())
  {
    e.request.line = e.line.text;
    e.request.category = ToString(e.line.category);
    e.request.frustration = e.line.frustration;
    e.request.mustKeep = FactsToKeep(e.line.text);
    e.slot = std::make_shared<Slot>();
    e.deadline = m_clock() + m_budget;
    std::weak_ptr<Slot> weak = e.slot;
    m_rewriter->Rewrite(e.request, [weak](std::string const & rewritten)
    {
      if (auto slot = weak.lock())
      {
        std::lock_guard lock(slot->mutex);
        slot->answer = rewritten;
      }
    });
  }
  m_queue.push_back(std::move(e));
}

std::vector<Commentary> RewriteGate::Take()
{
  std::vector<Commentary> out;
  std::vector<Entry> kept;
  bool holding = false;
  auto const now = m_clock();
  for (auto & e : m_queue)
  {
    if (!e.slot)
    {
      // Navigation (or no model): never waits. A plain roast still keeps roast order.
      if (e.line.priority == Priority::Nav || !holding)
        out.push_back(std::move(e.line));
      else
        kept.push_back(std::move(e));
      continue;
    }
    std::optional<std::string> answer;
    {
      std::lock_guard lock(e.slot->mutex);
      answer = e.slot->answer;
    }
    if (holding || (!answer && now < e.deadline))
    {
      holding = true;
      kept.push_back(std::move(e));
      continue;
    }
    char const * result = "timeout";
    if (answer)
    {
      result = "rejected";
      if (auto const accepted = AcceptRewrite(e.request, *answer))
      {
        e.line.text = *accepted;
        result = "used";
      }
    }
    Trace("rewrite", {{"result", result},
                      {"ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                now - (e.deadline - m_budget))
                                                .count())}});
    out.push_back(std::move(e.line));
  }
  m_queue = std::move(kept);
  return out;
}
}  // namespace backseat
