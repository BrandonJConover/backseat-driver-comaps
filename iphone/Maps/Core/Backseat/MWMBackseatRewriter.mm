#import "MWMBackseatRewriter.h"
#import "MWMRoutePoint+CPP.h"
#import "MWMRouter.h"
#import "MapsAppDelegate.h"

#import <AVFoundation/AVFoundation.h>

#include <CoreApi/Framework.h>

#include "backseat/rewriter.hpp"

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
  // "Take me to work": route there when the GPS knows where it is, otherwise search for it.
  auto const heard = GetFramework().GetRoutingManager().BackseatDriverSaid(text.UTF8String ?: "");
  using Kind = BackseatAdapter::HeardCommand::Kind;
  if (heard.kind == Kind::RouteTo)
  {
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
