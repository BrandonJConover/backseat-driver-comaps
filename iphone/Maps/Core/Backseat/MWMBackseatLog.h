#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// The drive log (libs/backseat/debug_log.hpp), for Swift: writes events from the voice layer and
/// exports the whole log for the share sheet. Everything stays on the phone until the driver shares it.
NS_SWIFT_NAME(BackseatLog)
@interface MWMBackseatLog : NSObject

/// One event, e.g. [MWMBackseatLog event:@"voice" fields:@{@"voice": @"system", @"why": @"deadline"}].
/// Fields are written in key order.
+ (void)event:(NSString *)event fields:(NSDictionary<NSString *, NSString *> *)fields NS_SWIFT_NAME(event(_:_:));
/// Like event:fields:, but the same event + key is written at most every 30 s (the rest are counted).
+ (void)throttledEvent:(NSString *)event
                   key:(NSString *)key
                fields:(NSDictionary<NSString *, NSString *> *)fields NS_SWIFT_NAME(throttled(_:key:_:));

/// "Keep a drive log" in Settings. On by default.
@property(class, nonatomic) BOOL enabled;

/// The whole log in one file in tmp, named like backseat-drive-log-2026-10-27-1242.jsonl, with a
/// header line (app version, iOS, device) plus `extra` (voice state: Kokoro ready, crash guard
/// tripped, chosen voice). Nil if it couldn't be written.
+ (nullable NSURL *)exportLogWithExtra:(NSDictionary<NSString *, NSString *> *)extra NS_SWIFT_NAME(exportLog(extra:));
/// Deletes every log file.
+ (void)clear;

@end

NS_ASSUME_NONNULL_END
