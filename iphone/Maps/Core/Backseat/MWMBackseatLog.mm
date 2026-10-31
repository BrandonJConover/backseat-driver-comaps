#import "MWMBackseatLog.h"

#import <UIKit/UIKit.h>
#include <sys/utsname.h>

#include <CoreApi/Framework.h>

#include "backseat/debug_log.hpp"

#include "platform/platform.hpp"

namespace
{
backseat::LogFields ToFields(NSDictionary<NSString *, NSString *> * dict)
{
  backseat::LogFields fields;
  for (NSString * key in [dict.allKeys sortedArrayUsingSelector:@selector(compare:)])
    fields.emplace_back(key.UTF8String, [dict[key] description].UTF8String);
  return fields;
}

NSString * DeviceModel()
{
  struct utsname info;
  uname(&info);
  return @(info.machine);  // "iPhone16,1" for an iPhone 15 Pro
}
}  // namespace

@implementation MWMBackseatLog

+ (void)event:(NSString *)event fields:(NSDictionary<NSString *, NSString *> *)fields
{
  backseat::Trace(event.UTF8String, ToFields(fields));
}

+ (void)throttledEvent:(NSString *)event key:(NSString *)key fields:(NSDictionary<NSString *, NSString *> *)fields
{
  backseat::TraceThrottled(event.UTF8String, key.UTF8String, ToFields(fields));
}

+ (void)initialize
{
  if (self != [MWMBackseatLog class])
    return;
  // One time: an earlier build could leave the log stored as off without the driver touching the
  // toggle. Turn it back on once; from then on the toggle is the driver's.
  NSString * const migrated = @"BackseatDebugLogMigrated";
  if (![NSUserDefaults.standardUserDefaults boolForKey:migrated])
  {
    GetFramework().GetRoutingManager().GetBackseat().SetDebugLogEnabled(true);
    [NSUserDefaults.standardUserDefaults setBool:YES forKey:migrated];
  }
}

+ (NSString *)directory
{
  return @(GetPlatform().SettingsDir().c_str());
}

+ (BOOL)enabled
{
  return GetFramework().GetRoutingManager().GetBackseat().DebugLogEnabled();
}

+ (void)setEnabled:(BOOL)enabled
{
  GetFramework().GetRoutingManager().GetBackseat().SetDebugLogEnabled(enabled);
}

+ (nullable NSURL *)exportLogWithExtra:(NSDictionary<NSString *, NSString *> *)extra
{
  NSDictionary * info = NSBundle.mainBundle.infoDictionary;
  NSString * version = [NSString stringWithFormat:@"%@ (%@)", info[@"CFBundleShortVersionString"] ?: @"?",
                                                  info[@"CFBundleVersion"] ?: @"?"];
  // Plain facts only: nothing here identifies the driver beyond what the log already holds.
  backseat::LogFields header = {
      {"app", version.UTF8String},
      {"ios", UIDevice.currentDevice.systemVersion.UTF8String},
      {"device", DeviceModel().UTF8String},
      {"locale", NSLocale.currentLocale.localeIdentifier.UTF8String},
      {"tz", NSTimeZone.localTimeZone.name.UTF8String},
  };
  for (auto & field : ToFields(extra))
    header.push_back(std::move(field));

  NSDateFormatter * fmt = [NSDateFormatter new];
  fmt.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
  fmt.dateFormat = @"yyyy-MM-dd-HHmm";
  NSString * name = [NSString stringWithFormat:@"backseat-drive-log-%@.jsonl", [fmt stringFromDate:NSDate.date]];
  NSURL * url = [NSFileManager.defaultManager.temporaryDirectory URLByAppendingPathComponent:name];

  if (!GetFramework().GetRoutingManager().GetBackseat().ExportDebugLog(url.path.UTF8String, header))
    return nil;
  return url;
}

+ (void)clear
{
  GetFramework().GetRoutingManager().GetBackseat().ClearDebugLog();
}

@end
