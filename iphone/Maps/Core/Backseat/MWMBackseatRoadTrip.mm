#import "MWMBackseatRoadTrip.h"

#import <Network/Network.h>

#include <CoreApi/Framework.h>

#include "backseat/debug_log.hpp"

#include "geometry/mercator.hpp"

namespace
{
/// Weather is checked this often while a route is being followed.
NSTimeInterval constexpr kWeatherEverySec = 15 * 60;
/// The nearest station is looked up again after the car has moved this far (degrees, ~10 km).
double constexpr kStationMoveDeg = 0.1;
/// NWS and MET Norway both ask every client to say who it is; MET Norway also wants a way to reach
/// the developer (it refuses requests without one), so this names the project's site.
NSString * const kUserAgent = @"BackseatDriver/1.0 github.com/BrandonJConover/backseat-driver";

nw_path_monitor_t gMonitor;
NSTimer * gWeatherTimer;
NSString * gStationUrl;
double gStationLat = 1000;
double gStationLon = 1000;
/// NWS only covers the US. Where it has no station (Canada, everywhere else) the weather comes from
/// MET Norway's free worldwide forecast instead, remembered per ~10 km cell like the station.
bool gOutsideNws = false;

BackseatAdapter & Backseat() { return GetFramework().GetRoutingManager().GetBackseat(); }

void GetJson(NSString * url, void (^done)(NSDictionary * _Nullable json), NSString * accept = @"application/geo+json")
{
  NSMutableURLRequest * request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:url]];
  [request setValue:kUserAgent forHTTPHeaderField:@"User-Agent"];
  [request setValue:accept forHTTPHeaderField:@"Accept"];
  request.timeoutInterval = 20;
  [[NSURLSession.sharedSession dataTaskWithRequest:request
                                 completionHandler:^(NSData * data, NSURLResponse * response, NSError * error) {
                                   NSInteger const status = [(NSHTTPURLResponse *)response statusCode];
                                   if (error || status != 200 || !data)
                                   {
                                     backseat::Trace("weather.error", {{"status", std::to_string(status)},
                                                                       {"url", url.UTF8String}});
                                     done(nil);
                                     return;
                                   }
                                   id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
                                   done([json isKindOfClass:NSDictionary.class] ? json : nil);
                                 }] resume];
}

/// km/h (what NWS reports) to m/s; -1 when missing.
double MpsFromKmh(id value) { return [value isKindOfClass:NSNumber.class] ? [value doubleValue] / 3.6 : -1; }

/// The observation's air temperature in °C (what NWS reports); NaN when missing.
double TempC(id temperature)
{
  id value = [temperature isKindOfClass:NSDictionary.class] ? temperature[@"value"] : nil;
  return [value isKindOfClass:NSNumber.class] ? [value doubleValue] : NAN;
}

void FetchObservation()
{
  if (!gStationUrl)
    return;
  GetJson([gStationUrl stringByAppendingString:@"/observations/latest"], ^(NSDictionary * json) {
    NSDictionary * props = json[@"properties"];
    if (![props isKindOfClass:NSDictionary.class])
      return;
    NSString * description = [props[@"textDescription"] isKindOfClass:NSString.class] ? props[@"textDescription"] : @"";
    double const wind = MpsFromKmh(props[@"windSpeed"][@"value"]);
    double const gust = MpsFromKmh(props[@"windGust"][@"value"]);
    double const tempC = TempC(props[@"temperature"]);
    dispatch_async(dispatch_get_main_queue(), ^{
      backseat::Trace("weather", {{"text", description.UTF8String}, {"wind", std::to_string(wind)},
                                {"temp", std::to_string(tempC)}});
      Backseat().OnWeather(description.UTF8String, wind, gust, tempC);
    });
  });
}

/// A number from MET Norway's JSON; `fallback` when missing.
double MetNumber(NSDictionary * details, NSString * key, double fallback)
{
  id value = [details isKindOfClass:NSDictionary.class] ? details[key] : nil;
  return [value isKindOfClass:NSNumber.class] ? [value doubleValue] : fallback;
}

/// Weather from MET Norway (api.met.no, free, CC BY 4.0, covers the whole world). Its symbol code
/// ("lightsnow", "rainshowers_day", "fog", "clearsky_night") carries the same words WeatherFromNws
/// looks for in an NWS description, so it goes through the same path. Wind comes in m/s already.
void FetchMet(double lat, double lon)
{
  NSString * url = [NSString
      stringWithFormat:@"https://api.met.no/weatherapi/locationforecast/2.0/complete?lat=%.2f&lon=%.2f", lat, lon];
  GetJson(url, ^(NSDictionary * json) {
    NSArray * series = json[@"properties"][@"timeseries"];
    NSDictionary * now = [series isKindOfClass:NSArray.class] && series.count ? series[0][@"data"] : nil;
    if (![now isKindOfClass:NSDictionary.class])
      return;
    NSDictionary * details = now[@"instant"][@"details"];
    id symbol = now[@"next_1_hours"][@"summary"][@"symbol_code"];
    if (![symbol isKindOfClass:NSString.class])
      symbol = now[@"next_6_hours"][@"summary"][@"symbol_code"];
    NSString * description = [symbol isKindOfClass:NSString.class] ? symbol : @"";
    double const wind = MetNumber(details, @"wind_speed", -1);
    double const gust = MetNumber(details, @"wind_speed_of_gust", -1);
    double const tempC = MetNumber(details, @"air_temperature", NAN);
    dispatch_async(dispatch_get_main_queue(), ^{
      backseat::Trace("weather", {{"source", "met.no"}, {"text", description.UTF8String},
                                {"wind", std::to_string(wind)}, {"temp", std::to_string(tempC)}});
      Backseat().OnWeather(description.UTF8String, wind, gust, tempC);
    });
  }, @"application/json");
}

void RefreshWeather()
{
  auto & framework = GetFramework();
  if (!framework.GetRoutingManager().IsRoutingActive())
    return;
  auto const position = framework.GetCurrentPosition();
  if (!position)
    return;
  auto const ll = mercator::ToLatLon(*position);
  // Only a rough position leaves the phone: two decimals, about a kilometer.
  double const lat = std::round(ll.m_lat * 100) / 100;
  double const lon = std::round(ll.m_lon * 100) / 100;
  if ((gStationUrl || gOutsideNws) && std::fabs(lat - gStationLat) < kStationMoveDeg &&
      std::fabs(lon - gStationLon) < kStationMoveDeg)
  {
    if (gOutsideNws)
      FetchMet(lat, lon);
    else
      FetchObservation();
    return;
  }
  NSString * points = [NSString stringWithFormat:@"https://api.weather.gov/points/%.2f,%.2f", lat, lon];
  GetJson(points, ^(NSDictionary * json) {
    NSString * stations = json[@"properties"][@"observationStations"];
    if (![stations isKindOfClass:NSString.class])
    {
      // Outside the US (or NWS is down): MET Norway instead, for this cell.
      dispatch_async(dispatch_get_main_queue(), ^{
        gStationUrl = nil;
        gOutsideNws = true;
        gStationLat = lat;
        gStationLon = lon;
        FetchMet(lat, lon);
      });
      return;
    }
    GetJson(stations, ^(NSDictionary * list) {
      NSArray * features = list[@"features"];
      NSString * station = [features isKindOfClass:NSArray.class] && features.count ? features[0][@"id"] : nil;
      if (![station isKindOfClass:NSString.class])
        return;
      dispatch_async(dispatch_get_main_queue(), ^{
        gStationUrl = station;
        gOutsideNws = false;
        gStationLat = lat;
        gStationLon = lon;
        FetchObservation();
      });
    });
  });
}
}  // namespace

@implementation MWMBackseatRoadTrip

+ (void)start
{
  if (gMonitor)
    return;
  gMonitor = nw_path_monitor_create();
  nw_path_monitor_set_queue(gMonitor, dispatch_get_main_queue());
  nw_path_monitor_set_update_handler(gMonitor, ^(nw_path_t path) {
    bool const online = nw_path_get_status(path) == nw_path_status_satisfied;
    backseat::Trace("signal", {{"online", online ? "yes" : "no"}});
    Backseat().OnConnectivity(online);
    // Back online after a dead zone: the weather may be stale.
    if (online)
      RefreshWeather();
  });
  nw_path_monitor_start(gMonitor);

  gWeatherTimer = [NSTimer scheduledTimerWithTimeInterval:kWeatherEverySec
                                                  repeats:YES
                                                    block:^(NSTimer *) { RefreshWeather(); }];
  // Also right when a route starts (the navigation screen appears): the timer alone could leave the
  // first 15 minutes without weather.
  [NSNotificationCenter.defaultCenter addObserverForName:@"BackseatNavigationVisibilityChanged"
                                                  object:nil
                                                   queue:NSOperationQueue.mainQueue
                                              usingBlock:^(NSNotification * note) {
                                                if ([note.object boolValue])
                                                  RefreshWeather();
                                              }];
}

@end
