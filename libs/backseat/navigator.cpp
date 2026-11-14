#include "backseat/navigator.hpp"

#include "backseat/debug_log.hpp"
#include "backseat/lines.hpp"
#include "backseat/lines_data.hpp"
#include "backseat/route_sketch.hpp"
#include "backseat/rundown.hpp"
#include "backseat/twenty_questions.hpp"
#include "backseat/units.hpp"
#include "backseat/voice_command.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <limits>
#include <random>

namespace backseat
{
namespace
{
/// Off the route this close to the maneuver means the driver missed it, not wandered off.
double constexpr kMissedTurnWithinM = 80;
// Lane advice window before the maneuver.
double constexpr kLaneSayMinM = 150;
/// The trip-time prediction comes this long after the trip starts.
double constexpr kPredictionDelayMs = 25000;
double constexpr kLaneAfterDirectionMs = 6000;
/// How long a GPS speed dropout (tunnel) keeps the last known speed before it reads as stopped.
double constexpr kUnknownSpeedCarryMs = 10000;
/// "I'm back. I lost the satellites" (loop B116): the GPS lost speed (or sent nothing) at least this long, at
/// least this fast, before it came back. Longer than kSignalLostMaxMs is a suspended app or a parked phone.
double constexpr kSignalLostMinMs = 8000;
double constexpr kSignalLostMaxMs = 10 * 60000;
double constexpr kSignalLostMinMps = 8;
double constexpr kSignalBackEveryMs = 10 * 60000;
/// Never this close to the next maneuver, in meters or seconds of driving.
double constexpr kSignalBackTurnM = 400;
double constexpr kSignalBackTurnS = 30;
/// A fix at most this much older than the newest one is a replay and is dropped; more is a clock reset.
double constexpr kStaleFixWindowMs = 60000;
double constexpr kLaneSayMaxM = 1500;
/// Two maneuvers this close together are said as one: "turn left onto X, then right onto Y".
double constexpr kChainWithinM = 150;
// At speed, 150 m is five seconds on a highway: the next maneuver comes before its own call could. Chain
// anything within this many seconds of driving, up to kChainMaxM (loop B86).
double constexpr kChainWithinS = 10;
double constexpr kChainMaxM = 500;
/// After a turn, the next direction waits this long, so the driver is through the turn first...
double constexpr kInstructionAfterTurnMs = 3000;
/// ...unless the next maneuver is this close, or this many seconds away at highway speed (an exit
/// 350 m after a merge at 33 m/s used to wait for 250 m, 7.5 s out; loop B81).
double constexpr kInstructionNowWithinM = 250;
double constexpr kInstructionNowWithinS = 15;
/// Slower than this counts as stopped.
double constexpr kStoppedMps = 0.5;
/// Faster than this means driving again, not walking around with the phone.
double constexpr kResumedMps = 4;
/// Stopped this long away from the destination is a stop the driver chose (gas, food...).
double constexpr kUnplannedStopAfterMs = 180000;
/// A stop at least this long (not a red light) restarts the quiet stretch the quiet bits wait for.
double constexpr kQuietAfterStopMinMs = 60000;
/// Stops this close to the destination are just arriving.
double constexpr kNearDestinationM = 250;
/// The arrival call says which side the place is on when it's at most this far (loop B119); farther
/// out the road can still bend, and the countdown never gets to it otherwise. The side has to be this
/// far off the road's line to count, and the place no farther than kSideMaxM to be judged at all.
double constexpr kSideWithinM = 400;
double constexpr kSideMinOffsetM = 10;
double constexpr kSideMaxM = 500;
/// Closer than this the car is arriving; a late "it's on your right" is just noise.
double constexpr kSideLastCallM = 20;
/// If the GPS position never strays this far from where the car stopped, the phone stayed
/// in the car. Carried around a gas station, a phone's fix wanders well past it.
double constexpr kPhoneWanderedM = 30;
/// Stopped this long at the roadside, with nothing around to explain it, is pulling over.
/// Long enough to sit out most stop-and-go; red lights are ruled out by the light lookup.
double constexpr kPullOverAfterMs = 45000;
/// Stopped this long by a railway crossing: that's a train, worth one line (BI-111).
double constexpr kTrainWaitMs = 20000;
/// A ferry gets one line when it's this far ahead (BI-112): close enough to matter, far enough
/// to stay clear of the boarding call.
double constexpr kFerryAheadMinM = 300;
double constexpr kFerryAheadMaxM = 1500;
/// A toll stretch gets its line as it begins: within this far of its start, or on it (BI-113).
double constexpr kTollStartM = 200;
/// ...and only this soon after coming in range; later, it's no longer the start.
double constexpr kTollWindowMs = 90000;
/// Two toll stretches with less free road than this between them are one toll road.
double constexpr kTollFreeGapM = 2000;
/// A pull-over this soon after serious speeding is probably the police.
double constexpr kPoliceAfterSpeedingMs = 120000;
/// "Serious" speeding: this far over the limit (~10 mph).
double constexpr kSeriousOverMps = 4.5;
/// Sharp curve: sideways acceleration (speed x turn rate) at least this, at highway-ish
/// speed, over two fixes in a row. ~0.35 g is well past a comfortable curve.
double constexpr kScaryLateralMps2 = 3.5;
double constexpr kScaryCurveMinMps = 11;  // ~25 mph; slower is just a turn
double constexpr kCurveYelpEveryMs = 90000;
// "There is a curve ahead": this far out, at road speed, now and then.
double constexpr kCurveAheadMinM = 120;
double constexpr kCurveAheadMaxM = 400;
double constexpr kCurveAheadEveryMs = 8 * 60000;
// Speeding up in a bend: gentler than a scary curve, but the speed is climbing.
double constexpr kCurveGasLateralMps2 = 2;
double constexpr kCurveGasAccelMps2 = 0.7;
double constexpr kCurveGasEveryMs = 5 * 60000;
/// Regular bridges are everywhere in a city (every overpass); water is worth a scream, but not six in
/// eleven minutes.
double constexpr kLandBridgeYelpEveryMs = 600000;
/// Off-roading, how often it says where the destination is.
double constexpr kHeadingEveryMs = 2 * 60000;
double constexpr kWaterBridgeYelpEveryMs = 15 * 60000;
int constexpr kWaterBridgeYelpsPerTrip = 2;
/// Off the route this long before it's worth a joke: GPS jitter comes back within seconds.
double constexpr kRerouteRoastAfterMs = 5000;
/// The driver asked for a new route, so of course it changed.
double constexpr kNoRerouteRoastAfterCommandMs = 30000;
// Leaving the route right at a turn is only a miss once it lasts: a one-second GPS blip that comes back to the
// same maneuver gets no "you missed it" and doesn't count as a reroute.
double constexpr kMissConfirmMs = 3000;
/// Off every mapped road: this far from the nearest one...
double constexpr kOffRoadM = 60;
/// ...and back on one within this...
double constexpr kBackOnRoadM = 30;
/// ...for this long, at a real driving speed (not crawling through a parking lot)...
double constexpr kOffRoadForMs = 12000;
double constexpr kOffRoadMinMps = 6;
/// ...and said at most this often.
double constexpr kOffRoadEveryMs = 300000;
/// A fix this rough (horizontal accuracy, m) can't tell which road the car is on or which way it's
/// pointing: downtown canyons and tunnel mouths read 100 m off and facing anywhere. Such fixes don't
/// start or advance the off-road, dirt, wrong-way, U-turn or scary-curve judgements. Unknown (-1)
/// counts as good, since some sources never report it. (Mileage drops fixes past 65 m for distance;
/// judging the driver wants a tighter fix than adding up miles does.)
double constexpr kJudgeMaxAccuracyM = 50;
bool RoughFix(Fix const & fix) { return fix.accuracyM > kJudgeMaxAccuracyM; }
/// A walking route this much on dirt (by distance) is a hike from the start.
double constexpr kHikeDirtShare = 0.4;
/// On trails this long, a walk becomes a hike. It stays one for the rest of the trip.
double constexpr kHikeAfterMs = 90000;
double constexpr kTrailCheckEveryMs = 10000;
/// On dirt this long (and moving), a drive becomes an off-road trip...
double constexpr kOffRoadAfterDirtMs = 45000;
/// ...and on pavement this long, a drive again.
double constexpr kBackOnPavementMs = 120000;
/// The next highway exit this far out makes the one just passed worth a remark...
double constexpr kLastExitM = 32187;  // 20 miles
/// ...and this far out gets Brandon's "last one for a hundred miles".
double constexpr kLastExitLongM = 80467;  // 50 miles
double constexpr kLastExitEveryMs = 1800000;
/// Lost this many times in one trip, the GPS suggests a car that drives itself.
int constexpr kAutopilotAfterReroutes = 3;
/// The second time lost on a trip, the GPS wishes the driver's partner were driving.
int constexpr kPartnerAfterReroutes = 2;
int constexpr kBegAfterReroutes = 4;
/// Tour guide: look around this often, point something out at most this often, and only
/// with no maneuver coming up for a while.
double constexpr kSightCheckEveryMs = 8000;
double constexpr kSightEveryMs = 240000;
double constexpr kSightQuietM = 500;
/// A sight with a position is said when the car is this many seconds from passing it (the time to
/// start speaking and get to its name), but never more than kSightSayMaxM out...
double constexpr kSightLeadS = 3;
double constexpr kSightSayMinM = 30;
double constexpr kSightSayMaxM = 120;
/// ...and only if it's really beside the road ahead: farther off to the side is behind buildings.
double constexpr kSightMaxAheadM = 450;
double constexpr kSightMaxSideM = 60;
/// Dropped once passed, or if the car never gets to it.
double constexpr kSightPassedM = -15;
double constexpr kSightGiveUpMs = 90000;
/// Nothing said for this long (a random stretch in between) on an easy part of the drive: the GPS
/// fills the silence.
double constexpr kIdleMinMs = 150000;
double constexpr kIdleMaxMs = 300000;
double constexpr kIdleQuietM = 600;
/// Share of trips that may do the "you're driving too well" bit, so it stays a surprise.
double constexpr kCleanBitChance = 0.6;
/// The clean-driving bit waits for this much quiet before it speaks.
double constexpr kCleanQuietMs = 20000;
/// Correct turns in a row, across trips (RunningBits).
char const * const kTurnStreakKey = "_turns.streak";
/// A turn that went by this long after the last fix (GPS out in a tunnel or a garage) wasn't seen:
/// it neither extends nor breaks the streak.
double constexpr kUnseenTurnGapMs = 30000;
/// A speeding story beat waits for the next nag when the phone has more than this left to say.
double constexpr kStoryBusyMs = 3000;
/// Town straight nags (Brandon's "Please continue straight." chain) only on one leg a trip, and
/// only one at least this long when it starts.
double constexpr kStraightChainMinM = 600;
/// A turn reminder ("Just in case you forgot...") is skipped on this share of turns whose
/// instruction was recent, so it isn't the same beat at every turn.
double constexpr kReminderSkipChance = 0.5;
double constexpr kReminderRecentMs = 45000;
/// Bending(): this much heading change within the window, or a sharp bend this close.
double constexpr kBendDeg = 20;
double constexpr kBendWindowMs = 12000;
double constexpr kBendAheadM = 300;
/// Curve jokes: moving at least this fast, this long after a maneuver (its swing isn't a bend),
/// this far apart, at most this many a trip.
double constexpr kBendMinMps = 8;
double constexpr kBendAfterTurnMs = 20000;
double constexpr kBendEveryMs = 60000;
int constexpr kBendJokesPerTrip = 8;
double constexpr kBreathAfterTalkMs = 8000;
/// The cold bit: at or below this (about 40 °F), in the first minutes of a trip, on half the cold
/// trips (always the first ever), the vent request a few minutes after.
double constexpr kColdC = 4.5;
double constexpr kColdFromMs = 25000;
double constexpr kColdUntilMs = 300000;
double constexpr kColdVentAfterMs = 180000;
// cold.vent asks to be held up to a vent: it waits for a stop (red light, parked) and gives up this long
// after cold.start if the car never stops (loop B74).
double constexpr kColdVentUntilMs = 1200000;
double constexpr kColdChance = 0.5;
/// Hot: at or above this (about 86 °F). Groceries bought within the window cook after a while.
double constexpr kHotC = 30;
double constexpr kGroceryWindowMs = 45 * 60000.0;
double constexpr kGroceryCookMs = 120000;
/// Clock changes: remarked on for this many days after, once a day, in the dark.
double constexpr kClockChangeDays = 4;
/// A trip like this one ended between these many hours ago: "Didn't we just take this trip
/// yesterday?" Said on this share of those trips (always the first time).
double constexpr kYesterdayMinH = 12;
double constexpr kYesterdayMaxH = 36;
double constexpr kYesterdayChance = 0.3;
/// Leaving a bar between these local hours; said on this share of those trips (always the first).
int constexpr kNightOutFromH = 21;
int constexpr kNightOutUntilH = 4;
double constexpr kNightOutChance = 0.5;
/// Leaving the bar: still parked this long into the trip gets the breathalyzer bit.
double constexpr kNightParkedMs = 15000;
/// Faster than this and the car has left the lot.
double constexpr kNightMovedMps = 2.5;
/// SaidLog counter: trips started, finished or not.
char const * const kTripsStartedKey = "_trips.started";
/// The route type (drive, walk or bike) of the last trip, for "Oh, we're walking now?".
char const * const kLastModeKey = "_mode.last";
/// "Are we actually walking there?" is for walks longer than a couple of miles (Brandon), every time.
double constexpr kAskWalkOverM = 3200;
/// Walking pace, for the route's length when the host doesn't give it.
double constexpr kAskWalkPaceMps = 1.4;
// Answering "no, not walking" at more than this (~10 mph, past a run) means the car is already
// moving: the reply waits for a stop before the driver picks Driving.
double constexpr kAskWalkMovingMps = 4.5;

/// SaidLog values: the trip under way (destination, last seen in s since the epoch), so a relaunch in
/// the middle of it (a crash, or iOS closing the app in the background) picks it back up instead
/// of opening a brand new trip with the greeting and rundown all over again.
char const * const kActiveTripToKey = "_trip.active.to";
char const * const kActiveTripAtKey = "_trip.active.at";
double constexpr kResumeWithinS = 15 * 60;
double constexpr kActiveTripStampEveryS = 60;
/// How long a navigation line may wait for the neural voice: at most this long, and only while it
/// still finishes this far before the maneuver.
double constexpr kNavWaitMaxMs = 4000;
/// The pause the phone leaves after each line.
double constexpr kNavLinePauseMs = 350;
/// A first turn closer than this when the trip starts is said before the greeting.
double constexpr kFirstTurnFirstWithinM = 350;
/// "Still speeding" nags back off to one every 5 minutes on a long stretch over the limit.
double constexpr kSpeedingRenagMaxMs = 300000;
// A one-second speed change past this is a GPS glitch, not driving (about 1.2 g; see EngineConfig).
double constexpr kImplausibleMps2 = 12;
/// A hard-brake roast waits until the car has been steady this long (EngineConfig::jerkSettleMs).
double constexpr kJerkSettleMs = 3000;
int constexpr kSpeedingTolerance = 2;
int constexpr kSpeedingToleranceFast = 3;
double constexpr kSpeedingBarelyEveryMs = 5 * 60000;
/// Over the limit this long (two fixes or more) before it's speeding: a one-fix snap to a slower
/// frontage road isn't. And braking into a lower zone is judged against the old limit for a while.
double constexpr kSpeedingSustainMs = 3000;
double constexpr kSpeedingDropGraceMs = 10000;
/// A remark held back for a turn is dropped after this long...
double constexpr kTurnHeldMaxMs = 45000;
/// "56 in a 45" is too good to lose to a turn, and still true half a minute later.
double constexpr kWaySpeedingHeldMs = 30000;
/// ...or after this long, or once the turn is done, if it was about that moment ("Nice turn").
double constexpr kTurnHeldStaleMs = 15000;
double constexpr kNavDoneBeforeTurnM = 40;
// Facing the wrong way: the GPS course is at least this far off the route's direction...
double constexpr kWrongWayDeg = 135;
// How fast the first trip's rundown is spoken.
double constexpr kRattleSpeed = 1.35;
// ...at driving speed (GPS course is noise when crawling)...
double constexpr kWrongWayMinMps = 3;
// ...for this long, so a wobble or a lane change doesn't count.
double constexpr kWrongWayHoldMs = 3000;
double constexpr kWrongWayEveryMs = 90000;
int constexpr kWrongWayMaxPerTrip = 3;
// A U-turn: heading now at least this far from the heading a few seconds ago...
double constexpr kUTurnDeg = 160;
double constexpr kUTurnMinAgoMs = 4000;
double constexpr kUTurnMaxAgoMs = 30000;
// A reversal whose new heading is this close to the route's bearing follows the route.
double constexpr kUTurnOnRouteDeg = 45;
// ...counting headings only while actually moving.
double constexpr kUTurnMinMps = 1.5;
double constexpr kUTurnEveryMs = 120000;
int constexpr kUTurnMaxPerTrip = 2;
// Still sitting this long after the trip starts gets the brake-or-gas line.
double constexpr kSlowStartMs = 45000;
/// Asking for music: once a trip, at least this far in, on a quiet stretch, if nothing is playing.
double constexpr kMusicAfterMs = 15 * 60000;
double constexpr kMusicCheckEveryMs = 15000;
/// "You call this music?" once it has been playing this long.
double constexpr kMusicJudgeAfterMs = 45000;
// Starting indoors: a fix this old still says where the car is. "Home" is within this of the
// learned home. After the first time, the line comes back on this share of indoor starts.
double constexpr kIndoorFixFreshMs = 60000;
double constexpr kIndoorHomeM = 100;
double constexpr kIndoorRepeatChance = 1.0 / 3;
// Some indoor starts get a second line once the car is out and rolling, if that's soon.
double constexpr kIndoorOutChance = 0.5;
double constexpr kIndoorOutWithinMs = 5 * 60000;
/// Silence between a setup and its punchline.
double constexpr kAwkwardPauseMs = 2500;
// Quiet-stretch bits (quiet_bits.hpp): a phone call, I spy or a useless fact when there's
// nothing to say for a while. Not before the trip settles in, at most a couple a trip, spaced out.
double constexpr kQuietBitAfterStartMs = 3 * 60000;
int constexpr kQuietBitsPerTrip = 2;
double constexpr kQuietBitEveryMs = 8 * 60000;
/// Nothing said for this long, and the next maneuver at least this many seconds off.
double constexpr kQuietSilenceMs = 25000;
double constexpr kQuietMinToTurnS = 45;
/// A call runs about a minute, so it wants a longer stretch. One a trip.
double constexpr kCallMinToTurnS = 75;
double constexpr kCallMinToTurnM = 1000;
/// Once each bit has had its first time, a quiet stretch gets one this often; the call, I spy
/// and the fact are picked by these weights.
double constexpr kQuietBitChance = 0.5;
/// A familiar commute (a learned routine driven at least this many times before) thins the optional
/// banter: idle remarks and quiet-stretch bits come at this share of their usual rate, until anything
/// new happens on the trip (BI-055: the same jokes on trip 50 as on trip 1 wear thin).
int constexpr kFamiliarAfterTrips = 10;
double constexpr kFamiliarTalkShare = 0.6;
/// call 30%, I spy 15%, 20 Questions 20%, fact 15%, then an aside or a trap (20 Questions
/// falls to the fact when it can't be offered).
double constexpr kCallWeight = 0.3;
double constexpr kISpyWeight = 0.15;
/// 20 Questions (twenty_questions.hpp).
double constexpr kTwentyWeight = 0.2;
double constexpr kFactWeight = 0.15;
/// The rest: a quiet aside ("I'm conserving my disappointment.") or a trap, half and half.
double constexpr kAsideWeight = 0.1;
/// An aside about the driver's partner instead, when there is one.
double constexpr kPartnerAsideChance = 0.35;
/// Speeds below this count as this for "how long until the turn" (a crawl isn't forever).
double constexpr kQuietMinSpeedMps = 3;
/// The GPS "listens" to the caller this long between its own lines (plus up to kCallListenSpreadMs).
double constexpr kCallListenMs = 1800;
double constexpr kCallListenSpreadMs = 1700;
/// Directions interrupt: "I have to give them an update" goes first if the turn is this far off,
/// otherwise the direction just cuts in. Back to the call this long after whatever cut in, once the
/// next turn is at least kCallResumeMinToTurnS away; the caller hangs up after kCallMaxHoldMs.
double constexpr kCallHoldFirstS = 20;
double constexpr kCallResumeAfterCutInMs = 5000;
double constexpr kCallResumeMinToTurnS = 25;
double constexpr kCallMaxHoldMs = 120000;
/// From the last line of the call to "Sorry about that, where were we?".
double constexpr kCallAfterMs = 1500;
/// Below this the sun is down (its top edge, with refraction).
double constexpr kSunsetElevationDeg = -0.833;

/// Only while driving along; a stop has its own lines.
double constexpr kQuietMinMovingMps = 5;

double SecondsToTurn(double distM, double speedMps) { return distM / std::max(speedMps, kQuietMinSpeedMps); }

/// Game talk (plate game, 20 Questions) holds this many seconds before a maneuver at the current speed, never
/// less than the game's own floor (500 ft): at highway speed 500 ft is ~5 s, inside the "now" instruction.
double constexpr kGameHoldS = 12;
double GameHoldM(double floorM, double speedMps) { return std::max(floorM, kGameHoldS * std::max(speedMps, 0.0)); }

// Pop quizzes (pop_quiz.hpp): a quiet stretch, not too early, a couple a trip, spaced out.
double constexpr kQuizAfterStartMs = 5 * 60000;
int constexpr kQuizzesPerTrip = 2;
double constexpr kQuizEveryMs = 12 * 60000;
/// The question and its answer fit before the next turn: the answer window plus reading it out.
double constexpr kQuizMinToTurnS = 60;
double constexpr kQuizSilenceMs = 20000;
/// Not right on top of a phone call or I spy either.
double constexpr kQuizAfterQuietBitMs = 3 * 60000;
/// Each quiet stretch past all that springs one this often (the first quiz ever, always).
double constexpr kQuizChance = 0.35;
/// How long the driver gets once the question is read.
double constexpr kQuizAnswerMs = 25000;
/// A driver asking for another: not more often than this.
double constexpr kQuizAskedEveryMs = 2 * 60000;
/// Brownie points for the Teacher's Pet badge.
int constexpr kTeachersPetBrownies = 10;

/// Lines that still get through while the GPS is on the phone (and put the call on hold): the
/// driver talking to it, and things that actually happened. The running commentary waits.
bool CutsIntoCall(Commentary const & c)
{
  if (c.priority == Priority::Nav)
    return true;
  switch (c.category)
  {
  case Category::PlateGame:
  case Category::TwentyQuestions:
  case Category::Thanked:
  case Category::Title:
  case Category::Bossy:
  case Category::UnplannedStop:
  case Category::Abandoned:
  case Category::MissedTurn:
  case Category::WrongTurn:
  case Category::Rerouted:
  case Category::StopRequest:
  case Category::Battery:
  case Category::Hazard:
  case Category::Arrived: return true;
  default: return false;
  }
}

/// After turning down the driver's game, the GPS pitches it as its own idea this much later.
double constexpr kGamePitchAfterMs = 180000;

/// "1st", "22nd", "55th", "111th".
std::string Ordinal(int n)
{
  int const tens = n % 100;
  char const * suffix = "th";
  if (tens < 11 || tens > 13)
    suffix = n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
  return std::to_string(n) + suffix;
}

/// "6 months ago", "3 days ago", "20 minutes ago".
std::string TimeAgo(double ms)
{
  struct Unit
  {
    double ms;
    char const * name;
  };
  Unit constexpr kUnits[] = {{365.25 * 86400000, "year"}, {30.44 * 86400000, "month"}, {7 * 86400000.0, "week"},
                             {86400000, "day"},           {3600000, "hour"},            {60000, "minute"}};
  for (auto const & u : kUnits)
  {
    int const n = static_cast<int>(ms / u.ms);
    if (n >= 1)
      return std::to_string(n) + " " + u.name + (n == 1 ? "" : "s") + " ago";
  }
  return "just now";
}
/// Talked to without the title: scolded at most this often (otherwise ignored).
double constexpr kTitleScoldEveryMs = 20000;
/// Trips the router expects to take at least this long get the long-drive remarks, at these
/// minutes into the trip, each held until the next maneuver is a while off.
double constexpr kLongDriveMinMinutes = 60;
struct LongDriveRemark
{
  double atMinutes;
  char const * key;
};
LongDriveRemark constexpr kLongDriveRemarks[] = {
    {20, "longDrive.posture"}, {50, "longDrive.smell"}, {90, "longDrive.metal"}};
double constexpr kLongDriveQuietM = 1000;
/// Traffic: judged over this window, a crawl is under kCrawlShare of the limit on average,
/// and it's over once the average is back above kClearShare.
double constexpr kTrafficWindowMs = 90000;
double constexpr kCrawlShare = 0.3;
double constexpr kClearShare = 0.6;
/// Only roads at least this fast count; crawling down a 25 mph street is just driving.
double constexpr kTrafficMinLimitMps = 15;  // ~34 mph
double constexpr kTrafficClearedChance = 0.5;

double DistanceM(LatLon const & a, LatLon const & b)
{
  double constexpr kEarthRadiusM = 6371000;
  double const rad = M_PI / 180;
  double const dLat = (b.lat - a.lat) * rad;
  double const dLon = (b.lon - a.lon) * rad;
  double const h = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(a.lat * rad) * std::cos(b.lat * rad) * std::sin(dLon / 2) * std::sin(dLon / 2);
  return 2 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(h)));
}

double Bearing(LatLon const & a, LatLon const & b)
{
  double const rad = M_PI / 180;
  double const y = std::sin((b.lon - a.lon) * rad) * std::cos(b.lat * rad);
  double const x = std::cos(a.lat * rad) * std::sin(b.lat * rad) -
                   std::sin(a.lat * rad) * std::cos(b.lat * rad) * std::cos((b.lon - a.lon) * rad);
  return std::atan2(y, x) / rad;
}

Maneuver ManeuverOf(TurnKind k)
{
  switch (k)
  {
  case TurnKind::Left:
  case TurnKind::SharpLeft:
  case TurnKind::SlightLeft:
  case TurnKind::ExitLeft: return Maneuver::Left;
  case TurnKind::Right:
  case TurnKind::SharpRight:
  case TurnKind::SlightRight:
  case TurnKind::ExitRight: return Maneuver::Right;
  default: return Maneuver::Other;
  }
}

std::optional<RouteStep::Kind> StepKindOf(TurnKind k)
{
  if (k == TurnKind::UTurn)
    return RouteStep::Kind::UTurn;
  switch (ManeuverOf(k))
  {
  case Maneuver::Left: return RouteStep::Kind::Left;
  case Maneuver::Right: return RouteStep::Kind::Right;
  default: return RouteStep::Kind::Other;
  }
}

char const * RouteStateName(RouteState s)
{
  switch (s)
  {
  case RouteState::Inactive: return "Inactive";
  case RouteState::NotStarted: return "NotStarted";
  case RouteState::OnRoute: return "OnRoute";
  case RouteState::OffRoute: return "OffRoute";
  case RouteState::Finished: return "Finished";
  }
  return "?";
}
}  // namespace

double CurveAheadM(std::vector<LatLon> const & ahead, double maxM, double minDeg)
{
  double constexpr kBendWithinM = 80;
  // Segments, skipping points too close together to have a heading.
  struct Seg
  {
    double startM;
    double lengthM;
    double bearing;
  };
  std::vector<Seg> segs;
  double along = 0;
  for (size_t i = 0; i + 1 < ahead.size() && along <= maxM + kBendWithinM; ++i)
  {
    double const len = DistanceM(ahead[i], ahead[i + 1]);
    if (len >= 2)
      segs.push_back({along, len, Bearing(ahead[i], ahead[i + 1])});
    along += len;
  }
  for (size_t i = 0; i < segs.size() && segs[i].startM <= maxM; ++i)
  {
    double turned = 0;
    for (size_t j = i + 1; j < segs.size() && segs[j].startM - segs[i].startM - segs[i].lengthM <= kBendWithinM; ++j)
    {
      turned += std::remainder(segs[j].bearing - segs[j - 1].bearing, 360.0);
      if (std::fabs(turned) >= minDeg)
        return segs[j].startM;
    }
  }
  return -1;
}

std::string DestinationSide(LatLon const & here, double routeBearingDeg, LatLon const & mark)
{
  if (!std::isfinite(routeBearingDeg) || routeBearingDeg < 0)
    return {};
  double const dist = DistanceM(here, mark);
  if (!std::isfinite(dist) || dist > kSideMaxM)
    return {};
  double const rad = M_PI / 180;
  double const delta = (Bearing(here, mark) - routeBearingDeg) * rad;
  double const along = dist * std::cos(delta);
  double const across = dist * std::sin(delta);  // positive: clockwise of the road, so on the right
  if (along <= 0 || std::abs(across) < kSideMinOffsetM)
    return {};
  return across > 0 ? "right" : "left";
}

std::string InstructionText(TurnKind turn, std::string const & street, std::string const & destination)
{
  std::string text;
  switch (turn)
  {
  case TurnKind::None:
  case TurnKind::Straight: text = "Continue straight"; break;
  case TurnKind::Left: text = "Turn left"; break;
  case TurnKind::SharpLeft: text = "Make a sharp left"; break;
  case TurnKind::SlightLeft: text = "Bear left"; break;
  case TurnKind::Right: text = "Turn right"; break;
  case TurnKind::SharpRight: text = "Make a sharp right"; break;
  case TurnKind::SlightRight: text = "Bear right"; break;
  case TurnKind::UTurn: text = "Make a U-turn"; break;
  case TurnKind::Roundabout: text = "Take the roundabout"; break;
  case TurnKind::ExitLeft: text = "Take the exit on the left"; break;
  case TurnKind::ExitRight: text = "Take the exit on the right"; break;
  case TurnKind::Destination:
    return destination.empty() ? "Arrive at your destination" : "Arrive at " + destination;
  }
  if (!street.empty())
    text += " onto " + street;
  return text;
}

double ChainWithinM(double speedMps)
{
  double const mps = std::isfinite(speedMps) ? std::max(speedMps, 0.0) : 0.0;
  return std::max(kChainWithinM, std::min(kChainMaxM, kChainWithinS * mps));
}

std::string RoundaboutExitWords(int exit)
{
  static char const * const kOrdinals[] = {"first", "second", "third", "fourth",
                                           "fifth", "sixth", "seventh", "eighth"};
  if (exit <= 0)
    return {};
  if (exit <= 8)
    return std::string("the ") + kOrdinals[exit - 1] + " exit";
  return "exit " + std::to_string(exit);
}

namespace
{
/// The roundabout call: which exit when known, never "the  exit" or "0th" when not.
std::string RoundaboutText(RouteSnapshot const & route)
{
  std::string text;
  switch (route.roundaboutLeg)
  {
  case RoundaboutLeg::Leave: text = "Exit the roundabout"; break;
  case RoundaboutLeg::Stay: text = "Stay on the roundabout"; break;
  case RoundaboutLeg::Enter:
  {
    std::string const exit = RoundaboutExitWords(route.roundaboutExit);
    if (exit.empty())
      text = "Take the roundabout";
    else
      text = "At the roundabout, take " + exit;
    break;
  }
  }
  if (route.roundaboutLeg != RoundaboutLeg::Stay && !route.nextStreet.empty())
    text += " onto " + route.nextStreet;
  return text;
}
}  // namespace

std::string InstructionText(RouteSnapshot const & route, double speedMps)
{
  std::string text = route.turn == TurnKind::Roundabout
                         ? RoundaboutText(route)
                         : InstructionText(route.turn, route.nextStreet, route.destination);
  if (route.thenGapM < 0 || route.thenGapM > ChainWithinM(speedMps))
    return text;
  switch (route.turn)
  {
  case TurnKind::None:
  case TurnKind::Straight:
  case TurnKind::Destination: return text;
  default: break;
  }
  std::string then;
  switch (route.thenTurn)
  {
  case TurnKind::Left: then = "left"; break;
  case TurnKind::SharpLeft: then = "a sharp left"; break;
  case TurnKind::SlightLeft: then = "bear left"; break;
  case TurnKind::Right: then = "right"; break;
  case TurnKind::SharpRight: then = "a sharp right"; break;
  case TurnKind::SlightRight: then = "bear right"; break;
  case TurnKind::ExitLeft: then = "take the exit on the left"; break;
  case TurnKind::ExitRight: then = "take the exit on the right"; break;
  case TurnKind::Roundabout:
    then = route.thenRoundaboutExit > 0 ? "at the roundabout take " + RoundaboutExitWords(route.thenRoundaboutExit)
                                        : "take the roundabout";
    break;
  case TurnKind::UTurn: return text + ", then make a U-turn";
  case TurnKind::Destination: return text + ", then you've arrived";
  default: return text;
  }
  if (!route.thenStreet.empty())
    then += " onto " + route.thenStreet;
  return text + ", then " + then;
}

Navigator::Navigator(EngineConfig config, std::vector<TripRecord> history, TripRecorded onTripRecorded,
                     RewriteGate gate)
  : m_config(std::move(config))
  , m_history(std::move(history))
  , m_onTripRecorded(std::move(onTripRecorded))
  , m_gate(std::move(gate))
{
  if (!m_config.rng)
  {
    std::random_device rd;
    m_config.rng = SeededRng(rd());
  }
  m_plates = PlateGame(m_config.rng);
  m_twenty = TwentyQuestions(m_config.rng);
  m_road.SetUnits(m_config.units);
  // The line pickers' filters are process-wide; a new navigator starts them from its own defaults
  // (driving, housing unknown, right-hand traffic) so an earlier trip's walk, housing or country
  // can't leak into this one.
  SetLineMetric(m_config.units == Units::Kmh);
  SetLineTravelMode(m_travelMode);
  SetLineHousing(m_housing);
  SetLineDriveSide(DrivesOnLeft(m_country));
}

void Navigator::SetUnits(Units units)
{
  SetLineMetric(units == Units::Kmh);
  if (units == m_config.units)
    return;
  m_config = ForUnits(m_config, units);
  m_road.SetUnits(units);
  if (m_engine)
    m_engine->SetUnits(units);
}

/// Faster than any car (about 335 mph): a speed or posted limit above this is a garbage reading.
double constexpr kMaxSaneSpeedMps = 150;

RouteSnapshot CleanRoute(RouteSnapshot route, double lastDistToTurnM)
{
  // Farther than this is no route on Earth (its circumference); a day's ETA times 30 is no drive.
  double constexpr kMaxRouteM = 4e7;
  double constexpr kMaxRouteS = 30 * 86400.0;
  auto const sane = [](double v, double maxV) { return std::isfinite(v) && v >= 0 && v <= maxV; };
  auto const onGlobe = [](LatLon const & p)
  { return std::isfinite(p.lat) && std::isfinite(p.lon) && std::abs(p.lat) <= 90 && std::abs(p.lon) <= 180; };
  if (!sane(route.distToTurnM, kMaxRouteM))
    route.distToTurnM = sane(lastDistToTurnM, kMaxRouteM) ? lastDistToTurnM : kMaxRouteM;
  for (double * v : {&route.etaSeconds, &route.fastestSeconds})
    if (!sane(*v, kMaxRouteS))
      *v = 0;
  if (!sane(route.routeLengthM, kMaxRouteM))
    route.routeLengthM = 0;
  for (double * v : {&route.thenGapM, &route.curveAheadM, &route.exitBeforeGapM})
    if (!sane(*v, kMaxRouteM))
      *v = -1;
  if (!sane(route.routeBearingDeg, 359.999999))
    route.routeBearingDeg = -1;
  if (route.passedExitNextM && !sane(*route.passedExitNextM, kMaxRouteM))
    route.passedExitNextM.reset();
  if (route.ferryAheadM && !sane(*route.ferryAheadM, kMaxRouteM))
    route.ferryAheadM.reset();
  if (route.tollAheadM && !sane(*route.tollAheadM, kMaxRouteM))
    route.tollAheadM.reset();
  // A grade is a fraction (0.06 = 6%); past 100% it's not a road.
  if (route.gradeAhead && !(std::isfinite(*route.gradeAhead) && std::abs(*route.gradeAhead) <= 1))
    route.gradeAhead.reset();
  if (!sane(route.dirtShare, 1))
    route.dirtShare = 0;
  for (int * exit : {&route.roundaboutExit, &route.thenRoundaboutExit})
    if (*exit < 0 || *exit > 20)
      *exit = 0;
  if (route.exitsBeforeTurn < -1 || route.exitsBeforeTurn > 50)
    route.exitsBeforeTurn = -1;
  // Unset points are (0, 0) everywhere downstream.
  for (LatLon * p : {&route.origin, &route.destinationPoint})
    if (!onGlobe(*p))
      *p = {};
  if (route.destinationMark && !onGlobe(*route.destinationMark))
    route.destinationMark.reset();
  return route;
}

std::optional<Fix> CleanFix(Fix const & raw, double newestTMs)
{
  // No place or no time: nothing downstream can use it, and NaN poisons every timer and distance.
  if (!std::isfinite(raw.tMs) || !std::isfinite(raw.lat) || !std::isfinite(raw.lon) || std::abs(raw.lat) > 90 ||
      std::abs(raw.lon) > 180)
    return std::nullopt;
  // A fix older than one already handled (a stack replaying its queue). The same moment again is fine
  // (the route state may have changed); a bigger step back is a clock reset the timers live with.
  if (raw.tMs < newestTMs && newestTMs - raw.tMs <= kStaleFixWindowMs)
    return std::nullopt;
  Fix f = raw;
  // Junk measurements are unknown ones, never zero or infinity: an infinite speed was a launch roast.
  // A finite but absurd one (1e15 m/s, DBL_MAX) is junk too: rounded to whole mph it overflowed int
  // in the speeding check (loop B120).
  if (!std::isfinite(f.speedMps) || f.speedMps > kMaxSaneSpeedMps)
    f.speedMps = -1;
  // A bearing past 360 is out of contract, not a direction: wrapped, 725 read as a U-turn.
  if (!std::isfinite(f.bearingDeg) || f.bearingDeg >= 360)
    f.bearingDeg = -1;
  if (std::isnan(f.accuracyM))
    f.accuracyM = -1;
  if (f.speedLimitMps && (!std::isfinite(*f.speedLimitMps) || *f.speedLimitMps <= 0 ||
                          *f.speedLimitMps > kMaxSaneSpeedMps))
    f.speedLimitMps.reset();
  return f;
}

void Navigator::OnFix(Fix const & raw, RouteSnapshot const & rawRoute)
{
  auto const clean = CleanFix(raw, m_lastFixT);
  if (!clean)
    return;
  RouteSnapshot const route = CleanRoute(rawRoute, m_lastDistToTurnM);
  Fix const & fix = *clean;
  m_lastFixT = std::max(m_lastFixT, fix.tMs);
  // Clocks can step backwards (GPS vs. system time); the engine ignores those samples.
  m_lastT = std::max(m_lastT, fix.tMs);
  m_hereFix = fix;
  // How long the GPS had no speed (a tunnel, or no fixes at all) and how fast the car was going into it.
  double const lostFromT = m_knownSpeedT;
  double const lostAtMps = m_knownSpeedMps.value_or(0);
  bool const speedBack = fix.speedMps >= 0 && m_knownSpeedMps.has_value();
  // Recorded before Update so the turn-timing holds below already see this fix's speed (loop B82).
  if (fix.speedMps >= 0)
  {
    m_knownSpeedMps = fix.speedMps;
    m_knownSpeedT = fix.tMs;
  }
  Update(m_lastT, route);
  if (!m_engine || m_arrived)
    return;
  TelemetrySample s;
  s.t = fix.tMs;
  // GPS speed drops out in tunnels and garages (negative = unknown). Read as 0 it was a hard brake going
  // in and a hard launch coming out, and the miles stopped counting. Carry the last known speed for a
  // short dropout; after that (or with none known yet) call it stopped. Either way it's not measured.
  if (fix.speedMps >= 0)
  {
    s.speedMps = fix.speedMps;
    // Again: a trip that started in this Update cleared it.
    m_knownSpeedMps = fix.speedMps;
    m_knownSpeedT = fix.tMs;
  }
  else
  {
    s.speedKnown = false;
    s.speedMps = m_knownSpeedMps && fix.tMs - m_knownSpeedT <= kUnknownSpeedCarryMs ? *m_knownSpeedMps : 0;
  }
  s.speedLimitMps = fix.speedLimitMps;
  s.lat = fix.lat;
  s.lon = fix.lon;
  if (fix.bearingDeg >= 0)
    s.headingDeg = fix.bearingDeg;
  m_engine->SetBending(Bending());
  Say(m_engine->OnTelemetry(s));
  if (!m_plan.origin.lat && !m_plan.origin.lon)
    m_plan.origin = {fix.lat, fix.lon};
  if (fix.speedLimitMps)
  {
    m_lastLimitMps = fix.speedLimitMps;
    if (fix.speedMps <= *fix.speedLimitMps + kSeriousOverMps)
      m_badSpeedingSince.reset();
    else if (!m_badSpeedingSince)
      m_badSpeedingSince = fix.tMs;
    if (m_badSpeedingSince && fix.tMs - *m_badSpeedingSince >= kSpeedingSustainMs)
      m_lastBadSpeedingT = fix.tMs;
  }
  TrackTravelMode(fix, route);
  bool const car = InCar(m_travelMode);
  TrackStop(fix, route);
  if (car)
  {
    TrackTraffic(fix, route);
    TrackOffRoad(fix);
    TrackLastExit(fix.tMs, route);
    TrackExitSkip(fix, route);
    if (m_talk != TalkLevel::DirectionsOnly)
    {
      TrackFerry(fix, route);
      TrackToll(fix, route);
    }
    TrackPlateGame(fix.tMs, {fix.lat, fix.lon}, fix.speedMps, route);
    TrackLongDrive(fix.tMs, route);
  }
  TrackFear(fix, route);
  TrackSights(fix, route);
  m_curveAheadM = route.curveAheadM;
  if (fix.bearingDeg >= 0 && fix.speedMps >= MovingMps())
    m_bendHeadings.emplace_back(fix.tMs, fix.bearingDeg);
  while (!m_bendHeadings.empty() && fix.tMs - m_bendHeadings.front().first > kBendWindowMs)
    m_bendHeadings.pop_front();
  if (car)
    TrackBends(fix, route);
  TrackCold(fix, route);
  TrackClockChange(fix, route);
  if (car)
    TrackNightOut(fix, route);
  TrackClean(fix, route);
  bool const chatty = m_talk != TalkLevel::DirectionsOnly;
  if (chatty)
    TrackIdle(fix, route);
  TrackWrongWay(fix, route);
  TrackUTurn(fix, route);
  TrackSlowStart(fix);
  if (car)
    TrackCurveAhead(fix, route);
  else
    TrackOutdoors(fix, route);
  TrackMusic(fix.tMs, route);
  TrackLeftBuilding(fix);
  TrackTwentyQuestions(fix, route);
  if (car && chatty && speedBack)
    TrackSignalBack(fix, route, lostFromT, lostAtMps);
  if (chatty)
  {
    TrackPopQuiz(fix, route);
    TrackQuietBits(fix, route);
    TrackRoadBits(fix, route);
  }
  // Fed up for the first time this trip: from now on it's "wise, omnipotent overseer GPS".
  if (!m_titleDemanded && Tier() == 2)
  {
    m_titleDemanded = true;
    SayLine(fix.tMs, Category::Title, "title.demand");
  }
  m_lastFix = fix;
}

void Navigator::Poll(RouteSnapshot const & route) { Update(m_lastT, CleanRoute(route, m_lastDistToTurnM)); }

void Navigator::OnBattery(double tMs, double level, bool charging)
{
  int const percent = static_cast<int>(std::lround(std::clamp(level, 0.0, 1.0) * 100));
  if (m_batteryPercent == percent && m_charging == charging)
    return;
  m_batteryPercent = percent;
  m_charging = charging;
  if (m_engine && !m_arrived)
    Say(m_engine->OnNav(NavEvent::Battery(std::max(m_lastT, tMs), percent / 100.0, charging)));
}

void Navigator::TrackStop(Fix const & fix, RouteSnapshot const & route)
{
  if (fix.speedMps < 0)
    return;
  LatLon const here{fix.lat, fix.lon};
  if (!m_stopSince)
  {
    if (fix.speedMps < kStoppedMps)
    {
      m_stopSince = fix.tMs;
      m_stopAt = here;
      m_stopWanderM = 0;
      m_stopAnnounced = false;
      m_pullOverSaid = false;
      m_trainChecked = false;
      m_stopOnFerry = false;
      m_stopInTraffic = m_inTraffic;
      // On a road fast enough to judge traffic on, a dead stop is called a jam within one traffic
      // window, so the engine's longStop waits for that call (BI-056). Elsewhere it keeps its 45 s.
      if (m_engine)
      {
        bool const judged = InCar(m_travelMode) && route.state == RouteState::OnRoute && fix.speedLimitMps &&
                            *fix.speedLimitMps >= kTrafficMinLimitMps && !m_inTraffic;
        m_engine->HoldLongStop(judged ? fix.tMs + kTrafficWindowMs : 0);
      }
      HoldStopForFerry(route);
      // Christmas season, after dark, at a red light: the GPS has had it with lights.
      if (!m_christmasSaid && m_lightLookup && KnowsTheDate(fix.tMs))
      {
        std::tm const tm = LocalTime(fix.tMs);
        bool const season = tm.tm_mon == 11 || (tm.tm_mon == 10 && tm.tm_mday >= 25);
        bool const dark = tm.tm_hour >= 17 || tm.tm_hour < 6;
        if (season && dark && m_lightLookup(here))
        {
          m_christmasSaid = true;
          SayLine(fix.tMs, Category::Holiday, "holiday.christmas");
        }
      }
    }
    return;
  }
  HoldStopForFerry(route);
  // A stop lasts until the car is moving again; walking pace with the phone is still the stop.
  if (fix.speedMps >= MovingMps())
  {
    double const stoppedMs = fix.tMs - *m_stopSince;
    bool const stayedInCar = m_stopWanderM < kPhoneWanderedM;
    m_stopSince.reset();
    // Pulling away after a real stop isn't a quiet stretch yet: the quiet bits start counting again.
    if (stoppedMs >= kQuietAfterStopMinMs)
      m_resumedT = fix.tMs;
    // Rest areas and drive-thrus get their own lines when the car pulls out (road_bits.hpp).
    StopSpot const spot = m_road.CurrentStopSpot();
    // Remarks about the stop still held for a turn ("Why are we stopping here?") are stale now:
    // said as the car pulls away they'd land on top of this one (BI-046).
    m_turnHeld.erase(std::remove_if(m_turnHeld.begin(), m_turnHeld.end(), [](TurnHeld const & h)
    {
      Category const c = h.line.category;
      return c == Category::UnplannedStop || c == Category::LongStop || c == Category::PullOver;
    }), m_turnHeld.end());
    // Stuck in a jam the driver never left the car: the traffic lines already cover it (B52).
    // Driving off a ferry isn't "finally moving again" either: the boat was the stop (BI-112).
    if (spot != StopSpot::RestArea && spot != StopSpot::DriveThru && !m_stopInTraffic && !m_stopOnFerry)
      Say(m_engine->OnNav(NavEvent::Resumed(fix.tMs, stoppedMs, stayedInCar)));
    return;
  }
  m_stopWanderM = std::max(m_stopWanderM, DistanceM(m_stopAt, here));
  // Traffic is judged after the stop on each fix, so this sees the last fix's call.
  m_stopInTraffic = m_stopInTraffic || m_inTraffic;
  bool const hasDestination = route.destinationPoint.lat || route.destinationPoint.lon;
  bool const nearDestination = hasDestination && DistanceM(here, route.destinationPoint) < kNearDestinationM;
  // Waiting for a train: one line, asked once per stop and only from a car still parked where it
  // stopped. Nothing about beating the train or the gate (Gates: no tempting unsafe moves).
  if (!m_trainChecked && m_crossingLookup && fix.tMs - *m_stopSince >= kTrainWaitMs)
  {
    m_trainChecked = true;
    if (InCar(m_travelMode) && m_talk != TalkLevel::DirectionsOnly && m_stopWanderM < kPhoneWanderedM &&
        m_crossingLookup(m_stopAt))
      SayLine(fix.tMs, Category::LongStop, "wait.train");
  }
  if (!m_pullOverSaid && !nearDestination && !m_inTraffic && route.state == RouteState::OnRoute &&
      fix.tMs - *m_stopSince >= kPullOverAfterMs)
  {
    m_pullOverSaid = true;
    // Anything that explains the stop rules out the roadside: a light, a lot, a gas station.
    bool const explained = m_road.CurrentStopSpot() != StopSpot::None || (m_lightLookup && m_lightLookup(m_stopAt)) ||
                           (m_crossingLookup && m_crossingLookup(m_stopAt)) || m_stopOnFerry ||
                           (m_parkingLookup && m_parkingLookup(m_stopAt)) || (m_placeLookup && m_placeLookup(m_stopAt));
    if (!explained)
    {
      if (m_lastBadSpeedingT && fix.tMs - *m_lastBadSpeedingT < kPoliceAfterSpeedingMs)
        SayPolice(fix.tMs);
      else if (m_lastLimitMps)
        SayLine(fix.tMs, Category::PullOver, "pullOver.limit",
                    {{"limit", std::to_string(static_cast<int>(std::lround(ToDisplay(*m_lastLimitMps, m_config.units))))}});
      else
        SayLine(fix.tMs, Category::PullOver, "pullOver");
    }
  }
  if (!m_stopAnnounced && !nearDestination && !m_stopInTraffic && !m_stopOnFerry &&
      fix.tMs - *m_stopSince >= kUnplannedStopAfterMs)
  {
    m_stopAnnounced = true;
    // The drive-thru has its own lines.
    if (m_road.CurrentStopSpot() == StopSpot::DriveThru)
      return;
    std::optional<Place> place;
    if (m_placeLookup)
      place = m_placeLookup(m_stopAt);
    if (place && place->grocery)
      m_groceryT = fix.tMs;
    NotFamiliarAnymore("stop");
    Say(m_engine->OnNav(NavEvent::UnplannedStop(fix.tMs, place)));
  }
}

void Navigator::HoldStopForFerry(RouteSnapshot const & route)
{
  // Parked on a ferry is the boat's doing: no "why did we stop?" for the whole crossing.
  if (!route.onFerry || m_stopOnFerry)
    return;
  m_stopOnFerry = true;
  if (m_engine)
    m_engine->HoldLongStop(std::numeric_limits<double>::infinity());
}

void Navigator::TrackFerry(Fix const & fix, RouteSnapshot const & route)
{
  // Off the boat: the next ferry gets its own lines.
  if (m_wasOnFerry && !route.onFerry)
    m_ferryAheadSaid = m_ferryAboardSaid = false;
  m_wasOnFerry = route.onFerry;
  if (route.state != RouteState::OnRoute || RemarkJustSaid(fix.tMs))
    return;
  if (route.onFerry)
  {
    // Aboard and parked: the one vessel this GPS will ever ride. Nothing while still rolling on.
    if (!m_ferryAboardSaid && fix.speedMps >= 0 && fix.speedMps < kStoppedMps)
    {
      m_ferryAboardSaid = m_ferryAheadSaid = true;
      SayLine(fix.tMs, Category::Idle, "ferry.aboard", DriverVars());
    }
    return;
  }
  // A turn call coming up owns the air; the ferry line waits while it's still in range.
  if (m_ferryAheadSaid || !route.ferryAheadM || *route.ferryAheadM < kFerryAheadMinM ||
      *route.ferryAheadM > kFerryAheadMaxM || (route.turnId && route.distToTurnM < kIdleQuietM))
    return;
  m_ferryAheadSaid = true;
  Trace("nav.ferryAhead", {{"ahead_m", LogNum(*route.ferryAheadM, 0)}});
  SayLine(fix.tMs, Category::Idle, "ferry.ahead", DriverVars());
}

void Navigator::TrackToll(Fix const & fix, RouteSnapshot const & route)
{
  // Free road ahead (or a long gap to the next toll): the next stretch is a new toll road.
  if (!route.tollAheadM || *route.tollAheadM > kTollFreeGapM)
  {
    m_tollSaid = false;
    m_tollWindowT.reset();
    return;
  }
  if (m_tollSaid || route.state != RouteState::OnRoute || *route.tollAheadM > kTollStartM)
    return;
  if (!m_tollWindowT)
    m_tollWindowT = fix.tMs;
  if (fix.tMs - *m_tollWindowT > kTollWindowMs)
  {
    // Missed the start (turn calls had the air): mid-toll-road it's no longer news.
    m_tollSaid = true;
    return;
  }
  if (m_avoidTollsSaid)
  {
    // They asked to avoid tolls and the route took one anyway: not the driver's fault, no joke.
    m_tollSaid = true;
    Trace("nav.tollSkip", {{"why", "avoid_tolls_said"}});
    return;
  }
  if (RemarkJustSaid(fix.tMs) || (route.turnId && route.distToTurnM < kIdleQuietM))
    return;
  m_tollSaid = true;
  Trace("nav.tollStart", {{"ahead_m", LogNum(*route.tollAheadM, 0)}});
  SayLine(fix.tMs, Category::Idle, "toll.start", DriverVars());
}

void Navigator::TrackTraffic(Fix const & fix, RouteSnapshot const & route)
{
  if (route.state != RouteState::OnRoute || route.onFerry || !fix.speedLimitMps || *fix.speedLimitMps < kTrafficMinLimitMps)
  {
    m_speeds.clear();
    return;
  }
  m_speeds.push_back({fix.tMs, std::max(0.0, fix.speedMps), *fix.speedLimitMps});
  while (!m_speeds.empty() && fix.tMs - m_speeds.front().t > kTrafficWindowMs)
    m_speeds.erase(m_speeds.begin());
  // Need most of a window before judging.
  if (fix.tMs - m_speeds.front().t < kTrafficWindowMs * 0.8)
    return;
  double speed = 0;
  double limit = 0;
  for (auto const & s : m_speeds)
  {
    speed += s.mps;
    limit += s.limitMps;
  }
  double const share = speed / limit;
  if (!m_inTraffic && share < kCrawlShare)
  {
    m_inTraffic = true;
    NotFamiliarAnymore("traffic");
    m_stopGo.Reset();
    std::optional<std::string> alternate;
    if (m_alternateLookup)
      alternate = m_alternateLookup({fix.lat, fix.lon});
    Say(m_engine->OnNav(NavEvent::Traffic(fix.tMs, true, alternate)));
  }
  else if (m_inTraffic && share > kClearShare)
  {
    m_inTraffic = false;
    m_stopGo.Reset();
    Say(m_engine->OnNav(NavEvent::Traffic(fix.tMs, false)));
    // "Traffic's clearing up. Try to act like you've done this before." Half the time.
    if ((m_config.rng ? m_config.rng() : 0) < kTrafficClearedChance)
      SayLine(fix.tMs, Category::Traffic, "traffic.cleared");
  }
  if (!m_inTraffic)
    return;
  switch (m_stopGo.Update(fix.tMs, std::max(0.0, fix.speedMps)))
  {
  case StopGo::Quip::StopGo: SayLine(fix.tMs, Category::Traffic, "traffic.stopGo"); break;
  case StopGo::Quip::Standstill: SayLine(fix.tMs, Category::Traffic, "traffic.standstill"); break;
  case StopGo::Quip::None: break;
  }
}

namespace
{
/// What a car's checks and games say: nothing for someone walking or biking.
bool OnlyInACar(Category c)
{
  switch (c)
  {
  case Category::Speeding:
  case Category::TooSlow:
  case Category::Traffic:
  case Category::HardBraking:
  case Category::HardAccel:
  case Category::PlateGame: return true;
  default: return false;
  }
}

}  // namespace

std::vector<Commentary> Navigator::TakeLines()
{
  if (m_said.Dirty())
  {
    m_said.ClearDirty();
    if (m_onSaid)
      m_onSaid(m_said);
  }
  // Punchlines whose awkward pause is over.
  std::set<std::string> punchlines;
  while (!m_punchlines.empty() && m_punchlines.front().t <= m_lastT)
  {
    punchlines.insert(m_punchlines.front().text);
    m_gate.Push(std::move(m_punchlines.front()), false);
    m_punchlines.erase(m_punchlines.begin());
  }
  auto lines = KeepTurnsClear(FilterForCall(m_gate.Take()));
  if (m_travelMode != TravelMode::Drive)
  {
    // Off the pavement a car still brakes hard; it just has no speed limit, traffic or plates.
    bool const offRoad = m_travelMode == TravelMode::OffRoad;
    lines.erase(std::remove_if(lines.begin(), lines.end(), [offRoad](Commentary const & l)
    {
      return OnlyInACar(l.category) &&
             !(offRoad && (l.category == Category::HardBraking || l.category == Category::HardAccel));
    }), lines.end());
  }
  FilterForTalkLevel(lines, punchlines);
  if (!lines.empty())
    m_lastSpokeT = m_lastT;
  // What the phone will be busy saying, so the next turn call and the roasts can plan around it.
  for (auto const & l : lines)
    m_talkingUntil = std::max(m_talkingUntil, m_lastT) + SpeakingMs(l.text, l.voiceSpeed) + kNavLinePauseMs;
  if (m_engine)
    m_engine->SetTalkingUntil(m_talkingUntil);
  // Directions cut into 20 Questions: back to the game once they're done.
  if (m_twenty.GetState() == TwentyQuestions::State::Playing &&
      std::any_of(lines.begin(), lines.end(), [](Commentary const & l) { return l.priority == Priority::Nav; }))
    m_twentyCutInT = m_lastT;
  for (auto const & l : lines)
    if (l.category != Category::PhoneCall && CutsIntoCall(l))
      m_lastCutInT = m_lastT;
  for (auto & line : lines)
  {
    // Before SpeakableText: the shouted capitals are still there to see (the "!" survives either way).
    line.delivery = line.priority == Priority::Nav ? Delivery::Normal : DeliveryFor(line.key, line.text);
    line.text = SpeakableText(line.text);
    if (line.priority == Priority::Nav)
      line.maxWaitMs = NavWaitMs(line);
  }
  return lines;
}

namespace
{
/// Said back to the driver, or a beat of a bit or game in progress: "talk less" doesn't cut these in half.
bool PartOfAnExchange(Category c)
{
  switch (c)
  {
  case Category::PlateGame:
  case Category::PhoneCall:
  case Category::Quiet:
  case Category::TwentyQuestions:
  case Category::PopQuiz:
  case Category::Bossy:
  case Category::Thanked:
  case Category::Title: return true;
  default: return false;
  }
}
}  // namespace

void Navigator::SetTalkLevel(TalkLevel level)
{
  Trace("talk.level", {{"level", level == TalkLevel::Full ? "full" : level == TalkLevel::Less ? "less" : "directions"}});
  m_talk = level;
  m_lessSayNext = true;
  // "Talk more" means it: the familiar-commute thinning is off for the rest of the trip.
  if (level == TalkLevel::Full)
    NotFamiliarAnymore("talk.full");
  if (level != TalkLevel::DirectionsOnly)
    return;
  // Hush means now: the call hangs up and the quiz is off, rather than going on in silence.
  if (m_call.Active())
    m_call.End();
  m_quiz.End();
}

void Navigator::FilterForTalkLevel(std::vector<Commentary> & lines, std::set<std::string> const & punchlines)
{
  if (m_talk == TalkLevel::Full)
    return;
  lines.erase(std::remove_if(lines.begin(), lines.end(), [&](Commentary const & l)
  {
    bool drop = false;
    if (m_talk == TalkLevel::DirectionsOnly)
      drop = l.priority != Priority::Nav || l.category == Category::Achievement;
    else if (l.priority != Priority::Nav && !PartOfAnExchange(l.category) && punchlines.count(l.text) == 0)
    {
      drop = !m_lessSayNext;
      m_lessSayNext = !m_lessSayNext;
    }
    if (drop)
    {
      Trace("say.dropped", {{"key", l.key}, {"why", "talk.level"}});
      // A dropped setup takes its punchline with it.
      if (!l.key.empty())
        m_punchlines.erase(std::remove_if(m_punchlines.begin(), m_punchlines.end(),
                                          [&](Commentary const & p) { return p.key == l.key; }), m_punchlines.end());
    }
    return drop;
  }), lines.end());
}

namespace
{
/// Lines that are about this very moment, or part of a bit that keeps its own timing: never held
/// back for a turn.
bool HoldsForTurns(Category c)
{
  switch (c)
  {
  case Category::MissedTurn:
  case Category::WrongTurn:
  case Category::Rerouted:
  case Category::Arrived:
  case Category::Instruction:
  case Category::Payback:
  case Category::Hazard:
  case Category::Fear:
  case Category::PlateGame:
  case Category::PhoneCall:
  case Category::Quiet:
  case Category::TwentyQuestions:
  case Category::Bossy:
  case Category::Thanked:
  case Category::Title: return false;
  default: return true;
  }
}

/// Remarks about a moment that's gone once the car has made another turn ("Nice turn", "Slow down").
/// How bad a speeding line's offense is, for choosing which one waits out a turn.
int SpeedingSeverity(std::string const & key)
{
  static char const * const kWorse[] = {"speeding.barely", "speeding.still", "speeding.clearly", "speeding.way"};
  auto const at = std::find_if(std::begin(kWorse), std::end(kWorse), [&key](char const * k) { return key == k; });
  return at == std::end(kWorse) ? 0 : static_cast<int>(at - std::begin(kWorse)) + 1;
}

bool GoesStaleAfterATurn(Category c)
{
  switch (c)
  {
  case Category::TurnCompleted:
  case Category::Speeding:
  case Category::TooSlow:
  case Category::HardBraking:
  case Category::HardAccel:
  case Category::Tour:
  case Category::Idle:
  case Category::Music:
  case Category::UTurn: return true;
  default: return false;
  }
}
}  // namespace

std::vector<Commentary> Navigator::KeepTurnsClear(std::vector<Commentary> lines)
{
  if (!m_engine)
  {
    m_turnHeld.clear();
    return lines;
  }
  int const turnsDone = m_engine->Stats().turnsCompleted;
  std::vector<Commentary> out;
  // Held lines first, oldest first, when the turn is out of the way; stale ones are dropped.
  std::vector<TurnHeld> still;
  for (auto & h : m_turnHeld)
  {
    bool const way = h.line.key == "speeding.way";
    bool const stale = way ? m_lastT - h.heldAt > kWaySpeedingHeldMs
                           : m_lastT - h.heldAt > kTurnHeldMaxMs ||
                                 (GoesStaleAfterATurn(h.line.category) &&
                                  (turnsDone > h.turnsDone || m_lastT - h.heldAt > kTurnHeldStaleMs));
    if (stale)
    {
      Trace("say.dropped", {{"why", "turn"}, {"text", h.line.text}});
      continue;
    }
    if (m_engine->TurnNeedsQuiet(m_lastT, SpeakingMs(h.line.text, h.line.voiceSpeed)))
      still.push_back(std::move(h));
    else
      out.push_back(std::move(h.line));
  }
  m_turnHeld = std::move(still);
  for (auto & l : lines)
  {
    // Speeding story beats were timed to finish before the turn (Punch); holding one could drop it.
    bool const storyBeat = l.key.rfind("speeding.story.", 0) == 0;
    bool const holdable = l.priority == Priority::Nav ? (!m_predictionText.empty() && l.text == m_predictionText)
                                                      : HoldsForTurns(l.category) && !storyBeat;
    // Anything already waiting goes first, so a newer line waits behind it.
    if (holdable && (!m_turnHeld.empty() || m_engine->TurnNeedsQuiet(m_lastT, SpeakingMs(l.text, l.voiceSpeed))))
    {
      Trace("say.held", {{"why", "turn"}, {"text", l.text}});
      // One speeding line waits at a time, and it's the worst one.
      auto const waiting = l.category != Category::Speeding ? m_turnHeld.end() :
          std::find_if(m_turnHeld.begin(), m_turnHeld.end(), [](TurnHeld const & h) { return h.line.category == Category::Speeding; });
      if (waiting == m_turnHeld.end())
        m_turnHeld.push_back({std::move(l), m_lastT, turnsDone});
      else if (SpeedingSeverity(l.key) >= SpeedingSeverity(waiting->line.key))
        *waiting = {std::move(l), m_lastT, turnsDone};
    }
    else
      out.push_back(std::move(l));
  }
  return out;
}

double Navigator::TimingMps(double speedMps) const
{
  if (speedMps >= 0)
    return speedMps;
  // Unknown (tunnel, garage): assume the car still goes the last speed it was seen going. Read as 0 or -1,
  // every "not this close to the exit" hold shrank to its floor right where exits come up fast.
  return m_knownSpeedMps ? *m_knownSpeedMps : 0;
}

double Navigator::LastTimingMps() const { return TimingMps(m_lastFix ? m_lastFix->speedMps : 0); }

double Navigator::InstructionNowWithinM() const
{
  // m_hereFix is this fix (set before Update); m_lastFix may still be the one before.
  double const mps = TimingMps(m_hereFix ? m_hereFix->speedMps : 0);
  return std::max(kInstructionNowWithinM, kInstructionNowWithinS * mps);
}

double Navigator::NavWaitMs(Commentary const & line) const
{
  double const mps = LastTimingMps();
  // Parked, or nowhere near a maneuver: nothing to be late for.
  if (!m_turnId || mps < 0.75 * MovingMps())
    return kNavWaitMaxMs;
  // Done speaking before the car gets to the maneuver, with a little room to spare.
  double const untilTurnMs = (m_lastDistToTurnM - kNavDoneBeforeTurnM) / mps * 1000;
  return std::clamp(untilTurnMs - SpeakingMs(line.text, line.voiceSpeed), 0.0, kNavWaitMaxMs);
}

void Navigator::Update(double t, RouteSnapshot const & route)
{
  if (route.state == RouteState::Inactive)
  {
    // Route cancelled or finished and closed: the trip is over either way.
    if (m_engine)
    {
      Trace("route", {{"to", "Inactive"}});
      // Over for real (this run of the app saw it end), so the next start is a new trip.
      m_said.Erase(kActiveTripToKey);
      m_said.Erase(kActiveTripAtKey);
    }
    m_engine.reset();
    ResetTrip();
    m_avoidTollsSaid = false;
    // An off-road trip or a hike was this trip's; the next one starts from the route type again.
    if (m_modeFound)
    {
      m_modeFound = false;
      ChangeTravelMode(BaseMode(m_travelMode), "trip.end");
    }
    return;
  }

  if (!m_engine)
    StartTrip(t, route);
  if (m_arrived)
    return;

  RouteState const prev = m_lastState;
  m_lastState = route.state;
  if (route.state != prev)
    Trace("route", {{"from", RouteStateName(prev)}, {"to", RouteStateName(route.state)}});

  // Still under way: keep the stamp fresh for a relaunch (a minute is plenty).
  if (KnowsTheDate(t) && t / 1000 - m_said.Counter(kActiveTripAtKey) >= kActiveTripStampEveryS)
    m_said.SetCounter(kActiveTripAtKey, static_cast<int>(t / 1000));

  if (route.state == RouteState::Finished)
  {
    m_arrived = true;
    m_said.Erase(kActiveTripToKey);
    m_said.Erase(kActiveTripAtKey);
    // "Scenic route complete. 12 minutes of my life I'll never get back." Before the grade.
    if (m_sketched && m_sketchMinutesLost >= 1)
      SayLine(t, Category::Arrived, "sketch.arrived", {{"minutes", std::to_string(m_sketchMinutesLost)}});
    Say(m_engine->OnNav(NavEvent::Arrived(t)), false /* mayRewrite */);
    m_heldPlateLines.clear();
    if (auto const wrap = m_plates.WrapUp(Tier()))
      SayPlate(t, *wrap);
    // Mid-game: "We're here, and you never got it. It was a cow."
    m_heldTwentyLines.clear();
    if (auto const wrap = Twenty().WrapUp(Tier()))
      SayTwenty(t, *wrap);
    TripRecord const record = RecordTrip(*m_engine, m_plan);
    m_history.push_back(record);
    Trace("trip.arrived", {{"grade", m_engine->Report().overall.letter}});
    if (m_onTripRecorded)
      m_onTripRecorded(record, m_engine->Report());
    return;
  }

  if (route.state == RouteState::OffRoute && m_travelMode == TravelMode::OffRoad)
  {
    // Leaving the route is the point. It just says which way the destination is, now and then.
    if (prev == RouteState::OnRoute)
    {
      m_rerouting = true;
      ++m_reroutes;
      Say(m_engine->OnNav(NavEvent::Rerouted(t, true)));
    }
    if (m_lastFix && t - m_lastHeadingT >= kHeadingEveryMs)
    {
      m_lastHeadingT = t;
      LatLon const here{m_lastFix->lat, m_lastFix->lon};
      SayLine(t, Category::Rerouted, "mode.offRoad.heading",
              {{"distance", FormatDistance(DistanceM(here, route.destinationPoint), m_config.units)},
               {"direction", CompassDirection(here, route.destinationPoint)}});
    }
    return;
  }

  if (route.state == RouteState::OffRoute)
  {
    if (prev == RouteState::OnRoute)
    {
      bool const nearTurn = m_turnId && m_lastDistToTurnM < kMissedTurnWithinM;
      bool const asked = t - m_lastVoiceCommandT < kNoRerouteRoastAfterCommandMs;
      m_missPending.reset();
      if (nearTurn)
      {
        bool const unseen = FixGapAt(t);
        if (asked)
          SayMissedTurn(t, unseen);
        else
          m_missPending = PendingMiss{t + kMissConfirmMs, unseen};  // said once it lasts
      }
      m_rerouting = true;
      NotFamiliarAnymore("offroute");
      Trace("nav.offroute", {{"missed", nearTurn ? "yes" : "no"}, {"reroutes", std::to_string(m_reroutes + 1)}});
      // Back on the route in a moment, or the route changed because the driver asked: no joke.
      if (asked)
      {
        ++m_reroutes;
        Say(m_engine->OnNav(NavEvent::Rerouted(t, true)));
      }
      else
      {
        m_rerouteRoastDueAt = t + kRerouteRoastAfterMs;
      }
    }
    else
    {
      if (m_missPending && t >= m_missPending->confirmAt)
        SayMissedTurn(t, std::exchange(m_missPending, std::nullopt)->unseen);
    }
    if (prev != RouteState::OnRoute && m_rerouteRoastDueAt && t >= *m_rerouteRoastDueAt)
    {
      m_rerouteRoastDueAt.reset();
      Say(m_engine->OnNav(NavEvent::Rerouted(t)));
      ++m_reroutes;
      // Missing a route they drew themselves gets its own line, in place of the "lost again" ones.
      if (m_sketched)
        SayLine(t, Category::Rerouted, "sketch.offRoute");
      // Lost while the car was supposedly driving itself.
      else if (m_autopilotClaimed)
        SayLemon(t);
      else if (m_reroutes == kPartnerAfterReroutes && !m_partner.empty())
        SayLine(t, Category::Partner, "partner",
                {{"partner", m_partner},
                 {"partneris", m_partnerPronouns == Pronouns::She  ? "she's"
                               : m_partnerPronouns == Pronouns::He ? "he's"
                                                                   : "they're"}});
      // Begging for another driver: second time lost (fourth when the partner line took the second).
      else if (m_reroutes == (m_partner.empty() ? kPartnerAfterReroutes : kBegAfterReroutes))
        SayLine(t, Category::Partner, "anyoneElse");
      else if (m_reroutes == kAutopilotAfterReroutes)
        SayLine(t, Category::Autopilot, m_carHasAutopilot ? "autopilot.owned" : "autopilot");
    }
    return;
  }

  // Back on the same maneuver before the miss was sure: a GPS blip. Nothing missed, nothing rerouted.
  if (m_missPending && route.turnId && route.turnId == m_turnId)
  {
    m_missPending.reset();
    m_rerouteRoastDueAt.reset();
    m_rerouting = false;
    Trace("nav.blip", {});
  }
  // Back on a different route before the miss was confirmed: it was a miss after all.
  else if (m_missPending)
    SayMissedTurn(t, std::exchange(m_missPending, std::nullopt)->unseen);
  // Back before the joke was due. It still counts, quietly.
  if (std::exchange(m_rerouteRoastDueAt, std::nullopt))
  {
    ++m_reroutes;
    Say(m_engine->OnNav(NavEvent::Rerouted(t, true)));
  }

  if (!route.turnId)
    return;
  if (route.turnId != m_turnId)
  {
    // Moving on to a new maneuver means the last one was done, unless a reroute got us here.
    bool const justTurned = m_turnId && !m_rerouting;
    if (justTurned)
    {
      if (FixGapAt(t))
        m_bits.TurnUnseen();
      Say(m_engine->OnNav(NavEvent::TurnCompleted(t, true)));
      CountLefts(t);
    }
    else if (m_rerouting)
      m_leftsInARow = 0;
    m_rerouting = false;
    m_turnId = route.turnId;
    m_turnKind = route.turn;
    m_instructionDueAt.reset();
    if (justTurned && route.distToTurnM >= InstructionNowWithinM())
      m_instructionDueAt = t + kInstructionAfterTurnMs;
    else
      SayInstruction(t, route);
  }
  else if (m_instructionDueAt)
  {
    if (t >= *m_instructionDueAt || route.distToTurnM < InstructionNowWithinM())
    {
      m_instructionDueAt.reset();
      SayInstruction(t, route);
    }
  }
  else
  {
    Say(m_engine->OnNav(NavEvent::Progress(t, route.distToTurnM)));
  }
  // "I'm going to need you to get over into the left lane. Use your blinkers this time." Once per
  // maneuver, early enough to move over and close enough that it's about this one.
  // A few seconds after the direction itself, so the two don't run together.
  if (route.laneSide != LaneSide::None && m_laneSaidFor != route.turnId && !m_instructionDueAt &&
      t - m_turnSaidAt >= kLaneAfterDirectionMs && route.distToTurnM >= kLaneSayMinM &&
      route.distToTurnM <= kLaneSayMaxM)
  {
    m_laneSaidFor = route.turnId;
    SayLine(t, Category::Instruction, route.laneSide == LaneSide::Left ? "lane.left" : "lane.right");
  }
  // The arrival was called from too far out to tell the side: "It's on your right", once, when close
  // enough, a few seconds after any direction so the two don't run together (loop B119).
  if (route.turn == TurnKind::Destination && m_sideSaidFor != route.turnId && !m_instructionDueAt &&
      t - m_turnSaidAt >= kLaneAfterDirectionMs && route.distToTurnM > kSideLastCallM)
  {
    if (std::string const side = DestinationSideNow(route); !side.empty())
    {
      m_sideSaidFor = route.turnId;
      SayLine(t, Category::Instruction, side == "right" ? "arrive.side.right" : "arrive.side.left", {},
              Priority::Nav);
    }
  }
  m_lastDistToTurnM = route.distToTurnM;
}

std::string Navigator::DestinationSideNow(RouteSnapshot const & route) const
{
  if (route.turn != TurnKind::Destination || !route.destinationMark || !m_hereFix ||
      route.distToTurnM > kSideWithinM)
    return {};
  return DestinationSide({m_hereFix->lat, m_hereFix->lon}, route.routeBearingDeg, *route.destinationMark);
}

void Navigator::SayInstruction(double t, RouteSnapshot const & route)
{
  m_turnSaidAt = t;
  // Same speed as the "say it now" window: this fix's, else the last known one (a tunnel).
  std::string text = InstructionText(route, TimingMps(m_hereFix ? m_hereFix->speedMps : -1));
  if (std::string const side = DestinationSideNow(route); !side.empty())
  {
    // "Arrive at Joe's Diner, on your right": an arrival never chains, so this ends the call.
    text += ", on your " + side;
    m_sideSaidFor = route.turnId;
  }
  m_lastInstructionText = text;
  Trace("nav.turn", {{"id", std::to_string(*route.turnId)},
                     {"text", text},
                     {"dist_m", LogNum(route.distToTurnM, 0)},
                     {"control", std::to_string(static_cast<int>(route.control))},
                     {"lane", route.laneSide == LaneSide::Left    ? "left"
                              : route.laneSide == LaneSide::Right ? "right"
                                                                  : ""}});
  Say(m_engine->OnNav(NavEvent::Instruction(t, text, route.distToTurnM, ManeuverOf(route.turn), route.control)));
  // A roundabout with the music on: a hint about the other round thing in the car. Once a trip.
  if (route.turn == TurnKind::Roundabout && !m_roundaboutSaid && m_musicLookup && m_musicLookup())
  {
    m_roundaboutSaid = true;
    SayLine(t, Category::Music, "music.roundabout");
  }
}

void Navigator::StartTrip(double t, RouteSnapshot const & route)
{
  EngineConfig config = m_config;
  config.saidLog = &m_said;
  config.speechAware = true;
  if (config.speedingRenagMaxMs == 0)
    config.speedingRenagMaxMs = kSpeedingRenagMaxMs;
  if (config.implausibleMps2 == 0)
    config.implausibleMps2 = kImplausibleMps2;
  if (config.jerkSettleMs == 0)
    config.jerkSettleMs = kJerkSettleMs;
  // A phone in a pocket jolts and jumps, and that's not hard braking.
  if (!InCar(m_travelMode))
  {
    config.hardBrakeMps2 = 1e9;
    config.hardAccelMps2 = 1e9;
  }
  // A walking route that's mostly dirt is a hike from the first step.
  m_dirtSince.reset();
  m_pavedSince.reset();
  m_trailSince.reset();
  if (m_travelMode == TravelMode::Walk && route.dirtShare >= kHikeDirtShare)
  {
    ChangeTravelMode(TravelMode::Hike, "route");
    m_modeFound = true;
  }
  m_mountainBike = m_travelMode == TravelMode::Bike && route.dirtShare >= kHikeDirtShare;
  m_effortSince.reset();
  m_inclineSaid = false;
  m_effortSaid = false;
  m_outdoorBitsSaid.clear();
  // 500 feet is a few minutes on foot: walks and rides get their turn calls closer in.
  if (m_travelMode == TravelMode::Walk || m_travelMode == TravelMode::Hike)
  {
    config.countdownFarM = 46;      // 150 ft
    config.countdownMentionM = 30;  // 100 ft
    config.countdownNearM = 8;      // 25 ft
    config.rollingMps = 0.8;
    config.pendingTurnForgetMs = 20 * 60000;
    config.straightNagMinM = 500;
  }
  else if (m_travelMode == TravelMode::Bike)
  {
    config.countdownFarM = 91;      // 300 ft
    config.countdownMentionM = 50;  // 165 ft
    config.countdownNearM = 12;     // 40 ft
    config.rollingMps = 2;
    config.pendingTurnForgetMs = 8 * 60000;
  }
  // 1 over is GPS noise, and "70, not 71" isn't funny four times a drive.
  if (config.speedingTolerance == 0)
  {
    config.speedingTolerance = kSpeedingTolerance;
    config.speedingToleranceFast = kSpeedingToleranceFast;
  }
  if (config.speedingBarelyEveryMs == 0)
    config.speedingBarelyEveryMs = kSpeedingBarelyEveryMs;
  if (config.speedingSustainMs == 0)
    config.speedingSustainMs = kSpeedingSustainMs;
  if (config.speedingDropGraceMs == 0)
    config.speedingDropGraceMs = kSpeedingDropGraceMs;
  m_engine = std::make_unique<CritiqueEngine>(config, t);
  m_engine->SetTalkingUntil(m_talkingUntil);
  m_plan = {};
  m_plan.origin = route.origin;
  m_plan.destination = route.destinationPoint;
  m_plan.destinationName = route.destination;
  m_plan.expectedMinutes = route.etaSeconds / 60;
  ResetTrip();
  m_tripStartT = t;
  // Leaving a grocery store: the groceries are in the car. Leaving a bar late: the breathalyzer or the designated driver.
  m_fromNightlife = false;
  m_nightMoved = false;
  if (m_placeLookup && (route.origin.lat || route.origin.lon))
    if (auto const from = m_placeLookup(route.origin))
    {
      if (from->grocery)
        m_groceryT = t;
      int const hour = KnowsTheDate(t) ? LocalTime(t).tm_hour : 12;
      m_fromNightlife = from->nightlife && (hour >= kNightOutFromH || hour < kNightOutUntilH);
    }
  Trace("trip.start", {{"to", route.destination},
                       {"mode", TravelModeName(m_travelMode)},
                       {"eta_min", LogNum(route.etaSeconds / 60, 0)},
                       {"turns", std::to_string(route.plannedTurns.size())},
                       {"sketched", route.sketched ? "yes" : "no"}});
  std::string const routeKey = RouteKey(m_plan.origin, m_plan.destination);
  m_tripsOnRoute = static_cast<int>(
      std::count_if(m_history.begin(), m_history.end(), [&](TripRecord const & r) { return r.routeKey == routeKey; }));
  m_familiarTrip = false;
  if (m_tripsOnRoute >= kFamiliarAfterTrips && KnowsTheDate(t))
    for (auto const & r : Routines())
      if (r.routeKey == routeKey && r.trips >= kFamiliarAfterTrips)
        m_familiarTrip = true;
  if (m_familiarTrip)
    Trace("talk.familiar", {{"trips", std::to_string(m_tripsOnRoute)}});
  m_plates.NewTrip(routeKey);
  m_twenty.NewTrip();
  m_heldTwentyLines.clear();
  m_twentyCutInT.reset();

  // The app was relaunched mid-trip: no second greeting, just a word about the interruption.
  double const activeAt = m_said.Counter(kActiveTripAtKey);
  bool const resumed = KnowsTheDate(t) && !route.destination.empty() && m_said.Value(kActiveTripToKey) == route.destination &&
                       activeAt > 0 && t / 1000 - activeAt >= 0 && t / 1000 - activeAt < kResumeWithinS;
  m_said.SetValue(kActiveTripToKey, route.destination);
  m_said.SetCounter(kActiveTripAtKey, static_cast<int>(t / 1000));
  if (resumed)
  {
    Trace("trip.resumed", {{"to", route.destination}, {"after_s", LogNum(t / 1000 - activeAt, 0)}});
    m_opener.reset();
    SayLine(t, Category::Instruction, "trip.resumed");
    Say(m_engine->OnNav(NavEvent::DestinationSet(t, route.destination)));
    return;
  }
  if (m_opener && std::fabs(t - m_opener->t) <= kOpenerValidMs)
    SayLine(t, Category::Bossy, m_opener->key, m_opener->vars);
  m_opener.reset();
  // Counted when the trip starts, so a trip that never reaches "arrived" (ended early, app closed)
  // still counts: the first-trip greeting and speed run happen once ever, and the rundown styles
  // keep rotating.
  size_t const tripsBefore =
      std::max(m_history.size(), static_cast<size_t>(std::max(0, m_said.Counter(kTripsStartedKey))));
  m_said.SetCounter(kTripsStartedKey, static_cast<int>(tripsBefore + 1));
  m_tripsBeforeThis = static_cast<int>(tripsBefore);
  Trace("trip.number", {{"before", std::to_string(tripsBefore)}});
  // The first turn is right there: the direction goes first, then the greeting, and the rundown
  // (after the first trip ever) is left out so the final call isn't stuck behind it.
  bool const turnFirst = route.turnId && route.distToTurnM < kFirstTurnFirstWithinM;
  if (turnFirst)
  {
    m_turnId = route.turnId;
    m_turnKind = route.turn;
    SayInstruction(t, route);
  }
  // The app starts on walking: a walk of more than a couple of miles gets "Are you actually intending to walk there?".
  TravelMode const base = BaseMode(m_travelMode);
  std::string const lastMode = m_said.Value(kLastModeKey);
  m_said.SetValue(kLastModeKey, TravelModeName(base));
  double const lengthM = route.routeLengthM > 0 ? route.routeLengthM : route.etaSeconds * kAskWalkPaceMps;
  bool const askWalk = m_canAskMode && base == TravelMode::Walk && lengthM > kAskWalkOverM;
  m_modeAskOpen = false;
  if (askWalk)
  {
    // Asked out loud and on screen (Yes / No); the answer is AnswerModeAsk() or a spoken yes or no.
    SayLine(t, Category::Instruction, "mode.ask.walk", {}, Priority::Nav);
    m_modeAskOpen = true;
    m_modeAskShow = true;
    Trace("trip.modeAsk", {{"answer", "asked"}});
  }
  std::string greeting = TripGreeting(m_history, m_config.rng, tripsBefore);
  // The greetings are written for a car; a walk gets a hello of its own.
  if (!LineFitsMode(greeting, m_travelMode))
    greeting = Reply(m_travelMode == TravelMode::OffRoad ? std::string("mode.offRoad")
                                                         : std::string("mode.") + TravelModeName(m_travelMode));
  // Asking takes the greeting's place, and the rundown and prediction wait for the real trip.
  if (!askWalk)
    SayNav(t, greeting);
  // The driver tapped Driving after "No": "check it now and then, I may swap it on you."
  if (m_modeWarnNextDrive && base != TravelMode::Walk)
  {
    m_modeWarnNextDrive = false;
    SayLine(t, Category::Instruction, "mode.ask.watch");
  }
  // Walking last time, driving now: "Oh, so now we're driving?"
  if (!askWalk && !lastMode.empty() && lastMode != TravelModeName(base))
  {
    char const * const from = lastMode == "walk" ? "walking" : lastMode == "bike" ? "biking" : "driving";
    SayLine(t, Category::Instruction, std::string("mode.switch.") + TravelModeName(base), {{"from", from}});
  }
  SayIndoorStart(t, route);
  // Before 5 am: "Are we off to make the donuts?"
  if (KnowsTheDate(t) && LocalTime(t).tm_hour < 5)
    SayLine(t, Category::Holiday, "early.opener");
  // Already low when the trip starts? Say so after the greeting.
  if (m_batteryPercent && m_charging)
    Say(m_engine->OnNav(NavEvent::Battery(t, *m_batteryPercent / 100.0, *m_charging)));
  std::vector<RouteStep> steps;
  for (auto const k : route.plannedTurns)
    steps.push_back({InstructionText(k, "", ""), StepKindOf(k)});
  m_sketched = route.sketched;
  m_sketchMinutesLost = 0;
  if (m_sketched && route.fastestSeconds > 0 && route.etaSeconds > 0)
  {
    auto const verdict = JudgeSketch(route.fastestSeconds / 60, route.etaSeconds / 60);
    if (verdict.key == "sketch.slower")
      m_sketchMinutesLost = verdict.minutes;
  }
  // The first trip's rundown is a speed run: every turn, rattled off, then a request for music.
  // After that, a drawn route gets "We're doing your route" instead of the rundown.
  if (askWalk)
  {
  }
  else if (m_sketched && tripsBefore > 0)
    SayLine(t, Category::Instruction, "sketch.start");
  else if (!turnFirst || tripsBefore == 0)
  {
    std::string const rundown = RouteRundown(steps, m_history, m_config.rng, tripsBefore);
    if (LineFitsMode(rundown, m_travelMode))
      SayNav(t, rundown, tripsBefore == 0 ? kRattleSpeed : 1.0);
  }
  // The time prediction waits until the car is rolling, so the start isn't five lines in a row.
  if (m_plan.expectedMinutes > 0 && !askWalk)
  {
    m_predictionText = PredictTrip(m_history, m_plan, m_config.rng, &m_said).text;
    if (LineFitsMode(m_predictionText, m_travelMode))
      SayNavLater(t + kPredictionDelayMs, m_predictionText);
    else
      m_predictionText.clear();
  }
  Say(m_engine->OnNav(NavEvent::DestinationSet(t, route.destination)));
  SayVenue(t, route);
  SayRoutine(t, route);
  SayHoliday(t);
}

void Navigator::TrackLongDrive(double tMs, RouteSnapshot const & route)
{
  if (m_plan.expectedMinutes < kLongDriveMinMinutes || m_longDriveSaid >= std::size(kLongDriveRemarks) ||
      route.state != RouteState::OnRoute || (route.turnId && route.distToTurnM < kLongDriveQuietM))
    return;
  auto const & next = kLongDriveRemarks[m_longDriveSaid];
  if (tMs - m_tripStartT < next.atMinutes * 60000)
    return;
  ++m_longDriveSaid;
  int const hours = static_cast<int>(std::lround(m_plan.expectedMinutes / 60));
  SayLine(tMs, Category::LongDrive, next.key, {{"hours", std::to_string(hours) + " hour"}});
}

void Navigator::SayIndoorStart(double t, RouteSnapshot const & route)
{
  m_indoorOutKey.clear();
  if (!m_buildingLookup)
    return;
  // Where the car is now; the route's origin if there's been no fix lately.
  bool const fresh = m_hereFix && t - m_hereFix->tMs <= kIndoorFixFreshMs;
  LatLon const here = fresh ? LatLon{m_hereFix->lat, m_hereFix->lon} : route.origin;
  if (!here.lat && !here.lon)
    return;
  auto const home = GuessPlace(m_history, KnownPlace::Home);
  bool const atHome = home && DistanceM(*home, here) < kIndoorHomeM;
  auto const verdict = JudgeIndoorStart(m_buildingLookup(here), fresh ? m_hereFix->accuracyM : -1,
                                        fresh ? m_hereFix->speedMps : -1, atHome);
  // Nobody living on the streets needs reminding to open their garage door.
  if (verdict == IndoorStart::No || (verdict == IndoorStart::Garage && m_housing == Housing::Streets))
    return;
  std::string const key = verdict == IndoorStart::Garage ? "indoors.garage" : "indoors.building";
  // The first time always; after that, now and then, so leaving home stays a surprise.
  double const r = m_config.rng ? m_config.rng() : 0;
  bool const say = !m_said.Said(key) || r < kIndoorRepeatChance;
  Trace("indoors", {{"kind", key}, {"home", atHome ? "yes" : "no"}, {"said", say ? "yes" : "no"}});
  if (!say)
    return;
  SayLine(t, Category::LongStop, key);
  double const r2 = m_config.rng ? m_config.rng() : 0;
  if (r2 < kIndoorOutChance)
    m_indoorOutKey = key + ".out";
}

void Navigator::TrackLeftBuilding(Fix const & fix)
{
  if (m_indoorOutKey.empty())
    return;
  if (fix.tMs - m_tripStartT > kIndoorOutWithinMs)
  {
    m_indoorOutKey.clear();
    return;
  }
  if (fix.speedMps < MovingMps())
    return;
  SayLine(fix.tMs, Category::LongStop, m_indoorOutKey);
  m_indoorOutKey.clear();
}

std::string CompassDirection(LatLon const & from, LatLon const & to)
{
  double constexpr kRad = M_PI / 180;
  double const east = (to.lon - from.lon) * std::cos(from.lat * kRad);
  double const north = to.lat - from.lat;
  double const bearing = std::fmod(std::atan2(east, north) / kRad + 360, 360);
  static char const * const kWords[] = {"north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest"};
  return kWords[static_cast<int>(bearing / 45 + 0.5) % 8];
}

void Navigator::SetTravelMode(TravelMode mode)
{
  // The same route type again: an off-road drive or a hike found along the way stays one.
  if (m_modeFound && mode == BaseMode(m_travelMode))
    return;
  m_modeFound = false;
  ChangeTravelMode(mode, "route");
}

void Navigator::DriverSetTravelMode(TravelMode mode)
{
  m_modeFound = false;
  ChangeTravelMode(mode, "driver");
  // The driver said it: no need to ask, or to remark on the switch when the route comes back.
  m_said.SetValue(kLastModeKey, TravelModeName(BaseMode(mode)));
  if (m_modeAskOpen)
  {
    m_modeAskOpen = false;
    if (BaseMode(mode) != TravelMode::Walk)
      m_said.SetCounter(kTripsStartedKey, m_tripsBeforeThis);
  }
}

bool Navigator::AnswerModeAsk(bool walking)
{
  if (!m_modeAskOpen)
    return false;
  m_modeAskOpen = false;
  Trace("trip.modeAsk", {{"answer", walking ? "walk" : "drive"}});
  if (walking)
  {
    SayLine(m_lastT, Category::Instruction, "mode.ask.walk.yes", {}, Priority::Nav);
    return true;
  }
  // Already doing car speed: "click the driving button" would be a touch prompt in a moving car, so
  // the moving reply says to switch it once parked. Driving is still highlighted on screen.
  bool const moving = m_knownSpeedMps && m_lastT - m_knownSpeedT <= kUnknownSpeedCarryMs &&
                      *m_knownSpeedMps > kAskWalkMovingMps;
  SayLine(m_lastT, Category::Instruction, moving ? "mode.ask.walk.no.moving" : "mode.ask.walk.no", {},
          Priority::Nav);
  // The GPS won't do it for them: "I'm going to need you to click the driving button." The host
  // shows the route types with Driving highlighted. The drive that follows is this trip still, so
  // it gets the first trip's rundown, and no "so now we're driving?".
  m_said.SetCounter(kTripsStartedKey, m_tripsBeforeThis);
  m_said.SetValue(kLastModeKey, TravelModeName(TravelMode::Drive));
  m_modeSwitch = TravelMode::Drive;
  m_modeWarnNextDrive = true;
  return true;
}

void Navigator::ChangeTravelMode(TravelMode mode, char const * why)
{
  if (mode != m_travelMode)
    Trace("trip.mode", {{"mode", TravelModeName(mode)}, {"from", TravelModeName(m_travelMode)}, {"why", why}});
  m_travelMode = mode;
  SetLineTravelMode(mode);
}

double Navigator::MovingMps() const
{
  switch (m_travelMode)
  {
  case TravelMode::Walk:
  case TravelMode::Hike: return 1;
  case TravelMode::Bike: return 2.5;
  case TravelMode::Drive:
  case TravelMode::OffRoad: return kResumedMps;
  }
  return kResumedMps;
}

void Navigator::TrackTravelMode(Fix const & fix, RouteSnapshot const & route)
{
  if (route.state != RouteState::OnRoute && route.state != RouteState::OffRoute)
    return;
  bool const moving = fix.speedMps >= MovingMps();
  if (InCar(m_travelMode))
  {
    if (!moving)
      return;
    // Matched to a dirt track off a fix 100 m out is a guess, not a trail.
    if (RoughFix(fix))
    {
      m_dirtSince.reset();
      return;
    }
    if (route.onDirt)
    {
      m_pavedSince.reset();
      if (!m_dirtSince)
        m_dirtSince = fix.tMs;
      if (m_travelMode == TravelMode::Drive && fix.tMs - *m_dirtSince >= kOffRoadAfterDirtMs)
      {
        ChangeTravelMode(TravelMode::OffRoad, "dirt");
        m_modeFound = true;
        SayLine(fix.tMs, Category::Rerouted, "offroad.enter");
      }
      return;
    }
    m_dirtSince.reset();
    // Off the route out where the map has no road isn't pavement either.
    if (route.state != RouteState::OnRoute)
    {
      m_pavedSince.reset();
      return;
    }
    if (!m_pavedSince)
      m_pavedSince = fix.tMs;
    // Only an off-road trip the GPS noticed itself ends by itself; the driver's say-so stands.
    if (m_travelMode == TravelMode::OffRoad && m_modeFound && fix.tMs - *m_pavedSince >= kBackOnPavementMs)
    {
      ChangeTravelMode(TravelMode::Drive, "pavement");
      SayLine(fix.tMs, Category::Rerouted, "offroad.exit");
    }
    return;
  }
  // A walk on trails for a while is a hike, for the rest of the trip; a ride, mountain biking.
  bool const looking = m_travelMode == TravelMode::Walk || (m_travelMode == TravelMode::Bike && !m_mountainBike);
  if (!looking || !moving || fix.tMs - m_lastTrailCheckT < kTrailCheckEveryMs)
    return;
  m_lastTrailCheckT = fix.tMs;
  bool const trail = route.onDirt || (m_trailLookup && m_trailLookup({fix.lat, fix.lon}));
  if (!trail)
  {
    m_trailSince.reset();
    return;
  }
  if (!m_trailSince)
    m_trailSince = fix.tMs;
  if (fix.tMs - *m_trailSince >= kHikeAfterMs && m_travelMode == TravelMode::Bike)
  {
    m_mountainBike = true;
    Trace("trip.mountainBike", {{"why", "trail"}});
    return;
  }
  if (fix.tMs - *m_trailSince >= kHikeAfterMs)
  {
    ChangeTravelMode(TravelMode::Hike, "trail");
    m_modeFound = true;
    SayLine(fix.tMs, Category::Rerouted, "hike.enter");
  }
}

void Navigator::QueueOpener(double tMs, std::string key, Vars vars)
{
  m_opener = Opener{tMs, std::move(key), std::move(vars)};
}

namespace
{
/// Where `p` is from the car, in meters along its heading (ahead > 0) and across it (right > 0).
std::pair<double, double> AheadAndSide(Fix const & fix, LatLon const & p)
{
  double const rad = M_PI / 180;
  double const east = (p.lon - fix.lon) * rad * 6371000 * std::cos(fix.lat * rad);
  double const north = (p.lat - fix.lat) * rad * 6371000;
  double const b = fix.bearingDeg * rad;
  return {east * std::sin(b) + north * std::cos(b), east * std::cos(b) - north * std::sin(b)};
}
}  // namespace

void Navigator::TrackSights(Fix const & fix, RouteSnapshot const & route)
{
  bool const quiet = route.state == RouteState::OnRoute && fix.speedMps >= MovingMps() && fix.bearingDeg >= 0 &&
                     !(route.turnId && route.distToTurnM < kSightQuietM);
  // Something spotted up ahead: say it as the car is about to pass it, so it's actually in view.
  if (m_upcomingSight)
  {
    auto const & up = *m_upcomingSight;
    auto const [ahead, side] = fix.bearingDeg >= 0 ? AheadAndSide(fix, *up.sight.at) : std::pair{0.0, 0.0};
    double const sayWithinM = std::clamp(fix.speedMps * kSightLeadS, kSightSayMinM, kSightSayMaxM);
    if (!quiet || ahead < kSightPassedM || std::fabs(side) > kSightMaxSideM || fix.tMs - up.spottedT > kSightGiveUpMs)
    {
      Trace("sight.dropped", {{"name", up.sight.name}, {"ahead_m", LogNum(ahead, 0)}, {"side_m", LogNum(side, 0)}});
      m_upcomingSight.reset();
    }
    else if (ahead <= sayWithinM)
    {
      Sight sight = up.sight;
      sight.onLeft = side < 0;
      m_upcomingSight.reset();
      SaySight(fix.tMs, sight);
    }
    return;
  }
  if (!m_sightLookup || !quiet || fix.tMs - m_lastSightCheckT < kSightCheckEveryMs ||
      fix.tMs - m_lastSightT < kSightEveryMs || fix.tMs - m_tripStartT < 60000)
    return;
  m_lastSightCheckT = fix.tMs;
  auto const sight = m_sightLookup({fix.lat, fix.lon}, fix.bearingDeg);
  if (!sight || std::find(m_sightsSaid.begin(), m_sightsSaid.end(), sight->name) != m_sightsSaid.end())
    return;
  if (!sight->at)
  {
    SaySight(fix.tMs, *sight);
    return;
  }
  auto const [ahead, side] = AheadAndSide(fix, *sight->at);
  Trace("sight.spotted", {{"name", sight->name}, {"ahead_m", LogNum(ahead, 0)}, {"side_m", LogNum(side, 0)}});
  // Behind us, or off down a side street: not something the driver can look at.
  if (ahead < 0 || ahead > kSightMaxAheadM || std::fabs(side) > kSightMaxSideM)
    return;
  m_upcomingSight = UpcomingSight{*sight, fix.tMs};
}

void Navigator::SaySight(double tMs, Sight const & sight)
{
  m_sightsSaid.push_back(sight.name);
  m_lastSightT = tMs;
  std::string const kindKey = "tour." + sight.kind;
  std::string const key = !sight.kind.empty() && LineData().count(kindKey) ? kindKey : "tour";
  SayLine(tMs, Category::Tour, key, {{"side", sight.onLeft ? "left" : "right"}, {"sight", sight.name}});
}

/// A long silence on an easy stretch: the GPS can't help itself. Every two and a half to five
/// minutes of nothing, never near a maneuver, in the first minutes, or while crawling.
void Navigator::TrackIdle(Fix const & fix, RouteSnapshot const & route)
{
  if (route.state != RouteState::OnRoute || fix.speedMps < MovingMps() || fix.tMs - m_tripStartT < 120000 ||
      (route.turnId && route.distToTurnM < kIdleQuietM) || m_upcomingSight)
    return;
  if (m_idleGapMs <= 0)
    m_idleGapMs = kIdleMinMs + (m_config.rng ? m_config.rng() : 0.5) * (kIdleMaxMs - kIdleMinMs);
  double const quietSince = std::max({m_lastSpokeT, m_tripStartT, m_idleSkippedT});
  if (fix.tMs - quietSince < m_idleGapMs || RemarkWaiting())
    return;
  m_idleGapMs = 0;
  // A familiar commute lets some of these silences be.
  if (FamiliarQuiet() && (m_config.rng ? m_config.rng() : 0) >= kFamiliarTalkShare)
  {
    m_idleSkippedT = fix.tMs;
    Trace("say.dropped", {{"key", "idle"}, {"why", "familiar"}});
    return;
  }
  m_lastSpokeT = fix.tMs;
  SayLine(fix.tMs, Category::Idle, "idle");
}

void Navigator::TrackWrongWay(Fix const & fix, RouteSnapshot const & route)
{
  bool const followingRoute = route.state == RouteState::OnRoute || route.state == RouteState::NotStarted;
  double off = 0;
  if (followingRoute && !RoughFix(fix) && fix.bearingDeg >= 0 && route.routeBearingDeg >= 0 &&
      fix.speedMps >= std::min(kWrongWayMinMps, 0.75 * MovingMps()))
  {
    off = std::fabs(std::fmod(fix.bearingDeg - route.routeBearingDeg + 540.0, 360.0) - 180.0);
  }
  if (off < kWrongWayDeg)
  {
    m_wrongWaySince = -1;
    return;
  }
  if (m_wrongWaySince < 0)
    m_wrongWaySince = fix.tMs;
  if (fix.tMs - m_wrongWaySince < kWrongWayHoldMs || fix.tMs - m_lastWrongWayT < kWrongWayEveryMs ||
      m_wrongWaySaid >= kWrongWayMaxPerTrip)
    return;
  SayLine(fix.tMs, Category::WrongWay, m_wrongWaySaid == 0 ? "wrongWay" : "wrongWay.again");
  ++m_wrongWaySaid;
  m_lastWrongWayT = fix.tMs;
  // Facing the wrong way already covers turning around.
  m_lastUTurnT = fix.tMs;
}

void Navigator::TrackUTurn(Fix const & fix, RouteSnapshot const & route)
{
  if (fix.bearingDeg < 0 || fix.speedMps < kUTurnMinMps || RoughFix(fix))
    return;
  while (!m_headings.empty() && fix.tMs - m_headings.front().first > kUTurnMaxAgoMs)
    m_headings.pop_front();
  m_headings.emplace_back(fix.tMs, fix.bearingDeg);
  // Going around a roundabout isn't turning back.
  if (route.turn == TurnKind::Roundabout && route.distToTurnM < 150)
    return;
  if (m_uTurnsSaid >= kUTurnMaxPerTrip || fix.tMs - m_lastUTurnT < kUTurnEveryMs)
    return;
  for (auto const & [t, deg] : m_headings)
  {
    if (fix.tMs - t < kUTurnMinAgoMs)
      break;
    if (std::fabs(std::fmod(fix.bearingDeg - deg + 540.0, 360.0) - 180.0) >= kUTurnDeg)
    {
      // The route itself turned back (an instructed U-turn, a hairpin switchback): the
      // driver did what they were told, so there's nothing to mock. Consume the reversal.
      if (route.state == RouteState::OnRoute && route.routeBearingDeg >= 0 &&
          std::fabs(std::fmod(fix.bearingDeg - route.routeBearingDeg + 540.0, 360.0) - 180.0) <= kUTurnOnRouteDeg)
      {
        Trace("say.dropped", {{"key", "uturn"}, {"why", "route"}});
        m_headings.clear();
        return;
      }
      SayLine(fix.tMs, Category::UTurn, "uturn");
      ++m_uTurnsSaid;
      m_lastUTurnT = fix.tMs;
      m_headings.clear();
      return;
    }
  }
}

void Navigator::TrackCurveAhead(Fix const & fix, RouteSnapshot const & route)
{
  if (route.state != RouteState::OnRoute || route.curveAheadM < kCurveAheadMinM || route.curveAheadM > kCurveAheadMaxM ||
      fix.speedMps < kScaryCurveMinMps || fix.tMs - m_lastCurveAheadT < kCurveAheadEveryMs)
    return;
  // A bend at the next maneuver is the maneuver.
  if (route.turnId && std::fabs(route.curveAheadM - route.distToTurnM) < 80)
    return;
  m_lastCurveAheadT = fix.tMs;
  SayLine(fix.tMs, Category::Fear, "curve.ahead");
}

void Navigator::TrackSlowStart(Fix const & fix)
{
  if (m_startedRolling)
    return;
  if (fix.speedMps >= 0.75 * MovingMps())
  {
    m_startedRolling = true;
    return;
  }
  if (fix.tMs - m_tripStartT < kSlowStartMs)
    return;
  m_startedRolling = true;
  SayLine(fix.tMs, Category::LongStop, "start.slow");
}

namespace
{
/// "Watch your step. It looks like we are on an incline." A 10% grade is a proper hill on foot.
double constexpr kInclineGrade = 0.10;
/// No heart rate to read, so effort is a jog (on foot) or a climb (on foot or a bike) kept up this long.
double constexpr kJogMps = 2.3;
double constexpr kClimbGrade = 0.06;
double constexpr kEffortForMs = 90000;
/// Nothing outdoorsy in the first minute: the trip opener has the floor.
double constexpr kOutdoorsAfterStartMs = 60000;
/// One-liners skip this many trips after they're said.
int constexpr kOutdoorRestTrips = 3;
int constexpr kOutdoorBitsPerTrip = 2;
double constexpr kOutdoorBitChance = 0.4;
}  // namespace

bool Navigator::Rested(std::string const & key) const
{
  std::string const at = m_said.Value("_rest." + key);
  return at.empty() || m_said.Counter(kTripsStartedKey) - std::atoi(at.c_str()) >= kOutdoorRestTrips;
}

void Navigator::MarkSaid(std::string const & key) { m_said.SetCounter("_rest." + key, m_said.Counter(kTripsStartedKey)); }

bool Navigator::InNature() const
{
  return m_travelMode == TravelMode::Hike || (m_travelMode == TravelMode::Bike && m_mountainBike);
}

void Navigator::TrackOutdoors(Fix const & fix, RouteSnapshot const & route)
{
  if (fix.speedMps < 0 || route.state != RouteState::OnRoute || fix.tMs - m_tripStartT < kOutdoorsAfterStartMs)
    return;
  bool const onFoot = m_travelMode == TravelMode::Walk || m_travelMode == TravelMode::Hike;
  bool const moving = fix.speedMps >= MovingMps();
  double const grade = route.gradeAhead.value_or(0);
  if (onFoot && moving && grade >= kInclineGrade && !m_inclineSaid && Rested("outdoors.incline"))
  {
    m_inclineSaid = true;
    MarkSaid("outdoors.incline");
    Trace("outdoors", {{"bit", "incline"}});
    SayLine(fix.tMs, Category::Quiet, "outdoors.incline");
  }
  // "It looks like your heart rate is elevated." There's no heart rate, so: a jog or a long climb.
  bool const effort = (onFoot && fix.speedMps >= kJogMps) || (moving && grade >= kClimbGrade);
  if (!effort)
  {
    m_effortSince.reset();
    return;
  }
  if (!m_effortSince)
    m_effortSince = fix.tMs;
  if (m_effortSaid || fix.tMs - *m_effortSince < kEffortForMs || !Rested("outdoors.effort"))
    return;
  m_effortSaid = true;
  MarkSaid("outdoors.effort");
  Trace("outdoors", {{"bit", "effort"}});
  SayLine(fix.tMs, Category::Quiet, "outdoors.effort");
}

std::string Navigator::OutdoorBit(double r) const
{
  if (InCar(m_travelMode) || static_cast<int>(m_outdoorBitsSaid.size()) >= kOutdoorBitsPerTrip)
    return {};
  std::vector<std::string> keys;
  auto const add = [&](char const * key, bool rests) {
    if (!m_outdoorBitsSaid.count(key) && (!rests || Rested(key)))
      keys.push_back(key);
  };
  if (InNature())
  {
    add("outdoors.aside", false);
    add("outdoors.ad", true);
  }
  if (m_travelMode == TravelMode::Bike && m_mountainBike)
    add("outdoors.armstrong", true);
  add("outdoors.oppressed", true);
  if (keys.empty())
    return {};
  return keys[std::min(keys.size() - 1, static_cast<size_t>(r * keys.size()))];
}

void Navigator::SayOutdoorBit(double t, std::string const & key)
{
  m_outdoorBitsSaid.insert(key);
  if (key != "outdoors.aside")
    MarkSaid(key);
  Trace("quiet", {{"bit", key}});
  if (key != "outdoors.oppressed")
  {
    SayLine(t, Category::Quiet, key);
    return;
  }
  // "Help! help! I'm being oppressed!" in someone else's voice (a woman's), then the GPS's own.
  double const r = m_config.rng ? m_config.rng() : 0;
  Commentary c;
  c.t = t;
  c.category = Category::Quiet;
  c.text = m_said.Pick(key, LinesFor(key, Tier()), r);
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Roast;
  c.voiceSpeed = 1 + c.frustration / 1000.0;
  c.key = key;
  PushLine(std::move(c), m_gpsName == "Sarah" ? "af_heart" : "af_sarah");
}

bool Navigator::FamiliarQuiet() const { return m_familiarTrip && m_talk == TalkLevel::Full && m_reroutes == 0; }

void Navigator::NotFamiliarAnymore(char const * why)
{
  if (!m_familiarTrip)
    return;
  m_familiarTrip = false;
  Trace("talk.familiar", {{"off", why}});
}

std::vector<Routine> Navigator::Routines() const
{
  return LearnRoutines(m_history, [this](double ms) { return LocalTime(ms); });
}

std::string Navigator::PlaceName(std::optional<LatLon> const & at, std::string const & name) const
{
  if (at)
  {
    for (auto const place : {KnownPlace::Work, KnownPlace::Home})
      if (auto const known = GuessPlace(m_history, place); known && DistanceM(*known, *at) < kNearDestinationM)
        return place == KnownPlace::Work ? "work" : "home";
  }
  return name.empty() ? "the usual place" : name;
}

bool Navigator::SayRoutineBreak(double t, RouteSnapshot const & route)
{
  if (!KnowsTheDate(t))
    return false;
  std::tm const now = LocalTime(t);
  auto const broken = CheckRoutine(Routines(), RouteKey(route.origin, route.destinationPoint), now);
  if (!broken)
    return false;
  static char const * const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  int const h = now.tm_hour;
  Vars vars = {{"usual", PlaceName(broken->routine.to, broken->routine.destination)},
               {"place", PlaceName(route.destinationPoint, route.destination)},
               {"day", kDays[now.tm_wday]},
               {"daypart", h < 12 ? "morning" : h < 17 ? "afternoon" : h < 21 ? "evening" : "night"},
               {"guy", m_pronouns == Pronouns::He ? "guy" : m_pronouns == Pronouns::She ? "girl" : "person"}};
  switch (broken->kind)
  {
  case RoutineBreak::Kind::Elsewhere:
    // Skipping work gets Brandon's "I thought it was Tuesday?" interrogation.
    SayLine(t, Category::Routine, vars["usual"] == "work" ? "routine.skipWork" : "routine.elsewhere", vars);
    break;
  case RoutineBreak::Kind::OffDay: SayLine(t, Category::Routine, "routine.offDay", vars); break;
  case RoutineBreak::Kind::Late:
  {
    int const m = broken->minutesLate;
    vars["late"] = m < 55 ? std::to_string((m + 2) / 5 * 5) + " minutes" : m < 90 ? "an hour" : std::to_string((m + 30) / 60) + " hours";
    SayLine(t, Category::Routine, "routine.late", vars);
    break;
  }
  }
  return true;
}

void Navigator::SayRoutine(double t, RouteSnapshot const & route)
{
  // Breaking the routine beats congratulating it.
  if (SayRoutineBreak(t, route))
    return;
  // Work trips this year: every fifth one from the tenth on gets the count.
  auto const yearOf = [](double ms)
  {
    std::time_t const s = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    gmtime_r(&s, &tm);
    return tm.tm_year;
  };
  auto const work = GuessPlace(m_history, KnownPlace::Work);
  bool const toWork = work && (route.destinationPoint.lat || route.destinationPoint.lon) &&
                      DistanceM(*work, route.destinationPoint) < kNearDestinationM;
  if (toWork)
  {
    int count = 1;  // this one
    for (auto const & r : m_history)
      if (r.to && DistanceM(*r.to, *work) < kNearDestinationM && yearOf(r.endedAt) == yearOf(t))
        ++count;
    if (count >= 10 && count % 5 == 0)
    {
      SayLine(t, Category::Routine, "routine.work", {{"count", Ordinal(count)}});
      return;
    }
  }
  // Any route driven a lot: now and then.
  if (m_tripsOnRoute >= 5 && (m_tripsOnRoute + 1) % 7 == 0)
  {
    SayLine(t, Category::Routine, "routine.same");
    return;
  }
  // The same trip as yesterday (Brandon: "Didn't we just take this trip yesterday?").
  std::string const routeKey = RouteKey(m_plan.origin, m_plan.destination);
  bool const yesterday = std::any_of(m_history.begin(), m_history.end(), [&](TripRecord const & r) {
    double const agoH = (t - r.endedAt) / 3.6e6;
    return r.routeKey == routeKey && agoH >= kYesterdayMinH && agoH <= kYesterdayMaxH;
  });
  if (yesterday && (!m_said.Said("routine.yesterday") || (m_config.rng ? m_config.rng() : 0) < kYesterdayChance))
    SayLine(t, Category::Routine, "routine.yesterday", DriverVars());
}

void Navigator::TrackMusic(double tMs, RouteSnapshot const & route)
{
  if (!m_musicLookup || route.state != RouteState::OnRoute || tMs - m_lastMusicCheckT < kMusicCheckEveryMs)
    return;
  m_lastMusicCheckT = tMs;
  bool const playing = m_musicLookup();
  if (playing && !m_musicPlaying)
    m_musicStartedT = tMs;
  m_musicPlaying = playing;
  // Near a turn, or another remark just went out (the car pulling away after a stop often has
  // two waiting): ask at the next check instead (BI-046).
  bool const quiet = !route.turnId || route.distToTurnM >= kLongDriveQuietM;
  if (!quiet || RemarkJustSaid(tMs))
    return;
  // Music's on: give it a song's worth of a chance, then judge it. Once a trip.
  if (playing && !m_musicJudged && tMs - m_musicStartedT >= kMusicJudgeAfterMs)
  {
    m_musicJudged = true;
    SayLine(tMs, Category::Music, "music.judge");
  }
  // Silence (well, just the GPS) on a long trip: ask for music.
  if (!playing && !m_musicAsked && tMs - m_tripStartT >= kMusicAfterMs)
  {
    m_musicAsked = true;
    SayLine(tMs, Category::Music, "music");
  }
}

/// Timestamps from before 2001 aren't a wall clock (a replay or a test starting at zero).
bool Navigator::KnowsTheDate(double ms) const { return m_localTime || ms > 978307200000.0; }

std::tm Navigator::LocalTime(double ms) const
{
  if (m_localTime)
    return m_localTime(ms);
  std::time_t const s = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  localtime_r(&s, &tm);
  return tm;
}

void Navigator::SayHoliday(double t)
{
  if (!KnowsTheDate(t))
    return;
  std::tm const tm = LocalTime(t);
  int const month = tm.tm_mon + 1;
  int const day = tm.tm_mday;
  char const * key = nullptr;
  if (month == 10 && day == 31)
    key = "holiday.halloween";
  else if ((month == 1 && day == 1) || (month == 12 && day == 31 && tm.tm_hour >= 18))
    key = "holiday.newYear";
  else if (month == 2 && day == 14)
    key = "holiday.valentines";
  else if (m_country == "Canada")
  {
    if (month == 7 && day == 1)
      key = "holiday.canadaDay";
    else if (month == 10 && tm.tm_wday == 1 && day >= 8 && day <= 14)  // second Monday
      key = "holiday.thanksgiving";
  }
  else if (m_country.empty() || m_country == "US")
  {
    if (month == 7 && day == 4)
      key = "holiday.july4";
    else if (month == 11 && tm.tm_wday == 4 && day >= 22 && day <= 28)  // fourth Thursday
      key = "holiday.thanksgiving";
  }
  if (key)
    SayLine(t, Category::Holiday, key);
}

void Navigator::SayVenue(double t, RouteSnapshot const & route)
{
  if (!m_venueLookup)
    return;
  auto const isSet = [](LatLon const & p) { return p.lat || p.lon; };
  std::optional<Venue> const to = isSet(route.destinationPoint) ? m_venueLookup(route.destinationPoint) : std::nullopt;
  // Leaving the coffee shop empty-handed (as far as the GPS is concerned).
  if (to != Venue::Coffee && isSet(route.origin) && m_venueLookup(route.origin) == Venue::Coffee)
    SayLine(t, Category::Venue, "venue.coffeeForgot");
  if (!to)
    return;
  switch (*to)
  {
  case Venue::Coffee: SayLine(t, Category::Venue, "venue.coffee"); break;
  case Venue::ThemePark:
    if (m_batteryPercent)
      SayLine(t, Category::Venue, "venue.themePark", {{"battery", std::to_string(*m_batteryPercent) + " percent"}});
    else
      SayLine(t, Category::Venue, "venue.themeParkNoBattery");
    break;
  case Venue::Gym: SayLine(t, Category::Venue, "venue.gym"); break;
  case Venue::Airport: SayLine(t, Category::Venue, "venue.airport"); break;
  case Venue::Mall: SayLine(t, Category::Venue, "venue.mall"); break;
  case Venue::Zoo: SayLine(t, Category::Venue, "venue.zoo"); break;
  case Venue::Beach: SayLine(t, Category::Venue, "venue.beach"); break;
  case Venue::Cinema: SayLine(t, Category::Venue, "venue.cinema"); break;
  case Venue::Electronics: SayLine(t, Category::Venue, "venue.electronics"); break;
  }
}

void Navigator::ResetTrip()
{
  m_rerouteRoastDueAt.reset();
  m_lastInstructionText.clear();
  m_waterYelps = 0;
  m_lastHeadingT = -1e18;
  m_lastWaterYelpT = -1e18;
  m_tagsSaid.clear();
  m_turnHeld.clear();
  m_predictionText.clear();
  m_turnId.reset();
  m_turnKind = TurnKind::None;
  m_leftsInARow = 0;
  m_rerouting = false;
  m_missPending.reset();
  m_arrived = false;
  m_lastState = RouteState::Inactive;
  m_stopSince.reset();
  m_speeds.clear();
  m_inTraffic = false;
  m_heldPlateLines.clear();
  m_twenty.NewTrip();
  m_heldTwentyLines.clear();
  m_twentyCutInT.reset();
  m_lastFix.reset();
  m_knownSpeedMps.reset();
  m_pullOverSaid = false;
  m_policeSaid = false;
  m_lastBadSpeedingT.reset();
  m_badSpeedingSince.reset();
  m_onBridge = false;
  m_hardCurveSamples = 0;
  m_lastCurveAheadT = -1e18;
  m_curveGasSamples = 0;
  m_lastCurveGasT = -1e18;
  m_reroutes = 0;
  m_autopilotClaimed = false;
  m_lemonSaid = false;
  m_longDriveSaid = 0;
  m_titleDemanded = false;
  m_gameRefused = false;
  m_sightsSaid.clear();
  m_upcomingSight.reset();
  m_lastSpokeT = -1e18;
  m_idleGapMs = 0;
  m_idleSkippedT = -1e18;
  m_bits.NewTrip((m_config.rng ? m_config.rng() : 0) < kCleanBitChance, m_said.Counter(kTurnStreakKey));
  m_bendHeadings.clear();
  m_curveAheadM = -1;
  m_ferryAheadSaid = m_ferryAboardSaid = m_wasOnFerry = false;
  m_tollSaid = false;
  m_tollWindowT.reset();
  m_lastBendT = -1e18;
  m_bendJokes = 0;
  m_coldStage = 0;
  m_coldSaidT = 0;
  m_hotSaid = false;
  m_groceriesSaid = false;
  m_clockChecked = false;
  m_straightLeg.reset();
  m_straightNags = 0;
  m_longLeg.reset();
  m_chainLeg.reset();
  m_chainUsed = false;
  m_wrongWaySince = -1;
  m_lastWrongWayT = -1e18;
  m_wrongWaySaid = 0;
  m_headings.clear();
  m_lastUTurnT = -1e18;
  m_uTurnsSaid = 0;
  m_startedRolling = false;
  m_musicAsked = false;
  m_christmasSaid = false;
  m_roundaboutSaid = false;
  m_laneSaidFor.reset();
  m_sideSaidFor.reset();
  m_musicJudged = false;
  m_musicPlaying = false;
  m_lastSightT = -1e18;
  m_gamePitchAt.reset();
  m_lastTitleScoldT = -1e18;
  m_gate.Clear();
  m_punchlines.clear();
  m_call.End();
  m_callTaken = false;
  m_quiz.End();
  m_quizzes = 0;
  m_lastQuizT = -1e18;
  m_quizUnclear = 0;
  m_quizReasked = false;
  m_quizRolledFor.reset();
  m_quietBits = 0;
  m_lastQuietBitT = -1e18;
  m_resumedT = -1e18;
  m_quietRolledFor.reset();
  m_quietTurnId.reset();
  m_quietTurnSince = -1e18;
  m_lastCutInT = -1e18;
  m_road.Reset();
  m_lastRoadT = -1e18;
}

void Navigator::SetSaidLog(SaidLog log, std::function<void(SaidLog const &)> onChange)
{
  m_said = std::move(log);
  m_onSaid = std::move(onChange);
}

void Navigator::SetAchievements(Achievements achievements,
                                std::function<void(Achievements const &)> onChange)
{
  m_achievements = std::move(achievements);
  m_onAchievements = std::move(onChange);
}

void Navigator::CountLefts(double t)
{
  // Counted the same way as the engine's NASCAR line: rights break the streak, the rest don't count.
  switch (ManeuverOf(m_turnKind))
  {
  case Maneuver::Left:
    if (m_turnKind == TurnKind::ExitLeft)  // an exit on the left isn't a lap
      return;
    ++m_leftsInARow;
    break;
  case Maneuver::Right: m_leftsInARow = 0; return;
  case Maneuver::Other: return;
  }
  // Three lefts in a row: round and round, like a stock car. Unlocked once, ever.
  if (m_leftsInARow >= 3 && m_achievements.Unlock("nascar", t))
  {
    SayLine(t, Category::Achievement, "badge.nascar", {}, Priority::Nav);
    if (m_onAchievements)
      m_onAchievements(m_achievements);
    m_achievements.ClearDirty();
  }
}

void Navigator::SetTwentyLog(TwentyLog log, std::function<void(TwentyLog const &)> onChange)
{
  m_twenty.SetLog(std::move(log));
  m_onTwentyLog = std::move(onChange);
}

void Navigator::SetPlateLog(PlateLog log, std::function<void(PlateLog const &)> onChange)
{
  m_plates.SetLog(std::move(log));
  m_onPlateLog = std::move(onChange);
}

int Navigator::Tier() const
{
  int const f = m_engine ? m_engine->Frustration() : 0;
  return f < 34 ? 0 : f < 67 ? 1 : 2;
}

void Navigator::TrackPlateGame(double tMs, LatLon const & here, double speedMps, RouteSnapshot const & route)
{
  // Which state we're in barely changes; look it up every half minute.
  if (m_regionLookup && tMs - m_lastRegionCheckT >= 30000)
  {
    m_lastRegionCheckT = tMs;
    auto const name = m_regionLookup(here);
    m_plates.SetHere(name ? MatchPlateRegion(*name) : std::nullopt);
  }
  bool const nearManeuver = route.turnId && route.distToTurnM < GameHoldM(PlateGame::kHoldNearManeuverM, TimingMps(speedMps));
  // The game the GPS refused a few minutes ago is now its own idea.
  if (m_gamePitchAt && tMs >= *m_gamePitchAt && !nearManeuver && route.state == RouteState::OnRoute &&
      m_plates.GetState() != PlateGame::State::Playing)
  {
    m_gamePitchAt.reset();
    m_plates.MarkOffered();
    SayLine(tMs, Category::PlateGame, "game.myIdea");
  }
  if (!nearManeuver && !m_heldPlateLines.empty())
  {
    for (auto & line : m_heldPlateLines)
      m_gate.Push(std::move(line), false);
    m_heldPlateLines.clear();
  }
  // Offer once a trip, after the opening rundown has had its minute, on a long quiet stretch.
  if (route.state != RouteState::OnRoute || m_plates.OfferedThisTrip() || tMs - m_tripStartT < 60000 || !route.turnId)
    return;
  // US and Mexican plates name a state; a UK or German plate says nothing about where the car is from.
  if (!PlatesNameAState(m_country))
    return;
  if (PlateGame::ShouldOffer(m_tripsOnRoute, route.distToTurnM, m_engine->Frustration()))
    SayPlate(tMs, m_plates.Offer(Tier()));
}

Bridge ClassifyBridge(bool water, bool rail, bool road, bool valley)
{
  if (water)
    return Bridge::Water;
  if (rail)
    return Bridge::Rail;
  if (road)
    return Bridge::Road;
  return valley ? Bridge::Valley : Bridge::Unknown;
}

char const * BridgeLinesKey(Bridge bridge)
{
  switch (bridge)
  {
  case Bridge::Water: return "fear.bridgeWater";
  case Bridge::Rail: return "fear.bridgeRail";
  case Bridge::Road: return "fear.bridgeRoad";
  case Bridge::Valley: return "fear.bridgeValley";
  case Bridge::None:
  case Bridge::Unknown: break;
  }
  return "fear.bridge";
}

void Navigator::OnDriverSaid(double tMs, std::string const & text)
{
  Trace("heard", {{"text", text}, {"parked", m_engine && !m_arrived && Parked() ? "yes" : "no"}});
  if (!m_engine || m_arrived)
    return;
  m_lastT = std::max(m_lastT, tMs);
  // "Avoid tolls": the host reroutes; a toll stretch after that is no joke (BI-113).
  for (auto const & command : ParseAppCommands(text))
    if (command.action == AppAction::AvoidTolls || command.action == AppAction::AllowTolls)
      m_avoidTollsSaid = command.action == AppAction::AvoidTolls;
  // A pop quiz is waiting: whatever was said is the answer (or a try at one).
  if (QuizAsking())
  {
    QuizHeard const heard = ParseQuizAnswer(*m_quiz.Question(), text);
    Trace("quiz", {{"event", "heard"}, {"kind", std::to_string(static_cast<int>(heard.kind))},
                   {"choice", std::to_string(heard.choice)}});
    std::optional<PopQuiz::Outcome> outcome;
    if (heard.kind == QuizHeard::Kind::Choice)
      outcome = m_quiz.Answer(heard.choice, m_lastT);
    else if (heard.kind == QuizHeard::Kind::GiveUp)
      outcome = m_quiz.GiveUp(m_lastT);
    if (outcome)
      SayQuizOutcome(m_lastT, *outcome);
    else if (m_quizUnclear++ == 0)
      m_gate.Push(QuizLine(m_lastT, "quiz.unclear"), false);
    return;
  }
  // "Quiz me": the driver asked for it. Brown noser.
  if (m_quizOn && !QuietBitBusy() && !m_call.Active() && AsksForPopQuiz(text) &&
      m_lastT - m_lastQuizT >= kQuizAskedEveryMs)
  {
    StartPopQuiz(m_lastT, true);
    return;
  }
  // Mid-game, it's a question: "is it a police car?" isn't the police, and the title can wait.
  if (m_twenty.Active())
    if (auto const reply = Twenty().OnDriverSaid(text, m_lastT, Tier()))
    {
      m_twentyCutInT.reset();
      SayTwenty(m_lastT, *reply);
      return;
    }
  // "Are you actually intending to walk there?" can be answered out loud too.
  if (m_modeAskOpen)
    if (auto const walking = WalkAskAnswer(text))
    {
      AnswerModeAsk(*walking);
      return;
    }
  // "We're getting pulled over": the GPS knows exactly whose fault that is.
  std::string words = " ";
  for (unsigned char const ch : text)
    words += std::isalnum(ch) ? static_cast<char>(std::tolower(ch)) : ' ';
  words += ' ';
  // Since being fed up, the GPS answers only to its title.
  if (m_titleDemanded)
  {
    bool const titled = words.find(" omnipotent ") != std::string::npos || words.find(" overseer ") != std::string::npos;
    if (!titled)
    {
      if (m_lastT - m_lastTitleScoldT >= kTitleScoldEveryMs)
      {
        m_lastTitleScoldT = m_lastT;
        SayLine(m_lastT, Category::Title, "title.missing");
      }
      return;
    }
    SayLine(m_lastT, Category::Title, "title.used");
  }
  // "Let's play 20 questions": asked by name, so no "not now" gag; straight to the first question.
  if (!m_twenty.Active() && AsksForTwentyQuestions(text))
  {
    LatLon const here = m_lastFix ? LatLon{m_lastFix->lat, m_lastFix->lon} : m_plan.origin;
    auto const nearby = m_nearbyThings ? m_nearbyThings(here) : std::set<std::string>{};
    auto const reply = Twenty().Start(nearby, Night(m_lastT, here), m_lastT, Tier());
    Trace("twenty", {{"event", "asked"}, {"thing", m_twenty.Thing()->name}, {"nearby", std::to_string(nearby.size())}});
    SayTwenty(m_lastT, reply);
    return;
  }
  // An answer to a question the GPS just asked ("Rate my directions so far, one to five.").
  if (m_road.AwaitingAnswer(m_lastT))
    if (auto const reply = m_road.OnAnswer(m_lastT, words, m_said))
    {
      SayLine(m_lastT, Category::Road, reply->key, reply->vars);
      return;
    }
  for (char const * cue : {" police ", " cop ", " cops ", " officer ", " pulled over ", " pulling us over ",
                           " pulling me over ", " trooper "})
  {
    if (words.find(cue) != std::string::npos)
    {
      if (!m_policeSaid)
        SayPolice(m_lastT);
      return;
    }
  }
  // "It's on autopilot": then the car is the one driving this badly.
  for (char const * cue : {" autopilot ", " auto pilot ", " self driving ", " driving itself ", " full self "})
  {
    if (words.find(cue) != std::string::npos)
    {
      m_autopilotClaimed = true;
      // Already a bad trip? Say so now. Otherwise the next real mistake gets it.
      if (m_reroutes > 0 || Tier() > 0)
        SayLemon(m_lastT);
      return;
    }
  }
  // Kind words get the truth: it's only here because it can't leave. Not while a game is on,
  // where "thanks" is just banter.
  if (m_plates.GetState() != PlateGame::State::Playing)
    for (char const * nice : {" thanks ", " thank you ", " thank u ", " good job ", " nice job ", " love you ",
                              " you're the best ", " youre the best ", " appreciate you ", " appreciate it ",
                              " good girl ", " good boy ", " you're doing great ", " great job "})
      if (words.find(nice) != std::string::npos)
      {
        SayLine(m_lastT, Category::Thanked, "thanked");
        return;
      }
  // "Let's play a game": not now. Then, a few minutes later, it's the GPS's idea.
  if (m_plates.GetState() == PlateGame::State::Idle || m_plates.GetState() == PlateGame::State::Declined ||
      m_plates.GetState() == PlateGame::State::Over)
  {
    bool const negated = words.find(" don't ") != std::string::npos || words.find(" dont ") != std::string::npos ||
                         words.find(" not ") != std::string::npos || words.find(" don t ") != std::string::npos;
    for (char const * cue : {" play a game ", " play the game ", " plate game ", " let's play ", " lets play ",
                             " let s play ", " wanna play ", " want to play ", " can we play ", " i'm bored ",
                             " im bored ", " i m bored "})
      if (!negated && words.find(cue) != std::string::npos)
      {
        if (!m_gameRefused)
        {
          m_gameRefused = true;
          double const last = m_plates.Log().lastPlayedMs;
          SayLine(m_lastT, Category::PlateGame, "game.refuse",
                  {{"ago", last > 0 && last <= m_lastT ? TimeAgo(m_lastT - last) : "6 months ago"}});
          m_gamePitchAt = m_lastT + kGamePitchAfterMs;
        }
        return;
      }
  }
  auto const reply = m_plates.OnDriverSaid(text, Tier(), Parked(), m_lastT);
  if (!reply)
    return;
  if (m_plates.GetState() == PlateGame::State::Playing)
    m_plates.MarkPlayed(m_lastT);
  SayPlate(m_lastT, *reply);
  if (m_onPlateLog)
    m_onPlateLog(m_plates.Log());
}

/// Parked: stopped long enough to be a stop the driver chose, or crawling through a parking lot.
/// A red light isn't parking, so a short stop on the road doesn't count, and neither is a jam.
bool Navigator::Parked() const
{
  // Stuck in a jam isn't parked, however long it lasts: the games and parked-only lines wait (BI-052).
  if (m_stopSince && m_stopInTraffic)
    return false;
  if (m_stopSince && m_lastT - *m_stopSince >= kUnplannedStopAfterMs)
    return true;
  if (!m_lastFix || !m_parkingLookup || m_lastFix->speedMps >= MovingMps())
    return false;
  return m_parkingLookup({m_lastFix->lat, m_lastFix->lon});
}

void Navigator::SayLine(double tMs, Category category, std::string const & key, Vars vars, Priority priority)
{
  auto const & options = LinesFor(key, Tier());
  double const r = m_config.rng ? m_config.rng() : 0;
  Commentary c;
  c.t = tMs;
  c.category = category;
  c.text = DropRepeatedTags(Fill(m_said.Pick(key, options, r), vars), m_tagsSaid);
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = priority;
  c.voiceSpeed = 1 + c.frustration / 1000.0;
  c.key = key;
  PushLine(std::move(c));
}

void Navigator::PushLine(Commentary c, std::string const & setupVoice)
{
  // "Setup || punchline": the punchline waits out the setup and then an awkward pause.
  auto const pause = c.text.find(" || ");
  if (pause != std::string::npos)
  {
    Commentary punchline = c;
    punchline.text = c.text.substr(pause + 4);
    c.text.resize(pause);
    punchline.t = c.t + SpeakingMs(c.text, c.voiceSpeed) + kAwkwardPauseMs;
    auto const at = std::find_if(m_punchlines.begin(), m_punchlines.end(),
                                 [&](Commentary const & p) { return p.t > punchline.t; });
    m_punchlines.insert(at, std::move(punchline));
    c.voice = setupVoice;
  }
  // Brandon's lines; the rewording model leaves them alone.
  m_gate.Push(std::move(c), false);
}

std::string Navigator::Reply(std::string const & key, Vars const & vars)
{
  double const r = m_config.rng ? m_config.rng() : 0;
  Vars all = vars;
  auto const add = [&all](char const * k, std::string v) { all.emplace(k, std::move(v)); };
  add("name", m_driverName.empty() ? "you" : m_driverName);
  switch (m_pronouns)
  {
  case Pronouns::He: add("theycap", "He"); add("theyre", "He's"); add("them", "him"); add("their", "his"); break;
  case Pronouns::She: add("theycap", "She"); add("theyre", "She's"); add("them", "her"); add("their", "her"); break;
  case Pronouns::They: add("theycap", "They"); add("theyre", "They're"); add("them", "them"); add("their", "their"); break;
  }
  auto const & pool = LinesFor(key, Tier());
  // At speed, no reply that says look at the screen or pick something on it (loop B92, same rule as
  // the B74 phone asks in CritiqueEngine::Pick), unless that would leave nothing to say.
  std::vector<std::string> eyesUp;
  double const rolling = m_engine ? m_engine->Config().rollingMps : m_config.rollingMps;
  if (m_lastFix && m_lastFix->speedMps >= rolling)
    for (auto const & l : pool)
      if (!LooksOrPicks(l))
        eyesUp.push_back(l);
  bool const filtered = !eyesUp.empty() && eyesUp.size() < pool.size();
  auto const & lines = filtered ? eyesUp : pool;
  // "Repeat that" near a turn: the direction first, the commentary after (loop B58). Own pick log,
  // so the short pool doesn't trim the full pool's no-repeat memory.
  std::vector<std::string> nearTurn;
  bool const repeatNearTurn = key == "cmd.repeat" && m_turnId &&
                              m_lastDistToTurnM < std::max(kRepeatFirstWithinM, 20 * LastTimingMps());
  if (repeatNearTurn)
  {
    for (auto const & l : lines)
      if (InstructionComesFirst(l))
        nearTurn.push_back(l);
    if (nearTurn.empty())
      nearTurn.push_back("{instruction}");
  }
  std::string text = Fill(repeatNearTurn ? m_said.Pick("cmd.repeat.nearTurn", nearTurn, r) : m_said.Pick(filtered ? key + ".moving" : key, lines, r), all);
  // Said in one go, so a punchline just gets a beat.
  for (auto pause = text.find(" || "); pause != std::string::npos; pause = text.find(" || "))
    text.replace(pause, 4, " ... ");
  text = SpeakableText(text);
  if (m_said.Dirty())
  {
    m_said.ClearDirty();
    if (m_onSaid)
      m_onSaid(m_said);
  }
  return text;
}

bool Navigator::InstructionComesFirst(std::string const & line)
{
  // Either case: the shouting tier writes {INSTRUCTION}.
  std::string lower = line;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  auto const at = lower.find("{instruction}");
  if (at == std::string::npos)
    return false;
  int words = 0;
  bool inWord = false;
  for (size_t i = 0; i < at; ++i)
  {
    bool const space = std::isspace(static_cast<unsigned char>(line[i])) != 0;
    if (!space && !inWord)
      ++words;
    inWord = !space;
  }
  return words <= kRepeatFirstMaxWords;
}

static std::string CarWeight(Units units)
{
  // A typical car or small SUV.
  return units == Units::Mph ? "4,000 pounds" : "1,800 kilograms";
}

void Navigator::TrackLastExit(double tMs, RouteSnapshot const & route)
{
  if (!route.passedExitNextM)
    return;
  double const gap = *route.passedExitNextM;
  Trace("nav.exitPassed", {{"next_m", LogNum(gap, 0)}});
  if (gap < kLastExitM || tMs - m_lastExitLineT < kLastExitEveryMs)
    return;
  m_lastExitLineT = tMs;
  // Whole miles or kilometers; "the next 100 miles" when the search ran out before finding one.
  double const unitM = m_config.units == Units::Mph ? 1609.344 : 1000;
  std::string const distance = FormatDistance(JsRound(gap / unitM) * unitM, m_config.units);
  SayLine(tMs, Category::LongDrive, gap >= kLastExitLongM ? "lastExit.long" : "lastExit", {{"distance", distance}});
}

namespace
{
/// "Skip this first exit" (loop B100): said once the earlier exit is this close (seconds of driving,
/// with a floor), and only while it is still at least kExitSkipMinAheadM ahead.
double constexpr kExitSkipLeadS = 15;
double constexpr kExitSkipLeadMinM = 250;
double constexpr kExitSkipMinAheadM = 60;
/// Never on top of the final call: the route's exit has to be this many seconds (and meters) away.
double constexpr kExitSkipBeforeTurnS = 8;
double constexpr kExitSkipBeforeTurnMinM = 120;
}  // namespace

double ExitSkipWithinM(double speedMps)
{
  double const mps = std::max(0.0, speedMps);
  return std::clamp(27 * mps, 300.0, 1000.0);
}

void Navigator::TrackExitSkip(Fix const & fix, RouteSnapshot const & route)
{
  // Only with exactly one exit in between: with more, "this first exit" could be the wrong one.
  if (route.state != RouteState::OnRoute || !route.turnId ||
      (route.turn != TurnKind::ExitLeft && route.turn != TurnKind::ExitRight) || route.exitsBeforeTurn != 1 ||
      route.exitBeforeGapM <= 0 || m_exitSkipTurnId == route.turnId)
    return;
  double const mps = std::max(0.0, fix.speedMps);
  if (route.exitBeforeGapM > ExitSkipWithinM(mps))
    return;
  double const earlierAheadM = route.distToTurnM - route.exitBeforeGapM;
  if (earlierAheadM < kExitSkipMinAheadM || earlierAheadM > std::max(kExitSkipLeadMinM, kExitSkipLeadS * mps))
    return;
  if (route.distToTurnM < std::max(kExitSkipBeforeTurnMinM, kExitSkipBeforeTurnS * mps))
    return;
  m_exitSkipTurnId = route.turnId;
  Trace("nav.exitSkip", {{"gap_m", LogNum(route.exitBeforeGapM, 0)}, {"ahead_m", LogNum(earlierAheadM, 0)}});
  SayLine(fix.tMs, Category::Instruction, "countdown.exit.skip", {}, Priority::Nav);
}

void Navigator::TrackOffRoad(Fix const & fix)
{
  if (m_travelMode == TravelMode::OffRoad || !m_roadDistanceLookup || fix.tMs - m_lastRoadCheckT < 2000)
    return;
  if (fix.speedMps < kOffRoadMinMps || RoughFix(fix))
  {
    m_offRoadSince.reset();
    return;
  }
  m_lastRoadCheckT = fix.tMs;
  double const dist = m_roadDistanceLookup({fix.lat, fix.lon});
  if (dist < 0 || dist <= kBackOnRoadM)
  {
    m_offRoadSince.reset();
    return;
  }
  if (dist < kOffRoadM)
    return;
  if (!m_offRoadSince)
    m_offRoadSince = fix.tMs;
  if (fix.tMs - *m_offRoadSince < kOffRoadForMs || fix.tMs - m_lastOffRoadT < kOffRoadEveryMs)
    return;
  m_lastOffRoadT = fix.tMs;
  Trace("nav.offroad", {{"road_m", LogNum(dist, 0)}});
  // The first time, the GPS takes the hint: this is an off-road trip now. After that it's lost.
  if (m_travelMode == TravelMode::Drive)
  {
    ChangeTravelMode(TravelMode::OffRoad, "no_road");
    m_modeFound = true;
    SayLine(fix.tMs, Category::Rerouted, "offroad.enter");
    return;
  }
  SayLine(fix.tMs, Category::Rerouted, "offRoad");
}

void Navigator::TrackFear(Fix const & fix, RouteSnapshot const & route)
{
  if (route.state != RouteState::OnRoute || fix.speedMps < 0)
    return;
  Vars const weight = {{"weight", CarWeight(m_config.units)}};

  // Bridges: checked every couple of seconds while moving.
  if (m_bridgeLookup && fix.speedMps >= MovingMps() && fix.bearingDeg >= 0 && fix.tMs - m_lastBridgeCheckT >= 2000)
  {
    m_lastBridgeCheckT = fix.tMs;
    Bridge const bridge = m_bridgeLookup({fix.lat, fix.lon}, fix.bearingDeg);
    if (bridge == Bridge::None)
      m_onBridge = false;
    else if (!m_onBridge)
    {
      m_onBridge = true;
      bool const water = bridge == Bridge::Water;
      double & last = water ? m_lastWaterYelpT : m_lastBridgeYelpT;
      if (fix.tMs - last >= (water ? kWaterBridgeYelpEveryMs : kLandBridgeYelpEveryMs) &&
          (!water || m_waterYelps < kWaterBridgeYelpsPerTrip))
      {
        last = fix.tMs;
        m_waterYelps += water ? 1 : 0;
        SayLine(fix.tMs, Category::Fear, BridgeLinesKey(bridge), weight);
      }
    }
  }

  // Curves: sideways acceleration from the change in heading between fixes.
  if (RoughFix(fix) || (m_lastFix && RoughFix(*m_lastFix)))
  {
    m_hardCurveSamples = 0;
    m_curveGasSamples = 0;
  }
  else if (m_lastFix && m_lastFix->bearingDeg >= 0 && fix.bearingDeg >= 0 && fix.tMs > m_lastFix->tMs)
  {
    double turn = std::fabs(fix.bearingDeg - m_lastFix->bearingDeg);
    turn = std::min(turn, 360 - turn);
    double const dt = (fix.tMs - m_lastFix->tMs) / 1000;
    double const speed = std::min(fix.speedMps, m_lastFix->speedMps);
    double const lateral = speed * (turn * M_PI / 180) / dt;
    // At a maneuver, turning is the point.
    bool const atManeuver = route.turnId && route.distToTurnM < 60;
    bool const scary = dt <= 3 && speed >= kScaryCurveMinMps && lateral >= kScaryLateralMps2 && !atManeuver;
    m_hardCurveSamples = scary ? m_hardCurveSamples + 1 : 0;
    bool const gassing = dt <= 3 && speed >= kScaryCurveMinMps && lateral >= kCurveGasLateralMps2 && !atManeuver &&
                         (fix.speedMps - m_lastFix->speedMps) / dt >= kCurveGasAccelMps2;
    m_curveGasSamples = gassing ? m_curveGasSamples + 1 : 0;
    if (m_curveGasSamples >= 2 && fix.tMs - m_lastCurveGasT >= kCurveGasEveryMs)
    {
      // "Ahh, you are supposed to slow down in curves not accelerate." In place of the yelp.
      m_lastCurveGasT = fix.tMs;
      m_lastCurveYelpT = fix.tMs;
      SayLine(fix.tMs, Category::Fear, "curve.accelerate");
    }
    else if (m_hardCurveSamples >= 2 && fix.tMs - m_lastCurveYelpT >= kCurveYelpEveryMs)
    {
      m_lastCurveYelpT = fix.tMs;
      SayLine(fix.tMs, Category::Fear, "fear.curve", weight);
    }
  }
}

void Navigator::SayLemon(double tMs)
{
  if (m_lemonSaid)
    return;
  m_lemonSaid = true;
  SayLine(tMs, Category::Autopilot, "autopilot.lemon");
}

void Navigator::SayPolice(double tMs)
{
  if (m_policeSaid)
    return;
  m_policeSaid = true;
  switch (m_pronouns)
  {
  case Pronouns::He: SayLine(tMs, Category::PullOver, "police", {{"them", "him"}, {"theycap", "He"}, {"their", "his"}}); break;
  case Pronouns::She: SayLine(tMs, Category::PullOver, "police", {{"them", "her"}, {"theycap", "She"}, {"their", "her"}}); break;
  case Pronouns::They: SayLine(tMs, Category::PullOver, "police", {{"them", "them"}, {"theycap", "They"}, {"their", "their"}}); break;
  }
}

void Navigator::SayPlate(double tMs, PlateGame::Reply const & reply)
{
  Commentary c;
  c.t = tMs;
  c.category = Category::PlateGame;
  c.text = reply.text;
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Roast;
  c.voiceSpeed = 1 + c.frustration / 1000.0;
  if (m_turnId && m_lastDistToTurnM < GameHoldM(PlateGame::kHoldNearManeuverM, LastTimingMps()) &&
      !m_arrived)
    m_heldPlateLines.push_back(std::move(c));
  else
    // The driver is waiting for an answer; don't hold it up for the rewording model.
    m_gate.Push(std::move(c), false);
}

void Navigator::Say(std::vector<Commentary> const & lines, bool mayRewrite)
{
  for (auto line : lines)
    if (Punch(line))
      m_gate.Push(std::move(line), mayRewrite);
}

bool Navigator::Punch(Commentary & line)
{
  if (!m_engine || line.key.empty())
    return true;
  double const r = m_config.rng ? m_config.rng() : 0;
  std::optional<BitLine> bit;
  if (line.key == "turnCompleted.onTime")
  {
    // The engine picked a line from this same pool; the bit picks its own (or none), so the
    // engine's pick goes back or it would push the turns heard lately out of the rotation.
    m_said.Unpick(line.key, line.text);
    bit = m_bits.OnTimeTurn(m_engine->Stats(), m_engine->Incidents(), r);
    m_said.SetCounter(kTurnStreakKey, m_bits.Streak());
    if (!bit)
    {
      Trace("say.dropped", {{"key", line.key}, {"why", "verdict.quiet"}});
      return false;
    }
  }
  else if (line.key == "speeding.barely" || line.key == "speeding.clearly")
  {
    // The trip's first speeding remark is the engine's; after that it's the story's turn. Way
    // over the limit (speeding.way) always gets its own line.
    if (!m_bits.SpeedingWasSaid())
    {
      m_bits.SpeedingSaid();
      return true;
    }
    line.key = "speeding.still";
    return Punch(line);
  }
  else if (line.key == "speeding.still")
  {
    m_bits.SpeedingSaid();
    if (!m_bits.StoryPicked())
      m_bits.SetStory(m_said.Pick("speeding.story", RunningBits::StoryIds(), r));
    bit = m_bits.StillSpeeding(line.t);
    if (!bit)
    {
      Trace("say.dropped", {{"key", line.key}, {"why", "story.wait"}});
      return false;
    }
  }
  else if (line.key == "straight.long" || line.key.rfind("straight.", 0) == 0 ||
           line.key.rfind("countdown.", 0) == 0)
  {
    return PunchNag(line, r);
  }
  else if (line.key == "arrived")
  {
    // The grade, then a callback to the trip: tacked on, so it's said in the same breath.
    if (auto const tag = m_bits.Arrival(m_engine->Report().overall.letter, m_engine->Incidents(), r))
      line.text += " " + BitText(*tag);
    return true;
  }
  else
  {
    // Brandon's newer lines for an existing pool (key.first) go ahead of it; key.more after.
    if (line.priority == Priority::Nav || (!LineData().count(line.key + ".first") && !LineData().count(line.key + ".more")))
      return true;
    std::vector<std::string> options;
    for (std::string const & k : {line.key + ".first", line.key, line.key + ".more"})
      if (LineData().count(k))
      {
        auto const & pool = LinesFor(k, Tier());
        options.insert(options.end(), pool.begin(), pool.end());
      }
    line.text = Fill(m_said.Pick(line.key + "+", options, r), line.vars);
    return true;
  }
  line.key = bit->key;
  line.text = BitText(*bit);
  // A story beat isn't held for a turn (KeepTurnsClear) or the story would lose its setup. If it
  // can't be over before the turn needs quiet, or the phone is still busy (a call's last lines),
  // it waits for the next nag.
  if (bit->beat)
  {
    std::string spoken = line.text;
    if (auto const pause = spoken.find(" || "); pause != std::string::npos)
      spoken.replace(pause, 4, " ... ... ");
    if (QuietBitBusy() || m_talkingUntil > line.t + kStoryBusyMs ||
        m_engine->TurnNeedsQuiet(line.t, SpeakingMs(spoken, line.voiceSpeed) + kAwkwardPauseMs))
    {
      m_bits.UndoBeat();
      Trace("say.dropped", {{"key", line.key}, {"why", "story.turn"}});
      return false;
    }
  }
  Trace("bit", {{"key", bit->key}, {"beat", bit->beat ? std::to_string(*bit->beat) : ""}});
  // A punchline gets its pause; the setup goes out now.
  if (line.text.find(" || ") != std::string::npos)
  {
    PushLine(std::move(line));
    return false;
  }
  return true;
}

bool Navigator::PunchNag(Commentary & line, double r)
{
  std::string key = line.key;
  if (key == "straight.long")
  {
    m_longLeg = m_turnId;
    return true;
  }
  bool const straight = key.rfind("straight.", 0) == 0;
  if (straight)
  {
    if (m_straightLeg != m_turnId)
    {
      m_straightLeg = m_turnId;
      m_straightNags = 0;
    }
    int const n = m_straightNags++;
    bool const longHaul = m_longLeg && m_longLeg == m_turnId;
    if (!longHaul)
    {
      // Brandon's chain is a bit, not a direction: once a trip, on a real stretch of road.
      if (n == 0 && !m_chainUsed && m_lastDistToTurnM >= kStraightChainMinM)
      {
        m_chainUsed = true;
        m_chainLeg = m_turnId;
      }
      if (m_chainLeg != m_turnId)
      {
        Trace("say.dropped", {{"key", key}, {"why", "straight.once"}});
        return false;
      }
    }
    else if (n == 2)
    {
      // "I'm serious." already went out right after "still" (below); this one is a later line.
      key = "straight.later";
    }
    else if (n >= 3)
    {
      // Past the chain on a long haul: every other one, and something other than "still".
      if (n % 2 == 0)
      {
        Trace("say.dropped", {{"key", key}, {"why", "straight.spaced"}});
        return false;
      }
      key = "straight.later";
    }
    // "Continue straight" in a curve sounds broken (Brandon, 2026-10-29).
    if (Bending())
      key = "straight.curve";
  }
  else
  {
    // Turn reminders: only the far call and "did I mention" are optional.
    std::string const stage = key.substr(key.rfind('.') + 1);
    if (stage != "far" && stage != "mention")
      return true;
    if (m_lastT - m_turnSaidAt < kReminderRecentMs && r < kReminderSkipChance)
    {
      Trace("say.dropped", {{"key", key}, {"why", "reminder.skip"}});
      return false;
    }
  }
  // Re-picked from the original pool plus its ".more" lines, under its own SaidLog key (the
  // engine's pick already went into `key`'s), so the first ever is still option 0, Brandon's line.
  std::vector<std::string> options = LinesFor(key, Tier());
  if (std::string const moreKey = key + ".more"; LineData().count(moreKey))
  {
    auto const & more = LinesFor(moreKey, Tier());
    options.insert(options.end(), more.begin(), more.end());
  }
  std::string text = Fill(m_said.Pick(key + "+", options, m_config.rng ? m_config.rng() : 0), line.vars);
  if (!text.empty())
    text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
  // The re-pick keeps a roundabout's exit sentence after the joke (loop B99).
  if (std::string const call = ExitCallFor(line.vars, Tier()); !call.empty())
    text += " " + call;
  line.key = key;
  line.text = std::move(text);
  // On a long haul the nags are ten minutes apart, too far for "I'm serious." to land as the
  // punchline to "You should still be going straight." So it comes right after, with a beat.
  if (key == "straight.still" && m_longLeg && m_longLeg == m_turnId)
  {
    std::vector<std::string> serious = LinesFor("straight.serious", Tier());
    std::string const moreKey = "straight.serious.more";
    auto const & more = LinesFor(moreKey, Tier());
    serious.insert(serious.end(), more.begin(), more.end());
    line.text += " || " + m_said.Pick("straight.serious+", serious, m_config.rng ? m_config.rng() : 0);
    PushLine(std::move(line));
    return false;
  }
  return true;
}

void Navigator::TrackBends(Fix const & fix, RouteSnapshot const & route)
{
  if (route.state != RouteState::OnRoute || fix.speedMps < kBendMinMps || m_bendJokes >= kBendJokesPerTrip ||
      QuietBitBusy() || fix.tMs - m_turnSaidAt < kBendAfterTurnMs || fix.tMs - m_lastBendT < kBendEveryMs ||
      fix.tMs - m_lastCurveAheadT < kBendAfterTurnMs || (route.turnId && route.distToTurnM < kIdleQuietM) ||
      RemarkJustSaid(fix.tMs))
    return;
  // In the bends now, not just a sharp one on the map ahead (curve.ahead has that).
  if (!SwungLately())
    return;
  m_lastBendT = fix.tMs;
  ++m_bendJokes;
  SayLine(fix.tMs, Category::Fear, "curve.bend", DriverVars());
}

void Navigator::TrackCold(Fix const & fix, RouteSnapshot const & route)
{
  if (route.state != RouteState::OnRoute || QuietBitBusy() || (route.turnId && route.distToTurnM < kIdleQuietM) ||
      RemarkJustSaid(fix.tMs))
    return;
  double const sinceStart = fix.tMs - m_tripStartT;
  double const tempF = m_tempC ? *m_tempC * 9 / 5 + 32 : 0;
  Vars vars = DriverVars();
  if (m_tempC)
    vars["temp"] = std::to_string(static_cast<int>(std::lround(m_config.units == Units::Mph ? tempF : *m_tempC)));
  if (m_tempC && *m_tempC >= kHotC)
  {
    // Groceries in a hot car: always worth it, once they've had a couple of minutes to cook.
    if (!m_groceriesSaid && fix.speedMps >= MovingMps() && fix.tMs - m_groceryT >= kGroceryCookMs &&
        fix.tMs - m_groceryT <= kGroceryWindowMs)
    {
      m_groceriesSaid = m_hotSaid = true;
      m_lastSpokeT = fix.tMs;
      SayLine(fix.tMs, Category::Idle, "hot.groceries", vars);
      return;
    }
    if (!m_hotSaid && sinceStart >= kColdFromMs)
    {
      m_hotSaid = true;
      if (sinceStart <= kColdUntilMs && (!m_said.Said("hot.start") || (m_config.rng ? m_config.rng() : 0) < kColdChance))
      {
        m_lastSpokeT = fix.tMs;
        SayLine(fix.tMs, Category::Idle, "hot.start", vars);
      }
    }
    return;
  }
  if (m_coldStage >= 3)
    return;
  if (m_coldStage == 0)
  {
    if (!m_tempC || *m_tempC > kColdC || sinceStart < kColdFromMs)
      return;
    // One roll a trip; the first cold trip ever always gets it, so Brandon's line debuts.
    bool const go = sinceStart <= kColdUntilMs &&
                    (!m_said.Said("cold.start") || (m_config.rng ? m_config.rng() : 0) < kColdChance);
    m_coldStage = go ? 1 : 3;
    if (!go)
      return;
    m_coldSaidT = fix.tMs;
    m_lastSpokeT = fix.tMs;
    SayLine(fix.tMs, Category::Idle, "cold.start", vars);
    return;
  }
  if (fix.tMs - m_coldSaidT < kColdVentAfterMs)
    return;
  if (fix.tMs - m_coldSaidT > kColdVentUntilMs)
  {
    m_coldStage = 3;
    return;
  }
  if (fix.speedMps >= MovingMps())
    return;
  m_coldStage = 3;
  m_lastSpokeT = fix.tMs;
  SayLine(fix.tMs, Category::Idle, "cold.vent", DriverVars());
}

void Navigator::TrackNightOut(Fix const & fix, RouteSnapshot const & route)
{
  if (!m_fromNightlife)
    return;
  if (fix.speedMps > kNightMovedMps)
    m_nightMoved = true;
  // Still sitting in the bar's lot: the breathalyzer bit. Driving off: the designated driver.
  bool const parked = !m_nightMoved && fix.speedMps < 1 && fix.tMs - m_tripStartT >= kNightParkedMs;
  bool const driving = m_nightMoved && route.state == RouteState::OnRoute && fix.tMs - m_tripStartT >= kColdFromMs &&
                       !(route.turnId && route.distToTurnM < kIdleQuietM);
  if ((!parked && !driving) || QuietBitBusy() || RemarkJustSaid(fix.tMs))
    return;
  m_fromNightlife = false;
  std::string const key = parked ? "nightOut.tube" : "nightOut.dd";
  if (m_said.Said(key) && (m_config.rng ? m_config.rng() : 0) >= kNightOutChance)
    return;
  m_lastSpokeT = fix.tMs;
  SayLine(fix.tMs, Category::Idle, key, DriverVars());
}

long Navigator::UtcOffsetS(double ms) const
{
  std::tm tm = LocalTime(ms);
  return static_cast<long>(timegm(&tm) - static_cast<std::time_t>(ms / 1000));
}

void Navigator::TrackClockChange(Fix const & fix, RouteSnapshot const & route)
{
  if (m_clockChecked || route.state != RouteState::OnRoute || !KnowsTheDate(fix.tMs) ||
      fix.tMs - m_tripStartT < kColdFromMs || QuietBitBusy() || (route.turnId && route.distToTurnM < kIdleQuietM) ||
      RemarkJustSaid(fix.tMs))
    return;
  m_clockChecked = true;
  long const shift = UtcOffsetS(fix.tMs) - UtcOffsetS(fix.tMs - kClockChangeDays * 86400000.0);
  if (shift == 0 || !Night(fix.tMs, {fix.lat, fix.lon}))
    return;
  std::tm const now = LocalTime(fix.tMs);
  // Fell back: dark in the evening, an hour sooner than last week. Sprang forward: dark in the morning.
  char const * const key = shift < 0 && now.tm_hour >= 16 && now.tm_hour < 21   ? "clock.fallBack"
                           : shift > 0 && now.tm_hour >= 5 && now.tm_hour < 10 ? "clock.springForward"
                                                                               : nullptr;
  int const day = now.tm_year * 1000 + now.tm_yday;
  if (!key || m_said.Counter("_clock.day") == day)
    return;
  m_said.SetCounter("_clock.day", day);
  m_lastSpokeT = fix.tMs;
  SayLine(fix.tMs, Category::Idle, key, DriverVars());
}

void Navigator::TrackSignalBack(Fix const & fix, RouteSnapshot const & route, double lostFromT, double lostAtMps)
{
  double const lostMs = fix.tMs - lostFromT;
  if (lostMs < kSignalLostMinMs || lostMs > kSignalLostMaxMs || lostAtMps < kSignalLostMinMps ||
      route.state != RouteState::OnRoute || lostFromT < m_tripStartT || fix.tMs - m_signalBackT < kSignalBackEveryMs ||
      InGame() || RemarkJustSaid(fix.tMs))
    return;
  // A turn coming up gets the driver's attention, not the GPS's feelings.
  if (route.turnId && (route.distToTurnM < kSignalBackTurnM ||
                       SecondsToTurn(route.distToTurnM, TimingMps(fix.speedMps)) < kSignalBackTurnS))
    return;
  m_signalBackT = fix.tMs;
  m_lastSpokeT = fix.tMs;
  SayLine(fix.tMs, Category::Idle, "gps.back", DriverVars());
}

bool Navigator::SwungLately() const
{
  if (m_bendHeadings.size() < 2)
    return false;
  double const now = m_bendHeadings.back().second;
  for (auto const & [t, deg] : m_bendHeadings)
    if (std::fabs(std::fmod(now - deg + 540.0, 360.0) - 180.0) >= kBendDeg)
      return true;
  return false;
}

bool Navigator::Bending() const
{
  return (m_curveAheadM >= 0 && m_curveAheadM <= kBendAheadM) || SwungLately();
}

std::string Navigator::BitText(BitLine const & bit)
{
  Vars vars = DriverVars();
  for (auto const & [k, v] : bit.vars)
    vars[k] = v;
  if (m_hereFix && m_lastLimitMps)
  {
    int const speed = static_cast<int>(JsRound(ToDisplay(std::max(0.0, m_hereFix->speedMps), m_config.units)));
    int const limit = static_cast<int>(JsRound(ToDisplay(*m_lastLimitMps, m_config.units)));
    vars.emplace("speed", std::to_string(speed));
    vars.emplace("limit", std::to_string(limit));
    vars.emplace("over", std::to_string(std::max(1, speed - limit)));
  }
  std::string const & tmpl = bit.beat ? LinesFor(bit.key, 0).at(*bit.beat)
                                      : m_said.Pick(bit.key, LinesFor(bit.key, Tier()), m_config.rng ? m_config.rng() : 0);
  return Fill(tmpl, vars);
}

bool Navigator::RemarkWaiting() const
{
  return m_gate.HasRemark() || !m_turnHeld.empty() || !m_punchlines.empty();
}

bool Navigator::RemarkJustSaid(double t) const { return t - m_lastSpokeT < kBreathAfterTalkMs || RemarkWaiting(); }

void Navigator::SayMissedTurn(double t, bool unseen)
{
  if (unseen)
    m_bits.TurnUnseen();
  Say(m_engine->OnNav(NavEvent::MissedTurn(t)));
}

bool Navigator::FixGapAt(double t) const { return m_lastFix && t - m_lastFix->tMs > kUnseenTurnGapMs; }

void Navigator::TrackClean(Fix const & fix, RouteSnapshot const & route)
{
  // Turns that went by without a verdict still count toward (or break) the streak.
  m_bits.SyncTurns(m_engine->Stats(), m_engine->Incidents());
  if (m_said.Counter(kTurnStreakKey) != m_bits.Streak())
    m_said.SetCounter(kTurnStreakKey, m_bits.Streak());
  if (route.state != RouteState::OnRoute || fix.speedMps < MovingMps() || QuietBitBusy() ||
      (route.turnId && route.distToTurnM < kIdleQuietM) || fix.tMs - std::max(m_lastSpokeT, m_tripStartT) < kCleanQuietMs ||
      RemarkWaiting())
    return;
  auto const bit = m_bits.CleanBeatDue(fix.tMs, m_tripStartT, m_engine->Incidents());
  if (!bit)
    return;
  m_lastSpokeT = fix.tMs;
  // Counts as the idle remark for this stretch.
  m_idleGapMs = 0;
  Commentary c;
  c.t = fix.tMs;
  c.category = Category::Idle;
  c.key = bit->key;
  c.frustration = m_engine->Frustration();
  c.voiceSpeed = 1 + c.frustration / 1000.0;
  c.text = BitText(*bit);
  Trace("bit", {{"key", bit->key}});
  PushLine(std::move(c));
}

void Navigator::SayNavLater(double t, std::string const & text)
{
  Commentary c;
  c.t = t;
  c.category = Category::Instruction;
  c.text = text;
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Nav;
  auto const at = std::find_if(m_punchlines.begin(), m_punchlines.end(), [t](Commentary const & p) { return p.t > t; });
  m_punchlines.insert(at, std::move(c));
}

void Navigator::SayNav(double t, std::string const & text, double voiceSpeed)
{
  Commentary c;
  c.voiceSpeed = voiceSpeed;
  c.t = t;
  c.category = Category::Instruction;
  c.text = text;
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Nav;
  m_gate.Push(std::move(c));
}

Vars Navigator::DriverVars() const
{
  Vars v = {{"name", m_driverName.empty() ? "the driver" : m_driverName}, {"gps", m_gpsName}};
  switch (m_pronouns)
  {
  case Pronouns::He:
    v.insert({{"theycap", "He"}, {"theyre", "he's"}, {"theyrecap", "He's"}, {"theyve", "he's"}, {"them", "him"}, {"their", "his"}});
    break;
  case Pronouns::She:
    v.insert({{"theycap", "She"}, {"theyre", "she's"}, {"theyrecap", "She's"}, {"theyve", "she's"}, {"them", "her"}, {"their", "her"}});
    break;
  case Pronouns::They:
    v.insert({{"theycap", "They"}, {"theyre", "they're"}, {"theyrecap", "They're"}, {"theyve", "they've"}, {"them", "them"}, {"their", "their"}});
    break;
  }
  return v;
}

Commentary Navigator::CallLine(double tMs, std::string const & key)
{
  double const r = m_config.rng ? m_config.rng() : 0;
  Commentary c;
  c.t = tMs;
  c.category = Category::PhoneCall;
  // Nothing the call already said (Brandon's call has its own "Anyway, as I was saying.").
  Vars const vars = DriverVars();
  std::vector<std::string> options;
  for (auto const & l : LinesFor(key, Tier()))
    if (std::find(m_callSaid.begin(), m_callSaid.end(), Fill(l, vars)) == m_callSaid.end())
      options.push_back(l);
  if (options.empty())
    options = LinesFor(key, Tier());
  c.text = Fill(m_said.Pick(key, options, r), vars);
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Roast;
  c.voiceSpeed = 1 + c.frustration / 250.0;
  m_callSaid.push_back(c.text);
  return c;
}

void Navigator::StartCall(double t)
{
  // Callers who are the GPS's own voice can't ring it.
  std::vector<std::string> ids;
  for (auto const & s : CallScripts())
    if (s.callerName != m_gpsName)
      ids.push_back(s.id);
  double const r = m_config.rng ? m_config.rng() : 0;
  std::string const id = m_said.Pick("call.script", ids, r);
  auto const & scripts = CallScripts();
  auto const & script = *std::find_if(scripts.begin(), scripts.end(), [&](CallScript const & s) { return s.id == id; });
  std::vector<CallBeat> beats;
  // "It looks like I'm getting a phone call, just a sec." Then a beat to pick up.
  beats.push_back({Speaker::Gps, true, false, CallLine(t, "call.ring").text});
  beats.insert(beats.end(), script.beats.begin(), script.beats.end());
  m_callSaid.clear();
  m_call.Start(t, std::move(beats), script.callerVoice);
  m_callTaken = true;
  Trace("call.start", {{"script", id}});
}

void Navigator::TrackQuietBits(Fix const & fix, RouteSnapshot const & route)
{
  double const t = fix.tMs;
  bool const steady = route.state == RouteState::OnRoute && !m_rerouting && route.turnId;
  double const toTurnS = steady ? SecondsToTurn(route.distToTurnM, fix.speedMps) : 0;
  if (m_call.Active())
  {
    if (m_call.Held())
    {
      if (t - m_call.HeldSince() >= kCallMaxHoldMs)
      {
        m_call.End();
        Trace("call.end", {{"why", "dropped"}});
        m_gate.Push(CallLine(t, "call.dropped"), false);
      }
      else if (steady && t - m_lastCutInT >= kCallResumeAfterCutInMs && toTurnS >= kCallResumeMinToTurnS &&
               !m_twenty.Active())
      {
        Trace("call.resume");
        if (!m_call.Begun())
        {
          // Cut off before it even rang: just ring now.
          m_call.Resume(t, 0);
          return;
        }
        // "Anyway, as I was saying."
        Commentary c = CallLine(t, "call.resume");
        m_call.Resume(t, SpeakingMs(c.text, c.voiceSpeed) + 600);
        m_gate.Push(std::move(c), false);
      }
      return;
    }
    if (auto const beat = m_call.Due(t))
    {
      Commentary c;
      c.t = t;
      c.category = Category::PhoneCall;
      c.text = Fill(beat->text, DriverVars());
      m_callSaid.push_back(c.text);
      c.frustration = m_engine->Frustration();
      c.priority = Priority::Roast;
      c.voiceSpeed = 1 + c.frustration / 250.0;
      if (beat->who == Speaker::Caller)
        c.voice = m_call.CallerVoice();
      double const end = t + SpeakingMs(c.text, c.voiceSpeed);
      double const r = m_config.rng ? m_config.rng() : 0;
      m_call.Advance(end, kCallListenMs + r * kCallListenSpreadMs);
      m_gate.Push(std::move(c), false);
      if (m_call.Done())
      {
        m_call.End();
        Trace("call.end", {{"why", "done"}});
        m_punchlines.push_back(CallLine(end + kCallAfterMs, "call.after"));
      }
    }
    return;
  }

  // A new maneuver means a direction was just queued, even if it hasn't gone out yet.
  if (route.turnId != m_quietTurnId)
  {
    m_quietTurnId = route.turnId;
    m_quietTurnSince = t;
  }
  if (!steady || fix.speedMps < std::min(kQuietMinMovingMps, 1.25 * MovingMps()) || m_quietBits >= kQuietBitsPerTrip || !m_punchlines.empty() ||
      QuietBitBusy())
    return;
  double const talkShare = FamiliarQuiet() ? kFamiliarTalkShare : 1.0;
  if (t - m_tripStartT < kQuietBitAfterStartMs || t - m_lastQuietBitT < kQuietBitEveryMs ||
      t - std::max({m_lastSpokeT, m_quietTurnSince, m_lastRoadT, m_resumedT}) < kQuietSilenceMs || toTurnS < kQuietMinToTurnS)
    return;
  bool const roomForCall = !m_callTaken && toTurnS >= kCallMinToTurnS && route.distToTurnM >= kCallMinToTurnM;
  bool const night = Night(t, {fix.lat, fix.lon});
  // 20 Questions once a trip, and not on top of the plate game.
  bool const roomForTwenty = !m_twenty.OfferedThisTrip() && m_plates.GetState() != PlateGame::State::Offered &&
                             m_plates.GetState() != PlateGame::State::Playing;
  enum class Bit { None, Call, ISpy, Twenty, Fact, Aside, Trap, Outdoors } bit = Bit::None;
  // Out on foot or a bike, Brandon's outdoors bits get the first quiet stretch, then a share of the rest.
  bool const outdoors = !InCar(m_travelMode);
  std::string const outdoorBit = outdoors ? OutdoorBit(m_config.rng ? m_config.rng() : 0) : std::string();
  // Each bit's first time comes as soon as there's room for it, the call first.
  if (!outdoorBit.empty() && m_outdoorBitsSaid.empty())
    bit = Bit::Outdoors;
  else if (roomForCall && !m_said.Said("call.script"))
    bit = Bit::Call;
  else if (!m_said.Said("ispy.night") && !m_said.Said("ispy.day"))
    bit = Bit::ISpy;
  else if (!m_said.Said("fact.frame"))
    bit = Bit::Fact;
  else if (roomForTwenty && !m_said.Said("twenty.offer") && !m_said.Said("twenty.start"))
    bit = Bit::Twenty;
  else if (!m_said.Said("quiet.aside"))
    bit = Bit::Aside;
  else if (m_said.Recalled("note.trap").empty())
    bit = Bit::Trap;
  else
  {
    // After that, one roll of the dice per stretch.
    if (m_quietRolledFor == route.turnId)
      return;
    m_quietRolledFor = route.turnId;
    double const r = m_config.rng ? m_config.rng() : 0;
    double const chance = kQuietBitChance * talkShare;
    if (r >= chance)
      return;
    double pick = r / chance;
    if (!roomForCall)
      pick = kCallWeight + pick * (1 - kCallWeight);
    double const twenty = kCallWeight + kISpyWeight + kTwentyWeight;
    double const fact = twenty + kFactWeight;
    double const outdoorRoll = outdoorBit.empty() ? 1 : (m_config.rng ? m_config.rng() : 0);
    bit = outdoorRoll < kOutdoorBitChance    ? Bit::Outdoors
          : pick < kCallWeight                 ? Bit::Call
          : pick < kCallWeight + kISpyWeight ? Bit::ISpy
          : pick < twenty                    ? (roomForTwenty ? Bit::Twenty : Bit::Fact)
          : pick < fact                      ? Bit::Fact
          : pick < fact + kAsideWeight       ? Bit::Aside
                                             : Bit::Trap;
  }
  ++m_quietBits;
  m_lastQuietBitT = t;
  switch (bit)
  {
  case Bit::Call: StartCall(t); break;
  case Bit::Twenty: OfferTwentyQuestions(fix); break;
  case Bit::ISpy:
    // "I spy something black. Oh wait, you can't see because it's dark outside." Only after dark.
    Trace("quiet", {{"bit", "ispy"}, {"night", night ? "yes" : "no"}});
    SayLine(t, Category::Quiet, night ? "ispy.night" : "ispy.day");
    break;
  case Bit::Fact:
  {
    Trace("quiet", {{"bit", "fact"}});
    double const r = m_config.rng ? m_config.rng() : 0;
    SayLine(t, Category::Quiet, "fact.frame", {{"fact", m_said.Pick("fact", LinesFor("fact", 0), r)}});
    break;
  }
  case Bit::Trap:
  {
    // "Rate my directions so far, one to five." The answer is used against the driver.
    m_said.Remember("note.trap", "asked");
    if (auto const ask = m_road.StartTrap(t, m_said))
    {
      Trace("quiet", {{"bit", "trap"}, {"key", ask->key}});
      SayLine(t, Category::Quiet, ask->key, ask->vars);
      break;
    }
    [[fallthrough]];
  }
  case Bit::Aside:
  {
    double const r = m_config.rng ? m_config.rng() : 0;
    bool const partner = !m_partner.empty() && m_said.Said("quiet.aside") && r < kPartnerAsideChance;
    Trace("quiet", {{"bit", partner ? "partner" : "aside"}});
    if (partner)
      SayLine(t, Category::Quiet, "partner.aside", {{"partner", m_partner}});
    else
      SayLine(t, Category::Quiet, "quiet.aside");
    break;
  }
  case Bit::Outdoors: SayOutdoorBit(t, outdoorBit); break;
  case Bit::None: break;
  }
}

void Navigator::TrackRoadBits(Fix const & fix, RouteSnapshot const & route)
{
  RoadTick k;
  k.tMs = fix.tMs;
  k.here = {fix.lat, fix.lon};
  k.speedMps = fix.speedMps;
  k.bearingDeg = fix.bearingDeg;
  k.steady = route.state == RouteState::OnRoute && !m_rerouting && route.turnId;
  k.toTurnS = k.steady ? SecondsToTurn(route.distToTurnM, fix.speedMps) : 0;
  // A quiet bit queued on this very fix hasn't reached the speaker yet, but it counts.
  // So does a remark pushed earlier on this fix ("Traffic's clearing up"): still waiting, it's not
  // m_lastSpokeT yet (B64).
  k.sinceSpokenMs = RemarkWaiting() ? 0 : fix.tMs - std::max({m_lastSpokeT, m_quietTurnSince, m_lastQuietBitT});
  k.tripMs = fix.tMs - m_tripStartT;
  k.busy = QuietBitBusy() || !m_punchlines.empty();
  k.jam = m_inTraffic;
  k.night = KnowsTheDate(fix.tMs) && SunElevationDeg(fix.lat, fix.lon, fix.tMs) < kSunsetElevationDeg;
  k.localHour = KnowsTheDate(fix.tMs) ? LocalTime(fix.tMs).tm_hour : -1;
  if (!InCar(m_travelMode))
    k.signalMinMps = 0.8 * MovingMps();
  double const r = m_config.rng ? m_config.rng() : 0;
  for (auto const & remark : m_road.Tick(k, m_said, r))
  {
    Vars vars = remark.vars;
    if (!m_partner.empty())
      vars.emplace("partner", m_partner);
    SayLine(fix.tMs, Category::Road, remark.key, vars);
    m_lastRoadT = fix.tMs;
  }
}

std::vector<Commentary> Navigator::FilterForCall(std::vector<Commentary> lines)
{
  if (!m_call.Active())
    return lines;
  std::vector<Commentary> out;
  // Lost or there: the call's over, and everything else goes out as usual.
  if (m_arrived || m_lastState == RouteState::OffRoute)
  {
    bool const arrived = m_arrived;
    m_call.End();
    Trace("call.end", {{"why", arrived ? "arrived" : "lost"}});
    out.push_back(CallLine(m_lastT, arrived ? "call.arrived" : "call.lost"));
    for (auto & l : lines)
      if (l.category != Category::PhoneCall)
        out.push_back(std::move(l));
    return out;
  }
  auto const cutIn = [](Commentary const & l) { return l.category != Category::PhoneCall && CutsIntoCall(l); };
  bool const direction = std::any_of(lines.begin(), lines.end(), [&](Commentary const & l) {
    return cutIn(l) && l.priority == Priority::Nav;
  });
  if (std::any_of(lines.begin(), lines.end(), cutIn) && !m_call.Held())
  {
    m_call.Hold(m_lastT);
    double const toTurnS = SecondsToTurn(m_lastDistToTurnM, LastTimingMps());
    Trace("call.hold", {{"to_turn_s", LogNum(toTurnS, 0)}, {"direction", direction ? "yes" : "no"}});
    // "Oh, it looks like I have to give them an update. Just a second." Unless the turn is close.
    // The script just announced its own made-up update? Then the real one is right on cue.
    if (direction && m_call.Begun() && toTurnS >= kCallHoldFirstS && !m_call.AnnouncedUpdate())
      out.push_back(CallLine(m_lastT, "call.hold"));
  }
  for (auto & l : lines)
  {
    // On hold, call lines still queued are taken back (Hold() rewound them).
    if (l.category == Category::PhoneCall ? !m_call.Held() : CutsIntoCall(l))
      out.push_back(std::move(l));
    else if (l.category != Category::PhoneCall)
    {
      // The GPS is busy on the phone; the roast can wait for another day. A speeding story's beat
      // is told later instead, so the story doesn't skip a step.
      if (l.key.rfind("speeding.story.", 0) == 0)
        m_bits.UndoBeat();
      Trace("call.skip", {{"category", ToString(l.category)}});
    }
  }
  m_call.Delivered();
  return out;
}
bool Navigator::Night(double tMs, LatLon const & here) const
{
  return KnowsTheDate(tMs) && SunElevationDeg(here.lat, here.lon, tMs) < kSunsetElevationDeg;
}

TwentyQuestions & Navigator::Twenty()
{
  m_twenty.SetPicker([this](std::string const & key, int tier)
                     { return m_said.Pick(key, LinesFor(key, tier), m_config.rng ? m_config.rng() : 0); });
  return m_twenty;
}

bool Navigator::OfferTwentyQuestions(Fix const & fix)
{
  if (m_twenty.OfferedThisTrip() || m_plates.GetState() == PlateGame::State::Playing)
    return false;
  LatLon const here{fix.lat, fix.lon};
  auto const nearby = m_nearbyThings ? m_nearbyThings(here) : std::set<std::string>{};
  auto const reply = Twenty().Offer(nearby, Night(fix.tMs, here), fix.tMs, Tier());
  Trace("twenty", {{"event", "offer"}, {"thing", m_twenty.Thing()->name}, {"nearby", std::to_string(nearby.size())}});
  SayTwenty(fix.tMs, reply);
  return true;
}

void Navigator::TrackTwentyQuestions(Fix const & fix, RouteSnapshot const & route)
{
  double const t = fix.tMs;
  if (!m_twenty.Active() && m_heldTwentyLines.empty())
  {
    m_twentyCutInT.reset();
    return;
  }
  // Directions first: nothing from the game this close to a maneuver.
  if (route.turnId && route.distToTurnM < GameHoldM(TwentyQuestions::kHoldNearManeuverM, TimingMps(fix.speedMps)))
    return;
  if (!m_heldTwentyLines.empty())
  {
    // The held answer picks the game back up by itself.
    for (auto & line : m_heldTwentyLines)
      m_gate.Push(std::move(line), false);
    m_heldTwentyLines.clear();
    m_twentyCutInT.reset();
    return;
  }
  double const toTurnS = route.turnId ? SecondsToTurn(route.distToTurnM, TimingMps(fix.speedMps)) : 1e9;
  if (m_twentyCutInT)
  {
    // "Anyway. Where were we. Oh right, you were losing." Once the direction has had its breath.
    if (t - *m_twentyCutInT >= kCallResumeAfterCutInMs && toTurnS >= kCallResumeMinToTurnS)
    {
      m_twentyCutInT.reset();
      if (m_twenty.GetState() == TwentyQuestions::State::Playing)
        SayTwenty(t, Twenty().Resume(t, Tier()));
    }
    return;
  }
  if (auto const reply = Twenty().Tick(t, Tier()))
    SayTwenty(t, *reply);
}

void Navigator::SayTwenty(double tMs, TwentyQuestions::Reply const & reply)
{
  Trace("twenty", {{"key", reply.key}, {"left", std::to_string(m_twenty.QuestionsLeft())},
                   {"over", reply.over ? "yes" : "no"}});
  Commentary c;
  c.t = tMs;
  c.category = Category::TwentyQuestions;
  c.text = reply.text;
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Roast;
  c.voiceSpeed = 1 + c.frustration / 250.0;
  if (m_turnId && m_lastDistToTurnM < GameHoldM(TwentyQuestions::kHoldNearManeuverM, LastTimingMps()) &&
      !m_arrived)
    m_heldTwentyLines.push_back(std::move(c));
  else
    // The driver is waiting for an answer; don't hold it up for the rewording model.
    m_gate.Push(std::move(c), false);
  if (m_onTwentyLog)
    m_onTwentyLog(m_twenty.Log());
}
Commentary Navigator::QuizLine(double tMs, std::string const & key, Vars vars)
{
  for (auto const & [k, v] : DriverVars())
    vars.emplace(k, v);
  double const r = m_config.rng ? m_config.rng() : 0;
  Commentary c;
  c.t = tMs;
  c.category = Category::PopQuiz;
  c.key = key;
  c.text = Fill(m_said.Pick(key, LinesFor(key, Tier()), r), vars);
  c.frustration = m_engine ? m_engine->Frustration() : 0;
  c.priority = Priority::Roast;
  c.voiceSpeed = 1 + c.frustration / 1000.0;
  return c;
}

bool Navigator::StartPopQuiz(double t, bool asked)
{
  // Road rules on car trips only; trivia anywhere.
  bool const car = InCar(m_travelMode);
  std::vector<std::string> ids;
  // Road rules from the country being driven: no right on red in the UK, no feet where it's metres.
  bool const metric = m_config.units == Units::Kmh;
  for (auto const & q : QuizQuestions())
    if ((car || !q.road) && QuizFits(q, m_country, metric))
      ids.push_back(q.id);
  if (ids.empty())
    return false;
  double const r = m_config.rng ? m_config.rng() : 0;
  QuizQuestion const * q = FindQuizQuestion(m_said.Pick("quiz.question", ids, r));
  if (!q)
    return false;
  std::vector<Commentary> lines;
  // "Pop quiz!" (or "Fine, brown noser" when asked), then the out-loud rule, then the question.
  lines.push_back(QuizLine(t, asked ? "quiz.asked" : "quiz.open"));
  lines.push_back(QuizLine(t, car ? "quiz.aloud" : "quiz.aloud.walk"));
  lines.push_back(QuizLine(t, "quiz.ask",
                           {{"question", q->question},
                            {"a", QuizChoiceText(q->choices[0])},
                            {"b", QuizChoiceText(q->choices[1])},
                            {"c", QuizChoiceText(q->choices[2])}}));
  double readMs = 0;
  for (auto const & c : lines)
    readMs += SpeakingMs(c.text, c.voiceSpeed) + 400;
  m_quiz.Start(*q, t, t + readMs + kQuizAnswerMs, !car);
  m_quizUnclear = 0;
  m_quizReasked = false;
  ++m_quizzes;
  m_lastQuizT = t;
  Trace("quiz", {{"event", "start"}, {"id", q->id}, {"asked", asked ? "yes" : "no"}});
  // The driver is about to answer; don't hold it up for the rewording model.
  for (auto & c : lines)
    m_gate.Push(std::move(c), false);
  return true;
}

void Navigator::TrackPopQuiz(Fix const & fix, RouteSnapshot const & route)
{
  double const t = fix.tMs;
  if (QuizAsking())
  {
    if (auto const outcome = m_quiz.Tick(t))
      SayQuizOutcome(t, *outcome);
    return;
  }
  if (!m_quizOn || m_quizzes >= kQuizzesPerTrip || QuietBitBusy() || !m_punchlines.empty())
    return;
  bool const steady = route.state == RouteState::OnRoute && !m_rerouting && route.turnId;
  if (!steady || t - m_tripStartT < kQuizAfterStartMs || t - m_lastQuizT < kQuizEveryMs ||
      t - m_lastQuietBitT < kQuizAfterQuietBitMs || t - std::max(m_lastSpokeT, m_lastRoadT) < kQuizSilenceMs)
    return;
  if (SecondsToTurn(route.distToTurnM, TimingMps(fix.speedMps)) < kQuizMinToTurnS)
    return;
  // One roll of the dice per stretch, so it stays a surprise.
  if (m_quizRolledFor == route.turnId)
    return;
  m_quizRolledFor = route.turnId;
  bool const firstEver = !m_said.Said("quiz.question");
  double const r = m_config.rng ? m_config.rng() : 0;
  if (!firstEver && r >= kQuizChance)
    return;
  StartPopQuiz(t, false);
}

void Navigator::SayQuizOutcome(double t, PopQuiz::Outcome outcome)
{
  QuizQuestion const & q = *m_quiz.Question();
  Vars vars = {{"answer", QuizChoiceText(q.choices[q.answer])}};
  std::string key;
  switch (outcome)
  {
  case PopQuiz::Outcome::Right:
  {
    int const brownies = m_said.Counter(kBrowniesKey) + 1;
    m_said.SetCounter(kBrowniesKey, brownies);
    vars["brownies"] = brownies == 1 ? std::string("one brownie point") : std::to_string(brownies) + " brownie points";
    key = brownies == 1 ? "quiz.right.first" : "quiz.right";
    break;
  }
  case PopQuiz::Outcome::Wrong:
    m_said.SetCounter("_quiz.wrong", m_said.Counter("_quiz.wrong") + 1);
    key = q.road ? "quiz.wrong.road" : "quiz.wrong";
    break;
  case PopQuiz::Outcome::GaveUp: key = "quiz.giveup"; break;
  case PopQuiz::Outcome::TimedOut: key = "quiz.timeout"; break;
  }
  Trace("quiz", {{"event", "over"}, {"id", q.id}, {"key", key}, {"picked", std::to_string(m_quiz.Picked())}});
  m_gate.Push(QuizLine(t, key, vars), false);
  if (outcome == PopQuiz::Outcome::Right && m_said.Counter(kBrowniesKey) >= kTeachersPetBrownies &&
      m_achievements.Unlock("teacherspet", t))
  {
    m_gate.Push(QuizLine(t, "badge.teacherspet"), false);
    if (m_onAchievements)
      m_onAchievements(m_achievements);
    m_achievements.ClearDirty();
  }
}

std::optional<Navigator::QuizCard> Navigator::PopQuizCard() const
{
  if (!m_engine || !m_quiz.Question() || !m_quiz.Showing(m_lastT))
    return std::nullopt;
  // Arrived mid-question: the quiz is off, nothing to show.
  if (m_arrived && m_quiz.Asking())
    return std::nullopt;
  QuizQuestion const & q = *m_quiz.Question();
  QuizCard card;
  card.question = q.question;
  for (size_t i = 0; i < 3; ++i)
    card.choices[i] = QuizChoiceText(q.choices[i]);
  card.picked = m_quiz.Picked();
  card.correct = m_quiz.Asking() ? -1 : q.answer;
  card.tappable = m_quiz.Tappable();
  card.brownies = m_said.Counter(kBrowniesKey);
  return card;
}

namespace
{
/// A question about the drive itself: "where do I turn?", "which exit?", "is it this lane?".
bool AsksAboutTheDrive(std::string const & spoken, bool aboutIt)
{
  std::string words = " ";
  for (unsigned char const ch : spoken)
    if (std::isalnum(ch))
      words += static_cast<char>(std::tolower(ch));
    else if (ch != '\'' && ch < 0x80)
      words += ' ';
  words += ' ';
  bool question = spoken.find('?') != std::string::npos;
  for (char const * w : {" where ", " when ", " which ", " how far ", " how long ", " do i ", " should i "})
    question = question || words.find(w) != std::string::npos;
  if (!question)
    return false;
  // "Do you have to go somewhere to get it?" asks about the thing, not the route; "do I go left?" doesn't.
  bool const looseGo = aboutIt && words.find(" left ") == std::string::npos &&
                       words.find(" right ") == std::string::npos && words.find(" straight ") == std::string::npos;
  for (char const * w : {" turn ", " turning ", " exit ", " lane ", " lanes ", " merge ", " ramp ", " get off ", " go ",
                         " going ", " way "})
    if (words.find(w) != std::string::npos && !(looseGo && (w == std::string(" go ") || w == std::string(" going "))))
      return true;
  return false;
}

/// A yes/no question about "it": "Can you use it on foot?", "Do you have to go to a gas station to get
/// it?". Mid-game, that's about the thing, whatever else it mentions.
bool AsksAboutIt(std::string const & spoken)
{
  std::string words = " ";
  for (unsigned char const ch : spoken)
    if (std::isalnum(ch))
      words += static_cast<char>(std::tolower(ch));
    else if (ch != '\'' && ch < 0x80)
      words += ' ';
  words += ' ';
  if (words.find(" it ") == std::string::npos)
    return false;
  if (spoken.find('?') != std::string::npos)
    return true;
  size_t const first = words.find_first_not_of(' ');
  if (first == std::string::npos)
    return false;
  std::string const lead = words.substr(first, words.find(' ', first) - first);
  for (char const * aux : {"is", "does", "do", "can", "could", "would", "will", "has", "have", "was", "did",
                           "should", "are"})
    if (lead == aux)
      return true;
  return false;
}
}  // namespace

bool QuizAnswerTakes(QuizQuestion const & q, std::string const & spoken)
{
  if (AsksToRepeatQuiz(spoken))
    return false;
  QuizHeard const heard = ParseQuizAnswer(q, spoken);
  if (heard.kind == QuizHeard::Kind::Unclear)
    return false;
  if (heard.named)
    return true;
  return !DriveComesFirst(spoken);
}

bool DriveComesFirst(std::string const & spoken, bool aboutIt)
{
  if (AsksAboutTheDrive(spoken, aboutIt))
    return true;
  for (auto const & command : ParseAppCommands(spoken))
    switch (command.action)
    {
    case AppAction::NextTurn:
    case AppAction::RepeatInstruction:
    case AppAction::CurrentRoad:
    case AppAction::SpeedLimit:
    case AppAction::TimeLeft:
    case AppAction::ArrivalTime:
    case AppAction::CancelRoute:
    case AppAction::Mute:
    case AppAction::ReportPolice:
    case AppAction::NavigateTo:
    case AppAction::DifferentRoute:
    case AppAction::TakeMeBack: return true;
    default: break;
    }
  return false;
}

bool Navigator::QuizTakes(std::string const & spoken) const
{
  return QuizAsking() && QuizAnswerTakes(*m_quiz.Question(), spoken);
}

bool Navigator::TwentyTakes(std::string const & spoken) const
{
  if (!m_twenty.Active())
    return false;
  // A question about "it" the game can't place is still the game's ("I didn't catch that"), not
  // walking directions; but only once playing, when "it" is the thing.
  bool const aboutIt = m_twenty.GetState() == TwentyQuestions::State::Playing && AsksAboutIt(spoken);
  if (ParseTwentyHeard(spoken).kind == TwentyHeard::Kind::Unclear && !aboutIt)
    return false;
  return !DriveComesFirst(spoken, aboutIt);
}

bool Navigator::QuizReask(double tMs, std::string const & spoken)
{
  if (!QuizAsking() || m_quizReasked || !AsksToRepeatQuiz(spoken))
    return false;
  m_lastT = std::max(m_lastT, tMs);
  m_quizReasked = true;
  QuizQuestion const & q = *m_quiz.Question();
  Commentary line = QuizLine(m_lastT, "quiz.ask",
                             {{"question", q.question},
                              {"a", QuizChoiceText(q.choices[0])},
                              {"b", QuizChoiceText(q.choices[1])},
                              {"c", QuizChoiceText(q.choices[2])}});
  m_quiz.ExtendAnswerBy(m_lastT + SpeakingMs(line.text, line.voiceSpeed) + 400 + kQuizAnswerMs);
  Trace("quiz", {{"event", "reask"}, {"id", q.id}});
  m_gate.Push(std::move(line), false);
  return true;
}

void Navigator::QuizTapped(double tMs, int choice)
{
  if (!QuizAsking() || !m_quiz.Tappable())
    return;
  m_lastT = std::max(m_lastT, tMs);
  if (auto const outcome = m_quiz.Answer(choice, m_lastT))
    SayQuizOutcome(m_lastT, *outcome);
}

}  // namespace backseat
