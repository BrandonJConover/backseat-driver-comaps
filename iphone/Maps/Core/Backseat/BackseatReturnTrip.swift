import CoreLocation

/// The way back: where the last trip started, so the "Where to?" card can offer "Head back" once
/// you've arrived somewhere. Saved when a trip starts from my position; offered for 12 hours, and
/// only while you're away from where it started.
@objc final class BackseatReturnTrip: NSObject {
  private static let latKey = "BackseatReturnLat"
  private static let lonKey = "BackseatReturnLon"
  private static let nameKey = "BackseatReturnName"
  private static let atKey = "BackseatReturnAt"
  /// Offered this long after the trip started.
  private static let keepFor: TimeInterval = 12 * 3600
  /// Closer than this to where it started, there's nothing to head back to.
  private static let awayMeters: CLLocationDistance = 300

  struct Origin {
    let coordinate: CLLocationCoordinate2D
    let name: String
  }

  static func install() {
    NotificationCenter.default.addObserver(forName: BackseatTalkButton.navigationVisibilityChanged, object: nil,
                                           queue: .main) { note in
      if (note.object as? Bool) ?? false { remember() }
    }
  }

  /// A trip just started: remember where from.
  private static func remember() {
    guard let start = MWMRouter.startPoint(), start.isMyPosition, let here = LocationManager.lastLocation() else { return }
    let defaults = UserDefaults.standard
    defaults.set(here.coordinate.latitude, forKey: latKey)
    defaults.set(here.coordinate.longitude, forKey: lonKey)
    defaults.set(name(for: here.coordinate), forKey: nameKey)
    defaults.set(Date().timeIntervalSince1970, forKey: atKey)
  }

  /// "Home" or "Work" when the start is a place driven to before, else where you started.
  private static func name(for coordinate: CLLocationCoordinate2D) -> String {
    let start = CLLocation(latitude: coordinate.latitude, longitude: coordinate.longitude)
    for route in MWMBackseatRewriter.pastRoutes(20) {
      guard let label = route["label"] as? String, !label.isEmpty,
            let lat = (route["lat"] as? NSNumber)?.doubleValue, let lon = (route["lon"] as? NSNumber)?.doubleValue
      else { continue }
      if start.distance(from: CLLocation(latitude: lat, longitude: lon)) < awayMeters { return label }
    }
    return "Where you started"
  }

  /// Where to head back to, if there's somewhere.
  static var origin: Origin? {
    let defaults = UserDefaults.standard
    let at = defaults.double(forKey: atKey)
    guard at > 0, Date().timeIntervalSince1970 - at < keepFor, let name = defaults.string(forKey: nameKey) else { return nil }
    let coordinate = CLLocationCoordinate2D(latitude: defaults.double(forKey: latKey), longitude: defaults.double(forKey: lonKey))
    if let here = LocationManager.lastLocation(),
       here.distance(from: CLLocation(latitude: coordinate.latitude, longitude: coordinate.longitude)) < awayMeters {
      return nil
    }
    return Origin(coordinate: coordinate, name: name)
  }

  /// Drives back the way you came, straight into navigation.
  static func headBack() {
    guard let origin else { return }
    MWMBackseatRewriter.route(to: origin.coordinate, title: origin.name)
  }
}
