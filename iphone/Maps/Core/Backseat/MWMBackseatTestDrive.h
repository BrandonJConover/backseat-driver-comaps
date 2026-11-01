#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Debug builds only: when launched with `-BackseatTestDestination "lat,lon"`, builds a car
/// route from the current position to that point and starts navigation, so a simulator run
/// with a simulated drive can exercise Backseat Driver without anyone tapping the screen.
@interface MWMBackseatTestDrive : NSObject

+ (void)startIfRequested;

@end

NS_ASSUME_NONNULL_END
