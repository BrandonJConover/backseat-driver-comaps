import SwiftUI

/// View for picking the Kokoro voice that gives directions and roasts the driver
struct SettingsBackseatView: View {
    // MARK: Properties

    /// If voice guidance should be provided during routing
    @State var shouldProvideVoiceRouting: Bool = true


    /// The Kokoro voice
    @State var selectedBackseatVoice: String = KokoroVoice.selectedVoiceID


    /// When the system voice fills in, shown under the test button
    @State var backseatVoiceStatus: String = ""


    /// A line to try a voice with
    private let testLine = "Oh good, you found the gas pedal. Now try the turn signal."


    /// The actual view
    var body: some View {
        List {
            Section {
                Button {
                    KokoroVoice.shared.test(testLine) {
                        MWMTextToSpeech.tts().play(testLine)
                    }
                    backseatVoiceStatus = KokoroVoice.shared.status
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
            } footer: {
                Text(verbatim: backseatVoiceStatus)
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
            shouldProvideVoiceRouting = Settings.shouldProvideVoiceRouting
            selectedBackseatVoice = KokoroVoice.selectedVoiceID
            backseatVoiceStatus = KokoroVoice.shared.status
        }
        .onChange(of: shouldProvideVoiceRouting) { changedShouldProvideVoiceRouting in
            Settings.shouldProvideVoiceRouting = changedShouldProvideVoiceRouting
        }
        .onChange(of: selectedBackseatVoice) { changedSelectedBackseatVoice in
            if changedSelectedBackseatVoice != KokoroVoice.selectedVoiceID {
                KokoroVoice.shared.selectVoice(changedSelectedBackseatVoice)
            }
            backseatVoiceStatus = KokoroVoice.shared.status
        }
    }
}
