// Brandon (2026-11-23): a route started by mistake stayed up after "cancel the new route", because only
// exact phrases cancelled; and anything said to the GPS that it has no action for got silence. Loose
// cancel, an undo that goes back to the route before, and an in-character answer for chat aimed at it.
#include "check.hpp"

#include "backseat/lines.hpp"
#include "backseat/route_undo.hpp"
#include "backseat/voice_command.hpp"

#include <string>

using namespace backseat;

namespace
{
bool HeardOnly(std::string const & said, AppAction action)
{
  auto const c = ParseAppCommands(said);
  return c.size() == 1 && c[0].action == action;
}

bool HeardAny(std::string const & said, AppAction action)
{
  for (auto const & c : ParseAppCommands(said))
    if (c.action == action)
      return true;
  return false;
}

double constexpr kMin = 60 * 1000;
LatLon const kWork{39.7392, -104.9903};
LatLon const kMelissaDrive{39.6, -105.1};
LatLon const kStore{39.75, -104.95};
}  // namespace

TEST(LooseCancelCatchesWordsInBetween)
{
  for (char const * said : {"cancel the new route", "Cancel the new route.", "get rid of this stupid route",
                            "stop the current navigation", "kill the directions", "clear the route",
                            "cancel this trip", "end the whole route", "turn off the navigation",
                            "okay cancel my route please", "cancel route"})
  {
    std::printf("  %s\n", said);
    CHECK(HeardOnly(said, AppAction::CancelRoute));
  }
}

TEST(LooseCancelLeavesStopsAndTheEndAlone)
{
  CHECK(!HeardAny("add a stop on the route", AppAction::CancelRoute));
  CHECK(!HeardAny("how long until the end of the route", AppAction::CancelRoute));
  CHECK(!HeardAny("don't cancel the route", AppAction::CancelRoute));
  CHECK(!HeardAny("I love this route", AppAction::CancelRoute));
  CHECK(!HeardAny("we had to stop at the store", AppAction::CancelRoute));
}

TEST(UndoRouteIsHeard)
{
  for (char const * said : {"go back to the old route", "take me back to the original route", "undo that",
                            "that's not where I wanted to go", "I didn't ask for that", "you misheard me",
                            "wrong destination", "go back to where we were going"})
  {
    std::printf("  %s\n", said);
    CHECK(HeardOnly(said, AppAction::UndoRoute));
  }
  // An undo is not a new destination called "the old route", nor heading back to the start.
  CHECK(!HeardAny("go back to the old route", AppAction::NavigateTo));
  CHECK(!HeardAny("go back to the old route", AppAction::TakeMeBack));
  CHECK(WordsSupportTool(AppAction::UndoRoute, "", "go back to the old route"));
  CHECK(!WordsSupportTool(AppAction::UndoRoute, "", "yeah"));
  CHECK(AppActionNamed("undo_route") == AppAction::UndoRoute);
  CHECK(ToolFor(AppAction::UndoRoute).action == AppAction::UndoRoute);
}

TEST(TakesItBackOnlyForShortRetractions)
{
  CHECK(TakesItBack("never mind"));
  CHECK(TakesItBack("no no no"));
  CHECK(TakesItBack("oh wait no, cancel that"));
  CHECK(TakesItBack("not there"));
  CHECK(!TakesItBack("yeah"));
  CHECK(!TakesItBack("never mind what my sister said about the restaurant last week it was fine"));
}

TEST(ChatterAimedAtTheGps)
{
  CHECK(ClassifyChatter("can you call my mom", "Echo") == Chatter::Request);
  CHECK(ClassifyChatter("play some Taylor Swift", "Echo") == Chatter::Request);
  CHECK(ClassifyChatter("order me a pizza please", "Echo") == Chatter::Request);
  CHECK(ClassifyChatter("what's your favorite color", "Echo") == Chatter::Question);
  CHECK(ClassifyChatter("do you like this song", "Echo") == Chatter::Question);
  CHECK(ClassifyChatter("hey Echo how's it going", "Echo") == Chatter::Question);
  CHECK(ClassifyChatter("Echo you're the worst", "Echo") == Chatter::Question);
  CHECK(ClassifyChatter("nice weather today Echo", "Echo") == Chatter::Hail);
  CHECK(ClassifyChatter("hey George", "George") == Chatter::Hail);
}

TEST(ChatterNotAimedAtTheGps)
{
  CHECK(ClassifyChatter("yeah", "Echo") == Chatter::None);
  CHECK(ClassifyChatter("um okay", "Echo") == Chatter::None);
  // A passenger's story: only an answer mid-conversation.
  CHECK(ClassifyChatter("my sister got a new dog last week", "Echo") == Chatter::Remark);
  CHECK(ClassifyChatter("where's the party tonight", "Echo") == Chatter::Remark);
  // The name as an ordinary word isn't a hail.
  CHECK(ClassifyChatter("we drove past the echo canyon sign", "Echo") == Chatter::Remark);
}

TEST(ChatLinesExistInEveryTier)
{
  for (char const * key : {"chat.request", "chat.question", "chat.remark", "chat.hold", "cmd.undo.restored",
                           "cmd.undo.cancel", "cmd.undo.none"})
    for (int tier = 0; tier < 3; ++tier)
    {
      std::printf("  %s %d\n", key, tier);
      CHECK(!LinesFor(key, tier).empty());
    }
}

TEST(UndoGoesBackToTheRouteBeforeAMistake)
{
  RouteUndo undo;
  undo.Built(0, kWork);
  undo.Named("Work");
  // "Yeah" misheard as Melissa Drive, 10 minutes in.
  undo.Built(10 * kMin, kMelissaDrive);
  auto const plan = undo.Undo(11 * kMin);
  CHECK(plan.kind == RouteUndo::Plan::Kind::Restore);
  CHECK(plan.to.name == "Work");
}

TEST(UndoAfterCancellingTheMistakeStillFindsTheRealRoute)
{
  RouteUndo undo;
  undo.Built(0, kWork);
  undo.Named("Work");
  undo.Closed(10 * kMin);
  undo.Built(10 * kMin, kMelissaDrive);
  // "Cancel the new route", then "go back to the old route".
  undo.Closed(11 * kMin);
  auto const plan = undo.Undo(12 * kMin);
  CHECK(plan.kind == RouteUndo::Plan::Kind::Restore);
  CHECK(plan.to.name == "Work");
}

TEST(UndoWithNothingBeforeDropsTheRoute)
{
  RouteUndo undo;
  CHECK(undo.Undo(0).kind == RouteUndo::Plan::Kind::Nothing);
  undo.Built(0, kMelissaDrive);
  CHECK(undo.Undo(kMin).kind == RouteUndo::Plan::Kind::Cancel);
}

TEST(UndoIgnoresReroutesAndOldHistory)
{
  RouteUndo undo;
  undo.Built(0, kWork);
  // A reroute to the same place isn't a new route.
  undo.Built(5 * kMin, {kWork.lat + 0.0005, kWork.lon});
  CHECK(undo.Undo(6 * kMin).kind == RouteUndo::Plan::Kind::Cancel);
  undo.Closed(7 * kMin);
  // An hour later, the old trip is history.
  undo.Built(67 * kMin, kStore);
  CHECK(undo.Undo(68 * kMin).kind == RouteUndo::Plan::Kind::Cancel);
}
