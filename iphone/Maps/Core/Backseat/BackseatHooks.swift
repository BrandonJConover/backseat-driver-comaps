import AVFoundation
import UIKit
import CoreMotion
import MediaPlayer

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
    traceAppAndAudio()
    VolumeComeback.shared.start()
  }

  /// App and audio session moments for the drive log, to explain a line that never played.
  private static func traceAppAndAudio() {
    let center = NotificationCenter.default
    center.addObserver(forName: UIApplication.didEnterBackgroundNotification, object: nil, queue: .main) { _ in
      BackseatLog.event("app.background", [:])
    }
    center.addObserver(forName: UIApplication.willEnterForegroundNotification, object: nil, queue: .main) { _ in
      BackseatLog.event("app.foreground", [:])
    }
    center.addObserver(forName: UIApplication.didReceiveMemoryWarningNotification, object: nil, queue: .main) { _ in
      BackseatLog.event("app.memoryWarning", [:])
    }
    center.addObserver(forName: AVAudioSession.interruptionNotification, object: nil, queue: .main) { note in
      let raw = note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt
      let type = raw.flatMap(AVAudioSession.InterruptionType.init)
      BackseatLog.event("audio.interrupted", ["phase": type == .began ? "began" : "ended"])
    }
    center.addObserver(forName: AVAudioSession.routeChangeNotification, object: nil, queue: .main) { note in
      let raw = note.userInfo?[AVAudioSessionRouteChangeReasonKey] as? UInt ?? 0
      let outputs = AVAudioSession.sharedInstance().currentRoute.outputs.map { "\($0.portType.rawValue):\($0.portName)" }
      BackseatLog.event("audio.route", ["output": outputs.joined(separator: ","), "reason": String(raw)])
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

/// "I won't be silenced!": turning the volume down gets a comeback, said at the volume it was before
/// the presses, and then the phone goes back to the level the driver picked. The engine decides
/// whether to say it (not at mute, not too often); this notices the presses and moves the volume.
///
/// iOS reports volume changes only while the app's audio session is active, so this reliably
/// catches a turn-down while the GPS is talking (which is when people reach for the button).
final class VolumeComeback: NSObject {
  static let shared = VolumeComeback()

  /// Presses this close together are one turn-down.
  private static let burst: TimeInterval = 0.8
  /// Never wait longer than this for the line to finish before handing the volume back.
  private static let giveBackMax: TimeInterval = 15
  /// Kokoro takes a moment to start; don't take "not speaking yet" for "done".
  private static let giveBackMin: TimeInterval = 1.5

  private var observation: NSKeyValueObservation?
  private var burstFrom: Float?
  private var burstTimer: Timer?
  /// A level this class set itself; the change it causes isn't the driver's.
  private var selfSet: Float?
  /// Where to put the volume back after the line; nil when not in the middle of the bit.
  private var giveBackTo: Float?
  /// The system volume slider, the one public way to set the volume. Never on screen, so the
  /// system's own volume indicator still shows the jump.
  private lazy var volumeView = MPVolumeView(frame: CGRect(x: -2000, y: -2000, width: 1, height: 1))

  func start() {
    guard observation == nil else { return }
    _ = volumeView
    observation = AVAudioSession.sharedInstance().observe(\.outputVolume, options: [.old, .new]) { [weak self] _, change in
      guard let old = change.oldValue, let new = change.newValue else { return }
      DispatchQueue.main.async { self?.changed(from: old, to: new) }
    }
  }

  private func changed(from old: Float, to new: Float) {
    guard abs(new - old) > 0.001 else { return }
    if let mine = selfSet, abs(new - mine) < 0.02 {
      selfSet = nil
      return
    }
    // The driver touched the volume mid-bit: their level wins, and it stays.
    giveBackTo = nil
    if new < old, burstFrom == nil { burstFrom = old }
    guard burstFrom != nil else { return }
    burstTimer?.invalidate()
    burstTimer = Timer.scheduledTimer(withTimeInterval: Self.burst, repeats: false) { [weak self] _ in
      self?.burstOver()
    }
  }

  private func burstOver() {
    guard let from = burstFrom else { return }
    burstFrom = nil
    let to = AVAudioSession.sharedInstance().outputVolume
    guard to < from, MWMTextToSpeech.isTTSEnabled() else { return }
    let foreground = UIApplication.shared.applicationState == .active
    let line = MWMBackseatRewriter.volumeDown(from: Double(from), to: Double(to), foreground: foreground)
    guard !line.isEmpty else { return }
    BackseatLog.event("volume.comeback", ["from": String(format: "%.2f", from), "to": String(format: "%.2f", to)])
    setVolume(from)
    giveBackTo = to
    KokoroVoice.shared.sayReply(line, voiceID: nil) { MWMTextToSpeech.tts().play(line) }
    let started = Date()
    DispatchQueue.main.asyncAfter(deadline: .now() + Self.giveBackMin) { [weak self] in
      self?.giveBack(started: started)
    }
  }

  private func giveBack(started: Date) {
    guard let level = giveBackTo else { return }
    if KokoroVoice.shared.anyVoiceSpeaking, Date().timeIntervalSince(started) < Self.giveBackMax {
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in self?.giveBack(started: started) }
      return
    }
    // Fine. The driver's level.
    giveBackTo = nil
    setVolume(level)
  }

  private func setVolume(_ level: Float) {
    guard let slider = volumeView.subviews.compactMap({ $0 as? UISlider }).first else { return }
    selfSet = level
    // The slider's subviews load a beat after the view is made.
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { slider.value = level }
  }
}
