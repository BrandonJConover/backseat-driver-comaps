// Backseat Driver's Kokoro voice on iOS: Kokoro-82M on Core ML (Neural Engine + CPU) through
// FluidAudio, the same engine EpubToM4B narrates with.
//
// Navigation lines never wait long on Kokoro: one that isn't ready to play within its budget (at
// least `navDeadline`, longer when the engine says the maneuver is far enough off) goes to the
// system voice and isn't rendered. Roasts queue, go stale after `roastMaxAge`, and are
// cut off when a navigation line needs the speaker. Settings picks the voice, or the system voice.
//
// Apple's BNNS can crash inside Core ML on iOS 26.4 and later (FluidAudio #817/#844). That can't be
// caught, so `CoreMLCrashGuard` notices a crash during Kokoro's own model load at the next launch.
// Kokoro stays the voice regardless: after such a crash it loads lazily on the first line instead
// of at launch, and only three load crashes in a row keep the system voice, for one session.

import AVFoundation
import FluidAudio
import Foundation
import Network
import os
import UIKit

@objc(BackseatKokoroVoice)
final class KokoroVoice: NSObject {
  @objc static let shared = KokoroVoice()

  /// True while the system voice is talking, so a Kokoro clip doesn't talk over it.
  @objc var systemIsSpeaking: (() -> Bool)?

  private static let log = os.Logger(subsystem: "com.brandonjconover.backseatdriver", category: "Kokoro")
  fileprivate static let navDeadline: TimeInterval = 2.5
  static let roastMaxAge: TimeInterval = 45
  private static let cacheLines = 32
  /// Set once the Core ML models have downloaded, so later launches don't wait for Wi-Fi.
  private static let installedKey = "backseat.kokoro.coreMLInstalled"

  /// The Kokoro voice picked in Settings. The system voice is never picked: it only fills in while
  /// Kokoro loads, after a Core ML crash, or for an instruction language Kokoro can't speak.
  static let voiceKey = "backseat.kokoro.voice"
  /// Echo, unless the driver picks another.
  static let defaultVoice = "am_echo"

  struct VoiceOption: Identifiable, Hashable {
    let id: String
    /// For the Settings list: "Echo (American male)".
    let name: String
    /// Out loud: "Echo".
    let spokenName: String
  }

  /// The American and British English voices FluidAudio publishes, named for the Settings list.
  static let voiceOptions: [VoiceOption] = {
    let accents = ["af": "American female", "am": "American male", "bf": "British female", "bm": "British male"]
    return KokoroAneConstants.englishVoices.compactMap { id in
      let parts = id.split(separator: "_", maxSplits: 1)
      guard parts.count == 2, let accent = accents[String(parts[0])] else { return nil }
      let spoken = parts[1].capitalized
      return VoiceOption(id: id, name: "\(spoken) (\(accent))", spokenName: spoken)
    }
  }()

  /// Voice ids and their spoken names, in the same order, for the navigator.
  @objc static var voiceIDs: [String] { voiceOptions.map(\.id) }
  @objc static var voiceSpokenNames: [String] { voiceOptions.map(\.spokenName) }

  /// The voice the driver named: an id ("am_echo") or a name ("echo", "Echo (American male)").
  @objc static func voiceID(matching spoken: String) -> String? {
    let wanted = spoken.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
    guard !wanted.isEmpty else { return nil }
    return voiceOptions.first {
      $0.id == wanted || $0.spokenName.lowercased() == wanted || $0.name.lowercased() == wanted
    }?.id
  }

  /// The picked voice. Anything else stored (such as the old "system" choice) means the default.
  static var selectedVoiceID: String {
    let id = UserDefaults.standard.string(forKey: voiceKey) ?? defaultVoice
    return voiceOptions.contains { $0.id == id } ? id : defaultVoice
  }

  static var selectedVoiceName: String {
    voiceOptions.first { $0.id == selectedVoiceID }?.name ?? selectedVoiceID
  }

  /// Kokoro speaks English only; other instruction languages stay with the system voice.
  private static var speaksInstructionLanguage: Bool {
    let language: String? = MWMTextToSpeech.savedLanguage()
    return language?.lowercased().hasPrefix("en") ?? true
  }

  /// The Kokoro voice to speak with, or nil when the instruction language isn't English.
  private static var voice: String? {
    speaksInstructionLanguage ? selectedVoiceID : nil
  }

  private enum State { case waiting, playing, fellBack }

  private final class Job {
    let text: String
    let nav: Bool
    let speed: Float
    let voice: String
    /// A reply to the driver: it never goes stale.
    let keep: Bool
    let fallback: () -> Void
    let generation: Int
    /// Navigation lines: how long to wait for Kokoro before the system voice says it.
    let budget: TimeInterval
    let queuedAt = Date()
    var state = State.waiting  // guarded by KokoroVoice.lock

    init(text: String, nav: Bool, speed: Float, voice: String, keep: Bool, fallback: @escaping () -> Void,
         generation: Int, budget: TimeInterval = KokoroVoice.navDeadline) {
      self.text = text
      self.nav = nav
      self.speed = speed
      self.voice = voice
      self.keep = keep
      self.fallback = fallback
      self.generation = generation
      self.budget = budget
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
  /// Kokoro can't speak on this phone: a Core ML crash on this iOS, or loading failed.
  private var failed = false
  /// The system voice had to stand in, so Kokoro's next line starts with a comeback (once a session).
  private var systemStoodIn = false
  private var recoveredThisSession = false
  /// The guard's lazy mode: the first line starts the load.
  private var loadOnFirstLine = false
  private var loading = false
  private var lastLoadAttempt = Date.distantPast
  /// After a failed load (not a crash), the next line tries again at most this often.
  private static let reloadEvery: TimeInterval = 60
  /// Lines that came in before Kokoro was ready, to say once it is.
  private var waitingForReady: [() -> Void] = []
  /// How long a line waits for Kokoro to load before the system voice says it instead.
  private static let readyWait: TimeInterval = 20
  /// How long after launch the warm-up starts, so loading the models isn't part of opening the app.
  private static let warmUpDelay: TimeInterval = 2
  private let player = ClipPlayer()

  /// The picked Kokoro voice, for the drive log; nil if Kokoro doesn't speak this language.
  var selectedVoiceID: String? { Self.voice }

  /// True when Kokoro is loaded and can speak the instruction language.
  @objc var isReady: Bool {
    lock.lock(); defer { lock.unlock() }
    return primary != nil && Self.voice != nil
  }

  /// A line for Settings saying when the system voice fills in, if it does right now.
  var status: String {
    if Self.voice == nil {
      return "These voices speak English only, so the iPhone voice reads instructions in the language you picked."
    }
    if CoreMLCrashGuard.mode == .paused {
      return "Kokoro paused after a crash, retrying next launch. The iPhone voice fills in until then."
    }
    lock.lock(); defer { lock.unlock() }
    if primary != nil { return "A voice you haven't used yet downloads the first time it speaks." }
    return "The voices are still downloading or loading (the download waits for Wi-Fi). Until then the iPhone voice fills in."
  }

  /// Switches voices and forgets queued and cached lines, so the old voice doesn't come back.
  func selectVoice(_ id: String) {
    UserDefaults.standard.set(id, forKey: Self.voiceKey)
    stop()
    lock.lock()
    cache.removeAll()
    cacheOrder.removeAll()
    lock.unlock()
  }

  /// Says `text` in the picked voice for the test button in Settings; `fallback` says it with the
  /// system voice when Kokoro can't.
  func test(_ text: String, fallback: @escaping () -> Void) {
    speak(text, nav: false, speed: 1, fallback: fallback)
  }

  /// True while Kokoro or the system voice has a line queued or playing, so the hands-free
  /// listener can ignore the GPS's own voice. Main queue only.
  @objc var anyVoiceSpeaking: Bool {
    isBusy || (systemIsSpeaking?() ?? false)
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
    let mode = CoreMLCrashGuard.checkAtLaunch()
    player.systemIsSpeaking = { [weak self] in self?.systemIsSpeaking?() ?? false }
    player.systemStoodIn = { [weak self] in
      guard let self else { return }
      self.lock.lock(); self.systemStoodIn = true; self.lock.unlock()
    }
    switch mode {
    case .paused:
      Self.log.warning("Kokoro's load crashed three launches in a row; the system voice speaks this session")
      giveUp()
      return
    case .lazy:
      // The last launch died loading Kokoro: wait for the first line rather than load at open.
      lock.lock(); loadOnFirstLine = true; lock.unlock()
      return
    case .normal:
      break
    }
    if UserDefaults.standard.bool(forKey: Self.installedKey) {
      // Not during launch itself: give the first screen a moment, then load in the background.
      DispatchQueue.main.asyncAfter(deadline: .now() + Self.warmUpDelay) { [self] in load() }
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

  /// "Use Kokoro now" in Settings, after the guard sat this session out.
  func retryNow() {
    CoreMLCrashGuard.override()
    lock.lock()
    let idle = primary == nil
    failed = false
    loadOnFirstLine = false
    lock.unlock()
    if idle { load() }
  }

  private func load() {
    lock.lock()
    if loading || primary != nil { lock.unlock(); return }
    loading = true
    lastLoadAttempt = Date()
    lock.unlock()
    // Every line but an imminent turn waits for it.
    Task.detached(priority: .userInitiated) { [self] in
      defer { lock.lock(); loading = false; lock.unlock() }
      let began = Date()
      let voice = Self.voice ?? Self.defaultVoice
      // The CPU first: after an install or update the Neural Engine recompiles the models, which
      // took a minute on an iPhone 15 Pro. The CPU is ready in seconds.
      do {
        CoreMLCrashGuard.beginLoad()
        defer { CoreMLCrashGuard.endLoad() }
        let cpu = KokoroAneManager(computeUnits: .cpuOnly)
        try await cpu.initialize(preloadVoices: [voice])
        UserDefaults.standard.set(true, forKey: Self.installedKey)
        _ = try await render("Hello.", speed: 1, voice: voice, with: cpu)
        CoreMLCrashGuard.loadSucceeded()
        becomeReady(cpu, cpuFallback: true, after: began, on: "the CPU")
      } catch {
        Self.log.error("Kokoro on the CPU failed: \(error.localizedDescription, privacy: .public)")
      }
      // Then the Neural Engine, quicker per line; CPU + Neural Engine with the noise and tail
      // stages on the CPU, since iOS refuses GPU work from the background, where a navigation app
      // spends most of its time. Its first line compiles the rest, so it isn't warmed with a render
      // here: that would run alongside the CPU's renders, and concurrent renders crash BNNS.
      do {
        CoreMLCrashGuard.beginLoad()
        defer { CoreMLCrashGuard.endLoad() }
        let engine = KokoroAneManager(computeUnits: .aneTailCpu)
        try await engine.initialize(preloadVoices: [voice])
        UserDefaults.standard.set(true, forKey: Self.installedKey)
        CoreMLCrashGuard.loadSucceeded()
        becomeReady(engine, cpuFallback: false, after: began, on: "the Neural Engine")
      } catch {
        Self.log.error("Kokoro unavailable, the system voice stays on: \(error.localizedDescription, privacy: .public)")
        lock.lock(); let ready = primary != nil; lock.unlock()
        if !ready {
          giveUp()
          allowRetry()
        }
      }
    }
  }

  /// Speaks with `engine` from now on, and says the lines that were waiting if it's the first.
  private func becomeReady(_ engine: KokoroAneManager, cpuFallback: Bool, after began: Date, on name: String) {
    lock.lock()
    let first = primary == nil
    primary = engine
    if cpuFallback { fallbackEngine = engine }
    failed = false
    let waiting = first ? waitingForReady : []
    if first { waitingForReady.removeAll() }
    lock.unlock()
    Self.log.info("Kokoro voice ready on \(name, privacy: .public) in \(Int(Date().timeIntervalSince(began) * 1000)) ms")
    if first {
      startWorker()
      waiting.forEach { $0() }
    }
  }

  /// Kokoro won't be speaking: the lines waiting for it go to the system voice now.
  private func giveUp() {
    lock.lock()
    failed = true
    let waiting = waitingForReady
    waitingForReady.removeAll()
    lock.unlock()
    waiting.forEach { $0() }
  }

  /// Lets the next start() try the download again.
  private func allowRetry() {
    lock.lock(); started = false; lock.unlock()
  }

  /// Says `text` with Kokoro, or calls `fallback` (on the main queue) to say it with the system voice.
  @objc func speak(_ text: String, nav: Bool, speed: Float, fallback: @escaping () -> Void) {
    enqueue(text, nav: nav, speed: speed, voice: nil, keep: false, fallback: fallback)
  }

  /// Like speak(_:nav:speed:fallback:), with the engine's wait budget for a navigation line: the
  /// maneuver is far enough off that waiting `maxWaitMs` for Kokoro still says it in time.
  @objc func speak(_ text: String, nav: Bool, speed: Float, maxWaitMs: Double, fallback: @escaping () -> Void) {
    enqueue(text, nav: nav, speed: speed, voice: nil, keep: false, fallback: fallback,
            budget: max(Self.navDeadline, maxWaitMs / 1000))
  }

  /// Answers the driver, in `voiceID` (the picked voice when nil or unknown). Replies play in the
  /// order given and never go stale, so a whole line-up of voices can take turns.
  @objc func sayReply(_ text: String, voiceID: String?, fallback: @escaping () -> Void) {
    enqueue(text, nav: false, speed: 1, voice: voiceID, keep: true, fallback: fallback)
  }

  /// Picks a voice without cutting off what's queued: the old voice finishes its goodbye.
  @objc func switchVoice(to id: String) {
    guard Self.voiceOptions.contains(where: { $0.id == id }) else { return }
    UserDefaults.standard.set(id, forKey: Self.voiceKey)
  }

  private func enqueue(_ text: String, nav: Bool, speed: Float, voice requested: String?, keep: Bool,
                       fallback: @escaping () -> Void, budget: TimeInterval = navDeadline,
                       noWait: Bool = false) {
    lock.lock()
    if loadOnFirstLine {
      loadOnFirstLine = false
      lock.unlock()
      BackseatLog.event("voice.reload", ["reason": "lazy load after a crash"])
      load()
      lock.lock()
    } else if primary == nil, failed, !loading, CoreMLCrashGuard.mode != .paused,
              Date().timeIntervalSince(lastLoadAttempt) > Self.reloadEvery {
      // The load failed earlier (no crash): try again rather than stay on the system voice.
      lock.unlock()
      BackseatLog.event("voice.reload", ["reason": "retry after a failed load"])
      load()
      lock.lock()
    }
    let known = requested.flatMap { id in Self.voiceOptions.contains { $0.id == id } ? id : nil }
    // Still loading: everything but a turn instruction waits for Kokoro rather than switching voices.
    if primary == nil, Self.voice != nil, !failed, !nav, !noWait {
      var said = false
      let say = { [self] in
        guard !said else { return }
        said = true
        enqueue(text, nav: nav, speed: speed, voice: requested, keep: keep, fallback: fallback, budget: budget,
                noWait: true)
      }
      waitingForReady.append(say)
      lock.unlock()
      DispatchQueue.main.asyncAfter(deadline: .now() + Self.readyWait) { [self] in
        lock.lock()
        let stillWaiting = primary == nil
        lock.unlock()
        // After the wait, enqueue() hands this line to the system voice; the next one still waits
        // for Kokoro, which may yet finish loading.
        if stillWaiting { say() }
      }
      return
    }
    guard primary != nil, let picked = Self.voice else {
      let why = CoreMLCrashGuard.mode == .paused ? "crashGuard" : failed ? "error" : "notReady"
      lock.unlock()
      Self.trace(text, voice: "system", why: why, budget: nav ? budget : 0)
      DispatchQueue.main.async { self.player.sayWithSystemVoice(nav: nav, fallback) }
      return
    }
    let voice = known ?? picked
    // Back after the system voice filled in: cough, blame lunch, then carry on. It goes ahead of
    // everything (the navigation queue) and has no deadline, so the line after it waits its turn.
    let recovering = systemStoodIn && !recoveredThisSession
    if recovering {
      systemStoodIn = false
      recoveredThisSession = true
      let comeback = MWMBackseatRewriter.voiceRecovered
      if !comeback.isEmpty {
        navJobs.append(Job(text: comeback, nav: true, speed: 1, voice: voice, keep: true, fallback: {},
                           generation: generation))
      }
    }
    let key = Self.key(text, speed, voice)
    if !recovering, let wav = cache[key] {
      touch(key)
      lock.unlock()
      Self.trace(text, voice: "kokoro-cached", budget: nav ? budget : 0)
      DispatchQueue.main.async { self.player.play(wav, nav: nav, keep: keep) }
      return
    }
    let job = Job(text: text, nav: nav, speed: speed, voice: voice, keep: keep, fallback: fallback,
                  generation: generation, budget: budget)
    if nav { navJobs.append(job) } else { roastJobs.append(job) }
    lock.unlock()
    wake?.yield()

    guard nav else { return }
    DispatchQueue.main.asyncAfter(deadline: .now() + job.budget) { [self] in
      lock.lock()
      let fallBack = job.state == .waiting && job.generation == generation
      if fallBack { job.state = .fellBack }
      lock.unlock()
      guard fallBack else { return }
      Self.log.debug("Kokoro too slow for a navigation line, using the system voice")
      Self.trace(job.text, voice: "system", why: "deadline", waitMs: Date().timeIntervalSince(job.queuedAt) * 1000,
                 budget: job.budget)
      player.sayWithSystemVoice(nav: true, job.fallback)
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
    // A navigation line the system voice already said isn't rendered: with one worker, warming the
    // cache with it kept roasts waiting until they went stale.
    while !navJobs.isEmpty {
      let job = navJobs.removeFirst()
      if job.state != .fellBack { return job }
    }
    while !roastJobs.isEmpty {
      let job = roastJobs.removeFirst()
      if job.keep || Date().timeIntervalSince(job.queuedAt) <= Self.roastMaxAge { return job }
    }
    return nil
  }

  /// Silence after a sentence, and at a comma. Kokoro's own pauses run sentences together.
  private static let sentencePause: TimeInterval = 0.35
  private static let commaPause: TimeInterval = 0.18
  /// Roasts any faster than this get hard to follow. Faster still (the first trip's speed run)
  /// is on purpose and keeps its speed.
  private static let roastSpeedCap: Float = 1.15
  private static let deliberatelyFast: Float = 1.3

  private func render(_ text: String, speed: Float, voice: String, with engine: KokoroAneManager) async throws -> Data {
    let fast = speed >= Self.deliberatelyFast
    let pace = fast ? speed : min(speed, Self.roastSpeedCap)
    var samples: [Float] = []
    var sampleRate = KokoroAneConstants.sampleRate
    // A speed run keeps its commas tight: "right, left, right" with pauses isn't fast.
    for (piece, pause) in Self.pieces(of: text, commas: !fast) {
      let result = try await engine.synthesizeDetailed(text: piece, voice: voice, speed: pace)
      sampleRate = result.sampleRate
      samples += result.samples
      samples += [Float](repeating: 0, count: Int(Double(sampleRate) * pause))
    }
    return try AudioWAV.data(from: samples, sampleRate: Double(sampleRate), normalize: false)
  }

  /// `text` split into sentences (and clauses at commas), each with the silence that follows it.
  private static func pieces(of text: String, commas: Bool) -> [(String, TimeInterval)] {
    var pieces: [(String, TimeInterval)] = []
    var current = ""
    func flush(_ pause: TimeInterval) {
      let piece = current.trimmingCharacters(in: .whitespaces)
      if !piece.isEmpty { pieces.append((piece, pause)) }
      current = ""
    }
    let chars = Array(text.replacingOccurrences(of: " || ", with: ". "))
    for (i, ch) in chars.enumerated() {
      current.append(ch)
      // Only at a word break, so "3.5 miles" and "St. Louis"-style dots mid-token stay put.
      let atBreak = i + 1 == chars.count || chars[i + 1] == " "
      guard atBreak else { continue }
      if ".?!".contains(ch) {
        flush(sentencePause)
      } else if commas && (ch == "," || ch == ";") {
        flush(commaPause)
      }
    }
    flush(0)
    if let last = pieces.indices.last { pieces[last].1 = 0 }
    return pieces.isEmpty ? [(text, 0)] : pieces
  }

  /// The CPU-only route, for a line the Neural Engine route failed on.
  private func cpuEngine(voice: String) async throws -> KokoroAneManager {
    if let fallbackEngine { return fallbackEngine }
    let engine = KokoroAneManager(computeUnits: .cpuOnly)
    try await engine.initialize(preloadVoices: [voice])
    fallbackEngine = engine
    return engine
  }

  private func synthesize(_ job: Job) async {
    lock.lock()
    let current = job.generation == generation
    let engine = primary
    lock.unlock()
    guard current, let engine else { return }

    let renderStart = Date()
    let wav: Data
    defer {
      Self.log.info("Kokoro rendered a \(job.nav ? "navigation" : "roast", privacy: .public) line in \(Int(Date().timeIntervalSince(renderStart) * 1000)) ms")
    }
    do {
      do {
        wav = try await render(job.text, speed: job.speed, voice: job.voice, with: engine)
      } catch {
        Self.log.error("Kokoro (Neural Engine) failed, retrying on the CPU: \(error.localizedDescription, privacy: .public)")
        wav = try await render(job.text, speed: job.speed, voice: job.voice, with: try await cpuEngine(voice: job.voice))
      }
    } catch {
      Self.log.error("Kokoro failed on a line: \(error.localizedDescription, privacy: .public)")
      BackseatLog.event("voice.error", ["error": error.localizedDescription, "text": String(job.text.prefix(60))])
      lock.lock()
      let fallBack = job.state == .waiting
      if fallBack { job.state = .fellBack }
      lock.unlock()
      if fallBack { Self.trace(job.text, voice: "system", why: "error", budget: job.nav ? job.budget : 0) }
      if fallBack { DispatchQueue.main.async { self.player.sayWithSystemVoice(nav: job.nav, job.fallback) } }
      return
    }

    lock.lock()
    let key = Self.key(job.text, job.speed, job.voice)
    cache[key] = wav
    touch(key)
    // Too late for a navigation line: the system voice already said it. The clip stays cached.
    let play = job.state == .waiting && job.generation == generation
    if play { job.state = .playing }
    lock.unlock()
    if play {
      Self.trace(job.text, voice: "kokoro", waitMs: renderStart.timeIntervalSince(job.queuedAt) * 1000,
                 renderMs: Date().timeIntervalSince(renderStart) * 1000, budget: job.nav ? job.budget : 0)
    }
    if play { DispatchQueue.main.async { self.player.play(wav, nav: job.nav, keep: job.keep) } }
  }

  /// One "voice" line in the drive log: who said it, and why the system voice did if it did.
  private static func trace(_ text: String, voice: String, why: String = "", waitMs: Double = 0, renderMs: Double = 0,
                            budget: TimeInterval = 0) {
    BackseatLog.event("voice", ["voice": voice, "why": why, "wait_ms": String(Int(waitMs)),
                                "budget_ms": String(Int(budget * 1000)),
                                "render_ms": String(Int(renderMs)), "text": String(text.prefix(60))])
  }

  /// Call with `lock` held.
  private func touch(_ key: String) {
    cacheOrder.removeAll { $0 == key }
    cacheOrder.append(key)
    while cacheOrder.count > Self.cacheLines {
      cache.removeValue(forKey: cacheOrder.removeFirst())
    }
  }

  private static func key(_ text: String, _ speed: Float, _ voice: String) -> String { "\(voice)|\(speed)|\(text)" }
}

/// Plays clips one at a time on the main queue, and is the one gate every spoken line goes through
/// (Kokoro clips and system-voice lines alike), so two voices never talk at once:
/// - nothing starts while something is talking, and clips get a short pause between them;
/// - a navigation line goes first, and if a roast is talking it fades the roast out and takes over;
///   a navigation line is never cut;
/// - a roast that has waited more than `roastStale` is dropped (replies to the driver are kept).
private final class ClipPlayer: NSObject, AVAudioPlayerDelegate {
  private struct Item {
    let wav: Data
    let nav: Bool
    var keep = false
    let queuedAt = Date()
  }

  var systemIsSpeaking: () -> Bool = { false }
  /// Called whenever a line goes to the system voice.
  var systemStoodIn: () -> Void = {}
  var isPlaying: Bool { current != nil || !navQueue.isEmpty || !roastQueue.isEmpty || !pendingSystem.isEmpty }
  private var navQueue: [Item] = []
  private var roastQueue: [Item] = []
  private var current: AVAudioPlayer?
  private var currentIsNav = false
  private var waitingSince: Date?
  /// System-voice lines waiting for the clip that's talking to finish.
  private var pendingSystem: [(nav: Bool, speak: () -> Void)] = []
  /// No clip starts before this: the pause after the last one.
  private var quietUntil = Date.distantPast
  /// How long a clip waits for the system voice before giving up on politeness.
  private static let systemWaitLimit: TimeInterval = 30
  /// A roast older than this isn't worth saying any more.
  private static let roastStale: TimeInterval = 8
  /// Silence between two clips.
  private static let gap: TimeInterval = 0.25
  /// How long a roast takes to fade out when a navigation line cuts in.
  private static let fade: TimeInterval = 0.2

  func play(_ wav: Data, nav: Bool, keep: Bool) {
    if nav {
      navQueue.append(Item(wav: wav, nav: true))
      if current != nil && !currentIsNav { cutRoast(for: "clip") }
    } else {
      roastQueue.append(Item(wav: wav, nav: false, keep: keep))
    }
    if current != nil || systemIsSpeaking() {
      BackseatLog.event("audio.queue", ["depth": String(navQueue.count + roastQueue.count), "nav": nav ? "1" : "0"])
    }
    playNext()
  }

  /// Runs `speak` (which talks with the system voice) once no clip is talking. A navigation line
  /// fades out a roast and goes now; anything else waits for the clip to finish.
  func sayWithSystemVoice(nav: Bool, _ speak: @escaping () -> Void) {
    systemStoodIn()
    if nav && current != nil && !currentIsNav { cutRoast(for: "system") }
    if current != nil {
      pendingSystem.append((nav, speak))
      BackseatLog.event("audio.queue", ["depth": String(pendingSystem.count), "nav": nav ? "1" : "0", "system": "1"])
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
    quietUntil = Date().addingTimeInterval(Self.gap)
    flushSystemLines()
    playNext()
  }

  func audioPlayerDecodeErrorDidOccur(_ player: AVAudioPlayer, error: Error?) {
    audioPlayerDidFinishPlaying(player, successfully: false)
  }

  /// The system voice queues its own utterances, so the waiting lines can all be handed over,
  /// navigation lines first.
  private func flushSystemLines() {
    let lines = pendingSystem.filter(\.nav) + pendingSystem.filter { !$0.nav }
    pendingSystem.removeAll()
    lines.forEach { $0.speak() }
  }

  /// Fades the roast that's talking and frees the speaker for a navigation line.
  private func cutRoast(for reason: String) {
    guard let roast = current, !currentIsNav else { return }
    BackseatLog.event("voice.cut", ["for": reason])
    current = nil
    roast.setVolume(0, fadeDuration: Self.fade)
    DispatchQueue.main.asyncAfter(deadline: .now() + Self.fade) { roast.stop() }
    quietUntil = Date().addingTimeInterval(Self.fade)
    flushSystemLines()
  }

  private func playNext() {
    guard current == nil else { return }
    let now = Date()
    if now < quietUntil {
      DispatchQueue.main.asyncAfter(deadline: .now() + quietUntil.timeIntervalSince(now)) { [weak self] in
        self?.playNext()
      }
      return
    }
    // Don't talk over the system voice (a line it took over).
    if systemIsSpeaking() && (waitingSince.map { Date().timeIntervalSince($0) < Self.systemWaitLimit } ?? true) {
      if waitingSince == nil { waitingSince = Date() }
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { [weak self] in self?.playNext() }
      return
    }
    waitingSince = nil
    roastQueue.removeAll { !$0.keep && Date().timeIntervalSince($0.queuedAt) > Self.roastStale }
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

/// A Core ML crash can't be caught, so a marker file is written right before Kokoro loads its
/// models (and warms up) and removed right after; finding it at launch means the app died during
/// that load. Crashes anywhere else (search, the listener, the map) never count, and nor do
/// per-line renders: one in flight on another thread when something else crashes proves nothing.
///
/// After a load crash, the next launch skips the eager warm-up and loads Kokoro on the first line
/// instead. A load that succeeds clears the streak. Three load crashes in a row keep the system
/// voice for that session only; the next launch tries again. Kokoro is never off for longer.
enum CoreMLCrashGuard {
  /// Launches in a row that found the load marker.
  static let streakKey = "backseat.coreML.loadCrashStreak"
  /// Load crashes in a row before one session goes without Kokoro.
  static let sessionLimit = 3
  /// The old guard (one crash anywhere switched Kokoro off for 30 days on that iOS build).
  private static let legacyKey = "backseat.coreML.crashes"
  private static let migratedKey = "backseat.coreML.guardV2"

  /// This session: what the guard decided at launch.
  enum Mode: Equatable {
    case normal
    /// Last launch died during the load: load on the first line, not at launch.
    case lazy
    /// Three load crashes in a row: the system voice this session.
    case paused
  }

  private(set) static var mode: Mode = .normal

  private static var markerURL: URL {
    FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
      .appendingPathComponent("BackseatKokoroRender.inflight")
  }

  /// Right before a model load or warm-up.
  static func beginLoad() {
    let url = markerURL
    try? FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try? Data("load".utf8).write(to: url, options: .atomic)
  }

  /// Right after it, whether it worked or threw.
  static func endLoad() {
    try? FileManager.default.removeItem(at: markerURL)
  }

  /// The load worked: the streak is over.
  static func loadSucceeded(defaults: UserDefaults = .standard) {
    defaults.set(0, forKey: streakKey)
    if mode != .normal { BackseatLog.event("voice.crashGuard", ["reason": "cleared after a good load"]) }
    mode = .normal
  }

  /// Once per launch, before Kokoro starts: decides `mode`.
  @discardableResult
  static func checkAtLaunch(defaults: UserDefaults = .standard) -> Mode {
    // This build replaces the old guard: forget what it recorded, so Kokoro is back right away.
    if !defaults.bool(forKey: migratedKey) {
      defaults.removeObject(forKey: legacyKey)
      defaults.set(0, forKey: streakKey)
      defaults.set(true, forKey: migratedKey)
      endLoad()
      BackseatLog.event("voice.crashGuard", ["reason": "reset for the new guard"])
    }
    guard FileManager.default.fileExists(atPath: markerURL.path) else {
      mode = .normal
      return mode
    }
    endLoad()
    let streak = defaults.integer(forKey: streakKey) + 1
    if streak >= sessionLimit {
      // Sit this session out, then start counting again.
      defaults.set(0, forKey: streakKey)
      mode = .paused
      BackseatLog.event("voice.crashGuard", ["reason": "Kokoro load crashed \(streak) launches in a row; system voice this session"])
    } else {
      defaults.set(streak, forKey: streakKey)
      mode = .lazy
      BackseatLog.event("voice.crashGuard", ["reason": "Kokoro load crashed last launch (\(streak) in a row); loading on the first line"])
    }
    return mode
  }

  /// "Use Kokoro now" in Settings.
  static func override(defaults: UserDefaults = .standard) {
    defaults.set(0, forKey: streakKey)
    mode = .normal
    BackseatLog.event("voice.crashGuard", ["reason": "driver chose Kokoro now"])
  }
}
