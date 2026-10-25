// Backseat Driver's Kokoro voice on iOS: EpubToM4B's MLX port of Kokoro with MisakiSwift G2P.
//
// Kokoro runs on the CPU only. A navigation app speaks with the screen off, iOS refuses GPU work
// from the background, and switching devices mid-trip is what crashed EpubToM4B; the CPU path
// avoids that whole class of crash.
//
// Navigation lines never wait on Kokoro: one that isn't ready to play within `navDeadline` goes to
// the system voice, and its Kokoro audio is kept for the next time the same line comes up. Roasts
// queue, go stale after `roastMaxAge`, and are cut off when a navigation line needs the speaker.

import AVFoundation
import CryptoKit
import Foundation
import MLX
import MLXRandom
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
  private static let voice = TTSVoice.afHeart

  // The weights EpubToM4B uses, pinned to an immutable revision and checked after download.
  private static let modelURL = URL(string:
    "https://huggingface.co/mlx-community/Kokoro-82M-bf16/resolve/a71e4d38b236d968966a2002c4c895dbd12b1c3c/kokoro-v1_0.safetensors")!
  private static let modelSHA256 = "4e9ecdf03b8b6cf906070390237feda473dc13327cb8d56a43deaa374c02acd8"

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
  private let jobsReady = DispatchSemaphore(value: 0)
  private var cache: [String: Data] = [:]
  private var cacheOrder: [String] = []
  private var tts: KokoroTTS?
  private var started = false
  private let player = ClipPlayer()

  @objc var isReady: Bool {
    lock.lock(); defer { lock.unlock() }
    return tts != nil
  }

  /// True while Kokoro has a line queued or playing, so the audio session must stay active.
  /// Main queue only.
  @objc var isBusy: Bool {
    lock.lock()
    let queued = !navJobs.isEmpty || !roastJobs.isEmpty
    lock.unlock()
    return queued || player.isPlaying
  }

  /// Downloads the weights once (Wi-Fi only) and loads Kokoro in the background.
  @objc func start() {
    #if targetEnvironment(simulator)
    // MLX can't run in the Simulator; the system voice speaks there.
    return
    #else
    lock.lock()
    if started { lock.unlock(); return }
    started = true
    lock.unlock()
    player.systemIsSpeaking = { [weak self] in self?.systemIsSpeaking?() ?? false }
    Task.detached(priority: .utility) { [self] in
      do {
        try await ensureModel()
        startWorker()
      } catch {
        Self.log.error("Kokoro unavailable, the system voice stays on: \(error.localizedDescription, privacy: .public)")
        allowRetry()
      }
    }
    #endif
  }

  /// Lets the next start() try the download again.
  private func allowRetry() {
    lock.lock(); started = false; lock.unlock()
  }

  /// Says `text` with Kokoro, or calls `fallback` (on the main queue) to say it with the system voice.
  @objc func speak(_ text: String, nav: Bool, speed: Float, fallback: @escaping () -> Void) {
    lock.lock()
    guard tts != nil else {
      lock.unlock()
      DispatchQueue.main.async(execute: fallback)
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
    jobsReady.signal()

    guard nav else { return }
    DispatchQueue.main.asyncAfter(deadline: .now() + Self.navDeadline) { [self] in
      lock.lock()
      let fallBack = job.state == .waiting && job.generation == generation
      if fallBack { job.state = .fellBack }
      lock.unlock()
      guard fallBack else { return }
      Self.log.debug("Kokoro too slow for a navigation line, using the system voice")
      // The system voice must not talk over a roast.
      player.interruptRoast()
      job.fallback()
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

  // MARK: - Model

  private static var modelFile: URL {
    let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
    // WeightLoader looks here.
    return support.appendingPathComponent("kokoro_models/kokoro-v1_0.safetensors")
  }

  private func ensureModel() async throws {
    let file = Self.modelFile
    if FileManager.default.fileExists(atPath: file.path) { return }
    try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)

    let config = URLSessionConfiguration.default
    // About 330 MB: wait for Wi-Fi rather than use cellular data.
    config.allowsCellularAccess = false
    config.allowsExpensiveNetworkAccess = false
    config.allowsConstrainedNetworkAccess = false
    config.waitsForConnectivity = true
    config.timeoutIntervalForResource = 24 * 60 * 60
    let session = URLSession(configuration: config)
    Self.log.info("Downloading the Kokoro model")
    let (downloaded, response) = try await session.download(from: Self.modelURL)
    defer { try? FileManager.default.removeItem(at: downloaded) }
    guard (response as? HTTPURLResponse)?.statusCode == 200 else {
      throw URLError(.badServerResponse)
    }
    guard try Self.sha256(of: downloaded) == Self.modelSHA256 else {
      throw URLError(.cannotDecodeContentData)
    }
    let partial = file.appendingPathExtension("partial")
    try? FileManager.default.removeItem(at: partial)
    try FileManager.default.moveItem(at: downloaded, to: partial)
    var values = URLResourceValues()
    values.isExcludedFromBackup = true
    var partialURL = partial
    try? partialURL.setResourceValues(values)
    try FileManager.default.moveItem(at: partial, to: file)
    Self.log.info("Kokoro model installed")
  }

  private static func sha256(of url: URL) throws -> String {
    let handle = try FileHandle(forReadingFrom: url)
    defer { try? handle.close() }
    var hasher = SHA256()
    while let chunk = try handle.read(upToCount: 1 << 20), !chunk.isEmpty {
      hasher.update(data: chunk)
    }
    return hasher.finalize().map { String(format: "%02x", $0) }.joined()
  }

  // MARK: - Synthesis

  private func startWorker() {
    let thread = Thread { [self] in
      // CPU only, and no compiled MLX functions: iOS has no CPU compiler for them.
      MLX.compile(enable: false)
      Device.setDefault(device: .cpu)
      Memory.cacheLimit = 32 * 1024 * 1024
      do {
        let engine = try KokoroMLXErrors.capture { try KokoroTTS() }
        // The first synthesis is slow; pay for it now, not on a turn.
        engine.warmUp()
        lock.lock(); tts = engine; lock.unlock()
        Self.log.info("Kokoro voice ready")
      } catch {
        Self.log.error("Could not load Kokoro: \(error.localizedDescription, privacy: .public)")
        return
      }
      while true {
        jobsReady.wait()
        guard let job = nextJob() else { continue }
        synthesize(job)
      }
    }
    thread.name = "KokoroSynth"
    thread.qualityOfService = .userInitiated
    thread.stackSize = 8 << 20
    thread.start()
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

  private func synthesize(_ job: Job) {
    lock.lock()
    let current = job.generation == generation
    let engine = tts
    lock.unlock()
    guard current, let engine else { return }

    let wav: Data
    do {
      wav = try autoreleasepool {
        let samples: [Float] = try Device.withDefaultDevice(.cpu) {
          try withRandomState(MLXRandom.RandomState()) {
            try KokoroMLXErrors.capture {
              let audio = try engine.generateAudio(voice: Self.voice, text: job.text, speed: job.speed)
              audio.eval()
              return audio.asArray(Float.self)
            }
          }
        }
        Memory.clearCache()
        return Self.wavData(samples, sampleRate: 24000)
      }
    } catch {
      Self.log.error("Kokoro failed on a line: \(error.localizedDescription, privacy: .public)")
      lock.lock()
      let fallBack = job.state == .waiting
      if fallBack { job.state = .fellBack }
      lock.unlock()
      if fallBack { DispatchQueue.main.async(execute: job.fallback) }
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

  private static func wavData(_ samples: [Float], sampleRate: Int) -> Data {
    var data = Data(capacity: 44 + samples.count * 2)
    func append<T: FixedWidthInteger>(_ value: T) { withUnsafeBytes(of: value.littleEndian) { data.append(contentsOf: $0) } }
    data.append(contentsOf: Array("RIFF".utf8)); append(UInt32(36 + samples.count * 2))
    data.append(contentsOf: Array("WAVE".utf8))
    data.append(contentsOf: Array("fmt ".utf8)); append(UInt32(16)); append(UInt16(1)); append(UInt16(1))
    append(UInt32(sampleRate)); append(UInt32(sampleRate * 2)); append(UInt16(2)); append(UInt16(16))
    data.append(contentsOf: Array("data".utf8)); append(UInt32(samples.count * 2))
    for sample in samples { append(Int16(max(-1, min(1, sample)) * Float(Int16.max))) }
    return data
  }
}

/// Plays clips one at a time on the main queue: navigation clips first, and they cut off a roast.
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

  func play(_ wav: Data, nav: Bool) {
    if nav {
      navQueue.append(Item(wav: wav, nav: true))
      if current != nil && !currentIsNav { finishCurrent() }
    } else {
      roastQueue.append(Item(wav: wav, nav: false))
    }
    playNext()
  }

  func interruptRoast() {
    if current != nil && !currentIsNav { finishCurrent() }
    playNext()
  }

  func stop() {
    navQueue.removeAll()
    roastQueue.removeAll()
    current?.stop()
    current = nil
  }

  func audioPlayerDidFinishPlaying(_ player: AVAudioPlayer, successfully flag: Bool) {
    guard player === current else { return }
    current = nil
    playNext()
  }

  private func finishCurrent() {
    current?.stop()
    current = nil
  }

  private func playNext() {
    guard current == nil else { return }
    // Don't talk over the system voice (a navigation line it took over); give it up to 8 s.
    if systemIsSpeaking() && (waitingSince.map { Date().timeIntervalSince($0) < 8 } ?? true) {
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
