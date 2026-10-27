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

#include "platform/settings.hpp"

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
  if (heard.kind == Kind::RouteTo)
  {
    // "Take me to work": route there when the GPS knows where it is, otherwise search for it.
    MWMRoutePoint * to = [[MWMRoutePoint alloc] initWithPoint:mercator::FromLatLon(heard.lat, heard.lon)
                                                        title:@(heard.name.c_str())
                                                     subtitle:@""
                                                         type:MWMRoutePointTypeFinish
                                            intermediateIndex:0];
    if (![MWMRouter isRoutingActive])
      [MWMRouter removePoints];
    [MWMRouter buildToPoint:to bestRouter:NO];
  }
  else if (heard.kind == Kind::Search)
  {
    [[MapsAppDelegate theApp] searchText:@(heard.query.c_str())];
  }
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
    gToolHandler(text, ^(BOOL used) {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!used)
          Perform(GetFramework().GetRoutingManager().BackseatDriverSaid(text.UTF8String ?: ""));
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

+ (void)runTool:(NSString *)name argument:(NSString *)argument spoken:(NSString *)spoken
{
  Perform(GetFramework().GetRoutingManager().BackseatRunTool(name.UTF8String ?: "", argument.UTF8String ?: "",
                                                             spoken.UTF8String ?: ""));
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

static_assert(static_cast<NSInteger>(BackseatAdapter::SetupStep::Passenger) == MWMBackseatSetupStepPassenger);

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
