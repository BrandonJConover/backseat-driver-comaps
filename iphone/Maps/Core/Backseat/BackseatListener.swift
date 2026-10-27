import AVFoundation
import Speech

/// The driver talks back, and the text goes to the navigator. Hands-free (the default), the mic
/// stays on while a route is being followed, so the driver can just talk; otherwise they tap the
/// mic, say "Texas plate", and it stops after a pause. Apple's speech recognizer, on the device.
@objc final class BackseatListener: NSObject {
  @objc static let shared = BackseatListener()

  /// Posted on the main thread when listening starts or stops, to update the mic buttons.
  static let listeningChanged = Notification.Name("BackseatListeningChanged")

  /// UserDefaults key: listen the whole time a route is followed.
  static let handsFreeKey = "backseat.handsFree"

  @objc static var handsFree: Bool {
    get { UserDefaults.standard.object(forKey: handsFreeKey) as? Bool ?? true }
    set {
      UserDefaults.standard.set(newValue, forKey: handsFreeKey)
      shared.followNavigation()
    }
  }

  /// Apple ends a recognition task after about a minute; start a fresh one before that.
  private static let segmentLimit: TimeInterval = 50
  /// Silence that ends what the driver said.
  private static let pauseToFinish: TimeInterval = 1.2

  private let recognizer = SFSpeechRecognizer(locale: Locale(identifier: "en-US"))
  private let audioEngine = AVAudioEngine()
  private var request: SFSpeechAudioBufferRecognitionRequest?
  private var task: SFSpeechRecognitionTask?
  private var timeout: Timer?
  private var segmentTimer: Timer?
  private var speakingCheck: Timer?
  private var heard = ""
  /// The GPS talked during this stretch, so what was heard may be its own voice.
  private var heardTheGPS = false
  /// Listening continuously, until the route ends or the driver taps the mic.
  private var continuous = false
  private var navigating = false
  /// The driver tapped the mic to mute hands-free listening for this route.
  private var mutedForRoute = false

  @objc private(set) var isListening = false {
    didSet { NotificationCenter.default.post(name: Self.listeningChanged, object: self) }
  }

  private override init() {
    super.init()
    NotificationCenter.default.addObserver(forName: BackseatTalkButton.navigationVisibilityChanged, object: nil,
                                           queue: .main) { [weak self] note in
      guard let self else { return }
      self.navigating = (note.object as? Bool) ?? false
      if !self.navigating { self.mutedForRoute = false }
      self.followNavigation()
    }
    NotificationCenter.default.addObserver(forName: .AVAudioEngineConfigurationChange, object: audioEngine,
                                           queue: .main) { [weak self] _ in
      // A headset or CarPlay came or went, and the engine stopped with the old route.
      guard let self, self.continuous else { return }
      self.cleanUp()
      self.followNavigation()
    }
  }

  /// Makes sure the listener exists, so it hears when navigation starts.
  @objc static func prepare() {
    _ = shared
  }

  /// Hands-free: pauses the conversation for this route, or resumes it. Otherwise listens for one
  /// phrase, or stops early and sends what was heard so far.
  @objc func toggle() {
    if continuous {
      mutedForRoute = true
      cleanUp()
      return
    }
    if isListening {
      finish()
      return
    }
    if navigating && Self.handsFree {
      mutedForRoute = false
      followNavigation()
      return
    }
    withPermission { self.start(continuous: false) }
  }

  /// Listens while a route is followed and hands-free is on, and stops otherwise.
  private func followNavigation() {
    let want = navigating && Self.handsFree && !mutedForRoute
    if want && !continuous {
      if isListening { cleanUp() }
      withPermission { self.start(continuous: true) }
    } else if !want && continuous {
      cleanUp()
    }
  }

  private func withPermission(_ then: @escaping () -> Void) {
    SFSpeechRecognizer.requestAuthorization { status in
      DispatchQueue.main.async {
        guard status == .authorized else {
          LOG(.warning, "Backseat: speech recognition not allowed (\(status.rawValue))")
          return
        }
        AVAudioApplication.requestRecordPermission { granted in
          DispatchQueue.main.async {
            if granted {
              then()
            } else {
              LOG(.warning, "Backseat: microphone not allowed")
            }
          }
        }
      }
    }
  }

  private func start(continuous: Bool) {
    guard let recognizer, recognizer.isAvailable, !isListening else { return }
    let session = AVAudioSession.sharedInstance()
    do {
      if continuous {
        // Voice chat cancels the GPS's own voice from the mic. Music keeps playing undimmed: a
        // route-long session would otherwise duck it the whole way.
        try session.setCategory(.playAndRecord, mode: .voiceChat,
                                options: [.mixWithOthers, .defaultToSpeaker, .allowBluetoothA2DP])
      } else {
        try session.setCategory(.playAndRecord, mode: .measurement, options: [.duckOthers, .defaultToSpeaker])
      }
      try session.setActive(true, options: .notifyOthersOnDeactivation)
    } catch {
      LOG(.error, "Backseat: audio session failed: \(error)")
      Self.restorePlayback()
      return
    }

    let input = audioEngine.inputNode
    input.removeTap(onBus: 0)
    input.installTap(onBus: 0, bufferSize: 1024, format: input.outputFormat(forBus: 0)) { [weak self] buffer, _ in
      self?.request?.append(buffer)
    }
    audioEngine.prepare()
    do {
      try audioEngine.start()
    } catch {
      LOG(.error, "Backseat: audio engine failed: \(error)")
      cleanUp()
      return
    }
    self.continuous = continuous
    isListening = true
    startSegment()
    if continuous {
      speakingCheck = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { [weak self] _ in
        if KokoroVoice.shared.anyVoiceSpeaking { self?.heardTheGPS = true }
      }
    }
  }

  /// One recognition task: a phrase, or up to `segmentLimit` of listening.
  private func startSegment() {
    guard let recognizer, isListening, request == nil else { return }
    let request = SFSpeechAudioBufferRecognitionRequest()
    request.shouldReportPartialResults = true
    if recognizer.supportsOnDeviceRecognition {
      request.requiresOnDeviceRecognition = true
    }
    // Most answers are one place name.
    request.contextualStrings = ["plate", "autopilot", "take me to", "work", "home", "Hawaii", "Alaska", "Colorado", "New Mexico", "Texas", "yes", "no"]
    self.request = request
    heard = ""
    heardTheGPS = KokoroVoice.shared.anyVoiceSpeaking

    task = recognizer.recognitionTask(with: request) { [weak self] result, error in
      DispatchQueue.main.async {
        guard let self, self.request === request else { return }
        if let result {
          self.heard = result.bestTranscription.formattedString
          // A short pause after speaking ends it; a final result ends it at once.
          self.restartTimeout(after: result.isFinal ? 0 : Self.pauseToFinish)
        }
        if error != nil {
          self.finish()
        }
      }
    }
    if continuous {
      timeout?.invalidate()
      timeout = nil
      segmentTimer?.invalidate()
      segmentTimer = Timer.scheduledTimer(withTimeInterval: Self.segmentLimit, repeats: false) { [weak self] _ in
        self?.finish()
      }
    } else {
      restartTimeout(after: 6)
    }
  }

  private func restartTimeout(after seconds: TimeInterval) {
    timeout?.invalidate()
    timeout = Timer.scheduledTimer(withTimeInterval: seconds, repeats: false) { [weak self] _ in
      self?.finish()
    }
  }

  /// Sends what was heard, then listens again when hands-free.
  private func finish() {
    guard isListening else { return }
    let text = heard.trimmingCharacters(in: .whitespacesAndNewlines)
    let echo = heardTheGPS || KokoroVoice.shared.anyVoiceSpeaking
    if continuous {
      timeout?.invalidate()
      timeout = nil
      segmentTimer?.invalidate()
      segmentTimer = nil
      request?.endAudio()
      task?.cancel()
      request = nil
      task = nil
      // The next one starts after this turn, so a failing recognizer can't spin.
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in self?.startSegment() }
    } else {
      cleanUp()
    }
    guard !text.isEmpty else { return }
    if continuous && echo {
      LOG(.info, "Backseat: dropped what was heard while the GPS was talking: \(text)")
      return
    }
    LOG(.info, "Backseat heard: \(text)")
    MWMBackseatRewriter.driverSaid(text)
  }

  private func cleanUp() {
    timeout?.invalidate()
    timeout = nil
    segmentTimer?.invalidate()
    segmentTimer = nil
    speakingCheck?.invalidate()
    speakingCheck = nil
    audioEngine.stop()
    audioEngine.inputNode.removeTap(onBus: 0)
    request?.endAudio()
    task?.cancel()
    request = nil
    task = nil
    continuous = false
    Self.restorePlayback()
    if isListening {
      isListening = false
    }
  }

  /// Back to the voice-guidance session MWMTextToSpeech sets up, so the GPS talks over music again.
  private static func restorePlayback() {
    let session = AVAudioSession.sharedInstance()
    try? session.setCategory(.playback, mode: .voicePrompt, options: [.interruptSpokenAudioAndMixWithOthers, .duckOthers])
    if !KokoroVoice.shared.anyVoiceSpeaking {
      try? session.setActive(false, options: .notifyOthersOnDeactivation)
    }
  }
}
