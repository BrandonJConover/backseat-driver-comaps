import AVFoundation
import Speech

/// The driver talks back: tap the mic, say "Texas plate", and the text goes to the navigator.
/// Uses Apple's speech recognizer, on the device when the language model is there.
@objc final class BackseatListener: NSObject {
  @objc static let shared = BackseatListener()

  /// Called on the main thread when listening starts or stops, to update the button.
  var onListeningChanged: ((Bool) -> Void)?

  private let recognizer = SFSpeechRecognizer(locale: Locale(identifier: "en-US"))
  private let audioEngine = AVAudioEngine()
  private var request: SFSpeechAudioBufferRecognitionRequest?
  private var task: SFSpeechRecognitionTask?
  private var timeout: Timer?
  private var heard = ""

  private(set) var isListening = false {
    didSet { onListeningChanged?(isListening) }
  }

  /// Starts listening, or stops early and sends what was heard so far.
  @objc func toggle() {
    if isListening {
      finish()
      return
    }
    SFSpeechRecognizer.requestAuthorization { status in
      DispatchQueue.main.async {
        guard status == .authorized else {
          LOG(.warning, "Backseat: speech recognition not allowed (\(status.rawValue))")
          return
        }
        AVAudioApplication.requestRecordPermission { granted in
          DispatchQueue.main.async {
            if granted {
              self.start()
            } else {
              LOG(.warning, "Backseat: microphone not allowed")
            }
          }
        }
      }
    }
  }

  private func start() {
    guard let recognizer, recognizer.isAvailable, !isListening else { return }
    let session = AVAudioSession.sharedInstance()
    do {
      try session.setCategory(.playAndRecord, mode: .measurement, options: [.duckOthers, .defaultToSpeaker])
      try session.setActive(true, options: .notifyOthersOnDeactivation)
    } catch {
      LOG(.error, "Backseat: audio session failed: \(error)")
      return
    }

    let request = SFSpeechAudioBufferRecognitionRequest()
    request.shouldReportPartialResults = true
    if recognizer.supportsOnDeviceRecognition {
      request.requiresOnDeviceRecognition = true
    }
    // Most answers are one place name.
    request.contextualStrings = ["plate", "Hawaii", "Alaska", "Colorado", "New Mexico", "Texas", "yes", "no"]
    self.request = request
    heard = ""

    let input = audioEngine.inputNode
    input.removeTap(onBus: 0)
    input.installTap(onBus: 0, bufferSize: 1024, format: input.outputFormat(forBus: 0)) { buffer, _ in
      request.append(buffer)
    }
    audioEngine.prepare()
    do {
      try audioEngine.start()
    } catch {
      LOG(.error, "Backseat: audio engine failed: \(error)")
      cleanUp()
      return
    }

    task = recognizer.recognitionTask(with: request) { [weak self] result, error in
      DispatchQueue.main.async {
        guard let self else { return }
        if let result {
          self.heard = result.bestTranscription.formattedString
          // A short pause after speaking ends it; a final result ends it at once.
          self.restartTimeout(after: result.isFinal ? 0 : 1.5)
        }
        if error != nil {
          self.finish()
        }
      }
    }
    isListening = true
    restartTimeout(after: 6)
  }

  private func restartTimeout(after seconds: TimeInterval) {
    timeout?.invalidate()
    timeout = Timer.scheduledTimer(withTimeInterval: seconds, repeats: false) { [weak self] _ in
      self?.finish()
    }
  }

  private func finish() {
    guard isListening else { return }
    let text = heard.trimmingCharacters(in: .whitespacesAndNewlines)
    cleanUp()
    if !text.isEmpty {
      LOG(.info, "Backseat heard: \(text)")
      MWMBackseatRewriter.driverSaid(text)
    }
  }

  private func cleanUp() {
    timeout?.invalidate()
    timeout = nil
    audioEngine.stop()
    audioEngine.inputNode.removeTap(onBus: 0)
    request?.endAudio()
    task?.cancel()
    request = nil
    task = nil
    try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
    if isListening {
      isListening = false
    }
  }
}
