import AVFoundation
import CoreLocation
import UIKit
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
  /// The mic is paused while the GPS talks, so its voice plays at full volume and isn't heard back.
  private var pausedForSpeech = false
  /// Takes the next thing heard instead of the navigator, e.g. an answer to a setup question.
  private var answerHandler: ((String) -> Void)?
  /// How long the mic waits for the first word when opened for one phrase.
  private var firstWordWait: TimeInterval = 6

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

  /// Sends the next thing the driver says (empty when they say nothing) to `handler` instead of the
  /// navigator, listening for it if the mic is off.
  func listenForAnswer(waiting: TimeInterval = 6, _ handler: @escaping (String) -> Void) {
    answerHandler = handler
    firstWordWait = waiting
    if !isListening {
      withPermission { self.start(continuous: false) }
    }
  }

  /// Stops waiting for an answer; what's said next goes to the navigator again.
  func stopWaitingForAnswer() {
    answerHandler = nil
  }

  /// "Stop listening": hands-free stays off for the rest of this route, as if the mic was tapped.
  @objc func pauseForRoute() {
    mutedForRoute = true
    if isListening { cleanUp() }
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
        // Not voice chat: its voice processing turned the GPS's own voice down to nothing. The mic
        // pauses while the GPS talks instead. Music keeps playing undimmed: a route-long session
        // would otherwise duck it the whole way.
        try session.setCategory(.playAndRecord, mode: .default,
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
        self?.followSpeech()
      }
    }
  }

  /// Pauses the mic while Kokoro or the system voice talks, and listens again once it's done.
  private func followSpeech() {
    let speaking = KokoroVoice.shared.anyVoiceSpeaking
    if speaking && !pausedForSpeech {
      pausedForSpeech = true
      endSegment()
      audioEngine.pause()
    } else if !speaking && pausedForSpeech {
      pausedForSpeech = false
      do {
        try audioEngine.start()
      } catch {
        LOG(.error, "Backseat: audio engine didn't restart: \(error)")
        cleanUp()
        return
      }
      startSegment()
    }
  }

  /// Drops the recognition task in progress without sending what it heard.
  private func endSegment() {
    timeout?.invalidate()
    timeout = nil
    segmentTimer?.invalidate()
    segmentTimer = nil
    request?.endAudio()
    task?.cancel()
    request = nil
    task = nil
  }

  /// One recognition task: a phrase, or up to `segmentLimit` of listening.
  private func startSegment() {
    guard let recognizer, isListening, request == nil, !pausedForSpeech else { return }
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
      restartTimeout(after: firstWordWait)
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
      endSegment()
      // The next one starts after this turn, so a failing recognizer can't spin.
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in self?.startSegment() }
    } else {
      firstWordWait = 6
      cleanUp()
    }
    if let answer = answerHandler, !(continuous && echo) {
      // An empty answer only counts when the mic was opened just for it.
      if !text.isEmpty || !continuous {
        answerHandler = nil
        answer(text)
      }
      return
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
    pausedForSpeech = false
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

/// The spoken first-run setup: the GPS introduces itself, gets the driver's name (and gets it
/// wrong on purpose), then asks where they're headed. Runs once, before the first trip.
@objc final class BackseatSetup: NSObject {
  private static let doneKey = "backseat.setupDone"
  private static var running = false

  @objc static func startIfNeeded() {
    guard !running, !UserDefaults.standard.bool(forKey: doneKey), MWMBackseatRewriter.driverName.isEmpty else { return }
    running = true
    // Give Kokoro up to half a minute to load, so the introduction is in its voice.
    waitForKokoro(tries: 60) {
      let voiceName = KokoroVoice.voiceOptions.first { $0.id == KokoroVoice.selectedVoiceID }?.spokenName ?? "Echo"
      say(.intro, voiceName: voiceName)
      askName(voiceName: voiceName, tries: 3)
    }
  }

  private static func askName(voiceName: String, tries: Int) {
    say(.name, voiceName: voiceName)
    listen { answer in
      if MWMBackseatRewriter.setupHeard(.name, answer: answer) || tries <= 1 {
        say(.wrongName, voiceName: voiceName)
        listen { answer in
          _ = MWMBackseatRewriter.setupHeard(.wrongName, answer: answer)
          say(.whereTo, voiceName: voiceName)
          UserDefaults.standard.set(true, forKey: doneKey)
          running = false
          listen { answer in
            if !answer.isEmpty { MWMBackseatRewriter.driverSaid(answer) }
          }
        }
      } else {
        askName(voiceName: voiceName, tries: tries - 1)
      }
    }
  }

  private static func say(_ step: MWMBackseatSetupStep, voiceName: String) {
    let line = MWMBackseatRewriter.setupLine(step, voiceName: voiceName)
    guard !line.isEmpty else { return }
    KokoroVoice.shared.sayReply(line, voiceID: nil) { MWMTextToSpeech.tts().play(line) }
  }

  /// Listens once the GPS has finished talking, so it doesn't hear itself.
  static func listen(waiting: TimeInterval = 6, _ handler: @escaping (String) -> Void) {
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
      if KokoroVoice.shared.anyVoiceSpeaking {
        listen(waiting: waiting, handler)
      } else {
        BackseatListener.shared.listenForAnswer(waiting: waiting, handler)
      }
    }
  }

  static var isDone: Bool {
    !running && (UserDefaults.standard.bool(forKey: doneKey) || !MWMBackseatRewriter.driverName.isEmpty)
  }

  static func waitForKokoro(tries: Int, then: @escaping () -> Void) {
    if KokoroVoice.shared.isReady || tries <= 0 {
      then()
      return
    }
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { waitForKokoro(tries: tries - 1, then: then) }
  }
}

/// When the app opens with no route, the GPS asks where to, so the driver can say it instead of
/// typing. At most every ten minutes, and not during first-run setup, which asks on its own.
@objc final class BackseatWhereTo: NSObject {
  private static let interval: TimeInterval = 10 * 60
  /// Long enough to find a place to stop and think.
  private static let answerWait: TimeInterval = 45
  private static var lastAsked: Date?
  private static var observer: NSObjectProtocol?

  @objc static func install() {
    observer = NotificationCenter.default.addObserver(forName: UIApplication.didBecomeActiveNotification,
                                                      object: nil, queue: .main) { _ in askIfNeeded() }
  }

  private static func askIfNeeded() {
    guard BackseatSetup.isDone, !MWMRouter.isRoutingActive(), MWMTextToSpeech.isTTSEnabled() else { return }
    if let lastAsked, Date().timeIntervalSince(lastAsked) < interval { return }
    lastAsked = Date()
    BackseatSetup.waitForKokoro(tries: 60) {
      guard !MWMRouter.isRoutingActive() else { return }
      let question = MWMBackseatRewriter.askWhereTo
      guard !question.isEmpty else { return }
      KokoroVoice.shared.sayReply(question, voiceID: nil) { MWMTextToSpeech.tts().play(question) }
      BackseatSetup.listen(waiting: answerWait) { answer in
        if answer.isEmpty {
          let shrug = MWMBackseatRewriter.noAnswerToWhereTo
          if !shrug.isEmpty {
            KokoroVoice.shared.sayReply(shrug, voiceID: nil) { MWMTextToSpeech.tts().play(shrug) }
          }
        } else {
          MWMBackseatRewriter.driverSaid(answer)
        }
        BackseatFileQuestions.askIfStillParked(after: 20)
      }
    }
  }
}

/// "Take me to Starbucks": searches like the search screen does (and shows it there), then reads
/// the three nearest results out loud and routes to the one the driver picks by voice.
@objc final class BackseatVoiceSearch: NSObject, MWMSearchObserver {
  private static var current: BackseatVoiceSearch?
  /// How long to wait for the search, and for the driver to pick.
  private static let searchWait: TimeInterval = 10
  private static let pickWait: TimeInterval = 20

  private let query: String
  private var results: [SearchResult] = []
  private var finished = false

  private init(query: String) {
    self.query = query
  }

  @objc static func start(_ query: String) {
    current?.finish()
    let search = BackseatVoiceSearch(query: query)
    current = search
    Search.add(search)
    MapsAppDelegate.theApp().searchText(query)
    DispatchQueue.main.asyncAfter(deadline: .now() + searchWait) { [weak search] in search?.speakResults() }
  }

  func onSearchCompleted() {
    speakResults()
  }

  /// The three nearest results, named with their street: "Starbucks Colfax Avenue".
  private func speakResults() {
    guard !finished else { return }
    finish()
    let here = LocationManager.lastLocation()
    let found = Search.getResults().filter { $0.itemType == .regular }.map { result -> (SearchResult, Double) in
      let place = CLLocation(latitude: result.coordinate.latitude, longitude: result.coordinate.longitude)
      return (result, here.map { $0.distance(from: place) } ?? 0)
    }.sorted { $0.1 < $1.1 }.prefix(3)
    results = found.map(\.0)
    let names = found.map { result, _ -> String in
      let street = result.addressText.split(separator: ",").first.map { String($0).trimmingCharacters(in: .whitespaces) }
      guard let street, !street.isEmpty, street != result.titleText else { return result.titleText }
      return "\(result.titleText) \(street)"
    }
    var pick = -1
    var none = ObjCBool(false)
    let line = MWMBackseatRewriter.speakSearchResults(query, names: names, distances: found.map { NSNumber(value: $0.1) },
                                                      pick: &pick, none: &none)
    say(line)
    if pick >= 0 {
      route(to: pick)
    } else if !none.boolValue {
      listenForPick(tries: 2)
    }
  }

  private func listenForPick(tries: Int) {
    BackseatSetup.listen(waiting: Self.pickWait) { [self] answer in
      guard !answer.isEmpty else { return }
      var index = -1
      let line = MWMBackseatRewriter.pickSearchResult(answer, index: &index)
      say(line)
      if index >= 0 {
        route(to: index)
      } else if tries > 1 {
        listenForPick(tries: tries - 1)
      }
    }
  }

  private func route(to index: Int) {
    guard results.indices.contains(index) else { return }
    let result = results[index]
    MWMBackseatRewriter.route(to: result.coordinate, title: result.titleText)
  }

  private func say(_ line: String) {
    guard !line.isEmpty else { return }
    KokoroVoice.shared.sayReply(line, voiceID: nil) { MWMTextToSpeech.tts().play(line) }
  }

  private func finish() {
    finished = true
    Search.remove(self)
  }
}

/// "For your file" questions, one at a time at a quiet moment: a minute into a route with no turn
/// for a mile and nobody talking, or right after "where to?" when no route started. The navigator
/// decides whether there's anything to ask (at most one every six hours).
@objc final class BackseatFileQuestions: NSObject {
  private static let checkEvery: TimeInterval = 15
  private static let settleTime: TimeInterval = 60
  private static let quietMeters: Double = 1500
  private static var timer: Timer?
  private static var routeStartedAt: Date?
  private static var asking = false

  @objc static func install() {
    timer = Timer.scheduledTimer(withTimeInterval: checkEvery, repeats: true) { _ in checkWhileDriving() }
  }

  private static func checkWhileDriving() {
    guard MWMRouter.isOnRoute() else {
      routeStartedAt = nil
      return
    }
    let started = routeStartedAt ?? Date()
    routeStartedAt = started
    guard Date().timeIntervalSince(started) > settleTime, MWMBackseatRewriter.metersToNextTurn > quietMeters else { return }
    ask()
  }

  /// After the "where to?" exchange: if no route started, it's a quiet moment.
  static func askIfStillParked(after delay: TimeInterval) {
    DispatchQueue.main.asyncAfter(deadline: .now() + delay) {
      if !MWMRouter.isRoutingActive() { ask() }
    }
  }

  private static func ask() {
    guard !asking, !KokoroVoice.shared.anyVoiceSpeaking, !BackseatListener.shared.isListening,
          MWMTextToSpeech.isTTSEnabled() else { return }
    let question = MWMBackseatRewriter.fileQuestion
    guard !question.isEmpty else { return }
    asking = true
    KokoroVoice.shared.sayReply(question, voiceID: nil) { MWMTextToSpeech.tts().play(question) }
    // Hands-free never sends silence, so give up on an answer after a while.
    DispatchQueue.main.asyncAfter(deadline: .now() + 40) {
      guard asking else { return }
      asking = false
      BackseatListener.shared.stopWaitingForAnswer()
    }
    BackseatSetup.listen(waiting: 15) { answer in
      guard asking else { return }
      asking = false
      let reply = MWMBackseatRewriter.fileAnswer(answer)
      if !reply.isEmpty {
        KokoroVoice.shared.sayReply(reply, voiceID: nil) { MWMTextToSpeech.tts().play(reply) }
      }
    }
  }
}
