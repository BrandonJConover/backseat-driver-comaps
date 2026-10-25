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

@end

NS_ASSUME_NONNULL_END
