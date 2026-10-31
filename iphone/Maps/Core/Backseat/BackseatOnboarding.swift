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

  private static func present(from host: UIViewController) {
    guard presented == nil else { return }
    let model = BackseatOnboardingModel()
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

  /// Faster, faster, half speed, then normal speed from the fourth press on.
  private static func tempo(afterSkips skips: Int) -> FirstLaunchTempo {
    switch skips {
    case 0: return .brisk
    case 1: return .faster
    case 2: return .fastest
    case 3: return .half
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
        read(from: resumeAt)
      }
      return
    }
    let last = paragraphs.count - 1
    let readToTheEnd = piece == pieces.count - 1 && queuedUpTo == pieces.count - 1 && !KokoroVoice.shared.anyVoiceSpeaking
    // However the voice is doing, the buttons come after about as long as reading it takes.
    let words = paragraphs.joined(separator: " ").split(separator: " ").count
    let tooLong = startedReadingAt.map { Date().timeIntervalSince($0) > Double(words) / (skips >= 3 ? 1.1 : 2.2) + 45 } ?? false
    if readToTheEnd || tooLong {
      monologueOver = true
      reading = last
      BackseatLog.event("onboarding", ["step": "disclaimer read", "timedOut": tooLong && !readToTheEnd ? "yes" : "no"])
    }
  }

  /// The Skip button: it doesn't skip. "Stop that, I'm monologuing right now! Be patient," then
  /// back to the sentence it was on.
  /// The fourth press brings in the other gender, and every press after that someone new, until
  /// they've all had a turn.
  private func changeDisclaimerVoice() {
    let pick = skips == 4 ? KokoroVoice.otherVoice(voiceID) : nil
    let everyone = KokoroVoice.voiceOptions.map(\.id).filter { $0 != voiceID && $0 != disclaimerVoice }
    let fresh = everyone.filter { !voicesHeard.contains($0) }
    if fresh.isEmpty { voicesHeard.removeAll() }
    disclaimerVoice = pick ?? fresh.randomElement() ?? everyone.randomElement() ?? disclaimerVoice
    if let disclaimerVoice { voicesHeard.insert(disclaimerVoice) }
  }

  /// For the scoreboard.
  func skipMissed() {
    guard skips > 0, !monologueOver else { return }
    misses += 1
  }

  /// A tap while it looks disabled gets called out, and doesn't count as a press.
  func skipPressed(enabled: Bool) {
    if !enabled { skipMissed() }
    guard !monologueOver, complaint == nil, Date().timeIntervalSince(lastSkip) > 2.5 else { return }
    lastSkip = Date()
    BackseatLog.event("onboarding", ["step": "skip pressed", "sentence": String(piece)])
    KokoroVoice.shared.stop()
    // The sentence comes back only once the complaint is over (checkReading): it's already rendered,
    // so queued now it would play straight away, before the complaint is even rendered.
    if enabled {
      skips += 1
      if skips >= 4 { changeDisclaimerVoice() }
    }
    let key = !enabled ? "onboard.skip.disabled" : skips == 1 ? "onboard.monologuing.first" : "onboard.monologuing"
    let line = MWMBackseatRewriter.line(key, vars: [:]).trimmingCharacters(in: .whitespacesAndNewlines)
    resumeAt = piece
    queuedUpTo = piece
    complaint = line
    complaintStarted = false
    complaintAt = Date()
    // On screen as well, for anyone who can't hear it.
    reply(line)
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

/// Where the Skip button is running to. It changes direction and speed on every move, and works
/// out its position frame by frame: tapping needs the real spot, which an animated offset hides.
/// Spots run from 0 to 1 across the area it moves in.
final class SkipDodger {
  private var from = CGPoint(x: 1, y: 1)
  private var to = CGPoint(x: 1, y: 1)
  private var start = Date.distantPast
  private var duration: TimeInterval = 0
  private var moves = 0
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
    if date.timeIntervalSince(start) >= duration { nextMove(at: date, jumping: jumping, quickness: quickness) }
    if jumping { return from }
    let t = CGFloat(date.timeIntervalSince(start) / duration)
    return CGPoint(x: from.x + (to.x - from.x) * t, y: from.y + (to.y - from.y) * t)
  }

  private func nextMove(at date: Date, jumping: Bool, quickness: Double) {
    from = to
    moves += 1
    // Across, then up or down, then diagonally, always towards the other side.
    let across = moves % 3 != 2
    let upOrDown = moves % 3 != 1
    to = CGPoint(x: across ? Self.otherSide(of: from.x) : from.x, y: upOrDown ? Self.otherSide(of: from.y) : from.y)
    start = date
    duration = (jumping ? .random(in: 1.2 ... 2.2) : .random(in: 0.35 ... 1.8)) / quickness
  }

  private static func otherSide(of value: CGFloat) -> CGFloat {
    value < 0.5 ? .random(in: 0.6 ... 1) : .random(in: 0 ... 0.4)
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
    VStack(alignment: .leading, spacing: 4) {
      Text("STEP \(step) OF 3").font(.caption.weight(.heavy)).foregroundColor(accent)
      Text(text).font(.system(size: 30, weight: .heavy, design: .default).width(.condensed))
    }
    .frame(maxWidth: .infinity, alignment: .leading)
  }

  @ViewBuilder private var speechBubble: some View {
    if !model.bubble.isEmpty {
      Text(model.bubble)
        .font(.body.weight(.medium))
        .foregroundColor(Color(BackseatTheme.ink))
        .padding(14)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 16, style: .continuous).fill(Color.white))
        .transition(.opacity)
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
            }
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
          Text(BackseatOnboardingModel.question(q)).font(.title3.weight(.semibold))
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
