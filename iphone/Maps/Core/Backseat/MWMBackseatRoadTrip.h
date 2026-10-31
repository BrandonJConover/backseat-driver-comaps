#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Feeds the road-trip remarks (libs/backseat/road_bits.hpp) what only the phone knows: whether it
/// has a data connection ("No signal out here"), and the weather where the car is ("Wet roads. Leave
/// extra room."). Weather comes from the US National Weather Service (api.weather.gov): free, no key,
/// no account, so it can never produce a bill. Outside the US it finds nothing and stays quiet.
NS_SWIFT_NAME(BackseatRoadTrip)
@interface MWMBackseatRoadTrip : NSObject

/// Call once at launch (next to where the drive log is set up). Safe to call again.
+ (void)start;

@end

NS_ASSUME_NONNULL_END
