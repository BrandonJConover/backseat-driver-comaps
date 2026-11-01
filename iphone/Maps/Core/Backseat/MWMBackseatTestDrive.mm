#import "MWMBackseatTestDrive.h"

#import "MWMAlertViewController.h"
#import "MWMLocationManager.h"
#import "MWMRouter.h"
#import "MWMSettings.h"

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
  // A fresh install asks to accept the routing disclaimer before navigating; accept it here.
  [MWMSettings setRoutingDisclaimerApproved];
  __block int fixTicks = 0;
  __block int sinceRequest = -1;
  __block int attempts = 0;
  [NSTimer scheduledTimerWithTimeInterval:2 repeats:YES block:^(NSTimer * timer) {
    if (![MWMLocationManager lastLocation])
    {
      NSLog(@"Backseat test drive: waiting for a location fix");
      return;
    }
    // The core gets the fix a moment after the location manager does; building before that
    // fails with "Check GPS signal". Give it a few seconds.
    if (++fixTicks < 3)
      return;
    if ([MWMRouter isRouteBuilt] && ![MWMRouter isOnRoute])
    {
      NSLog(@"Backseat test drive: route built, starting navigation");
      [MWMRouter startRouting];
      [timer invalidate];
      return;
    }
    if (sinceRequest >= 0 && ++sinceRequest < 5)
      return;
    if (attempts == 5)
    {
      NSLog(@"Backseat test drive: giving up, no route after %d attempts (active=%d built=%d)", attempts,
            [MWMRouter isRoutingActive], [MWMRouter isRouteBuilt]);
      [timer invalidate];
      return;
    }
    ++attempts;
    sinceRequest = 0;
    // Clear a failure alert from an earlier attempt so it doesn't cover the screenshots.
    MWMAlertViewController * alerts = [MWMAlertViewController activeAlertController];
    if (alerts.view.superview && alerts.view.subviews.count > 0)
      [alerts closeAlert:nil];
    if (attempts > 1)
    {
      NSLog(@"Backseat test drive: no route yet (active=%d built=%d), retry %d", [MWMRouter isRoutingActive],
            [MWMRouter isRouteBuilt], attempts);
      [MWMRouter removePoints];
    }
    [MWMRouter setType:MWMRouterTypeVehicle];
    m2::PointD const pt = mercator::FromLatLon(lat, lon);
    MWMRoutePoint * finish = [[MWMRoutePoint alloc] initWithCGPoint:CGPointMake(pt.x, pt.y)
                                                              title:@"Grandma's"
                                                           subtitle:@""
                                                               type:MWMRoutePointTypeFinish
                                                  intermediateIndex:0];
    NSLog(@"Backseat test drive: building a route to %f,%f", lat, lon);
    [MWMRouter buildToPoint:finish bestRouter:NO];
  }];
#endif
}

@end
