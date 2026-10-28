#import "MWMBackseatRewriter.h"
#import "MWMRoutePoint+CPP.h"
#import "MWMRouter.h"
#import "MWMRoutingOptions.h"
#import "MWMTextToSpeech.h"
#import "MapViewController.h"
#import "MapsAppDelegate.h"
#import "SwiftBridge.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreApi/MWMFrameworkHelper.h>

#include <CoreApi/Framework.h>

#include "backseat/rewriter.hpp"
#include "backseat/voice_command.hpp"

#include "geometry/mercator.hpp"

#include "platform/distance.hpp"
#include "platform/settings.hpp"

#include "routing/following_info.hpp"

#include <atomic>
#include <memory>

namespace
{
class BlockRewriter : public backseat::LineRewriter
{
public:
  explicit BlockRewriter(MWMBackseatRewriteHandler handler) : m_handler([handler copy]) {}

  bool Available() const override { return m_available; }

  void Rewrite(backseat::RewriteRequest const & request, Done done) override
  {
    NSString * instructions = @(backseat::RewriteInstructions().c_str());
    NSString * prompt = @(backseat::RewritePrompt(request).c_str());
    auto shared = std::make_shared<Done>(std::move(done));
    MWMBackseatRewriteHandler handler = m_handler;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      handler(instructions, prompt, ^(NSString * text) { (*shared)(text.UTF8String ?: ""); });
    });
  }

  void SetAvailable(bool available) { m_available = available; }

private:
  MWMBackseatRewriteHandler m_handler;
  std::atomic<bool> m_available{false};
};

std::shared_ptr<BlockRewriter> & Installed()
{
  static std::shared_ptr<BlockRewriter> rewriter;
  return rewriter;
}

MWMBackseatToolHandler gToolHandler;
BOOL gToolsAvailable = NO;

/// Says a line in `voiceId` (the picked voice when empty), in turn with the other replies.
void SayReply(std::string const & text, std::string const & voiceId)
{
  if (text.empty())
    return;
  NSString * line = @(text.c_str());
  [BackseatKokoroVoice.shared sayReply:line
                               voiceID:voiceId.empty() ? nil : @(voiceId.c_str())
                              fallback:^{ [[MWMTextToSpeech tts] play:line]; }];
}

void SetRoutingOption(void (^change)(MWMRoutingOptions * options))
{
  MWMRoutingOptions * options = [[MWMRoutingOptions alloc] init];
  change(options);
  [options save];
  [NSNotificationCenter.defaultCenter postNotificationName:@"RoutingOptionsChanged" object:nil];
  if ([MWMRouter isRoutingActive])
    [MWMRouter rebuildWithBestRouter:NO];
}

void ShowRouteOverview()
{
  auto & framework = GetFramework();
  auto const & polyline = framework.GetRoutingManager().GetRoutePolyline();
  if (!polyline.IsValid())
    return;
  auto rect = polyline.GetPolyline().GetLimitRect();
  rect.Scale(1.2);
  [MWMFrameworkHelper stopLocationFollow];
  framework.ShowRect(rect, -1 /* maxScale */, true /* animation */, true /* useVisibleViewport */);
}

void Perform(backseat::AppCommand const & command)
{
  using backseat::AppAction;
  switch (command.action)
  {
  case AppAction::CancelRoute: [MWMRouter stopRouting]; break;
  case AppAction::Mute: [MWMTextToSpeech setTTSEnabled:NO]; break;
  case AppAction::Unmute: [MWMTextToSpeech setTTSEnabled:YES]; break;
  case AppAction::AvoidTolls: SetRoutingOption(^(MWMRoutingOptions * o) { o.avoidToll = YES; }); break;
  case AppAction::AllowTolls: SetRoutingOption(^(MWMRoutingOptions * o) { o.avoidToll = NO; }); break;
  case AppAction::AvoidHighways: SetRoutingOption(^(MWMRoutingOptions * o) { o.avoidMotorway = YES; }); break;
  case AppAction::AllowHighways: SetRoutingOption(^(MWMRoutingOptions * o) { o.avoidMotorway = NO; }); break;
  case AppAction::ChangeVoice:
    if (NSString * voice = [BackseatKokoroVoice voiceIDMatching:@(command.argument.c_str())])
      [BackseatKokoroVoice.shared switchVoiceTo:voice];
    else
      [[MapViewController sharedController] openSettings];
    break;
  case AppAction::ZoomIn: [MWMFrameworkHelper zoomMap:MWMZoomModeIn]; break;
  case AppAction::ZoomOut: [MWMFrameworkHelper zoomMap:MWMZoomModeOut]; break;
  case AppAction::Recenter: GetFramework().SwitchMyPositionNextMode(); break;
  case AppAction::RouteOverview: ShowRouteOverview(); break;
  case AppAction::OpenSettings: [[MapViewController sharedController] openSettings]; break;
  case AppAction::StopListening: [BackseatListener.shared pauseForRoute]; break;
  case AppAction::ListCommands: [BackseatCommandsSheet present]; break;
  // The navigator handles these itself.
  case AppAction::NavigateTo:
  case AppAction::RepeatInstruction:
  case AppAction::TimeLeft:
  case AppAction::ReportPolice:
  case AppAction::PlayPlateGame:
  case AppAction::ListVoices:
  case AppAction::Count: break;
  }
}

/// Carries out what the navigator made of the driver's words: the reply first (in the voice that's
/// leaving, before any mute), then the route or search, the actions, and other voices' lines.
void Perform(BackseatAdapter::HeardCommand const & heard)
{
  SayReply(heard.reply, {});
  using Kind = BackseatAdapter::HeardCommand::Kind;
  // "Take me to work": route there when the GPS knows where it is, otherwise search for it and
  // read out what it finds.
  if (heard.kind == Kind::RouteTo)
    [MWMBackseatRewriter routeTo:CLLocationCoordinate2DMake(heard.lat, heard.lon) title:@(heard.name.c_str())];
  else if (heard.kind == Kind::Search)
    [BackseatVoiceSearch start:@(heard.query.c_str())];
  for (auto const & command : heard.actions)
    Perform(command);
  for (auto const & line : heard.voiceLines)
    SayReply(line.text, line.voiceId);
}
}  // namespace

@implementation MWMBackseatRewriter

+ (void)installWithHandler:(MWMBackseatRewriteHandler)handler
{
  auto & rewriter = Installed();
  rewriter = std::make_shared<BlockRewriter>(handler);
  GetFramework().GetRoutingManager().SetBackseatRewriter(rewriter);
}

+ (void)setAvailable:(BOOL)available
{
  if (auto const & rewriter = Installed())
    rewriter->SetAvailable(available);
}

+ (void)installMusicCheck
{
  GetFramework().GetRoutingManager().SetBackseatMusicCheck(
      [] { return static_cast<bool>(AVAudioSession.sharedInstance.isOtherAudioPlaying); });
}

+ (void)driverSaid:(NSString *)text
{
  // With the on-device model, it picks the app action; what it can't place goes to the keywords.
  if (gToolHandler && gToolsAvailable)
  {
    gToolHandler(text, ^(BOOL used, NSString * chat) {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (used)
          return;
        auto const heard = GetFramework().GetRoutingManager().BackseatDriverSaid(text.UTF8String ?: "");
        // Nothing the GPS listens for (no command, answer or place): the model's chat answers instead.
        bool const nothing = heard.kind == BackseatAdapter::HeardCommand::Kind::None && heard.actions.empty() &&
                             heard.reply.empty() && heard.voiceLines.empty();
        if (nothing)
          SayReply(chat.UTF8String ?: "", {});
        else
          Perform(heard);
      });
    });
    return;
  }
  Perform(GetFramework().GetRoutingManager().BackseatDriverSaid(text.UTF8String ?: ""));
}

+ (void)installToolHandler:(MWMBackseatToolHandler)handler
{
  gToolHandler = [handler copy];
}

+ (void)setToolsAvailable:(BOOL)available
{
  gToolsAvailable = available;
}

+ (NSArray<NSString *> *)toolNames
{
  NSMutableArray<NSString *> * names = [NSMutableArray array];
  for (auto const & tool : backseat::AppTools())
    [names addObject:@(tool.name)];
  return names;
}

+ (NSString *)toolGuide
{
  NSMutableString * guide = [NSMutableString string];
  for (auto const & tool : backseat::AppTools())
  {
    [guide appendFormat:@"- %s: %s", tool.name, tool.description];
    if (tool.argument)
      [guide appendFormat:@" Argument: %s.", tool.argument];
    [guide appendString:@"\n"];
  }
  return guide;
}

+ (void)routeTo:(CLLocationCoordinate2D)coordinate title:(NSString *)title
{
  MWMRoutePoint * to = [[MWMRoutePoint alloc] initWithPoint:mercator::FromLatLon(coordinate.latitude, coordinate.longitude)
                                                      title:title
                                                   subtitle:@""
                                                       type:MWMRoutePointTypeFinish
                                          intermediateIndex:0];
  if (![MWMRouter isRoutingActive])
    [MWMRouter removePoints];
  // Said or tapped from the "Where to?" card: drive there now, no preview.
  [MWMRouter startWhenReady];
  [MWMRouter buildToPoint:to bestRouter:NO];
}

+ (NSString *)speakSearchResults:(NSString *)query
                           names:(NSArray<NSString *> *)names
                       distances:(NSArray<NSNumber *> *)distances
                            pick:(NSInteger *)pick
                            none:(BOOL *)none
{
  std::vector<BackseatAdapter::SearchResult> results;
  for (NSUInteger i = 0; i < names.count && i < distances.count; ++i)
    results.push_back({names[i].UTF8String, distances[i].doubleValue});
  auto const speech = GetFramework().GetRoutingManager().BackseatSpeakSearchResults(query.UTF8String ?: "",
                                                                                    std::move(results));
  if (pick)
    *pick = speech.pick ? static_cast<NSInteger>(*speech.pick) : -1;
  if (none)
    *none = speech.none;
  return @(speech.line.c_str());
}

+ (NSString *)pickSearchResult:(NSString *)answer index:(NSInteger *)index
{
  auto const [picked, line] = GetFramework().GetRoutingManager().BackseatPickSearchResult(answer.UTF8String ?: "");
  *index = picked ? static_cast<NSInteger>(*picked) : -1;
  return @(line.c_str());
}

+ (NSArray<NSString *> *)commandDescriptions
{
  NSMutableArray<NSString *> * descriptions = [NSMutableArray array];
  for (auto const & tool : backseat::AppTools())
    [descriptions addObject:@(tool.description)];
  return descriptions;
}

+ (void)runTool:(NSString *)name argument:(NSString *)argument spoken:(NSString *)spoken
{
  Perform(GetFramework().GetRoutingManager().BackseatRunTool(name.UTF8String ?: "", argument.UTF8String ?: "",
                                                             spoken.UTF8String ?: ""));
}

+ (double)metersToNextTurn
{
  if (![MWMRouter isOnRoute])
    return -1;
  routing::FollowingInfo info;
  GetFramework().GetRoutingManager().GetRouteFollowingInfo(info);
  if (!info.IsValid())
    return -1;
  return info.m_distToTurn.To(platform::Distance::Units::Meters).GetDistance();
}

+ (double)speedLimitMps
{
  if (![MWMRouter isOnRoute])
    return -1;
  routing::FollowingInfo info;
  GetFramework().GetRoutingManager().GetRouteFollowingInfo(info);
  return info.IsValid() ? info.m_speedLimitMps : -1;
}

+ (void)shareVoices
{
  std::vector<backseat::VoiceOption> voices;
  NSArray<NSString *> * ids = BackseatKokoroVoice.voiceIDs;
  NSArray<NSString *> * names = BackseatKokoroVoice.voiceSpokenNames;
  for (NSUInteger i = 0; i < ids.count && i < names.count; ++i)
    voices.push_back({ids[i].UTF8String, names[i].UTF8String});
  GetFramework().GetRoutingManager().SetBackseatVoices(std::move(voices));
}

#pragma mark - Who's in the car

+ (NSString *)personalityString:(char const *)key fallback:(NSString *)fallback
{
  std::string value;
  if (!settings::Get(key, value) || value.empty())
    return fallback;
  return @(value.c_str());
}

+ (void)setPersonalityString:(NSString *)value forKey:(char const *)key
{
  settings::Set(key, std::string(value.UTF8String ?: ""));
  GetFramework().GetRoutingManager().ReloadBackseatPersonality();
}

+ (NSString *)partner
{
  return [self personalityString:BackseatAdapter::kPartnerKey fallback:@""];
}

+ (void)setPartner:(NSString *)partner
{
  [self setPersonalityString:[partner stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]
                      forKey:BackseatAdapter::kPartnerKey];
}

+ (NSString *)partnerPronouns
{
  return [self personalityString:BackseatAdapter::kPartnerPronounsKey fallback:@"they"];
}

+ (void)setPartnerPronouns:(NSString *)pronouns
{
  [self setPersonalityString:pronouns forKey:BackseatAdapter::kPartnerPronounsKey];
}

+ (NSString *)driverPronouns
{
  return [self personalityString:BackseatAdapter::kDriverPronounsKey fallback:@"they"];
}

+ (void)setDriverPronouns:(NSString *)pronouns
{
  [self setPersonalityString:pronouns forKey:BackseatAdapter::kDriverPronounsKey];
}

+ (NSString *)driverName
{
  return [self personalityString:BackseatAdapter::kDriverNameKey fallback:@""];
}

+ (void)setDriverName:(NSString *)driverName
{
  [self setPersonalityString:[driverName stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]
                      forKey:BackseatAdapter::kDriverNameKey];
}

+ (NSString *)driverAge
{
  return [self personalityString:BackseatAdapter::kDriverAgeKey fallback:@""];
}

+ (void)setDriverAge:(NSString *)driverAge
{
  [self setPersonalityString:[driverAge stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]
                      forKey:BackseatAdapter::kDriverAgeKey];
}

+ (void)beginProfileEdit
{
  GetFramework().GetRoutingManager().BackseatBeginProfileEdit();
}

+ (NSString *)endProfileEdit
{
  return @(GetFramework().GetRoutingManager().BackseatEndProfileEdit().c_str());
}

+ (NSString *)setupLine:(MWMBackseatSetupStep)step voiceName:(NSString *)voiceName
{
  auto const line = GetFramework().GetRoutingManager().BackseatSetupLine(
      static_cast<BackseatAdapter::SetupStep>(step), voiceName.UTF8String ?: "");
  return @(line.c_str());
}

+ (BOOL)setupHeard:(MWMBackseatSetupStep)step answer:(NSString *)answer
{
  return GetFramework().GetRoutingManager().BackseatSetupHeard(static_cast<BackseatAdapter::SetupStep>(step),
                                                               answer.UTF8String ?: "");
}

+ (NSString *)askWhereTo
{
  return @(GetFramework().GetRoutingManager().BackseatAskWhereTo().c_str());
}

+ (NSString *)noAnswerToWhereTo
{
  return @(GetFramework().GetRoutingManager().BackseatNoAnswerToWhereTo().c_str());
}

+ (NSString *)voiceRecovered
{
  return @(GetFramework().GetRoutingManager().BackseatVoiceRecovered().c_str());
}

static_assert(static_cast<NSInteger>(BackseatAdapter::SetupStep::Passenger) == MWMBackseatSetupStepPassenger);

+ (NSArray<NSDictionary<NSString *, id> *> *)pastRoutes:(NSInteger)max
{
  NSMutableArray<NSDictionary<NSString *, id> *> * routes = [NSMutableArray array];
  for (auto const & r : GetFramework().GetRoutingManager().BackseatPastRoutes(static_cast<size_t>(MAX(max, 0))))
    [routes addObject:@{
      @"label": @(r.label.c_str()),
      @"destination": @(r.destination.c_str()),
      @"lat": @(r.lat),
      @"lon": @(r.lon),
      @"trips": @(r.trips),
      @"minutes": @(r.usualMinutes),
    }];
  return routes;
}

+ (BOOL)remember:(NSString *)fact topic:(NSString *)topic
{
  return GetFramework().GetRoutingManager().BackseatRemember(topic.UTF8String ?: "", fact.UTF8String ?: "");
}

+ (BOOL)forget:(NSString *)topic
{
  return GetFramework().GetRoutingManager().BackseatForget(topic.UTF8String ?: "");
}

+ (NSArray<NSDictionary<NSString *, id> *> *)memories
{
  NSMutableArray<NSDictionary<NSString *, id> *> * memories = [NSMutableArray array];
  for (auto const & m : GetFramework().GetRoutingManager().BackseatMemories())
    [memories addObject:@{
      @"topic": @(m.topic.c_str()),
      @"fact": @(m.fact.c_str()),
      @"learnedAt": [NSDate dateWithTimeIntervalSince1970:m.learnedAt / 1000.0],
    }];
  return memories;
}

+ (NSString *)chatInstructions:(NSString *)voiceName
{
  return @(GetFramework().GetRoutingManager().BackseatChatInstructions(voiceName.UTF8String ?: "").c_str());
}

+ (NSString *)fileQuestion
{
  return @(GetFramework().GetRoutingManager().BackseatFileQuestion().c_str());
}

+ (NSString *)fileAnswer:(NSString *)spoken
{
  return @(GetFramework().GetRoutingManager().BackseatFileAnswer(spoken.UTF8String ?: "").c_str());
}

+ (NSString *)nicenessMoved
{
  return @(GetFramework().GetRoutingManager().BackseatNicenessMoved().c_str());
}

+ (NSString *)mapBrowsed
{
  return @(GetFramework().GetRoutingManager().BackseatMapBrowsed().c_str());
}

+ (NSString *)typingDestination
{
  return @(GetFramework().GetRoutingManager().BackseatTypingDestination().c_str());
}

+ (NSString *)roughRoad
{
  return @(GetFramework().GetRoutingManager().BackseatRoughRoad().c_str());
}

+ (NSString *)nicenessSlidBack
{
  return @(GetFramework().GetRoutingManager().BackseatNicenessSlidBack().c_str());
}

+ (NSString *)arrivalSurvey
{
  return @(GetFramework().GetRoutingManager().BackseatArrivalSurvey().c_str());
}

+ (NSString *)surveyAnswer:(NSString *)spoken
{
  return @(GetFramework().GetRoutingManager().BackseatSurveyAnswer(spoken.UTF8String ?: "").c_str());
}

#pragma mark - Mileage

+ (BOOL)mileageOn
{
  return GetFramework().GetRoutingManager().BackseatMileageOn();
}

+ (void)setMileageOn:(BOOL)on
{
  settings::Set(BackseatAdapter::kMileageOnKey, static_cast<bool>(on));
}

+ (BOOL)mileageTalk
{
  bool on = true;
  settings::TryGet(BackseatAdapter::kMileageTalkKey, on);
  return on;
}

+ (void)setMileageTalk:(BOOL)on
{
  settings::Set(BackseatAdapter::kMileageTalkKey, static_cast<bool>(on));
}

+ (BOOL)homeIsWorkplace
{
  bool on = false;
  settings::TryGet(BackseatAdapter::kHomeIsWorkplaceKey, on);
  return on;
}

+ (void)setHomeIsWorkplace:(BOOL)on
{
  settings::Set(BackseatAdapter::kHomeIsWorkplaceKey, static_cast<bool>(on));
}

+ (NSString *)driveSample:(double)lat
                      lon:(double)lon
                    speed:(double)speedMps
                 accuracy:(double)accuracyM
                 activity:(NSInteger)activity
                     time:(double)timeMs
{
  return @(GetFramework()
               .GetRoutingManager()
               .BackseatDriveSample(lat, lon, speedMps, accuracyM, static_cast<int>(activity), timeMs)
               .c_str());
}

+ (NSString *)mileageFinish
{
  return @(GetFramework().GetRoutingManager().BackseatMileageFinish().c_str());
}

+ (NSDictionary<NSString *, id> *)mileageSummary:(NSInteger)year
{
  auto const y = GetFramework().GetRoutingManager().BackseatMileageSummary(static_cast<int>(year));
  NSMutableArray<NSNumber *> * unknown = [NSMutableArray array];
  for (auto const i : y.unknown)
    [unknown addObject:@(i)];
  NSMutableArray<NSNumber *> * flagged = [NSMutableArray array];
  for (auto const i : y.flagged)
    [flagged addObject:@(i)];
  return @{
    @"businessMiles": @(y.businessMiles),
    @"personalMiles": @(y.personalMiles),
    @"commuteMiles": @(y.commuteMiles),
    @"unknownMiles": @(y.unknownMiles),
    @"businessDrives": @(y.businessDrives),
    @"personalDrives": @(y.personalDrives),
    @"unknownDrives": @(y.unknownDrives),
    @"ratePerMile": @(y.ratePerMile),
    @"estimatedDeduction": @(y.estimatedDeduction),
    @"unknown": unknown,
    @"flagged": flagged,
  };
}

+ (NSArray<NSDictionary<NSString *, id> *> *)mileageLog
{
  NSMutableArray<NSDictionary<NSString *, id> *> * log = [NSMutableArray array];
  auto const drives = GetFramework().GetRoutingManager().BackseatMileageLog();
  for (size_t i = 0; i < drives.size(); ++i)
  {
    auto const & d = drives[i];
    [log addObject:@{
      @"index": @(i),
      @"startedAt": [NSDate dateWithTimeIntervalSince1970:d.drive.startedAt / 1000.0],
      @"fromName": @(d.fromName.c_str()),
      @"toName": @(d.toName.c_str()),
      @"fromLat": @(d.drive.from.lat),
      @"fromLon": @(d.drive.from.lon),
      @"toLat": @(d.drive.to.lat),
      @"toLon": @(d.drive.to.lon),
      @"miles": @(d.drive.Miles()),
      @"bucket": @(backseat::BucketName(d.bucket)),
      @"why": @(backseat::WhyName(d.why)),
      @"outOfState": @((d.flags & backseat::kOutOfState) != 0),
      @"farFromHome": @((d.flags & backseat::kFarFromHome) != 0),
      @"newPlace": @((d.flags & backseat::kNewPlace) != 0),
      @"purpose": @(d.purpose.c_str()),
    }];
  }
  return log;
}

+ (NSString *)mileageCsv:(NSInteger)year
{
  return @(GetFramework().GetRoutingManager().BackseatMileageCsv(static_cast<int>(year)).c_str());
}

+ (NSInteger)similarDrives:(NSInteger)index
{
  return static_cast<NSInteger>(GetFramework().GetRoutingManager().BackseatSimilarDrives(static_cast<size_t>(index)));
}

+ (NSString *)reassignDrive:(NSInteger)index bucket:(NSInteger)bucket similar:(BOOL)similar purpose:(NSString *)purpose
{
  auto const b = static_cast<backseat::Bucket>(std::clamp<NSInteger>(bucket, 0, 2));
  return @(GetFramework()
               .GetRoutingManager()
               .BackseatReassignDrive(static_cast<size_t>(index), b, similar, purpose.UTF8String ?: "")
               .c_str());
}

+ (void)tagPlace:(NSString *)name lat:(double)lat lon:(double)lon role:(NSInteger)role
{
  GetFramework().GetRoutingManager().BackseatTagPlace(name.UTF8String ?: "", lat, lon,
                                                      static_cast<backseat::PlaceRole>(std::clamp<NSInteger>(role, 0, 3)));
}

+ (NSArray<NSDictionary<NSString *, id> *> *)taggedPlaces
{
  NSMutableArray<NSDictionary<NSString *, id> *> * places = [NSMutableArray array];
  for (auto const & p : GetFramework().GetRoutingManager().BackseatTaggedPlaces())
    [places addObject:@{@"name": @(p.name.c_str()), @"lat": @(p.at.lat), @"lon": @(p.at.lon), @"role": @(static_cast<int>(p.role))}];
  return places;
}

+ (NSString *)mileageYearEnd
{
  return @(GetFramework().GetRoutingManager().BackseatMileageYearEnd().c_str());
}

+ (NSString *)mileageTurnedOn
{
  return @(GetFramework().GetRoutingManager().BackseatMileageTurnedOn().c_str());
}

+ (BOOL)mileageListening
{
  return GetFramework().GetRoutingManager().BackseatMileageListening();
}

+ (NSString *)mileageAnswer:(NSString *)spoken
{
  return @(GetFramework().GetRoutingManager().BackseatMileageAnswer(spoken.UTF8String).c_str());
}

+ (NSString *)businessOwner
{
  return @(GetFramework().GetRoutingManager().BackseatBusinessOwner().c_str());
}

+ (void)setBusinessOwner:(NSString *)businessOwner
{
  GetFramework().GetRoutingManager().BackseatSetBusinessOwner(businessOwner.UTF8String);
}

+ (NSString *)takeSketchVerdict
{
  return @(GetFramework().GetRoutingManager().BackseatTakeSketchVerdict().c_str());
}

+ (NSString *)sketchCleared
{
  return @(GetFramework().GetRoutingManager().BackseatSketchCleared().c_str());
}

+ (NSArray<NSArray<NSString *> *> *)learnedFacts
{
  NSMutableArray<NSArray<NSString *> *> * facts = [NSMutableArray array];
  for (auto const & [label, value] : GetFramework().GetRoutingManager().BackseatLearnedFacts())
    [facts addObject:@[@(label.c_str()), @(value.c_str())]];
  return facts;
}

+ (BOOL)carHasAutopilot
{
  bool autopilot = false;
  return settings::Get(BackseatAdapter::kAutopilotKey, autopilot) && autopilot;
}

+ (void)setCarHasAutopilot:(BOOL)carHasAutopilot
{
  settings::Set(BackseatAdapter::kAutopilotKey, static_cast<bool>(carHasAutopilot));
  GetFramework().GetRoutingManager().ReloadBackseatPersonality();
}

@end
