// Backseat Driver's Kokoro voice on iOS: Kokoro-82M on Core ML (Neural Engine + CPU) through
// FluidAudio, the same engine EpubToM4B narrates with.
//
// Navigation lines never wait on Kokoro: one that isn't ready to play within `navDeadline` goes to
// the system voice, and its Kokoro audio is kept for the next time the same line comes up. Roasts
// queue, go stale after `roastMaxAge`, and are cut off when a navigation line needs the speaker.
//
// Apple's BNNS can crash inside Core ML on iOS 26.4 and later (FluidAudio #817/#844). That can't be
// caught, so `CoreMLCrashGuard` notices it at the next launch and keeps the system voice for that
// iOS build: a crash while driving should happen at most once.

import AVFoundation
import FluidAudio
import Foundation
import Network
import os

@objc(BackseatKokoroVoice)
final class KokoroVoice: NSObject {
  @objc static let shared = KokoroVoice()

  /// True while the system voice is talking, so a Kokoro clip doesn't talk over it.
  @objc var systemIsSpeaking: (() -> Bool)?

  private static let log = os.Logger(subsystem: "com.brandonjconover.backseatdriver", category: "Kokoro")
  private static let navDeadline: TimeInterval = 1.2
  private static let roastMaxAge: TimeInterval = 15
  private static let cacheLines = 32
  private static let voice = "af_heart"
  /// Set once the Core ML models have downloaded, so later launches don't wait for Wi-Fi.
  private static let installedKey = "backseat.kokoro.coreMLInstalled"

  private enum State { case waiting, playing, fellBack }

  private final class Job {
    let text: String
    let nav: Bool
    let speed: Float
    let fallback: () -> Void
    let generation: Int
    let queuedAt = Date()
    var state = State.waiting  // guarded by KokoroVoice.lock

    init(text: String, nav: Bool, speed: Float, fallback: @escaping () -> Void, generation: Int) {
      self.text = text
      self.nav = nav
      self.speed = speed
      self.fallback = fallback
      self.generation = generation
    }
  }

  private let lock = NSLock()
  private var navJobs: [Job] = []
  private var roastJobs: [Job] = []
  private var generation = 0
  private var wake: AsyncStream<Void>.Continuation?
  private var cache: [String: Data] = [:]
  private var cacheOrder: [String] = []
  private var primary: KokoroAneManager?
  private var fallbackEngine: KokoroAneManager?
  private var pathMonitor: NWPathMonitor?
  private var started = false
  private let player = ClipPlayer()

  @objc var isReady: Bool {
    lock.lock(); defer { lock.unlock() }
    return primary != nil
  }

  /// True while Kokoro has a line queued or playing, so the audio session must stay active.
  /// Main queue only.
  @objc var isBusy: Bool {
    lock.lock()
    let queued = !navJobs.isEmpty || !roastJobs.isEmpty
    lock.unlock()
    return queued || player.isPlaying
  }

  /// Downloads the Core ML models once (on Wi-Fi) and loads Kokoro in the background.
  @objc func start() {
    lock.lock()
    if started { lock.unlock(); return }
    started = true
    lock.unlock()
    if CoreMLCrashGuard.checkAtLaunch() {
      Self.log.error("Core ML crashed during the last Kokoro line")
    }
    guard !CoreMLCrashGuard.isDisabledForThisOS() else {
      Self.log.info("Kokoro is off on this iOS build after a Core ML crash; the system voice speaks")
      return
    }
    player.systemIsSpeaking = { [weak self] in self?.systemIsSpeaking?() ?? false }
    if UserDefaults.standard.bool(forKey: Self.installedKey) {
      load()
      return
    }
    // The first download is a few hundred MB: wait for a network that isn't cellular or Low Data.
    let monitor = NWPathMonitor()
    monitor.pathUpdateHandler = { [weak self] path in
      guard let self, path.status == .satisfied, !path.isExpensive, !path.isConstrained else { return }
      monitor.cancel()
      self.load()
    }
    pathMonitor = monitor
    Self.log.info("Waiting for Wi-Fi to download the Kokoro voice")
    monitor.start(queue: DispatchQueue(label: "KokoroNetwork"))
  }

  private func load() {
    Task.detached(priority: .utility) { [self] in
      do {
        // CPU + Neural Engine with the noise and tail stages on the CPU: iOS refuses GPU work from
        // the background, where a navigation app spends most of its time.
        let engine = KokoroAneManager(computeUnits: .aneTailCpu)
        try await engine.initialize(preloadVoices: [Self.voice])
        UserDefaults.standard.set(true, forKey: Self.installedKey)
        // The first line compiles the Neural Engine graph; pay for it now, not on a turn.
        _ = try await render("Hello.", speed: 1, with: engine)
        lock.lock(); primary = engine; lock.unlock()
        Self.log.info("Kokoro voice ready")
        startWorker()
      } catch {
        Self.log.error("Kokoro unavailable, the system voice stays on: \(error.localizedDescription, privacy: .public)")
        allowRetry()
      }
    }
  }

  /// Lets the next start() try the download again.
  private func allowRetry() {
    lock.lock(); started = false; lock.unlock()
  }

  /// Says `text` with Kokoro, or calls `fallback` (on the main queue) to say it with the system voice.
  @objc func speak(_ text: String, nav: Bool, speed: Float, fallback: @escaping () -> Void) {
    lock.lock()
    guard primary != nil else {
      lock.unlock()
      DispatchQueue.main.async { self.player.sayWithSystemVoice(fallback) }
      return
    }
    let key = Self.key(text, speed)
    if let wav = cache[key] {
      touch(key)
      lock.unlock()
      DispatchQueue.main.async { self.player.play(wav, nav: nav) }
      return
    }
    let job = Job(text: text, nav: nav, speed: speed, fallback: fallback, generation: generation)
    if nav { navJobs.append(job) } else { roastJobs.append(job) }
    lock.unlock()
    wake?.yield()

    guard nav else { return }
    DispatchQueue.main.asyncAfter(deadline: .now() + Self.navDeadline) { [self] in
      lock.lock()
      let fallBack = job.state == .waiting && job.generation == generation
      if fallBack { job.state = .fellBack }
      lock.unlock()
      guard fallBack else { return }
      Self.log.debug("Kokoro too slow for a navigation line, using the system voice")
      player.sayWithSystemVoice(job.fallback)
    }
  }

  /// Stops playback and forgets queued lines.
  @objc func stop() {
    lock.lock()
    generation += 1
    navJobs.removeAll()
    roastJobs.removeAll()
    lock.unlock()
    DispatchQueue.main.async { self.player.stop() }
  }

  // MARK: - Synthesis

  private func startWorker() {
    let (stream, continuation) = AsyncStream<Void>.makeStream(bufferingPolicy: .bufferingNewest(1))
    lock.lock(); wake = continuation; lock.unlock()
    continuation.yield()
    // One line at a time: concurrent Core ML renders are what made BNNS crash in EpubToM4B.
    Task.detached(priority: .userInitiated) { [self] in
      for await _ in stream {
        while let job = nextJob() {
          await synthesize(job)
        }
      }
    }
  }

  private func nextJob() -> Job? {
    lock.lock(); defer { lock.unlock() }
    if !navJobs.isEmpty { return navJobs.removeFirst() }
    while !roastJobs.isEmpty {
      let job = roastJobs.removeFirst()
      if Date().timeIntervalSince(job.queuedAt) <= Self.roastMaxAge { return job }
    }
    return nil
  }

  private func render(_ text: String, speed: Float, with engine: KokoroAneManager) async throws -> Data {
    CoreMLCrashGuard.begin()
    defer { CoreMLCrashGuard.end() }
    return try await engine.synthesize(text: text, voice: Self.voice, speed: speed)
  }

  /// The CPU-only route, for a line the Neural Engine route failed on.
  private func cpuEngine() async throws -> KokoroAneManager {
    if let fallbackEngine { return fallbackEngine }
    let engine = KokoroAneManager(computeUnits: .cpuOnly)
    try await engine.initialize(preloadVoices: [Self.voice])
    fallbackEngine = engine
    return engine
  }

  private func synthesize(_ job: Job) async {
    lock.lock()
    let current = job.generation == generation
    let engine = primary
    lock.unlock()
    guard current, let engine else { return }

    let wav: Data
    do {
      do {
        wav = try await render(job.text, speed: job.speed, with: engine)
      } catch {
        Self.log.error("Kokoro (Neural Engine) failed, retrying on the CPU: \(error.localizedDescription, privacy: .public)")
        wav = try await render(job.text, speed: job.speed, with: try await cpuEngine())
      }
    } catch {
      Self.log.error("Kokoro failed on a line: \(error.localizedDescription, privacy: .public)")
      lock.lock()
      let fallBack = job.state == .waiting
      if fallBack { job.state = .fellBack }
      lock.unlock()
      if fallBack { DispatchQueue.main.async { self.player.sayWithSystemVoice(job.fallback) } }
      return
    }

    lock.lock()
    let key = Self.key(job.text, job.speed)
    cache[key] = wav
    touch(key)
    // Too late for a navigation line: the system voice already said it. The clip stays cached.
    let play = job.state == .waiting && job.generation == generation
    if play { job.state = .playing }
    lock.unlock()
    if play { DispatchQueue.main.async { self.player.play(wav, nav: job.nav) } }
  }

  /// Call with `lock` held.
  private func touch(_ key: String) {
    cacheOrder.removeAll { $0 == key }
    cacheOrder.append(key)
    while cacheOrder.count > Self.cacheLines {
      cache.removeValue(forKey: cacheOrder.removeFirst())
    }
  }

  private static func key(_ text: String, _ speed: Float) -> String { "\(speed)|\(text)" }
}

/// Plays clips one at a time on the main queue: navigation clips first, and they cut off a roast.
/// Only one voice talks at a time: a line handed to the system voice waits for a navigation clip to
/// end, and a clip waits for the system voice.
private final class ClipPlayer: NSObject, AVAudioPlayerDelegate {
  private struct Item {
    let wav: Data
    let nav: Bool
    let queuedAt = Date()
  }

  var systemIsSpeaking: () -> Bool = { false }
  var isPlaying: Bool { current != nil || !navQueue.isEmpty || !roastQueue.isEmpty }
  private var navQueue: [Item] = []
  private var roastQueue: [Item] = []
  private var current: AVAudioPlayer?
  private var currentIsNav = false
  private var waitingSince: Date?
  /// System-voice lines waiting for the current navigation clip to finish.
  private var pendingSystem: [() -> Void] = []
  /// How long a clip waits for the system voice before giving up on politeness.
  private static let systemWaitLimit: TimeInterval = 30

  func play(_ wav: Data, nav: Bool) {
    if nav {
      navQueue.append(Item(wav: wav, nav: true))
      if current != nil && !currentIsNav { finishCurrent() }
    } else {
      roastQueue.append(Item(wav: wav, nav: false))
    }
    playNext()
  }

  /// Runs `speak` (which talks with the system voice) once no clip is talking: a roast is cut off,
  /// a navigation clip is allowed to finish first.
  func sayWithSystemVoice(_ speak: @escaping () -> Void) {
    if current != nil && !currentIsNav { finishCurrent() }
    if current != nil {
      pendingSystem.append(speak)
    } else {
      speak()
    }
  }

  func stop() {
    navQueue.removeAll()
    roastQueue.removeAll()
    pendingSystem.removeAll()
    current?.stop()
    current = nil
  }

  func audioPlayerDidFinishPlaying(_ player: AVAudioPlayer, successfully flag: Bool) {
    guard player === current else { return }
    current = nil
    flushSystemLines()
    playNext()
  }

  func audioPlayerDecodeErrorDidOccur(_ player: AVAudioPlayer, error: Error?) {
    audioPlayerDidFinishPlaying(player, successfully: false)
  }

  /// The system voice queues its own utterances, so the waiting lines can all be handed over.
  private func flushSystemLines() {
    let lines = pendingSystem
    pendingSystem.removeAll()
    lines.forEach { $0() }
  }

  private func finishCurrent() {
    current?.stop()
    current = nil
  }

  private func playNext() {
    guard current == nil else { return }
    // Don't talk over the system voice (a navigation line it took over).
    if systemIsSpeaking() && (waitingSince.map { Date().timeIntervalSince($0) < Self.systemWaitLimit } ?? true) {
      if waitingSince == nil { waitingSince = Date() }
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { [weak self] in self?.playNext() }
      return
    }
    waitingSince = nil
    roastQueue.removeAll { Date().timeIntervalSince($0.queuedAt) > 15 }
    let item: Item
    if !navQueue.isEmpty { item = navQueue.removeFirst() }
    else if !roastQueue.isEmpty { item = roastQueue.removeFirst() }
    else { return }
    do {
      try AVAudioSession.sharedInstance().setActive(true)
      let player = try AVAudioPlayer(data: item.wav)
      player.delegate = self
      current = player
      currentIsNav = item.nav
      if !player.play() {
        current = nil
        playNext()
      }
    } catch {
      current = nil
      playNext()
    }
  }
}

/// A Core ML crash can't be caught, so a marker file is written before each render and removed
/// after; finding it at launch means Core ML crashed mid-line. After `crashLimit` crashes within
/// `window` on one iOS build, Kokoro stays off for that build; an iOS update tries again.
/// (EpubToM4B's guard, with a lower limit because this app is used while driving.)
enum CoreMLCrashGuard {
  static let crashesKey = "backseat.coreML.crashes"  // [String: [Double]]: OS build → crash times
  static let crashLimit = 1
  static let window: TimeInterval = 30 * 24 * 3600

  private static var markerURL: URL {
    FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
      .appendingPathComponent("BackseatKokoroRender.inflight")
  }

  static var currentOSBuild: String { ProcessInfo.processInfo.operatingSystemVersionString }

  static func begin() {
    let url = markerURL
    try? FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try? Data(currentOSBuild.utf8).write(to: url, options: .atomic)
  }

  static func end() {
    try? FileManager.default.removeItem(at: markerURL)
  }

  /// Records a crash if the last render never returned. Returns true then.
  @discardableResult
  static func checkAtLaunch(defaults: UserDefaults = .standard, now: Date = Date()) -> Bool {
    let marker = markerURL
    guard FileManager.default.fileExists(atPath: marker.path) else { return false }
    let build = (try? String(contentsOf: marker, encoding: .utf8)) ?? currentOSBuild
    var crashes = defaults.dictionary(forKey: crashesKey) as? [String: [Double]] ?? [:]
    crashes[build, default: []].append(now.timeIntervalSince1970)
    defaults.set(crashes, forKey: crashesKey)
    try? FileManager.default.removeItem(at: marker)
    return true
  }

  static func isDisabledForThisOS(defaults: UserDefaults = .standard, now: Date = Date()) -> Bool {
    let crashes = (defaults.dictionary(forKey: crashesKey) as? [String: [Double]])?[currentOSBuild] ?? []
    return crashes.filter { now.timeIntervalSince1970 - $0 <= window }.count >= crashLimit
  }
}
