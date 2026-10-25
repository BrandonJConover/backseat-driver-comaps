#include "backseat/rewriter.hpp"

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

std::optional<std::string> AcceptRewrite(RewriteRequest const & request, std::string const & answer)
{
  // First non-empty line, without quotes or markdown the model may have added.
  std::string text;
  size_t start = 0;
  while (start <= answer.size() && text.empty())
  {
    size_t const end = std::min(answer.find('\n', start), answer.size());
    text = TrimSpace(answer.substr(start, end - start));
    start = end + 1;
  }
  text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return c == '*' || c == '#' || c == '`'; }),
             text.end());
  text = TrimSpace(text);
  while (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front())
    text = TrimSpace(text.substr(1, text.size() - 2));
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
    if (answer)
      if (auto const accepted = AcceptRewrite(e.request, *answer))
        e.line.text = *accepted;
    out.push_back(std::move(e.line));
  }
  m_queue = std::move(kept);
  return out;
}
}  // namespace backseat
