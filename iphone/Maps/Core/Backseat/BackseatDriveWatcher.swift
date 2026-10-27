import CoreLocation
import CoreMotion
import UIKit

/// Feeds the mileage log: notices drives the driver starts without navigation, in the background.
///
/// Parked, it only watches cheaply (significant location changes, visits, a 150 m fence around
/// where the car is parked, and the motion coprocessor), each of which can wake the app. Once the
/// phone says it's in a car, or the fence is crossed, it switches to full GPS until the car has sat
/// still for a few minutes. Off unless the driver turned on "Log my drives automatically".
@objc final class BackseatDriveWatcher: NSObject, CLLocationManagerDelegate {
  @objc static let shared = BackseatDriveWatcher()

  private static let parkedRadius: CLLocationDistance = 150
  private static let parkedRegionID = "backseat.parked"
  /// No car motion and no movement for this long: back to watching cheaply.
  private static let idleAfter: TimeInterval = 5 * 60
  private static let movingMps: CLLocationSpeed = 2

  private let location = CLLocationManager()
  private let motion = CMMotionActivityManager()
  private var running = false
  private var tracking = false
  /// backseat::Activity: 0 unknown, 1 stationary, 2 walking, 3 running, 4 cycling, 5 automotive.
  private var activity = 0
  private var lastLocation: CLLocation?
  private var lastFedMs: Double = 0
  private var lastSignOfDriving = Date()
  private var idleTimer: Timer?
  private var backgroundSession: AnyObject?
  private var terminateObserver: NSObjectProtocol?

  /// At launch: watches only if the driver turned the log on.
  @objc static func startIfEnabled() {
    if MWMBackseatRewriter.mileageOn {
      shared.start(askingPermission: false)
    }
  }

  /// The driver turned "Log my drives automatically" on or off. Permission is only asked here.
  func setEnabled(_ on: Bool) {
    MWMBackseatRewriter.mileageOn = on
    if on {
      start(askingPermission: true)
      // The first time: "Whose business are we tracking?"
      say(MWMBackseatRewriter.mileageTurnedOn())
    } else {
      stop()
    }
  }

  private func start(askingPermission: Bool) {
    guard !running else { return }
    running = true
    location.delegate = self
    if askingPermission {
      location.requestAlwaysAuthorization()
    }
    location.startMonitoringSignificantLocationChanges()
    location.startMonitoringVisits()
    if CMMotionActivityManager.isActivityAvailable() {
      motion.startActivityUpdates(to: .main) { [weak self] update in
        if let update { self?.onActivity(update) }
      }
    }
    terminateObserver = NotificationCenter.default.addObserver(forName: UIApplication.willTerminateNotification,
                                                               object: nil, queue: .main) { [weak self] _ in
      self?.finish()
    }
  }

  private func stop() {
    guard running else { return }
    running = false
    finish()
    stopTracking()
    location.stopMonitoringSignificantLocationChanges()
    location.stopMonitoringVisits()
    location.monitoredRegions.filter { $0.identifier == Self.parkedRegionID }.forEach { location.stopMonitoring(for: $0) }
    motion.stopActivityUpdates()
    if let terminateObserver { NotificationCenter.default.removeObserver(terminateObserver) }
    terminateObserver = nil
  }

  // MARK: - Full GPS while driving

  private func startTracking() {
    guard running, !tracking else { return }
    tracking = true
    lastSignOfDriving = Date()
    location.allowsBackgroundLocationUpdates = true
    location.pausesLocationUpdatesAutomatically = false
    location.activityType = .automotiveNavigation
    location.desiredAccuracy = kCLLocationAccuracyNearestTenMeters
    location.distanceFilter = 20
    location.showsBackgroundLocationIndicator = true
    location.startUpdatingLocation()
    if #available(iOS 17.0, *) {
      backgroundSession = CLBackgroundActivitySession()
    }
    idleTimer = Timer.scheduledTimer(withTimeInterval: 30, repeats: true) { [weak self] _ in self?.checkParked() }
  }

  private func stopTracking() {
    guard tracking else { return }
    tracking = false
    location.stopUpdatingLocation()
    location.allowsBackgroundLocationUpdates = false
    if #available(iOS 17.0, *) {
      (backgroundSession as? CLBackgroundActivitySession)?.invalidate()
    }
    backgroundSession = nil
    idleTimer?.invalidate()
    idleTimer = nil
    fenceParkedSpot()
  }

  /// Back to watching cheaply once nothing has looked like driving for a while.
  private func checkParked() {
    guard Date().timeIntervalSince(lastSignOfDriving) > Self.idleAfter else { return }
    finish()
    stopTracking()
  }

  /// Leaving this circle wakes the app, so the next drive is caught from the start.
  private func fenceParkedSpot() {
    guard running, let lastLocation else { return }
    location.monitoredRegions.filter { $0.identifier == Self.parkedRegionID }.forEach { location.stopMonitoring(for: $0) }
    let region = CLCircularRegion(center: lastLocation.coordinate, radius: Self.parkedRadius, identifier: Self.parkedRegionID)
    region.notifyOnExit = true
    region.notifyOnEntry = false
    location.startMonitoring(for: region)
  }

  // MARK: - Feeding the log

  private func feed(_ fix: CLLocation, at timeMs: Double) {
    // The log needs updates in time order.
    guard timeMs >= lastFedMs else { return }
    lastFedMs = timeMs
    lastLocation = fix
    if fix.speed >= Self.movingMps || activity == 5 {
      lastSignOfDriving = Date()
    }
    say(MWMBackseatRewriter.driveSample(fix.coordinate.latitude, lon: fix.coordinate.longitude,
                                         speed: fix.speed >= 0 ? fix.speed : -1, accuracy: fix.horizontalAccuracy,
                                         activity: activity, time: timeMs))
  }

  private func finish() {
    say(MWMBackseatRewriter.mileageFinish())
  }

  private func onActivity(_ update: CMMotionActivity) {
    let kind: Int
    if update.automotive {
      kind = 5
    } else if update.cycling {
      kind = 4
    } else if update.running {
      kind = 3
    } else if update.walking {
      kind = 2
    } else if update.stationary {
      kind = 1
    } else {
      kind = 0
    }
    activity = kind
    if kind == 5 && update.confidence != .low {
      lastSignOfDriving = Date()
      startTracking()
    }
    // Walking away ends the drive right away, even without a new fix.
    if let lastLocation {
      feed(lastLocation, at: max(lastFedMs, Date().timeIntervalSince1970 * 1000))
    }
  }

  private func say(_ line: String) {
    guard !line.isEmpty, MWMTextToSpeech.isTTSEnabled() else { return }
    KokoroVoice.shared.sayReply(line, voiceID: nil) { MWMTextToSpeech.tts().play(line) }
    // "Was that your house?", "Whose business?", "Was that trip for business?": listen once.
    if MWMBackseatRewriter.mileageListening {
      BackseatSetup.listen(waiting: 6) { [weak self] answer in
        self?.say(MWMBackseatRewriter.mileageAnswer(answer))
      }
    }
  }

  // MARK: - CLLocationManagerDelegate

  func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
    for fix in locations {
      feed(fix, at: fix.timestamp.timeIntervalSince1970 * 1000)
    }
    // A significant change while parked means we're probably moving.
    if !tracking, let fix = locations.last, fix.speed >= Self.movingMps || activity == 5 {
      startTracking()
    }
  }

  func locationManager(_ manager: CLLocationManager, didExitRegion region: CLRegion) {
    if region.identifier == Self.parkedRegionID {
      startTracking()
    }
  }

  func locationManager(_ manager: CLLocationManager, didVisit visit: CLVisit) {
    // Arrived somewhere and staying: the drive is over.
    if visit.departureDate == .distantFuture {
      finish()
    }
  }
}
