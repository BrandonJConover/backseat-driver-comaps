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


    /// What the GPS has worked out about the driver, as label / value pairs
    @State var learnedFacts: [[String]] = []


    /// The actual view
    var body: some View {
        List {
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
                if learnedFacts.isEmpty {
                    Text(verbatim: "Nothing yet. Drive somewhere.")
                        .foregroundStyle(.secondary)
                }

                ForEach(learnedFacts, id: \.self) { fact in
                    HStack {
                        Text(verbatim: fact.first ?? "")

                        Spacer()

                        Text(verbatim: fact.last ?? "")
                            .foregroundStyle(.secondary)
                            .multilineTextAlignment(.trailing)
                    }
                }
            } header: {
                HStack(spacing: 4) {
                    Image(systemName: "brain")
                        .imageScale(.small)

                    Text(verbatim: "What I've learned about you")
                }
            } footer: {
                Text(verbatim: "Picked up from your trips and what you say, and used to call you out when you break your routine.")
            }
        }
        .accentColor(.accent)
        .navigationTitle(Text(verbatim: "Backseat Driver"))
        .onAppear {
            reload()
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


    /// Reads every setting again, e.g. after coming back from the voice list
    private func reload() {
        shouldProvideVoiceRouting = Settings.shouldProvideVoiceRouting
        isHandsFree = BackseatListener.handsFree
        driverName = MWMBackseatRewriter.driverName
        partner = MWMBackseatRewriter.partner
        partnerPronouns = MWMBackseatRewriter.partnerPronouns
        driverPronouns = MWMBackseatRewriter.driverPronouns
        carHasAutopilot = MWMBackseatRewriter.carHasAutopilot
        voiceName = KokoroVoice.selectedVoiceName
        voiceStatus = KokoroVoice.shared.status
        learnedFacts = MWMBackseatRewriter.learnedFacts
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

