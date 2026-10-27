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
            Section {
                NavigationLink {
                    SettingsBackseatFileView()
                } label: {
                    HStack {
                        Image(systemName: "folder")

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
                        Image(systemName: "play.circle")

                        Text("pref_tts_test_voice_title")
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
                VStack(spacing: 6) {
                    Slider(value: $niceness, in: 0...1) { editing in
                        if !editing && niceness > 0 { mockNiceness() }
                    }
                    .tint(.red)
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


    /// A line to try a voice with
    private static let testLine = "Oh good, you found the gas pedal. Now try the turn signal."


    /// Says the test line in the picked voice, or the system voice when Kokoro can't
    static func playTest() {
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
                        Image(systemName: "play.circle")

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
