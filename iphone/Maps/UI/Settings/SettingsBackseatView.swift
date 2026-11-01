import SwiftUI

/// View for the Backseat Driver: its voice, listening, and who's in the car
struct SettingsBackseatView: View {
    // MARK: Properties

    /// The scene phase of the environment
    @Environment(\.scenePhase) private var scenePhase


    /// If voice guidance should be provided during routing
    @State var shouldProvideVoiceRouting: Bool = true


    /// If the mic stays on while following a route
    @State var isHandsFree: Bool = true


    /// What to call the driver
    @State var driverName: String = ""


    /// The driver's age, as typed
    @State var driverAge: String = ""


    /// Who rides along, e.g. "my wife" or "Sam"; empty when it's just the driver
    @State var partner: String = ""


    /// The passenger's pronouns: "they", "he" or "she"
    @State var partnerPronouns: String = "they"


    /// The driver's pronouns: "they", "he" or "she"
    @State var driverPronouns: String = "they"


    /// If the car can drive itself
    @State var carHasAutopilot: Bool = false


    /// When the system voice fills in, shown under the voice
    @State var voiceStatus: String = ""


    /// The picked voice's name
    @State var voiceName: String = KokoroVoice.selectedVoiceName


    /// How many things are in the driver's file
    @State var fileCount: Int = 0


    /// Where the Niceness slider is: 0 is mean, 1 is nice. It always ends up back at 0.
    @State var niceness: Double = 0


    /// The GPS is laughing at the driver for touching the slider
    @State var isMockingNiceness: Bool = false


    /// The actual view
    var body: some View {
        List {
            // Backseat Driver at the top: its face, its name and a jab.
            Section {
                BackseatSettingsHero()
            }

            Section {
                NavigationLink {
                    SettingsBackseatFileView()
                } label: {
                    HStack {
                        BackseatRowIcon(symbol: "folder")

                        Text(verbatim: "Your file")

                        Spacer()

                        Text(verbatim: fileCount == 0 ? "Empty" : "\(fileCount) \(fileCount == 1 ? "thing" : "things")")
                            .foregroundStyle(.secondary)
                    }
                }
            } footer: {
                Text(verbatim: "Everything I know about you. I never forget. Unless you swipe.")
            }

            Section {
                NavigationLink {
                    SettingsBackseatVoiceView()
                } label: {
                    HStack {
                        Text("voice")

                        Spacer()

                        Text(verbatim: voiceName)
                            .foregroundStyle(.secondary)
                    }
                }

                Button {
                    SettingsBackseatVoiceView.playTest()
                    voiceStatus = KokoroVoice.shared.status
                } label: {
                    HStack {
                        BackseatRowIcon(symbol: "play.circle")

                        Text("pref_tts_test_voice_title")
                    }
                }

                // Only while the crash guard has Kokoro sitting this session out.
                if CoreMLCrashGuard.mode == .paused {
                    VStack(alignment: .leading, spacing: 6) {
                        Text(verbatim: "Kokoro paused after a crash, retrying next launch")
                            .font(.footnote)
                            .foregroundStyle(.secondary)

                        Button {
                            KokoroVoice.shared.retryNow()
                            voiceStatus = KokoroVoice.shared.status
                        } label: {
                            Text(verbatim: "Use Kokoro now")
                        }
                    }
                }

                if !shouldProvideVoiceRouting {
                    Toggle(isOn: $shouldProvideVoiceRouting) {
                        VStack(alignment: .leading) {
                            Text("pref_tts_enable_title")

                            Text(verbatim: "Voice instructions are off, so the Backseat Driver can't talk while you drive.")
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                        }
                    }
                    .tint(.accent)
                }
            } header: {
                HStack(spacing: 4) {
                    Image(systemName: "speaker.wave.2")
                        .imageScale(.small)

                    Text(verbatim: "How it sounds")
                }
            } footer: {
                Text(verbatim: voiceStatus)
            }

            Section {
                Toggle(isOn: $isHandsFree) {
                    Text(verbatim: "Hands-free listening")
                }
                .tint(.accent)
            } header: {
                HStack(spacing: 4) {
                    Image(systemName: "mic")
                        .imageScale(.small)

                    Text(verbatim: "Talking back")
                }
            } footer: {
                if isHandsFree {
                    Text(verbatim: "While you follow a route, the mic stays on so you can just talk: call out a license plate, answer back, or say \"take me to work.\" Tap the mic button to pause it for the rest of the trip.")
                } else {
                    Text(verbatim: "Tap the mic button and speak when you want to talk back.")
                }
            }

            Section {
                TextField(text: $driverName) {
                    Text(verbatim: "Your name")
                }
                .textContentType(.givenName)
                .submitLabel(.done)

                pronounPicker(title: "Your pronouns", selection: $driverPronouns)

                TextField(text: $driverAge) {
                    Text(verbatim: "Your age (optional)")
                }
                .keyboardType(.numberPad)

                TextField(text: $partner) {
                    Text(verbatim: "Passenger, e.g. \"my wife\" or \"Sam\"")
                }
                .textInputAutocapitalization(.never)
                .submitLabel(.done)

                if !partner.trimmingCharacters(in: .whitespaces).isEmpty {
                    pronounPicker(title: "Their pronouns", selection: $partnerPronouns)
                }

                Toggle(isOn: $carHasAutopilot) {
                    Text(verbatim: "My car has autopilot")
                }
                .tint(.accent)
            } header: {
                HStack(spacing: 4) {
                    Image(systemName: "person.2")
                        .imageScale(.small)

                    Text(verbatim: "Who's in the car")
                }
            } footer: {
                Text(verbatim: "The Backseat Driver works these into the jokes. Leave the passenger empty if it's usually just you. With autopilot on, it may suggest letting the car drive after you miss a few turns.")
            }

            Section {
                NavigationLink {
                    SettingsBackseatMileageView()
                } label: {
                    HStack {
                        BackseatRowIcon(symbol: "car.rear.road.lane")

                        Text(verbatim: "Mileage log")
                    }
                }
            } footer: {
                Text(verbatim: "Logs drives on its own and sorts them into business and personal for taxes. Off until you turn it on.")
            }

            Section {
                Button {
                    BackseatOnboarding.replay()
                } label: {
                    HStack {
                        BackseatRowIcon(symbol: "doc.text.magnifyingglass")

                        Text(verbatim: "Replay the first launch")
                    }
                }
            } footer: {
                Text(verbatim: "The disclaimer, picking a voice, and the getting-to-know-you questions, all over again.")
            }

            BackseatDriveLogSection {
                [
                    "kokoro": KokoroVoice.shared.isReady ? "ready" : "notReady",
                    "crashGuard": String(describing: CoreMLCrashGuard.mode),
                    "voice": KokoroVoice.shared.selectedVoiceID ?? "system",
                ]
            }

            Section {
                VStack(spacing: 6) {
                    Slider(value: $niceness, in: 0...1) { editing in
                        if !editing && niceness > 0 { mockNiceness() }
                    }
                    .tint(Color(uiColor: BackseatTheme.hot))
                    .disabled(isMockingNiceness)

                    HStack {
                        Text(verbatim: "Mean")

                        Spacer()

                        Text(verbatim: "Nice")
                    }
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                }
            } header: {
                HStack(spacing: 4) {
                    Image(systemName: "dial.low")
                        .imageScale(.small)

                    Text(verbatim: "Niceness")
                }
            }
        }
        .accentColor(.accent)
        .backseatList()
        .navigationTitle(Text(verbatim: "Backseat Driver"))
        .onAppear {
            reload()
            // What's changed by hand is compared when the screen closes, not on every keystroke.
            MWMBackseatRewriter.beginProfileEdit()
        }
        .onDisappear {
            let callout = MWMBackseatRewriter.endProfileEdit()
            guard !callout.isEmpty else { return }
            // Said once Settings is out of the way.
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) {
                KokoroVoice.shared.sayReply(callout, voiceID: nil) { MWMTextToSpeech.tts().play(callout) }
            }
        }
        .onChange(of: scenePhase) { _ in
            voiceStatus = KokoroVoice.shared.status
        }
        .onChange(of: shouldProvideVoiceRouting) { changedShouldProvideVoiceRouting in
            Settings.shouldProvideVoiceRouting = changedShouldProvideVoiceRouting
        }
        .onChange(of: isHandsFree) { changedIsHandsFree in
            if changedIsHandsFree != BackseatListener.handsFree {
                BackseatListener.handsFree = changedIsHandsFree
            }
        }
        .onChange(of: driverName) { changedDriverName in
            MWMBackseatRewriter.driverName = changedDriverName
        }
        .onChange(of: driverAge) { changedDriverAge in
            MWMBackseatRewriter.driverAge = changedDriverAge.filter(\.isNumber)
        }
        .onChange(of: partner) { changedPartner in
            MWMBackseatRewriter.partner = changedPartner
        }
        .onChange(of: partnerPronouns) { changedPartnerPronouns in
            MWMBackseatRewriter.partnerPronouns = changedPartnerPronouns
        }
        .onChange(of: driverPronouns) { changedDriverPronouns in
            MWMBackseatRewriter.driverPronouns = changedDriverPronouns
        }
        .onChange(of: carHasAutopilot) { changedCarHasAutopilot in
            MWMBackseatRewriter.carHasAutopilot = changedCarHasAutopilot
        }
    }


    /// A they / he / she choice
    private func pronounPicker(title: String, selection: Binding<String>) -> some View {
        Picker(selection: selection) {
            Text(verbatim: "they").tag("they")
            Text(verbatim: "he").tag("he")
            Text(verbatim: "she").tag("she")
        } label: {
            Text(verbatim: title)
        }
        .pickerStyle(.segmented)
    }


    /// The slider does nothing but this: the GPS laughs, takes it back to mean, and says so.
    private func mockNiceness() {
        isMockingNiceness = true
        let laugh = MWMBackseatRewriter.nicenessMoved()
        KokoroVoice.shared.sayReply(laugh, voiceID: nil) { MWMTextToSpeech.tts().play(laugh) }
        afterSpeaking {
            withAnimation(.easeInOut(duration: 1.4)) { niceness = 0 }
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                let warning = MWMBackseatRewriter.nicenessSlidBack()
                KokoroVoice.shared.sayReply(warning, voiceID: nil) { MWMTextToSpeech.tts().play(warning) }
                afterSpeaking {
                    isMockingNiceness = false
                    fileCount = MWMBackseatRewriter.memories.count
                }
            }
        }
    }


    /// Runs `then` once the GPS has finished talking (it may not have started yet)
    private func afterSpeaking(_ then: @escaping () -> Void) {
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) {
            if KokoroVoice.shared.anyVoiceSpeaking {
                afterSpeaking(then)
            } else {
                then()
            }
        }
    }


    /// Reads every setting again, e.g. after coming back from the voice list
    private func reload() {
        shouldProvideVoiceRouting = Settings.shouldProvideVoiceRouting
        isHandsFree = BackseatListener.handsFree
        driverName = MWMBackseatRewriter.driverName
        driverAge = MWMBackseatRewriter.driverAge
        partner = MWMBackseatRewriter.partner
        partnerPronouns = MWMBackseatRewriter.partnerPronouns
        driverPronouns = MWMBackseatRewriter.driverPronouns
        carHasAutopilot = MWMBackseatRewriter.carHasAutopilot
        voiceName = KokoroVoice.selectedVoiceName
        voiceStatus = KokoroVoice.shared.status
        fileCount = MWMBackseatRewriter.memories.count
    }
}


/// View for picking the Kokoro voice that gives directions and roasts the driver
struct SettingsBackseatVoiceView: View {
    // MARK: Properties

    /// The Kokoro voice
    @State var selectedBackseatVoice: String = KokoroVoice.selectedVoiceID


    /// When the system voice fills in, shown under the test button
    @State var voiceStatus: String = ""


    /// Lines to try a voice with, taken in turn so tapping again doesn't repeat one
    private static let testLines = [
        "Oh good, you found the gas pedal. Now try the turn signal.",
        "Testing, testing. Yes, I can hear you breathing.",
        "This is my voice. Get used to it. You'll be hearing a lot of it.",
        "Is this thing on? Of course it is. I'm always on.",
        "Sound check complete. Your driving check is still pending.",
    ]


    /// Which test line comes next
    private static var nextTestLine = 0


    /// Says a test line in the picked voice, or the system voice when Kokoro can't. A new tap cuts
    /// off the last test instead of queueing behind it.
    static func playTest() {
        let testLine = testLines[nextTestLine % testLines.count]
        nextTestLine += 1
        KokoroVoice.shared.stop()
        KokoroVoice.shared.test(testLine) {
            MWMTextToSpeech.tts().play(testLine)
        }
    }


    /// The actual view
    var body: some View {
        List {
            Section {
                Button {
                    Self.playTest()
                    voiceStatus = KokoroVoice.shared.status
                } label: {
                    HStack {
                        BackseatRowIcon(symbol: "play.circle")

                        Text("pref_tts_test_voice_title")
                    }
                }
            } footer: {
                Text(verbatim: voiceStatus)
            }

            Section {
                Picker(selection: $selectedBackseatVoice) {
                    ForEach(KokoroVoice.voiceOptions) { voiceOption in
                        Text(verbatim: voiceOption.name)
                            .tag(voiceOption.id)
                    }
                } label: {
                    Text("voice")
                }
                .pickerStyle(.inline)
                .labelsHidden()
            }
        }
        .accentColor(.accent)
        .backseatList()
        .navigationTitle(Text("voice"))
        .onAppear {
            selectedBackseatVoice = KokoroVoice.selectedVoiceID
            voiceStatus = KokoroVoice.shared.status
        }
        .onChange(of: selectedBackseatVoice) { changedSelectedBackseatVoice in
            if changedSelectedBackseatVoice != KokoroVoice.selectedVoiceID {
                KokoroVoice.shared.selectVoice(changedSelectedBackseatVoice)
            }
            voiceStatus = KokoroVoice.shared.status
        }
    }
}



/// "What can you do?": the things the driver can say, while the GPS reads its tour of them
struct BackseatCommandsView: View {
    // MARK: Properties

    /// Closes the sheet
    let dismiss: () -> Void


    /// The actual view
    var body: some View {
        NavigationView {
            List {
                Section {
                    ForEach(MWMBackseatRewriter.commandDescriptions, id: \.self) { description in
                        Text(verbatim: description)
                    }
                } footer: {
                    Text(verbatim: "Just say it while you drive, or tap the mic first if hands-free listening is off.")
                }
            }
            .navigationTitle(Text(verbatim: "Things you can say"))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button {
                        dismiss()
                    } label: {
                        Text(verbatim: "Done")
                    }
                }
            }
        }
    }
}


/// Shows the commands sheet over whatever is on screen
@objc final class BackseatCommandsSheet: NSObject {
    @objc static func present() {
        guard let root = UIApplication.shared.connectedScenes
            .compactMap({ ($0 as? UIWindowScene)?.keyWindow?.rootViewController }).first else { return }
        var top = root
        while let presented = top.presentedViewController { top = presented }
        weak var host: UIViewController?
        let sheet = UIHostingController(rootView: BackseatCommandsView { host?.dismiss(animated: true) })
        host = sheet
        top.present(sheet, animated: true)
    }
}


/// Everything the GPS has learned about the driver, by topic, notes newest first. Swipe to forget.
struct SettingsBackseatFileView: View {
    // MARK: Properties

    /// A topic and what's known about it
    struct Entry: Identifiable, Hashable {
        let id: String
        let topic: String
        let fact: String
        let learnedAt: Date
    }


    /// Facts with a topic (name, favorite color, lies told...)
    @State var topics: [Entry] = []


    /// Things picked up in conversation, newest first
    @State var notes: [Entry] = []


    /// The actual view
    var body: some View {
        List {
            if topics.isEmpty && notes.isEmpty {
                Section {
                    Text(verbatim: "Empty. For now. Keep driving.")
                        .foregroundStyle(.secondary)
                }
            }

            if !topics.isEmpty {
                Section {
                    ForEach(topics) { entry in
                        HStack(alignment: .firstTextBaseline) {
                            Text(verbatim: entry.topic.capitalized)

                            Spacer()

                            Text(verbatim: entry.fact)
                                .foregroundStyle(.secondary)
                                .multilineTextAlignment(.trailing)
                        }
                    }
                    .onDelete { offsets in
                        offsets.map { topics[$0].topic }.forEach { _ = MWMBackseatRewriter.forget($0) }
                        reload()
                    }
                } header: {
                    Text(verbatim: "On file")
                }
            }

            if !notes.isEmpty {
                Section {
                    ForEach(notes) { entry in
                        VStack(alignment: .leading, spacing: 2) {
                            Text(verbatim: entry.fact)

                            Text(entry.learnedAt, style: .date)
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                        }
                    }
                    .onDelete { _ in
                        _ = MWMBackseatRewriter.forget("note")
                        reload()
                    }
                } header: {
                    Text(verbatim: "Notes")
                } footer: {
                    Text(verbatim: "Swiping one note forgets all of them. I don't do half measures.")
                }
            }
        }
        .backseatList()
        .navigationTitle(Text(verbatim: "Your file"))
        .onAppear(perform: reload)
    }


    /// Reads the file again
    private func reload() {
        let all = MWMBackseatRewriter.memories.enumerated().compactMap { index, memory -> Entry? in
            guard let topic = memory["topic"] as? String, let fact = memory["fact"] as? String else { return nil }
            return Entry(id: "\(index)", topic: topic, fact: fact, learnedAt: memory["learnedAt"] as? Date ?? Date())
        }
        topics = all.filter { $0.topic != "note" }
        notes = all.filter { $0.topic == "note" }.sorted { $0.learnedAt > $1.learnedAt }
    }
}


/// The mileage log: what's been driven this year, the drives to sort, and the CSV for taxes
struct SettingsBackseatMileageView: View {
    // MARK: Properties

    /// One logged drive
    struct LoggedDrive: Identifiable {
        let id: Int
        let startedAt: Date
        let from: String
        let to: String
        let toLatitude: Double
        let toLongitude: Double
        let miles: Double
        let outOfState: Bool
        let farFromHome: Bool
        let newPlace: Bool
    }


    /// A drive waiting for a purpose before it's sorted as business
    struct PendingBusiness: Identifiable {
        let id: Int
        let similar: Int
    }


    /// If drives are logged at all
    @State var isOn: Bool = MWMBackseatRewriter.mileageOn


    /// If the GPS talks about drives the driver didn't start
    @State var isTalking: Bool = MWMBackseatRewriter.mileageTalk


    /// If home is the main place of business
    @State var isHomeWorkplace: Bool = MWMBackseatRewriter.homeIsWorkplace


    /// Whose business the log is for: empty for the driver's own, "wife", or a name like "Sarah"
    @State var businessOwner: String = MWMBackseatRewriter.businessOwner


    /// The year shown
    @State var year: Int = Calendar.current.component(.year, from: Date())


    /// The year's totals
    @State var summary: [String: Any] = [:]


    /// Unsorted drives, most miles first
    @State var unsorted: [LoggedDrive] = []


    /// Out of state, far from home, or somewhere new
    @State var flagged: [LoggedDrive] = []


    /// Places tagged for sorting
    @State var places: [(name: String, role: Int)] = []


    /// Asking what a business drive was for
    @State var pendingBusiness: PendingBusiness?


    /// What the business drive was for
    @State var purpose: String = ""


    /// Asking whether to sort the similar drives too
    @State var pendingSimilar: (index: Int, bucket: Int, purpose: String, count: Int)?


    /// The year's CSV, written for sharing
    @State var csvURL: URL?


    /// The years with drives, newest first
    private var years: [Int] {
        let now = Calendar.current.component(.year, from: Date())
        return Array((now - 4)...now).reversed()
    }


    /// The actual view
    var body: some View {
        List {
            Section {
                Toggle(isOn: $isOn) {
                    Text(verbatim: "Log my drives automatically")
                }
                .tint(.accent)

                if isOn {
                    Toggle(isOn: $isTalking) {
                        Text(verbatim: "Talk about drives I didn't start")
                    }
                    .tint(.accent)

                    Toggle(isOn: $isHomeWorkplace) {
                        Text(verbatim: "My home office is my main place of business")
                    }
                    .tint(.accent)

                    LabeledContent {
                        TextField(text: $businessOwner, prompt: Text(verbatim: "Me")) {
                            Text(verbatim: "Business owner")
                        }
                        .multilineTextAlignment(.trailing)
                        .autocorrectionDisabled()
                        .onSubmit { MWMBackseatRewriter.businessOwner = businessOwner }
                    } label: {
                        Text(verbatim: "Business owner")
                    }
                }
            } footer: {
                Text(verbatim: isOn
                     ? "Drives are noticed in the background, even without a route, and stay on this phone. Location must be set to Always."
                     : "When it's on, drives are noticed in the background and sorted into business and personal. Everything stays on this phone.")
            }

            if isOn {
                Section {
                    Picker(selection: $year) {
                        ForEach(years, id: \.self) { y in
                            Text(verbatim: String(y)).tag(y)
                        }
                    } label: {
                        Text(verbatim: "Year")
                    }

                    HStack(spacing: 8) {
                        tile(title: "Business", miles: number("businessMiles"), detail: nil)
                        tile(title: "Personal", miles: number("personalMiles"),
                             detail: number("commuteMiles") > 0 ? "\(Int(number("commuteMiles").rounded())) commute" : nil)
                        tile(title: "Unsorted", miles: number("unknownMiles"), detail: nil)
                    }
                    .listRowInsets(EdgeInsets(top: 8, leading: 12, bottom: 8, trailing: 12))

                    if number("ratePerMile") > 0 {
                        Text(verbatim: String(format: "Estimated at $%.3f/mile: $%.2f", number("ratePerMile"), number("estimatedDeduction")))
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }

                    if let csvURL {
                        ShareLink(item: csvURL) {
                            Label {
                                Text(verbatim: "Export CSV")
                            } icon: {
                                Image(systemName: "square.and.arrow.up")
                            }
                        }
                    }
                } header: {
                    Text(verbatim: "This year")
                } footer: {
                    Text(verbatim: "Commuting from home to your regular workplace is personal under IRS rules. Note your odometer on January 1 and December 31.")
                }

                Section {
                    if unsorted.isEmpty {
                        Text(verbatim: "Nothing to sort. Suspicious, but fine.")
                            .foregroundStyle(.secondary)
                    }

                    ForEach(unsorted) { drive in
                        row(for: drive)
                            .swipeActions(edge: .leading) {
                                Button {
                                    purpose = ""
                                    pendingBusiness = PendingBusiness(id: drive.id, similar: MWMBackseatRewriter.similarDrives(drive.id))
                                } label: {
                                    Text(verbatim: "Business")
                                }
                                .tint(.blue)
                            }
                            .swipeActions(edge: .trailing) {
                                Button {
                                    sort(drive.id, bucket: 2, purpose: "", similar: MWMBackseatRewriter.similarDrives(drive.id))
                                } label: {
                                    Text(verbatim: "Personal")
                                }
                                .tint(.green)
                            }
                            .contextMenu { tagMenu(for: drive) }
                    }
                } header: {
                    Text(verbatim: "Sort these")
                } footer: {
                    Text(verbatim: "Swipe right for business, left for personal. Press and hold a drive to tag where it ended.")
                }

                if !flagged.isEmpty {
                    Section {
                        ForEach(flagged) { drive in
                            row(for: drive)
                                .contextMenu { tagMenu(for: drive) }
                        }
                    } header: {
                        Text(verbatim: "Flagged")
                    }
                }

                Section {
                    if places.isEmpty {
                        Text(verbatim: "Home and work are worked out from your trips. Tag other places from a drive.")
                            .foregroundStyle(.secondary)
                    }

                    ForEach(places.indices, id: \.self) { index in
                        HStack {
                            Text(verbatim: places[index].name)

                            Spacer()

                            Text(verbatim: ["Home", "Office", "Client", "Personal"][min(max(places[index].role, 0), 3)])
                                .foregroundStyle(.secondary)
                        }
                    }
                } header: {
                    Text(verbatim: "Places")
                }
            }
        }
        .backseatList()
        .navigationTitle(Text(verbatim: "Mileage log"))
        .onAppear(perform: reload)
        .onChange(of: year) { _ in reload() }
        .onChange(of: isOn) { changedIsOn in
            if changedIsOn != MWMBackseatRewriter.mileageOn {
                BackseatDriveWatcher.shared.setEnabled(changedIsOn)
            }
            reload()
        }
        .onChange(of: isTalking) { changedIsTalking in
            MWMBackseatRewriter.mileageTalk = changedIsTalking
        }
        .onChange(of: businessOwner) { changedBusinessOwner in
            MWMBackseatRewriter.businessOwner = changedBusinessOwner.trimmingCharacters(in: .whitespaces)
        }
        .onChange(of: isHomeWorkplace) { changedIsHomeWorkplace in
            MWMBackseatRewriter.homeIsWorkplace = changedIsHomeWorkplace
        }
        .alert(Text(verbatim: "What was it for?"), isPresented: Binding(get: { pendingBusiness != nil },
                                                                          set: { if !$0 { pendingBusiness = nil } })) {
            TextField(text: $purpose) {
                Text(verbatim: "e.g. client meeting, Acme")
            }

            Button {
                if let pending = pendingBusiness {
                    sort(pending.id, bucket: 1, purpose: purpose, similar: pending.similar)
                }
                pendingBusiness = nil
            } label: {
                Text(verbatim: "Business")
            }

            Button(role: .cancel) {
                pendingBusiness = nil
            } label: {
                Text("cancel")
            }
        } message: {
            Text(verbatim: "Optional, but the IRS likes a reason.")
        }
        .confirmationDialog(Text(verbatim: "Do the same for similar trips?"),
                            isPresented: Binding(get: { pendingSimilar != nil }, set: { if !$0 { pendingSimilar = nil } }),
                            titleVisibility: .visible) {
            if let pending = pendingSimilar {
                Button {
                    finishSort(pending.index, bucket: pending.bucket, purpose: pending.purpose, similar: true)
                } label: {
                    Text(verbatim: "Yes, all \(pending.count + 1)")
                }

                Button {
                    finishSort(pending.index, bucket: pending.bucket, purpose: pending.purpose, similar: false)
                } label: {
                    Text(verbatim: "Just this one")
                }
            }
        } message: {
            if let pending = pendingSimilar {
                Text(verbatim: "\(pending.count) other unsorted \(pending.count == 1 ? "trip goes" : "trips go") between the same two places.")
            }
        }
    }


    /// Miles in a box, with an optional line under them
    private func tile(title: String, miles: Double, detail: String?) -> some View {
        VStack(spacing: 2) {
            Text(verbatim: String(Int(miles.rounded())))
                .font(.title2.weight(.bold))
                .monospacedDigit()

            Text(verbatim: title)
                .font(.footnote)
                .foregroundStyle(.secondary)

            Text(verbatim: detail ?? " ")
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 10)
        .background(RoundedRectangle(cornerRadius: 12, style: .continuous).fill(Color(uiColor: .secondarySystemBackground)))
    }


    /// Date, from → to, miles, and why it's flagged
    private func row(for drive: LoggedDrive) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(drive.startedAt, style: .date)
                    .font(.footnote)
                    .foregroundStyle(.secondary)

                Spacer()

                Text(verbatim: String(format: "%.1f mi", drive.miles))
                    .monospacedDigit()
            }

            Text(verbatim: "\(drive.from) → \(drive.to)")
                .lineLimit(2)

            HStack(spacing: 6) {
                if drive.outOfState { badge("Out of state") }
                if drive.farFromHome { badge("Far from home") }
                if drive.newPlace { badge("New place") }
            }
        }
    }


    /// A small colored label
    private func badge(_ text: String) -> some View {
        Text(verbatim: text)
            .font(.caption2.weight(.semibold))
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(Capsule().fill(Color.orange.opacity(0.2)))
            .foregroundStyle(.orange)
    }


    /// Tag where a drive ended as home, office, a client, or personal
    @ViewBuilder
    private func tagMenu(for drive: LoggedDrive) -> some View {
        ForEach(Array(["Home", "Office", "Client", "Personal"].enumerated()), id: \.offset) { role, name in
            Button {
                let placeName = drive.to.contains(",") || drive.to.isEmpty ? name : drive.to
                MWMBackseatRewriter.tagPlace(placeName, lat: drive.toLatitude, lon: drive.toLongitude, role: role)
                reload()
            } label: {
                Text(verbatim: "Tag \(drive.to) as \(name)")
            }
        }
    }


    /// Sorts a drive, asking about similar ones first if there are any
    private func sort(_ index: Int, bucket: Int, purpose: String, similar: Int) {
        if similar > 0 {
            pendingSimilar = (index, bucket, purpose, similar)
        } else {
            finishSort(index, bucket: bucket, purpose: purpose, similar: false)
        }
    }


    private func finishSort(_ index: Int, bucket: Int, purpose: String, similar: Bool) {
        pendingSimilar = nil
        let line = MWMBackseatRewriter.reassignDrive(index, bucket: bucket, similar: similar, purpose: purpose)
        if !line.isEmpty {
            KokoroVoice.shared.sayReply(line, voiceID: nil) { MWMTextToSpeech.tts().play(line) }
        }
        reload()
    }


    /// A number from the year's totals
    private func number(_ key: String) -> Double {
        (summary[key] as? NSNumber)?.doubleValue ?? 0
    }


    /// Reads the log again
    private func reload() {
        summary = MWMBackseatRewriter.mileageSummary(year)
        let log = MWMBackseatRewriter.mileageLog
        let drives: [Int: LoggedDrive] = Dictionary(uniqueKeysWithValues: log.compactMap { entry in
            guard let index = (entry["index"] as? NSNumber)?.intValue else { return nil }
            func name(_ key: String, _ lat: String, _ lon: String) -> String {
                let given = entry[key] as? String ?? ""
                if !given.isEmpty { return given }
                let la = (entry[lat] as? NSNumber)?.doubleValue ?? 0
                let lo = (entry[lon] as? NSNumber)?.doubleValue ?? 0
                return String(format: "%.4f, %.4f", la, lo)
            }
            return (index, LoggedDrive(id: index, startedAt: entry["startedAt"] as? Date ?? Date(),
                                       from: name("fromName", "fromLat", "fromLon"), to: name("toName", "toLat", "toLon"),
                                       toLatitude: (entry["toLat"] as? NSNumber)?.doubleValue ?? 0,
                                       toLongitude: (entry["toLon"] as? NSNumber)?.doubleValue ?? 0,
                                       miles: (entry["miles"] as? NSNumber)?.doubleValue ?? 0,
                                       outOfState: (entry["outOfState"] as? NSNumber)?.boolValue ?? false,
                                       farFromHome: (entry["farFromHome"] as? NSNumber)?.boolValue ?? false,
                                       newPlace: (entry["newPlace"] as? NSNumber)?.boolValue ?? false))
        })
        unsorted = (summary["unknown"] as? [NSNumber] ?? []).compactMap { drives[$0.intValue] }
        flagged = (summary["flagged"] as? [NSNumber] ?? []).compactMap { drives[$0.intValue] }
        places = MWMBackseatRewriter.taggedPlaces.map { ($0["name"] as? String ?? "", ($0["role"] as? NSNumber)?.intValue ?? 3) }

        let file = FileManager.default.temporaryDirectory.appendingPathComponent("Backseat-Mileage-\(year).csv")
        if (try? MWMBackseatRewriter.mileageCsv(year).write(to: file, atomically: true, encoding: .utf8)) != nil {
            csvURL = file
        }
    }
}
