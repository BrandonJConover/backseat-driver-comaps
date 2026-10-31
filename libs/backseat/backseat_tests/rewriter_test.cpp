#include "check.hpp"

#include "backseat/navigator.hpp"
#include "backseat/rewriter.hpp"
#include "backseat/units.hpp"

using namespace backseat;
using namespace std::chrono_literals;

namespace
{
/// A model that answers whatever it's told to, when it's told to.
class FakeModel : public LineRewriter
{
public:
  bool available = true;
  std::vector<RewriteRequest> asked;
  std::vector<Done> pending;

  bool Available() const override { return available; }
  void Rewrite(RewriteRequest const & request, Done done) override
  {
    asked.push_back(request);
    pending.push_back(std::move(done));
  }
  void Answer(size_t i, std::string const & text) { pending.at(i)(text); }
};

struct FakeClock
{
  std::chrono::steady_clock::time_point now{};
  RewriteGate::Clock Fn()
  {
    return [this] { return now; };
  }
};

Commentary Line(std::string text, Priority p = Priority::Roast)
{
  Commentary c;
  c.text = std::move(text);
  c.priority = p;
  c.category = p == Priority::Nav ? Category::Instruction : Category::Speeding;
  return c;
}

RewriteRequest Req(std::string line)
{
  RewriteRequest r;
  r.line = line;
  r.mustKeep = FactsToKeep(line);
  return r;
}
}  // namespace

TEST(FactsAreTheNumbers)
{
  auto const facts = FactsToKeep("It's 50, not 51. Also 1.5 miles. 3.");
  CHECK_EQUAL(facts.size(), size_t{4});
  CHECK_EQUAL(facts[0], std::string("50"));
  CHECK_EQUAL(facts[2], std::string("1.5"));
  CHECK_EQUAL(facts[3], std::string("3"));
}

TEST(RewritesMustKeepTheFacts)
{
  auto const r = Req("Slow down, it's 50 not 51.");
  CHECK(AcceptRewrite(r, "51 in a 50? Bold.").has_value());
  CHECK(!AcceptRewrite(r, "51 in a 45? Bold.").has_value());   // made up a number
  CHECK(!AcceptRewrite(r, "Speeding again? Bold.").has_value());  // dropped them
  CHECK(!AcceptRewrite(r, "").has_value());
  CHECK(!AcceptRewrite(r, std::string(400, 'a') + " 50 51").has_value());  // rambling
}

TEST(RewritesAreCleanedUp)
{
  auto const r = Req("You stopped again.");
  CHECK_EQUAL(*AcceptRewrite(r, "  \"**Oh, stopping again?** Wonderful.\"\nHope that helps!"),
              std::string("Oh, stopping again? Wonderful."));
}

TEST(GateUsesTheRewriteWhenItArrivesInTime)
{
  auto model = std::make_shared<FakeModel>();
  FakeClock clock;
  RewriteGate gate(1000ms, clock.Fn());
  gate.SetRewriter(model);
  gate.Push(Line("It's 50, not 51."));
  CHECK(gate.Take().empty());  // waiting on the model
  model->Answer(0, "51 in a 50. Living dangerously.");
  auto const out = gate.Take();
  CHECK_EQUAL(out.size(), size_t{1});
  CHECK_EQUAL(out[0].text, std::string("51 in a 50. Living dangerously."));
  CHECK_EQUAL(model->asked[0].mustKeep.size(), size_t{2});
}

TEST(GateFallsBackWhenTheModelIsLateOrWrong)
{
  auto model = std::make_shared<FakeModel>();
  FakeClock clock;
  RewriteGate gate(1000ms, clock.Fn());
  gate.SetRewriter(model);
  gate.Push(Line("It's 50, not 51."));
  clock.now += 1001ms;
  auto out = gate.Take();
  CHECK_EQUAL(out.size(), size_t{1});
  CHECK_EQUAL(out[0].text, std::string("It's 50, not 51."));
  model->Answer(0, "too late, 50 51");  // nobody listening any more; must not crash

  gate.Push(Line("It's 50, not 51."));
  model->Answer(1, "Speeding. Again.");
  out = gate.Take();
  CHECK_EQUAL(out[0].text, std::string("It's 50, not 51."));
}

TEST(GateNeverHoldsNavigation)
{
  auto model = std::make_shared<FakeModel>();
  FakeClock clock;
  RewriteGate gate(1000ms, clock.Fn());
  gate.SetRewriter(model);
  gate.Push(Line("First roast"));
  gate.Push(Line("Turn left onto Elm Street", Priority::Nav));
  gate.Push(Line("Second roast"));
  auto out = gate.Take();
  CHECK_EQUAL(out.size(), size_t{1});
  CHECK_EQUAL(out[0].text, std::string("Turn left onto Elm Street"));
  model->Answer(1, "Second, reworded");  // answered out of order
  CHECK(gate.Take().empty());            // still in order behind the first
  model->Answer(0, "First, reworded");
  out = gate.Take();
  CHECK_EQUAL(out.size(), size_t{2});
  CHECK_EQUAL(out[0].text, std::string("First, reworded"));
  CHECK_EQUAL(out[1].text, std::string("Second, reworded"));
}

TEST(GateSkipsTheModelWhenUnavailableOrUrgent)
{
  auto model = std::make_shared<FakeModel>();
  model->available = false;
  RewriteGate gate;
  gate.SetRewriter(model);
  gate.Push(Line("Roast"));
  CHECK_EQUAL(gate.Take().size(), size_t{1});
  CHECK(model->asked.empty());

  model->available = true;
  gate.Push(Line("Waiting roast"));
  gate.Push(Line("You have arrived. C minus."), false);
  auto const out = gate.Take();  // the urgent line flushes the waiting one, canned
  CHECK_EQUAL(out.size(), size_t{2});
  CHECK_EQUAL(out[0].text, std::string("Waiting roast"));
}

TEST(NavigatorRewordsRoastsButNotDirections)
{
  auto model = std::make_shared<FakeModel>();
  EngineConfig config;
  config.rng = SeededRng(3);
  Navigator nav(config);
  nav.SetRewriter(model);
  RouteSnapshot r;
  r.state = RouteState::OnRoute;
  r.turnId = 1;
  r.turn = TurnKind::Left;
  r.distToTurnM = 900;
  r.nextStreet = "Elm Street";
  Fix f;
  f.speedMps = FromDisplay(50, Units::Mph);
  f.speedLimitMps = FromDisplay(50, Units::Mph);
  nav.OnFix(f, r);
  auto const opening = nav.TakeLines();
  CHECK(!opening.empty());
  CHECK(model->asked.empty());  // greeting, rundown and directions are never reworded
  f.tMs = 6000;
  f.speedMps = FromDisplay(53, Units::Mph);
  nav.OnFix(f, r);
  CHECK_EQUAL(model->asked.size(), size_t{1});
  CHECK_EQUAL(model->asked[0].category, std::string("speeding"));
  CHECK(!RewritePrompt(model->asked[0]).empty());
}

// From the drive home: the model's chatter was spoken instead of the line.
TEST(RewritesWithoutTheChatter)
{
  auto const r = Req("You missed the turn.");
  CHECK(!AcceptRewrite(r, "Sorry, I can't assist with that request.").has_value());
  CHECK(!AcceptRewrite(r, "I'm sorry, but I can't help with that.").has_value());
  CHECK(!AcceptRewrite(r, "Sure, here is your response:").has_value());
  CHECK(!AcceptRewrite(r, "Here is the line in a fresh, sarcastic way:").has_value());
  CHECK_EQUAL(*AcceptRewrite(r, "Here is the line in a fresh, sarcastic way:\n\"Missed it. Again.\""),
              std::string("Missed it. Again."));
  CHECK_EQUAL(*AcceptRewrite(r, "Line: For crying out loud, that was the turn."),
              std::string("For crying out loud, that was the turn."));
  CHECK_EQUAL(*AcceptRewrite(r, "Here's the thing: you missed it."), std::string("Here's the thing: you missed it."));
}

TEST(RewritesThatAreTheModelTalkingAreThrownOut)
{
  RewriteRequest r;
  r.line = "That was a right. I said left.";
  r.category = "wrongTurn";
  // All spoken on Brandon's drive home (2026-10-27).
  CHECK(!AcceptRewrite(r, "Sorry, I can't assist with that request.").has_value());
  CHECK(!AcceptRewrite(r, "Sure, here is your response:").has_value());
  CHECK(!AcceptRewrite(r, "Here is the line in a fresh, sarcastic way:").has_value());
  CHECK(!AcceptRewrite(r, "As an AI, I can't judge your driving.").has_value());
  // A preamble line is skipped, and an echoed label is dropped.
  CHECK_EQUAL(AcceptRewrite(r, "Here is the line in a fresh, sarcastic way:\nLeft. The other left.").value_or(""),
              std::string("Left. The other left."));
  CHECK_EQUAL(AcceptRewrite(r, "Line: Left. The other left.").value_or(""), std::string("Left. The other left."));
  CHECK(AcceptRewrite(r, "Left means left. Wild concept, I know.").has_value());
}
