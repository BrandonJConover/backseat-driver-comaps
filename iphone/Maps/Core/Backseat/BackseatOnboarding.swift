import AVFAudio
import Speech
import SwiftUI
import UIKit

/// The first launch, Brandon's script, in three screens:
///  1. The disclaimer, on screen and read out loud. A Skip button that doesn't skip: the GPS stops,
///     says "Stop that, I'm monologuing", and picks up where it left off. Then "do you accept these
///     terms?", where No and Skip only get the driver as far as a technicality.
///  2. Picking a voice: every voice auditions with its code phrase. Saying one stops the line-up, the
///     original voice says "I can't believe you actually fell for that", and the driver has to type
///     the phrase to confirm before the new voice takes over.
///  3. Getting to know you: name (and the Gary bit), gender, married, house / apartment / the streets,
///     with commentary. The answers stay on the phone and decide which jokes fit.
/// The words live in the shared engine (libs/backseat/onboarding.cpp, "onboard.*" lines), so Android
/// says the same thing.
@objc final class BackseatOnboarding: NSObject {
  static let doneKey = "backseat.onboardingDone"
  /// Name CoMaps next to Organic Maps in the disclaimer's thank-you (Brandon chose this, 2026-10-31).
  static let creditCoMaps = true
  private static weak var presented: UIViewController?

  @objc static var isDone: Bool { UserDefaults.standard.bool(forKey: doneKey) }

  /// From the map screen: the first launch shows the onboarding, after that the usual spoken check-in.
  @objc static func startIfNeeded(from host: UIViewController) {
    if BackseatUITour.run(from: host) { return }
    if isDone {
      BackseatSetup.startIfNeeded()
    } else {
      present(from: host)
    }
  }

  /// Settings: "Replay the first launch".
  static func replay() {
    guard let root = UIApplication.shared.connectedScenes.compactMap({ ($0 as? UIWindowScene)?.keyWindow }).first?
      .rootViewController
    else { return }
    var top = root
    while let next = top.presentedViewController { top = next }
    present(from: top)
  }

  #if DEBUG
  /// The UI tour (BackseatUITour): straight to a later screen, with a line in the bubble.
  static func presentForTour(from host: UIViewController, screen: BackseatOnboardingModel.Screen) {
    present(from: host) { model in
      model.screen = screen
      model.monologueOver = true
      model.bubble = screen == .voices ? "Pick whoever you like. I'll still be the one judging you."
        : "Brandon. Interesting. I thought it would have been Gary. Can I call you Gary?"
      if screen == .aboutYou { model.aboutStep = .question(1) }
    }
  }
  #endif

  private static func present(from host: UIViewController, configure: ((BackseatOnboardingModel) -> Void)? = nil) {
    guard presented == nil else { return }
    let model = BackseatOnboardingModel()
    configure?(model)
    let controller = UIHostingController(rootView: BackseatOnboardingView(model: model))
    controller.modalPresentationStyle = .fullScreen
    controller.isModalInPresentation = true
    controller.overrideUserInterfaceStyle = .dark
    model.finish = { [weak controller] in
      UserDefaults.standard.set(true, forKey: doneKey)
      controller?.dismiss(animated: true) { BackseatSetup.finishAfterOnboarding() }
    }
    presented = controller
    BackseatLog.event("onboarding", ["step": "start"])
    host.present(controller, animated: true)
  }
}

final class BackseatOnboardingModel: ObservableObject {
  enum Screen { case disclaimer, voices, aboutYou }
  enum AboutStep: Equatable { case name, gary, question(Int), wrapUp }

  @Published var screen: Screen = .disclaimer
  /// The GPS's last line (other than the disclaimer itself), for the speech bubble.
  @Published var bubble = ""
  var finish: () -> Void = {}

  // MARK: Disclaimer

  let paragraphs = MWMBackseatRewriter.disclaimerParagraphs(BackseatOnboarding.creditCoMaps)
  /// The paragraph being read.
  @Published var reading = 0
  /// The whole thing has been read (or there's no voice): the answer buttons show.
  @Published var monologueOver = false
  /// 0 for the first "do you accept these terms?", 1 after a No.
  @Published var termsRound = 0
  @Published var termsAnswered = false
  /// The disclaimer as it's read out: one sentence at a time, each with its paragraph, so Skip can
  /// pick up at the sentence it interrupted rather than the top of the paragraph.
  private lazy var pieces: [(paragraph: Int, text: String)] = paragraphs.indices.flatMap { p in
    Self.sentences(MWMBackseatRewriter.spokenDisclaimer(paragraphs[p])).map { (paragraph: p, text: $0) }
  }
  /// The sentence being said.
  private var piece = 0
  /// Up to here is queued with the voice; the next one is queued when the one before starts.
  private var queuedUpTo = -1
  /// After Skip: the complaint being said, whether it has started, when it was asked for, and
  /// the sentence to go back to after it.
  private var complaint: String?
  private var complaintStarted = false
  private var complaintAt = Date.distantPast
  private var resumeAt = 0
  private var lastSkip = Date.distantPast
  /// Presses of Skip that counted. From the first one on, the button runs away.
  @Published private(set) var skips = 0
  /// Taps on the button while it looked disabled, or next to it once it was moving.
  @Published private(set) var misses = 0
  /// Who reads the rest of the disclaimer after the fourth press (nil: the driver's voice).
  private var disclaimerVoice: String?
  private var voicesHeard = Set<String>()
  /// The complaint is in the voice that's leaving; the next one takes over after it.
  private var voiceChangeDue = false
  private var startedReadingAt: Date?
  private var poll: Timer?
  private var observer: NSObjectProtocol?

  // MARK: Voices

  @Published var voiceID = KokoroVoice.selectedVoiceID
  /// The voice whose phrase was said, waiting for the driver to type it.
  @Published var pendingVoiceID: String?
  @Published var typedPhrase = ""
  @Published var askToType = false
  private var voiceChanged = false

  // MARK: About you

  @Published var aboutStep: AboutStep = .name
  @Published var name = MWMBackseatRewriter.driverName

  init() {
    observer = NotificationCenter.default.addObserver(forName: BackseatCaption.lineStarted, object: nil,
                                                      queue: .main) { [weak self] note in
      guard let self, let text = note.object as? String else { return }
      self.lineStarted(text)
    }
  }

  deinit {
    poll?.invalidate()
    if let observer { NotificationCenter.default.removeObserver(observer) }
  }

  private func say(_ line: String, in voice: String? = nil) {
    guard !line.isEmpty else { return }
    // Skip changes how the disclaimer is read. The later screens go back to the first pace and voice.
    let onDisclaimer = screen == .disclaimer
    KokoroVoice.shared.sayReply(line, tempo: onDisclaimer ? Self.tempo(afterSkips: skips) : .brisk,
                                voiceID: voice ?? (onDisclaimer ? disclaimerVoice : nil)) {
      MWMTextToSpeech.tts().play(line)
    }
  }

  /// Faster, faster, a third of normal speed, then normal speed from the fourth press on.
  private static func tempo(afterSkips skips: Int) -> FirstLaunchTempo {
    switch skips {
    case 0: return .brisk
    case 1: return .faster
    case 2: return .fastest
    case 3: return .third
    default: return .normal
    }
  }

  /// Says it and shows it in the bubble.
  private func reply(_ line: String, in voice: String? = nil) {
    guard !line.isEmpty else { return }
    bubble = line
    say(line, in: voice)
  }

  /// "It was him slash her slash the officer! He slash she..." -> one string per sentence.
  static func sentences(_ text: String) -> [String] {
    var out: [String] = []
    var current = ""
    let chars = Array(text)
    for (i, ch) in chars.enumerated() {
      current.append(ch)
      let ends = ".!?".contains(ch) && (i + 1 == chars.count || chars[i + 1] == " ")
      if ends {
        let sentence = current.trimmingCharacters(in: .whitespaces)
        if !sentence.isEmpty { out.append(sentence) }
        current = ""
      }
    }
    let rest = current.trimmingCharacters(in: .whitespaces)
    if !rest.isEmpty { out.append(rest) }
    return out
  }

  /// Runs `then` once the GPS has stopped talking.
  private func whenQuiet(_ then: @escaping () -> Void) {
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [weak self] in
      guard self != nil else { return }
      if KokoroVoice.shared.anyVoiceSpeaking { self?.whenQuiet(then) } else { then() }
    }
  }

  // MARK: Disclaimer

  func startDisclaimer() {
    guard startedReadingAt == nil else { return }
    startedReadingAt = Date()
    // A muted phone gets the text and the buttons; nobody has to wait for a voice that won't come.
    guard MWMTextToSpeech.isTTSEnabled() else {
      monologueOver = true
      return
    }
    // Give Kokoro up to half a minute to load (the first launch downloads it), so it's in its voice.
    BackseatSetup.waitForKokoro(tries: 60) { [weak self] in self?.read(from: 0) }
    poll = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in self?.checkReading() }
  }

  private func read(from i: Int) {
    guard i < pieces.count else { return }
    queuedUpTo = i
    say(pieces[i].text)
  }

  private func lineStarted(_ text: String) {
    guard screen == .disclaimer else {
      bubble = text
      return
    }
    if text == complaint {
      complaintStarted = true
      return
    }
    // The first match from the sentence before on (a sentence can't come back earlier).
    guard complaint == nil,
          let i = pieces.indices.first(where: { $0 >= max(piece - 1, 0) && pieces[$0].text == text }) else { return }
    piece = i
    reading = pieces[i].paragraph
    // The next sentence renders while this one plays.
    if i + 1 < pieces.count, queuedUpTo <= i {
      queuedUpTo = i + 1
      say(pieces[i + 1].text)
    }
  }

  private func checkReading() {
    guard !monologueOver else {
      poll?.invalidate()
      return
    }
    if let pending = complaint {
      // Back to the interrupted sentence once the complaint has been said, or if it never starts.
      let said = complaintStarted && !KokoroVoice.shared.anyVoiceSpeaking
      if said || (!complaintStarted && Date().timeIntervalSince(complaintAt) > 8) {
        BackseatLog.event("onboarding", ["step": "resume", "sentence": String(resumeAt), "complained": said ? "yes" : "no",
                                         "line": String(pending.prefix(40))])
        complaint = nil
        bubble = ""
        if voiceChangeDue {
          voiceChangeDue = false
          changeDisclaimerVoice()
        }
        read(from: resumeAt)
      }
      return
    }
    let last = paragraphs.count - 1
    let readToTheEnd = piece == pieces.count - 1 && queuedUpTo == pieces.count - 1 && !KokoroVoice.shared.anyVoiceSpeaking
    // However the voice is doing, the buttons come after about as long as reading it takes.
    let words = paragraphs.joined(separator: " ").split(separator: " ").count
    let tooLong = startedReadingAt.map { Date().timeIntervalSince($0) > Double(words) / (skips >= 3 ? 0.75 : 2.2) + 45 } ?? false
    if readToTheEnd || tooLong {
      monologueOver = true
      reading = last
      BackseatLog.event("onboarding", ["step": "disclaimer read", "timedOut": tooLong && !readToTheEnd ? "yes" : "no"])
    }
  }

  /// The Skip button: it doesn't skip. "Stop that, I'm monologuing right now! Be patient," then
  /// back to the sentence it was on.
  /// Someone new takes over: the other gender where possible, nobody twice until they've all had
  /// a turn, and never the driver's own voice.
  private func changeDisclaimerVoice() {
    let current = disclaimerVoice ?? voiceID
    let others = KokoroVoice.voiceOptions.map(\.id).filter { $0 != voiceID && $0 != current }
    var fresh = others.filter { !voicesHeard.contains($0) }
    if fresh.isEmpty {
      voicesHeard.removeAll()
      fresh = others
    }
    let otherGender = fresh.filter { $0.dropFirst().first != current.dropFirst().first }
    guard let next = otherGender.randomElement() ?? fresh.randomElement() else { return }
    disclaimerVoice = next
    voicesHeard.insert(next)
  }

  /// For the scoreboard.
  func skipMissed() {
    guard skips > 0, !monologueOver else { return }
    misses += 1
  }

  /// A tap while it looks disabled gets called out on screen, and the reading carries on.
  func skipPressed(enabled: Bool) {
    guard enabled else {
      skipMissed()
      showDisabledCallout()
      return
    }
    guard !monologueOver, complaint == nil, Date().timeIntervalSince(lastSkip) > 2.5 else { return }
    lastSkip = Date()
    BackseatLog.event("onboarding", ["step": "skip pressed", "sentence": String(piece)])
    KokoroVoice.shared.stop()
    // The sentence comes back only once the complaint is over (checkReading): it's already rendered,
    // so queued now it would play straight away, before the complaint is even rendered.
    skips += 1
    voiceChangeDue = skips >= 4
    // It only complains once. After that the press just changes how the reading goes.
    if skips > 1 {
      if voiceChangeDue {
        voiceChangeDue = false
        changeDisclaimerVoice()
      }
      bubble = ""
      read(from: piece)
      return
    }
    let line = MWMBackseatRewriter.line("onboard.monologuing.first", vars: [:]).trimmingCharacters(in: .whitespacesAndNewlines)
    resumeAt = piece
    queuedUpTo = piece
    complaint = line
    complaintStarted = false
    complaintAt = Date()
    // On screen as well, for anyone who can't hear it.
    reply(line)
  }

  private func showDisabledCallout() {
    guard !monologueOver, complaint == nil else { return }
    let line = MWMBackseatRewriter.line("onboard.skip.disabled", vars: [:])
    bubble = line
    DispatchQueue.main.asyncAfter(deadline: .now() + 2.5) { [weak self] in
      if self?.bubble == line { self?.bubble = "" }
    }
  }

  /// The real skip, at the very bottom of the page: no more reading, straight to the question.
  func reallySkip() {
    BackseatLog.event("onboarding", ["step": "really skipped"])
    complaint = nil
    voiceChangeDue = false
    bubble = ""
    KokoroVoice.shared.stop()
    monologueOver = true
    reading = paragraphs.count - 1
  }

  /// 0 yes, 1 no, 2 skip.
  func answerTerms(_ answer: Int) {
    guard !termsAnswered else { return }
    var done: ObjCBool = false
    let lines = MWMBackseatRewriter.termsAnswer(answer, round: termsRound, done: &done)
    BackseatLog.event("onboarding", ["step": "terms", "answer": ["yes", "no", "skip"][answer], "round": String(termsRound)])
    KokoroVoice.shared.stop()
    bubble = lines.joined(separator: " ")
    lines.forEach { say($0) }
    guard done.boolValue else {
      termsRound = 1
      return
    }
    termsAnswered = true
    whenQuiet { [weak self] in self?.showVoices() }
  }

  // MARK: Voices

  private func showVoices() {
    screen = .voices
    bubble = ""
    reply(MWMBackseatRewriter.line("onboard.voices.intro", vars: [:]))
    // The line-up listens for the phrases. Rather than iOS's speech prompt landing cold in the
    // middle of it, ask first and say why; the line-up starts once that's answered.
    if SFSpeechRecognizer.authorizationStatus() == .notDetermined {
      askToListen = true
    } else {
      startLineUp()
    }
  }

  /// The listening primer on the voices screen: iOS hasn't been asked yet.
  @Published var askToListen = false

  /// "Sure, listen": the speech and mic prompts, then the line-up. Tapping a voice works either way.
  func allowListening() {
    BackseatLog.event("onboarding", ["step": "listen primer", "answer": "yes"])
    SFSpeechRecognizer.requestAuthorization { status in
      DispatchQueue.main.async {
        guard status == .authorized else { return self.listeningAnswered() }
        AVAudioApplication.requestRecordPermission { _ in
          DispatchQueue.main.async { self.listeningAnswered() }
        }
      }
    }
  }

  func declineListening() {
    BackseatLog.event("onboarding", ["step": "listen primer", "answer": "not now"])
    askToListen = false
    reply(["Fine. Tap one, then. Like it's 2009.", "Not listening. Noted. I'll talk anyway.",
           "Suit yourself. Fingers it is."].randomElement() ?? "")
  }

  private func listeningAnswered() {
    askToListen = false
    guard screen == .voices else { return }
    startLineUp()
  }

  private func startLineUp() {
    let ids = KokoroVoice.voiceOptions.map(\.id).filter { $0 != voiceID }
    BackseatVoicePreview.start(voiceIDs: ids, lines: MWMBackseatRewriter.voiceAuditions(ids),
                               onPick: { [weak self] id in self?.phraseSaid(id) }, onFinished: nil)
  }

  static func phrase(_ id: String) -> String { MWMBackseatRewriter.voicePhrase(id).capitalized }

  static func spokenName(_ id: String) -> String {
    KokoroVoice.voiceOptions.first { $0.id == id }?.spokenName ?? id
  }

  /// They said a code phrase. The original voice gloats, then wants it in writing.
  private func phraseSaid(_ id: String) {
    KokoroVoice.shared.stop()
    pendingVoiceID = id
    typedPhrase = ""
    reply(MWMBackseatRewriter.line("onboard.voices.fellForIt", vars: [:]), in: voiceID)
    say(MWMBackseatRewriter.line("onboard.voices.typeIt", vars: [:]), in: voiceID)
    askToType = true
  }

  func confirmTyped() {
    guard let id = pendingVoiceID else { return }
    guard MWMBackseatRewriter.typed(typedPhrase, matchesPhrase: Self.phrase(id)) else {
      reply(MWMBackseatRewriter.line("onboard.voices.typo", vars: [:]), in: voiceID)
      typedPhrase = ""
      // The alert closes on any button; it comes back for another try.
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in self?.askToType = true }
      return
    }
    pendingVoiceID = nil
    BackseatLog.event("onboarding", ["step": "voice typed", "voice": id])
    pick(id)
  }

  func cancelTyping() {
    pendingVoiceID = nil
    reply(MWMBackseatRewriter.line("onboard.voices.cancel", vars: [:]), in: voiceID)
  }

  /// Switches to `id`, and the new voice says hello.
  func pick(_ id: String) {
    BackseatVoicePreview.cancel()
    KokoroVoice.shared.selectVoice(id)
    voiceID = id
    voiceChanged = true
    reply(MWMBackseatRewriter.voicePreviewLine("cmd.voice", voice: Self.spokenName(id)))
  }

  func voicesDone() {
    BackseatVoicePreview.cancel()
    KokoroVoice.shared.stop()
    if !voiceChanged { say(MWMBackseatRewriter.line("onboard.voices.kept", vars: [:])) }
    screen = .aboutYou
    aboutStep = .name
    bubble = ""
    // "Before we begin, I want to get to know you on a first name basis... My name is Echo."
    let voiceName = Self.spokenName(voiceID)
    reply(MWMBackseatRewriter.setupLine(.intro, voiceName: voiceName))
    say(MWMBackseatRewriter.setupLine(.name, voiceName: voiceName))
  }

  // MARK: About you

  func nameEntered() {
    let typed = name.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !typed.isEmpty else { return }
    KokoroVoice.shared.stop()
    guard MWMBackseatRewriter.setupHeard(.name, answer: typed) else {
      reply(MWMBackseatRewriter.setupLine(.name, voiceName: Self.spokenName(voiceID)))
      return
    }
    name = MWMBackseatRewriter.driverName
    // "Brendon. Interesting. I thought it would have been Gary. Can I call you Gary?"
    reply(MWMBackseatRewriter.setupLine(.wrongName, voiceName: Self.spokenName(voiceID)))
    aboutStep = .gary
  }

  func garyAnswered(_ yes: Bool) {
    KokoroVoice.shared.stop()
    reply(MWMBackseatRewriter.line(yes ? "onboard.gary.yes" : "onboard.gary.no", vars: ["name": name]))
    ask(0)
  }

  private func ask(_ question: Int) {
    aboutStep = .question(question)
    say(MWMBackseatRewriter.aboutYouQuestion(question))
  }

  static func question(_ q: Int) -> String {
    ["Are you a man or a woman?", "Are you married?", "Where do you live?"][q]
  }

  static func label(_ choice: String) -> String {
    ["man": "Man", "woman": "Woman", "skip": "Rather not say", "yes": "Yes", "no": "No",
     "complicated": "It's complicated", "house": "A house", "apartment": "An apartment",
     "streets": "The streets"][choice] ?? choice.capitalized
  }

  func answered(_ question: Int, _ choice: String) {
    KokoroVoice.shared.stop()
    let comment = MWMBackseatRewriter.aboutYouAnswered(question, answer: choice)
    BackseatLog.event("onboarding", ["step": "about you", "question": String(question)])
    reply(comment)
    if question < 2 {
      ask(question + 1)
    } else {
      aboutStep = .wrapUp
      // How easy that was, the contract (the legally binding one), and the promise.
      say(MWMBackseatRewriter.line("onboard.wrapUp", vars: [:]))
    }
  }

  func done() {
    BackseatLog.event("onboarding", ["step": "done"])
    finish()
  }
}

/// Where the Skip button is: it bounces off the edges like a screen saver, worked out frame by
/// frame because tapping needs the real spot, which an animated offset hides. Spots run from 0 to 1
/// across the area it moves in.
final class SkipDodger {
  private var position = CGPoint(x: 1, y: 1)
  /// Per second, before `quickness`. Not the same both ways, so it wanders all over the area.
  private var velocity = CGVector(dx: -0.31, dy: -0.47)
  private var lastFrame: Date?
  private var nextJump = Date.distantPast
  private var enabled = true
  private var enabledUntil = Date.distantPast

  /// It also takes turns looking disabled, for no fixed length of time.
  func isEnabled(at date: Date) -> Bool {
    if date >= enabledUntil {
      // The first turn starts out enabled: the press that set it moving shouldn't look ignored.
      enabled = enabledUntil == .distantPast || !enabled
      enabledUntil = date.addingTimeInterval(enabled ? .random(in: 0.6 ... 1.4) : .random(in: 0.5 ... 1.2))
    }
    return enabled
  }

  /// `jumping` (Reduce Motion): it sits still for a while, then appears somewhere else.
  func spot(at date: Date, jumping: Bool, quickness: Double) -> CGPoint {
    if jumping {
      if date >= nextJump {
        position = CGPoint(x: .random(in: 0 ... 1), y: .random(in: 0 ... 1))
        nextJump = date.addingTimeInterval(.random(in: 1.2 ... 2.2) / quickness)
      }
      return position
    }
    // A long gap (the app was in the background) isn't a long way to travel.
    let elapsed = CGFloat(min(lastFrame.map { date.timeIntervalSince($0) } ?? 0, 0.1) * quickness)
    lastFrame = date
    position.x += velocity.dx * elapsed
    position.y += velocity.dy * elapsed
    if position.x < 0 || position.x > 1 {
      velocity.dx = -velocity.dx
      position.x = min(max(position.x, 0), 1)
    }
    if position.y < 0 || position.y > 1 {
      velocity.dy = -velocity.dy
      position.y = min(max(position.y, 0), 1)
    }
    return position
  }
}

struct BackseatOnboardingView: View {
  @ObservedObject var model: BackseatOnboardingModel

  private let accent = Color(BackseatTheme.accent)
  private let muted = Color(BackseatTheme.muted)
  private let card = Color(BackseatTheme.card)
  @Environment(\.accessibilityReduceMotion) private var reduceMotion
  @State private var dodger = SkipDodger()
  private static let skipColors: [Color] = [Color(BackseatTheme.accent), .orange, .green, .pink, .cyan, .mint]

  var body: some View {
    ZStack {
      Color(BackseatTheme.asphalt).ignoresSafeArea()
      switch model.screen {
      case .disclaimer: disclaimer
      case .voices: voices
      case .aboutYou: aboutYou
      }
    }
    .foregroundColor(Color(BackseatTheme.text))
    .animation(.easeInOut(duration: 0.25), value: model.screen)
  }

  private func title(_ text: String, step: Int) -> some View {
    VStack(alignment: .leading, spacing: 10) {
      // Three road-sign segments: how far through setup this is.
      HStack(spacing: 6) {
        ForEach(1 ... 3, id: \.self) { i in
          Capsule()
            .fill(i <= step ? accent : Color(BackseatTheme.raised))
            .frame(height: 5)
        }
      }
      .accessibilityElement()
      .accessibilityLabel(Text("Step \(step) of 3"))
      Text("STEP \(step) OF 3").font(.caption.weight(.heavy)).tracking(1.2).foregroundColor(accent)
        .accessibilityHidden(true)
      Text(text).font(.system(size: 34, weight: .heavy, design: .default).width(.condensed))
        .fixedSize(horizontal: false, vertical: true)
    }
    .frame(maxWidth: .infinity, alignment: .leading)
  }

  /// The GPS's face with what it just said, like the driving screen: a warm white bubble whose
  /// square corner points at the face. The face gets crankier as setup goes on.
  @ViewBuilder private var speechBubble: some View {
    if !model.bubble.isEmpty {
      HStack(alignment: .bottom, spacing: 10) {
        BackseatFaceRepresentable(mood: faceMood, size: 44)
          .frame(width: 44, height: 44)
          .accessibilityHidden(true)
        Text(model.bubble)
          .font(.body.weight(.medium))
          .foregroundColor(Color(BackseatTheme.ink))
          .fixedSize(horizontal: false, vertical: true)
          .padding(.horizontal, 14)
          .padding(.vertical, 11)
          .frame(maxWidth: .infinity, alignment: .leading)
          .background(
            UnevenRoundedRectangle(topLeadingRadius: 18, bottomLeadingRadius: 4, bottomTrailingRadius: 18,
                                   topTrailingRadius: 18, style: .continuous)
              .fill(Color(BackseatTheme.text))
              .shadow(color: .black.opacity(0.35), radius: 10, y: 5)
          )
          .id(model.bubble)
          .transition(.asymmetric(insertion: .scale(scale: 0.85, anchor: .bottomLeading).combined(with: .opacity),
                                  removal: .opacity))
      }
      .animation(.spring(response: 0.35, dampingFraction: 0.7), value: model.bubble)
      .transition(.opacity)
    }
  }

  /// Calm on the disclaimer, less so once Skip has been pressed, resigned by the questions.
  private var faceMood: Int {
    switch model.screen {
    case .disclaimer: return model.skips == 0 ? 8 : model.skips < 3 ? 5 : 2
    case .voices: return 6
    case .aboutYou: return 4
    }
  }

  private func choiceButton(_ label: String, primary: Bool = false, action: @escaping () -> Void) -> some View {
    Button(action: action) {
      Text(label)
        .font(.headline)
        .frame(maxWidth: .infinity, minHeight: 50)
        .foregroundColor(primary ? Color(BackseatTheme.ink) : Color(BackseatTheme.text))
        .background(RoundedRectangle(cornerRadius: BackseatTheme.buttonRadius, style: .continuous)
          .fill(primary ? accent : Color(BackseatTheme.raised)))
    }
    .buttonStyle(BackseatPressStyle())
  }

  // MARK: Screen 1

  /// Comes up on the third press, and keeps score from there.
  private var scoreboard: some View {
    HStack(spacing: 0) {
      scoreColumn("SUCCESSES", model.skips)
      Rectangle().fill(muted).frame(width: 1, height: 44)
      scoreColumn("MISSES", model.misses)
    }
    .padding(.vertical, 8)
    .background(RoundedRectangle(cornerRadius: 14, style: .continuous).fill(card))
  }

  private func scoreColumn(_ label: String, _ score: Int) -> some View {
    VStack(spacing: 0) {
      Text(label).font(.caption2.weight(.heavy)).foregroundColor(muted)
      Text("\(score)").font(.system(size: 40, weight: .heavy).width(.condensed).monospacedDigit()).foregroundColor(accent)
    }
    .frame(maxWidth: .infinity)
  }

  /// It's a Skip button. It doesn't skip.
  private func skipButton(fill: Color? = nil) -> some View {
    Button {
      model.skipPressed(enabled: model.skips == 0 || dodger.isEnabled(at: Date()))
    } label: {
      Label("Skip", systemImage: "forward.fill")
        .font(.headline)
        .padding(.horizontal, 28)
        .frame(minHeight: 46)
        .foregroundColor(fill == nil ? Color(BackseatTheme.text) : Color(BackseatTheme.ink))
        .background(Capsule().fill(fill ?? Color(BackseatTheme.raised)))
    }
  }

  private var disclaimer: some View {
    VStack(spacing: 16) {
      title("Before you go anywhere", step: 1)
      ScrollViewReader { proxy in
        ScrollView {
          VStack(alignment: .leading, spacing: 16) {
            ForEach(Array(model.paragraphs.enumerated()), id: \.offset) { i, paragraph in
              Text(paragraph)
                .font(.body)
                .foregroundColor(model.monologueOver || i == model.reading ? Color(BackseatTheme.text) : muted)
                .id(i)
            }
            if model.monologueOver {
              Text(model.termsRound == 0 ? "(Yes / No)" : "(Yes / No / Skip)").font(.body.weight(.semibold))
            }
            if !model.monologueOver {
              Button("Skip", action: model.reallySkip)
                .font(.footnote)
                .foregroundColor(muted)
                .underline()
                .padding(.top, 24)
            }
          }
          .padding(.vertical, 8)
        }
        .onChange(of: model.reading) { i in withAnimation { proxy.scrollTo(i, anchor: .center) } }
        .simultaneousGesture(TapGesture().onEnded(model.skipMissed))
      }
      // It stays over the text, clear of the speech bubble and the answer buttons.
      .overlay {
        if model.skips > 0, !model.monologueOver {
          GeometryReader { area in
            TimelineView(.animation) { timeline in
              // A bit quicker with each press after the second, up to twice the speed.
              let spot = dodger.spot(at: timeline.date, jumping: reduceMotion,
                                     quickness: min(1 + 0.25 * Double(max(model.skips - 2, 0)), 2))
              let inset = CGSize(width: min(70, area.size.width / 2), height: min(28, area.size.height / 2))
              let enabled = dodger.isEnabled(at: timeline.date)
              let time = timeline.date.timeIntervalSinceReferenceDate
              // From the second press on it also spins (not with Reduce Motion) and changes color.
              let wild = model.skips >= 2
              let fill = wild ? Self.skipColors[Int(time / 0.6) % Self.skipColors.count] : nil
              let turn = wild && !reduceMotion ? time.truncatingRemainder(dividingBy: 1.5) / 1.5 * 360 : 0
              skipButton(fill: fill).rotationEffect(.degrees(turn)).opacity(enabled ? 1 : 0.35).position(x: inset.width + spot.x * (area.size.width - 2 * inset.width),
                                  y: inset.height + spot.y * (area.size.height - 2 * inset.height))
            }
          }
        }
      }
      if model.skips >= 3 { scoreboard.transition(.opacity) }
      speechBubble
      if !model.monologueOver {
        if model.skips == 0 {
          skipButton().frame(maxWidth: .infinity, alignment: .trailing)
        }
      } else if !model.termsAnswered {
        HStack(spacing: 10) {
          choiceButton("Yes", primary: true) { model.answerTerms(0) }
          choiceButton("No") { model.answerTerms(1) }
          if model.termsRound > 0 { choiceButton("Skip") { model.answerTerms(2) } }
        }
      }
    }
    .padding(20)
    .onAppear(perform: model.startDisclaimer)
  }

  // MARK: Screen 2

  private var voices: some View {
    VStack(spacing: 14) {
      title("Pick a voice", step: 2)
      Text("Each voice gives you a phrase. Say it out loud to pick that voice, or tap one.")
        .font(.subheadline).foregroundColor(muted)
        .frame(maxWidth: .infinity, alignment: .leading)
      if model.askToListen {
        listenPrimer.transition(.move(edge: .top).combined(with: .opacity))
      }
      ScrollView {
        LazyVStack(spacing: 8) {
          ForEach(KokoroVoice.voiceOptions) { option in
            Button { model.pick(option.id) } label: {
              HStack {
                VStack(alignment: .leading, spacing: 2) {
                  Text(option.name).font(.headline)
                  Text("Say \u{201C}\(BackseatOnboardingModel.phrase(option.id))\u{201D}")
                    .font(.subheadline).foregroundColor(muted)
                }
                Spacer()
                if option.id == model.voiceID {
                  Image(systemName: "checkmark.circle.fill").foregroundColor(accent)
                }
              }
              .padding(14)
              .background(RoundedRectangle(cornerRadius: 14, style: .continuous).fill(card))
              .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous)
                .strokeBorder(option.id == model.voiceID ? accent : Color(BackseatTheme.line),
                              lineWidth: option.id == model.voiceID ? 2 : 1))
            }
            .buttonStyle(BackseatPressStyle())
            .foregroundColor(Color(BackseatTheme.text))
          }
        }
      }
      speechBubble
      choiceButton("Keep \(BackseatOnboardingModel.spokenName(model.voiceID))", primary: true, action: model.voicesDone)
    }
    .padding(20)
    .alert("Type it to confirm", isPresented: $model.askToType) {
      TextField("The phrase you just said", text: $model.typedPhrase)
        .textInputAutocapitalization(.words)
        .autocorrectionDisabled()
      Button("Confirm", action: model.confirmTyped)
      Button("Never mind", role: .cancel, action: model.cancelTyping)
    } message: {
      if let id = model.pendingVoiceID {
        Text("Type \u{201C}\(BackseatOnboardingModel.phrase(id))\u{201D} to switch to \(BackseatOnboardingModel.spokenName(id)).")
      }
    }
  }

  /// Before iOS asks for the mic: why, in the GPS's own voice, with the answer one tap away.
  private var listenPrimer: some View {
    VStack(alignment: .leading, spacing: 12) {
      HStack(alignment: .top, spacing: 12) {
        Image(systemName: "mic.fill")
          .font(.system(size: 18, weight: .bold))
          .foregroundColor(Color(BackseatTheme.ink))
          .frame(width: 40, height: 40)
          .background(Circle().fill(accent))
          .accessibilityHidden(true)
        VStack(alignment: .leading, spacing: 4) {
          Text("Can I listen?").font(.system(size: 22, weight: .heavy).width(.condensed))
          Text("Say a phrase to pick a voice. On the road you can just talk to me: \u{201C}take me home,\u{201D} \u{201C}report police.\u{201D} Your iPhone will ask next.")
            .font(.subheadline).foregroundColor(muted)
            .fixedSize(horizontal: false, vertical: true)
        }
      }
      HStack(spacing: 10) {
        choiceButton("Sure, listen", primary: true, action: model.allowListening)
        choiceButton("Not now", action: model.declineListening)
      }
    }
    .padding(16)
    .background(RoundedRectangle(cornerRadius: 18, style: .continuous).fill(card))
    .overlay(RoundedRectangle(cornerRadius: 18, style: .continuous).strokeBorder(accent.opacity(0.5), lineWidth: 1))
  }

  // MARK: Screen 3

  private var aboutYou: some View {
    VStack(spacing: 16) {
      title("Getting to know you", step: 3)
      speechBubble
      Spacer(minLength: 0)
      switch model.aboutStep {
      case .name:
        VStack(spacing: 12) {
          TextField("Your name", text: $model.name)
            .font(.title3)
            .padding(14)
            .background(RoundedRectangle(cornerRadius: 14, style: .continuous).fill(card))
            .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous).strokeBorder(accent.opacity(0.6), lineWidth: 1.5))
            .tint(accent)
            .textContentType(.givenName)
            .submitLabel(.next)
            .onSubmit(model.nameEntered)
          choiceButton("That's me", primary: true, action: model.nameEntered)
        }
      case .gary:
        HStack(spacing: 10) {
          choiceButton("Sure", primary: true) { model.garyAnswered(true) }
          choiceButton("No") { model.garyAnswered(false) }
        }
      case .question(let q):
        VStack(spacing: 10) {
          Text(BackseatOnboardingModel.question(q)).font(.system(size: 24, weight: .heavy).width(.condensed))
            .frame(maxWidth: .infinity, alignment: .leading)
          ForEach(MWMBackseatRewriter.aboutYouChoices(q), id: \.self) { choice in
            choiceButton(BackseatOnboardingModel.label(choice)) { model.answered(q, choice) }
          }
        }
      case .wrapUp:
        choiceButton("Let's drive", primary: true, action: model.done)
      }
      Spacer(minLength: 0)
      Text("No details leave this app I promise. These details just help inform my jokes. If you are living in the streets I don\u{2019}t want to remind you about the fact you don\u{2019}t have a house. I\u{2019}m not that cruel.")
        .font(.footnote)
        .foregroundColor(muted)
        .frame(maxWidth: .infinity, alignment: .leading)
    }
    .padding(20)
  }
}
