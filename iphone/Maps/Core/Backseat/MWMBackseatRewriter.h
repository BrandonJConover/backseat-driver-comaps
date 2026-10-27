#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef void (^MWMBackseatRewriteCompletion)(NSString * text);
typedef void (^MWMBackseatRewriteHandler)(NSString * instructions, NSString * prompt,
                                          MWMBackseatRewriteCompletion completion);

/// Hands Backseat Driver's roasts to an on-device language model for rewording. The handler
/// runs off the main thread and may answer late or never; the canned line covers for it.
@interface MWMBackseatRewriter : NSObject

+ (void)installWithHandler:(MWMBackseatRewriteHandler)handler;
+ (void)setAvailable:(BOOL)available;
/// Something the driver said out loud (speech recognition), e.g. "I see a Texas plate". Main thread.
+ (void)driverSaid:(NSString *)text;
/// Lets the GPS see whether other audio (music, podcasts) is playing.
+ (void)installMusicCheck;

/// Who's in the car, for the roasts. Pronouns are "they", "he" or "she". Setting any of these
/// reloads the navigator's personality at once.
@property(class, nonatomic, copy) NSString * partner;
@property(class, nonatomic, copy) NSString * partnerPronouns;
@property(class, nonatomic, copy) NSString * driverPronouns;
@property(class, nonatomic) BOOL carHasAutopilot;

@end

NS_ASSUME_NONNULL_END
