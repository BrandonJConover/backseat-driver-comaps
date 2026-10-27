import CoreMotion

/// Small moments the GPS has opinions about: panning the map mid-trip, typing a destination, and
/// potholes. The engine decides whether to say anything (and how often); this only notices.
@objc final class BackseatHooks: NSObject {
  /// A jolt along gravity this big (in g) counts as a bump...
  private static let bumpG = 0.6
  /// ...and this many within `roughWindow` seconds is a rough stretch.
  private static let roughBumps = 3
  private static let roughWindow: TimeInterval = 4
  /// Below this it's parking-lot speed bumps, not the road.
  private static let drivingMps = 8.0

  private static let motion = CMMotionManager()
  private static var bumps: [Date] = []
  private static var lastBump = Date.distantPast

  @objc static func install() {
    NotificationCenter.default.addObserver(forName: BackseatTalkButton.navigationVisibilityChanged, object: nil,
                                           queue: .main) { note in
      if (note.object as? Bool) ?? false { startRoughRoad() } else { stopRoughRoad() }
    }
  }

  /// The driver dragged the map away from following the car during a trip.
  @objc static func mapBrowsed() {
    say(MWMBackseatRewriter.mapBrowsed())
  }

  /// The first character typed into the search field.
  @objc static func typingDestination() {
    say(MWMBackseatRewriter.typingDestination())
  }

  private static func startRoughRoad() {
    guard motion.isDeviceMotionAvailable, !motion.isDeviceMotionActive else { return }
    bumps = []
    motion.deviceMotionUpdateInterval = 1.0 / 50
    motion.startDeviceMotionUpdates(to: .main) { data, _ in
      guard let data else { return }
      let a = data.userAcceleration
      let g = data.gravity
      // Acceleration along gravity: up and down, whichever way the phone is mounted.
      let vertical = abs(a.x * g.x + a.y * g.y + a.z * g.z)
      let now = Date()
      // One pothole shakes for a few samples; count it once.
      guard vertical > bumpG, now.timeIntervalSince(lastBump) > 0.3 else { return }
      lastBump = now
      guard (LocationManager.lastLocation()?.speed ?? 0) >= drivingMps, MWMRouter.isOnRoute() else { return }
      bumps = bumps.filter { now.timeIntervalSince($0) <= roughWindow } + [now]
      if bumps.count >= roughBumps {
        bumps = []
        say(MWMBackseatRewriter.roughRoad())
      }
    }
  }

  private static func stopRoughRoad() {
    if motion.isDeviceMotionActive { motion.stopDeviceMotionUpdates() }
    bumps = []
  }

  private static func say(_ line: String) {
    guard !line.isEmpty, MWMTextToSpeech.isTTSEnabled() else { return }
    KokoroVoice.shared.sayReply(line, voiceID: nil) { MWMTextToSpeech.tts().play(line) }
  }
}
