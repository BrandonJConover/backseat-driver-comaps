// Real-time rewording of roasts by an on-device language model (Apple Intelligence,
// Gemini Nano, or a small bundled model). The engine still picks a canned line instantly;
// the model gets a short head start to say it in fresh words. If it is late, or its line
// drops a fact or rambles, the canned line is spoken instead. Navigation lines are never
// sent to the model.
#pragma once

#include "backseat/types.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace backseat
{
struct RewriteRequest
{
  /// The canned line the engine picked.
  std::string line;
  /// What it is about, e.g. "speeding", "unplannedStop".
  std::string category;
  /// 0..100: how fed up the GPS is. The model should sound like it.
  int frustration = 0;
  /// Bits of `line` that must appear word for word in the rewrite (numbers, mostly).
  std::vector<std::string> mustKeep;
};

/// Implemented by each platform on top of its on-device model.
class LineRewriter
{
public:
  using Done = std::function<void(std::string const & rewritten)>;

  virtual ~LineRewriter() = default;
  /// Whether a model is ready on this device right now. Checked before each request.
  virtual bool Available() const = 0;
  /// Starts a rewrite and returns at once. `done` may be called from any thread, at most
  /// once; not calling it is fine (the canned line goes out at the deadline).
  virtual void Rewrite(RewriteRequest const & request, Done done) = 0;
};

/// The prompt the platform models get, shared so every platform sounds the same.
std::string RewriteInstructions();
/// The per-line prompt for `request`.
std::string RewritePrompt(RewriteRequest const & request);

/// Digit runs in `line` ("51", "50", "3"), which a rewrite must keep.
std::vector<std::string> FactsToKeep(std::string const & line);

/// Cleans a model's answer and checks it still says what the canned line said. Returns the
/// line to speak, or nothing if the canned one should be used.
std::optional<std::string> AcceptRewrite(RewriteRequest const & request, std::string const & answer);

/// Holds roasts briefly while the model rewords them and releases lines in order.
/// Navigation lines pass straight through, ahead of any roast still being rewritten.
class RewriteGate
{
public:
  using Clock = std::function<std::chrono::steady_clock::time_point()>;

  explicit RewriteGate(std::chrono::milliseconds budget = std::chrono::milliseconds(1200), Clock clock = {});

  void SetRewriter(std::shared_ptr<LineRewriter> rewriter) { m_rewriter = std::move(rewriter); }

  /// `mayRewrite` false sends a roast out as is, e.g. the arrival line, which has to be
  /// spoken before the platform stops asking.
  void Push(Commentary line, bool mayRewrite = true);
  /// Lines ready to speak, in order: navigation lines always, roasts once rewritten or out
  /// of time. A roast still waiting holds back the roasts after it, never navigation.
  std::vector<Commentary> Take();
  /// Drops anything still waiting (trip over).
  void Clear() { m_queue.clear(); }

private:
  struct Slot
  {
    std::mutex mutex;
    std::optional<std::string> answer;
  };
  struct Entry
  {
    Commentary line;
    RewriteRequest request;
    /// Empty for lines that aren't being rewritten.
    std::shared_ptr<Slot> slot;
    std::chrono::steady_clock::time_point deadline;
  };

  std::chrono::milliseconds m_budget;
  Clock m_clock;
  std::shared_ptr<LineRewriter> m_rewriter;
  std::vector<Entry> m_queue;
};
}  // namespace backseat
