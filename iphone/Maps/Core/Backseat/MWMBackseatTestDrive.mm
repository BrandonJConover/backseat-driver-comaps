#import "MWMBackseatTestDrive.h"

#import "MWMLocationManager.h"
#import "MWMRouter.h"

#include "geometry/mercator.hpp"

@implementation MWMBackseatTestDrive

+ (void)startIfRequested
{
#ifdef DEBUG
  NSString * destination = [NSUserDefaults.standardUserDefaults stringForKey:@"BackseatTestDestination"];
  NSArray<NSString *> * parts = [destination componentsSeparatedByString:@","];
  if (parts.count != 2)
    return;
  double const lat = parts[0].doubleValue;
  double const lon = parts[1].doubleValue;
  __block BOOL requested = NO;
  __block int ticks = 0;
  [NSTimer scheduledTimerWithTimeInterval:2 repeats:YES block:^(NSTimer * timer) {
    ++ticks;
    if (![MWMLocationManager lastLocation])
    {
      NSLog(@"Backseat test drive: waiting for a location fix");
      return;
    }
    if (!requested)
    {
      requested = YES;
      [MWMRouter setType:MWMRouterTypeVehicle];
      m2::PointD const pt = mercator::FromLatLon(lat, lon);
      MWMRoutePoint * finish = [[MWMRoutePoint alloc] initWithCGPoint:CGPointMake(pt.x, pt.y)
                                                                title:@"Grandma's"
                                                             subtitle:@""
                                                                 type:MWMRoutePointTypeFinish
                                                    intermediateIndex:0];
      NSLog(@"Backseat test drive: building a route to %f,%f", lat, lon);
      [MWMRouter buildToPoint:finish bestRouter:NO];
      return;
    }
    if ([MWMRouter isRouteBuilt] && ![MWMRouter isOnRoute])
    {
      NSLog(@"Backseat test drive: route built, starting navigation");
      [MWMRouter startRouting];
      [timer invalidate];
      return;
    }
    if (ticks % 5 == 0)
      NSLog(@"Backseat test drive: still no route (active=%d built=%d)", [MWMRouter isRoutingActive],
            [MWMRouter isRouteBuilt]);
  }];
#endif
}

@end
