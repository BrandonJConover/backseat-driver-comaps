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
  /// What the answer is for, in the drive log: destination, survey, mileage...
  private var answerPurpose = "answer"
  /// How long the mic waits for the first word when opened for one phrase.
  private var firstWordWait: TimeInterval = 6
  /// After this, a hands-free wait for a destination is over. Hands-free never sends silence, so a
  /// wait with no end would take whatever the driver says next, minutes later and about anything.
  private var answerDeadline: Date?
  /// Gets everything heard as it's heard, with the mic open while the GPS talks. Empty text means a
  /// fresh transcript has started.
  private var overhearHandler: ((String) -> Void)?
  /// Names the recognizer should expect while overhearing.
  private var overhearNames: [String] = []

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
      // The route started: nobody's picking a destination any more.
      if self.navigating && self.answerDeadline != nil { self.stopWaitingForAnswer() }
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
  func listenForAnswer(waiting: TimeInterval = 6, for purpose: String = "answer",
                       _ handler: @escaping (String) -> Void) {
    answerHandler = handler
    answerPurpose = purpose
    firstWordWait = waiting
    answerDeadline = purpose == "destination" ? Date().addingTimeInterval(waiting) : nil
    if !isListening {
      withPermission { self.start(continuous: false) }
    }
  }

  /// 20 Questions: the driver answers out loud. Hands-free already hears it while a route is
  /// followed; otherwise the mic opens once, after the GPS has finished talking, and what's heard
  /// goes to the navigator like anything else the driver says.
  @objc func listenForGameAnswer() {
    if isListening && continuous { return }
    BackseatSetup.listen(waiting: 15, for: "game") { answer in
      guard !answer.isEmpty else { return }
      MWMBackseatRewriter.driverSaid(answer)
    }
  }

  /// Stops waiting for an answer; what's said next goes to the navigator again.
  func stopWaitingForAnswer() {
    answerHandler = nil
    answerDeadline = nil
  }

  /// Opens the mic for `handler` and keeps it open while the GPS talks, so the driver can cut in.
  /// Without permission the handler just never hears anything.
  func startOverhearing(names: [String], _ handler: @escaping (String) -> Void) {
    overhearHandler = handler
    overhearNames = names
    if isListening { cleanUp() }
    withPermission { [weak self] in
      guard let self, self.overhearHandler != nil else { return }
      self.start(continuous: true)
    }
  }

  /// Back to listening the way the driver has it set.
  func stopOverhearing() {
    guard overhearHandler != nil else { return }
    overhearHandler = nil
    overhearNames = []
    cleanUp()
    followNavigation()
  }

  /// The voices' session handling can stop the engine between lines; picks it back up.
  func keepOverhearing() {
    guard overhearHandler != nil, isListening, !audioEngine.isRunning else { return }
    do {
      try AVAudioSession.sharedInstance().setActive(true, options: .notifyOthersOnDeactivation)
      try audioEngine.start()
    } catch {
      LOG(.warning, "Backseat: audio engine didn't restart: \(error)")
    }
  }

  /// "Stop listening": hands-free stays off for the rest of this route, as if the mic was tapped.
  @objc func pauseForRoute() {
    mutedForRoute = true
    if isListening { cleanUp() }
  }

  /// Listens while a route is followed and hands-free is on, and stops otherwise.
  private func followNavigation() {
    if overhearHandler != nil {
      if !isListening { withPermission { self.start(continuous: true) } }
      return
    }
    let want = navigating && Self.handsFree && !mutedForRoute
    if want && !continuous {
      if isListening { cleanUp() }
      withPermission { self.start(continuous: true) }
    } else if !want && continuous {
      cleanUp()
    }
  }

  private func withPermission(_ then: @escaping () -> Void) {
    // The simulator UI tour can't answer the permission prompt, and it would cover the screen.
    guard !BackseatUITour.active else { return }
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
      // Not .error: in Debug builds that aborts the app, and this is recoverable (usually the app is
      // in the background or another app holds the mic).
      LOG(.warning, "Backseat: audio session failed: \(error)")
      BackseatLog.event("stt.error", ["where": "session", "error": error.localizedDescription])
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
      LOG(.warning, "Backseat: audio engine failed: \(error)")
      BackseatLog.event("stt.error", ["where": "engine.start", "error": error.localizedDescription])
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
    guard overhearHandler == nil else { return }
    let speaking = KokoroVoice.shared.anyVoiceSpeaking
    if speaking && !pausedForSpeech {
      pausedForSpeech = true
      // Mid-game, what the driver said just before a line started is still their move. The line
      // hasn't played yet, so this can't be the GPS hearing itself.
      if MWMBackseatRewriter.inGame, !heardTheGPS, !heard.isEmpty {
        finish(beforeTheGPS: true)
      } else {
        endSegment()
      }
      audioEngine.pause()
    } else if !speaking && pausedForSpeech {
      pausedForSpeech = false
      do {
        // The voices' session handling (or the app going to the background) may have deactivated
        // the session while the mic was paused; the engine can't start on a dead session.
        try AVAudioSession.sharedInstance().setActive(true, options: .notifyOthersOnDeactivation)
        try audioEngine.start()
      } catch {
        LOG(.warning, "Backseat: audio engine didn't restart: \(error)")
        BackseatLog.event("stt.error", ["where": "engine.restart", "error": error.localizedDescription])
        // Start over from scratch in a moment instead of leaving hands-free dead for the trip.
        let wasContinuous = continuous
        cleanUp()
        if wasContinuous {
          DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in self?.followNavigation() }
        }
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
    request.contextualStrings = ["plate", "autopilot", "take me to", "work", "home", "Hawaii", "Alaska", "Colorado", "New Mexico", "Texas", "yes", "no", "is it", "twenty questions", "I give up", "a hint"] + overhearNames
    self.request = request
    heard = ""
    heardTheGPS = KokoroVoice.shared.anyVoiceSpeaking
    overhearHandler?("")

    task = recognizer.recognitionTask(with: request) { [weak self] result, error in
      DispatchQueue.main.async {
        guard let self, self.request === request else { return }
        if let result {
          self.heard = result.bestTranscription.formattedString
          self.overhearHandler?(self.heard)
          guard self.request === request else { return }
          // A short pause after speaking ends it; a final result ends it at once.
          self.restartTimeout(after: result.isFinal ? 0 : Self.pauseToFinish)
        }
        if let error {
          let code = (error as NSError).code
          // 1110 is "no speech detected", the normal end of a quiet listen.
          if code != 1110 && code != 216 && code != 301 {
            BackseatLog.event("stt.error", ["error": error.localizedDescription, "code": String(code)])
          }
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
  private func finish(beforeTheGPS: Bool = false) {
    guard isListening else { return }
    if overhearHandler != nil {
      // Only a fresh recognition task: the handler already heard this one as it went.
      endSegment()
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in self?.startSegment() }
      return
    }
    var text = heard.trimmingCharacters(in: .whitespacesAndNewlines)
    // Its own voice can come back after it stops talking, and "Wrong way again" isn't the driver.
    if !text.isEmpty, let line = MWMBackseatRewriter.echo(of: text) {
      BackseatLog.event("stt.echo", ["text": text, "line": line])
      text = ""
    }
    let echo = !beforeTheGPS && (heardTheGPS || KokoroVoice.shared.anyVoiceSpeaking)
    if let answerDeadline, Date() > answerDeadline {
      LOG(.info, "Backseat: stopped waiting for a \(answerPurpose)")
      stopWaitingForAnswer()
    }
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
        stopWaitingForAnswer()
        BackseatLog.event("stt", ["for": answerPurpose, "text": text])
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
    BackseatLog.event("stt", ["for": "command", "text": text])
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

/// "What voices do you have?": each voice takes its turn, with the mic open. Say a voice's name (or the
/// silly phrase it just gave) and the line-up stops there, that voice is picked, the voice that was up
/// next complains it never got to speak, and the one that got cut off tells you that you're not
/// missing much. Without mic permission it just plays through, and the Settings list still works.
@objc final class BackseatVoicePreview: NSObject {
  private static var current: BackseatVoicePreview?
  /// Long enough for a line's own mentions of its voice's name, which are all near the start of it,
  /// plus the recognizer catching up. Until then, that name is probably the speaker being overheard.
  private static let ownNameEcho: TimeInterval = 4.5

  private struct Turn {
    let voiceID: String
    let line: String
    let names: [String]
  }

  private let turns: [Turn]
  private let startingVoice = KokoroVoice.selectedVoiceID
  /// First launch: a pick goes here (the voice id) instead of switching voices on the spot, so the
  /// old voice can gloat and the driver can type the phrase first. `onFinished` hears that the
  /// line-up ran out with nobody picked.
  private var onPick: ((String) -> Void)?
  private var onFinished: (() -> Void)?
  private var timer: Timer?
  /// Whose line is on; -1 during the introduction.
  private var index = -1
  private var lineStartedAt: Date?
  /// The transcript so far, and how many times each voice was already named in it when the mic could
  /// only have been hearing the GPS.
  private var heard = ""
  private var baseline: [[Int]] = []

  private init(turns: [Turn]) {
    self.turns = turns
  }

  /// Plays `lines` in `voiceIDs` in turn, after whatever is being said now.
  @objc static func start(voiceIDs: [String], lines: [String]) {
    start(voiceIDs: voiceIDs, lines: lines, onPick: nil, onFinished: nil)
  }

  /// Like start(voiceIDs:lines:), with the first launch's own handling of a pick.
  static func start(voiceIDs: [String], lines: [String], onPick: ((String) -> Void)?, onFinished: (() -> Void)?) {
    current?.end()
    let turns = zip(voiceIDs, lines).map { id, line -> Turn in
      let spoken = KokoroVoice.voiceOptions.first { $0.id == id }?.spokenName ?? id
      return Turn(voiceID: id, line: line, names: [spoken, MWMBackseatRewriter.voicePhrase(id)].filter { !$0.isEmpty })
    }
    guard !turns.isEmpty else { return }
    let preview = BackseatVoicePreview(turns: turns)
    preview.onPick = onPick
    preview.onFinished = onFinished
    current = preview
    preview.begin()
  }

  /// Stops the line-up (the driver left the screen or tapped a voice).
  static func cancel() {
    guard let preview = current else { return }
    preview.onFinished = nil
    preview.end()
  }

  private func begin() {
    baseline = turns.map { $0.names.map { _ in 0 } }
    BackseatListener.shared.startOverhearing(names: turns.flatMap(\.names)) { [weak self] text in
      self?.overheard(text)
    }
    // Not at once: the introduction was queued just now and may not show as speaking yet.
    timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in self?.tick() }
  }

  private func tick() {
    // Picked from the Settings list while this played: that's the answer.
    guard KokoroVoice.selectedVoiceID == startingVoice else {
      end()
      return
    }
    BackseatListener.shared.keepOverhearing()
    if KokoroVoice.shared.anyVoiceSpeaking {
      if lineStartedAt == nil, KokoroVoice.shared.isSpeakingNow { lineStartedAt = Date() }
      return
    }
    guard index + 1 < turns.count else {
      let finished = onFinished
      end()
      finished?()
      return
    }
    index += 1
    remember()
    lineStartedAt = nil
    let line = turns[index].line
    KokoroVoice.shared.sayReply(line, voiceID: turns[index].voiceID) { MWMTextToSpeech.tts().play(line) }
  }

  private func overheard(_ text: String) {
    heard = text
    if text.isEmpty {
      remember()
      return
    }
    for (i, turn) in turns.enumerated() {
      for (n, name) in turn.names.enumerated() {
        let count = MWMBackseatRewriter.countVoiceName(name, in: text)
        // The voice that's talking says its own name and phrase. The name counts once its mentions
        // are over; either one counts if it's said more often than the line says it.
        if i == index {
          let saidByLine = MWMBackseatRewriter.countVoiceName(name, in: turn.line)
          let isName = n == 0
          let stillEcho = !isName || lineStartedAt.map { Date().timeIntervalSince($0) < Self.ownNameEcho } ?? true
          if stillEcho && count - baseline[i][n] <= saidByLine { continue }
        }
        if count > baseline[i][n] {
          picked(i)
          return
        }
      }
    }
  }

  /// Everything named so far has been dealt with.
  private func remember() {
    for (i, turn) in turns.enumerated() {
      for (n, name) in turn.names.enumerated() {
        baseline[i][n] = heard.isEmpty ? 0 : MWMBackseatRewriter.countVoiceName(name, in: heard)
      }
    }
  }

  private func picked(_ i: Int) {
    let cutOff = index
    end()
    if let onPick {
      BackseatLog.event("voice.preview", ["picked": turns[i].voiceID, "cut_off": cutOff >= 0 ? turns[cutOff].voiceID : "",
                                          "for": "first launch"])
      onPick(turns[i].voiceID)
      return
    }
    KokoroVoice.shared.selectVoice(turns[i].voiceID)
    BackseatLog.event("voice.preview", ["picked": turns[i].voiceID, "cut_off": cutOff >= 0 ? turns[cutOff].voiceID : ""])
    guard cutOff >= 0 else {
      // Nobody's turn was cut short: the new voice just says hello.
      say(MWMBackseatRewriter.voicePreviewLine("cmd.voice", voice: turns[i].names[0]), in: turns[i].voiceID)
      return
    }
    // The voice that never got its turn; after the last voice, that's the first.
    let next = turns[(cutOff + 1) % turns.count]
    say(MWMBackseatRewriter.voicePreviewLine("voice.preview.complaint", voice: next.names[0]), in: next.voiceID)
    say(MWMBackseatRewriter.voicePreviewLine("voice.preview.notMissing", voice: turns[cutOff].names[0]),
        in: turns[cutOff].voiceID)
  }

  private func say(_ line: String, in voiceID: String) {
    guard !line.isEmpty else { return }
    KokoroVoice.shared.sayReply(line, voiceID: voiceID) { MWMTextToSpeech.tts().play(line) }
  }

  private func end() {
    timer?.invalidate()
    timer = nil
    BackseatListener.shared.stopOverhearing()
    if Self.current === self { Self.current = nil }
  }
}

/// The spoken first-run setup: the GPS introduces itself, gets the driver's name (and gets it
/// wrong on purpose), then asks where they're headed. Runs once, before the first trip.
@objc final class BackseatSetup: NSObject {
  private static let doneKey = "backseat.setupDone"
  private static var running = false

  @objc static func startIfNeeded() {
    guard !running, BackseatOnboarding.isDone, !UserDefaults.standard.bool(forKey: doneKey),
          MWMBackseatRewriter.driverName.isEmpty else { return }
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
  static func listen(waiting: TimeInterval = 6, for purpose: String = "answer", _ handler: @escaping (String) -> Void) {
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
      if KokoroVoice.shared.anyVoiceSpeaking {
        listen(waiting: waiting, for: purpose, handler)
      } else {
        BackseatListener.shared.listenForAnswer(waiting: waiting, for: purpose, handler)
      }
    }
  }

  static var isDone: Bool {
    !running && BackseatOnboarding.isDone &&
      (UserDefaults.standard.bool(forKey: doneKey) || !MWMBackseatRewriter.driverName.isEmpty)
  }

  /// The first-launch screens already did the introductions and got the name: all that's left is
  /// "Okay Doug, where are we headed?" (after whatever it's still saying), and listening for it.
  static func finishAfterOnboarding() {
    UserDefaults.standard.set(true, forKey: doneKey)
    let voiceName = KokoroVoice.voiceOptions.first { $0.id == KokoroVoice.selectedVoiceID }?.spokenName ?? "Echo"
    say(.whereTo, voiceName: voiceName)
    listen(waiting: 45, for: "destination") { answer in
      if !answer.isEmpty { MWMBackseatRewriter.driverSaid(answer) }
    }
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
      BackseatSetup.listen(waiting: answerWait, for: "destination") { answer in
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
  private static let confirmWait: TimeInterval = 15

  private let query: String
  private var results: [SearchResult] = []
  private var finished = false

  private let asStop: Bool

  private init(query: String, asStop: Bool) {
    self.query = query
    self.asStop = asStop
  }

  @objc static func start(_ query: String, asStop: Bool) {
    current?.finish()
    let search = BackseatVoiceSearch(query: query, asStop: asStop)
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
    var confirm = ObjCBool(false)
    let line = MWMBackseatRewriter.speakSearchResults(query, names: names, distances: found.map { NSNumber(value: $0.1) },
                                                      pick: &pick, none: &none, confirm: &confirm)
    say(line)
    if confirm.boolValue {
      listenForConfirm(pick)
    } else if pick >= 0 {
      route(to: pick)
    } else if !none.boolValue {
      listenForPick(tries: 2)
    }
  }

  private func listenForPick(tries: Int) {
    BackseatSetup.listen(waiting: Self.pickWait, for: "destination") { [self] answer in
      guard !answer.isEmpty else { return }
      var index = -1
      var confirm = ObjCBool(false)
      let line = MWMBackseatRewriter.pickSearchResult(answer, index: &index, confirm: &confirm)
      say(line)
      if confirm.boolValue {
        listenForConfirm(index)
      } else if index >= 0 {
        route(to: index)
      } else if tries > 1 {
        listenForPick(tries: tries - 1)
      }
    }
  }

  /// "Latheron Place? Say yes." Routes on yes; anything else, or nothing, leaves the route alone.
  private func listenForConfirm(_ index: Int) {
    var answered = false
    let settle: (String) -> Void = { [self] answer in
      guard !answered else { return }
      answered = true
      var confirmed = ObjCBool(false)
      say(MWMBackseatRewriter.confirmSearchResult(answer, confirmed: &confirmed))
      if confirmed.boolValue { route(to: index) }
    }
    BackseatSetup.listen(waiting: Self.confirmWait, for: "confirm", settle)
    // Hands-free never sends silence.
    DispatchQueue.main.asyncAfter(deadline: .now() + Self.confirmWait + 5) {
      guard !answered else { return }
      BackseatListener.shared.stopWaitingForAnswer()
      settle("")
    }
  }

  private func route(to index: Int) {
    guard results.indices.contains(index) else { return }
    let result = results[index]
    if asStop {
      MWMBackseatRewriter.addStop(result.coordinate, title: result.titleText)
    } else {
      MWMBackseatRewriter.route(to: result.coordinate, title: result.titleText)
    }
  }

  private func say(_ line: String) {
    guard !line.isEmpty else { return }
    KokoroVoice.shared.sayReply(line, voiceID: nil) { MWMTextToSpeech.tts().play(line) }
  }

  private func finish() {
    finished = true
    // Not from inside MWMSearch's observer loop: removing an observer while it enumerates them
    // threw "mutated while being enumerated" and took the app down.
    DispatchQueue.main.async { [self] in Search.remove(self) }
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
    arrivalTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { _ in checkArrival() }
  }

  private static var arrivalTimer: Timer?
  private static var arrived = false

  /// Once the route is finished and the arrival line has been said: "Test 47 complete. On a scale
  /// of one to ten..." Listen once, and the navigator answers whatever was said, or nothing.
  private static func checkArrival() {
    let finished = MWMRouter.isRouteFinished()
    defer { arrived = finished }
    guard finished, !arrived else { return }
    afterSpeaking {
      let question = MWMBackseatRewriter.arrivalSurvey
      guard !question.isEmpty else { return }
      KokoroVoice.shared.sayReply(question, voiceID: nil) { MWMTextToSpeech.tts().play(question) }
      BackseatSetup.listen(waiting: 6, for: "survey") { answer in
        let reply = MWMBackseatRewriter.surveyAnswer(answer)
        if !reply.isEmpty {
          KokoroVoice.shared.sayReply(reply, voiceID: nil) { MWMTextToSpeech.tts().play(reply) }
        }
      }
    }
  }

  /// Runs `then` once Kokoro and the system voice have both finished.
  private static func afterSpeaking(_ then: @escaping () -> Void) {
    DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
      if KokoroVoice.shared.anyVoiceSpeaking { afterSpeaking(then) } else { then() }
    }
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
    BackseatSetup.listen(waiting: 15, for: "file") { answer in
      guard asking else { return }
      asking = false
      let reply = MWMBackseatRewriter.fileAnswer(answer)
      if !reply.isEmpty {
        KokoroVoice.shared.sayReply(reply, voiceID: nil) { MWMTextToSpeech.tts().play(reply) }
      }
    }
  }
}
