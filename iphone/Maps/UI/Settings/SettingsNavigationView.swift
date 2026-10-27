import SwiftUI

/// View for the navigation settings
struct SettingsNavigationView: View {
    // MARK: Properties
    
    /// The scene phase of the environment
    @Environment(\.scenePhase) private var scenePhase
    
    
    /// If the perspective view should be used during routing
    @State var hasPerspectiveViewWhileRouting: Bool = true
    
    
    /// If auto zoom should be used during routing
    @State var hasAutoZoomWhileRouting: Bool = true
    
    
    /// If voice guidance should be provided during routing
    @State var shouldProvideVoiceRouting: Bool = true
    
    
    /// The selected language for voice guidance during routing
    @State var selectedLanguageForVoiceRouting: Settings.VoiceRoutingLanguage.ID? = nil
    
    
    /// If street names should be announced in the voice guidance during routing
    @State var shouldAnnounceStreetnamesWhileVoiceRouting: Bool = false
    
    
    /// The selected announcement of speed traps in the voice guidance during routing
    @State var selectedAnnouncingSpeedTrapsWhileVoiceRouting: Settings.AnnouncingSpeedTrapsWhileVoiceRouting = .never
    
    
    /// If toll roads should be avoided during routing
    @State var shouldAvoidTollRoadsWhileRouting: Bool = false
    
    
    /// If unpaved roads should be avoided during routing
    @State var shouldAvoidUnpavedRoadsWhileRouting: Bool = false
    
    
    /// If paved roads should be avoided during routing
    @State var shouldAvoidPavedRoadsWhileRouting: Bool = false
    
    
    /// If ferries should be avoided during routing
    @State var shouldAvoidFerriesWhileRouting: Bool = false
    
    
    /// If motorways should be avoided during routing
    @State var shouldAvoidMotorwaysWhileRouting: Bool = false
    
    
    /// If steps should be avoided during routing
    @State var shouldAvoidStepsWhileRouting: Bool = false
    
    
    /// A date for forcing a refresh of the view
    @State var forceRefreshDate: Date = Date.now
    
    
    /// The actual view
    var body: some View {
        List {
            Section {
                Toggle("pref_map_3d_title", isOn: $hasPerspectiveViewWhileRouting)
                    .tint(.accent)
                
                Toggle("pref_map_auto_zoom", isOn: $hasAutoZoomWhileRouting)
                    .tint(.accent)
            }
            
            Section {
                Toggle("pref_tts_enable_title", isOn: $shouldProvideVoiceRouting)
                    .tint(.accent)
                
                if shouldProvideVoiceRouting {
                    Picker(selection: $selectedLanguageForVoiceRouting) {
                        ForEach(Settings.availableLanguagesForVoiceRouting) { languageForVoiceRouting in
                            Text(languageForVoiceRouting.localizedName)
                                .tag(languageForVoiceRouting.id)
                        }
                    } label: {
                        Text("pref_tts_language_title")
                    }
                    
                    NavigationLink {
                        SettingsBackseatVoiceView()
                    } label: {
                        HStack {
                            Text("voice")

                            Spacer()

                            Text(verbatim: KokoroVoice.selectedVoiceName)
                                .foregroundStyle(.secondary)
                                .id(UUID())
                        }
                    }
                    
                    Toggle(isOn: $shouldAnnounceStreetnamesWhileVoiceRouting) {
                        VStack(alignment: .leading) {
                            Text("pref_tts_street_names_title")
                            
                            Text("pref_tts_street_names_description")
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                        }
                    }
                    .tint(.accent)
                    
                    Picker(selection: $selectedAnnouncingSpeedTrapsWhileVoiceRouting) {
                        ForEach(Settings.AnnouncingSpeedTrapsWhileVoiceRouting.allCases) { announcingSpeedTrapsWhileVoiceRouting in
                            Text(announcingSpeedTrapsWhileVoiceRouting.description)
                        }
                    } label: {
                        Text("speedcams_alert_title")
                    }
                }
            } header: {
                HStack(spacing: 4) {
                    Image(systemName: "speaker.wave.2")
                        .imageScale(.small)
                    
                    Text("pref_tts_title")
                }
            } footer: {
                if shouldProvideVoiceRouting {
                    Button {
                        KokoroVoice.shared.test("In five hundred feet, turn right. Try to keep up.") {
                            Settings.playVoiceRoutingTest()
                        }
                    } label: {
                        Text("pref_tts_test_voice_title")
                            .bold()
                            .lineLimit(1)
                            .padding(4)
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(BorderedButtonStyle())
                    .foregroundStyle(.alternativeAccent)
                    .padding([.top, .bottom])
                    .listRowInsets(.init(top: 0, leading: 0, bottom: 0, trailing: 0))
                }
            }
            .id(forceRefreshDate)
            
            Section {
                Toggle(isOn: $shouldAvoidTollRoadsWhileRouting) {
                    Label {
                        Text("avoid_tolls")
                    } icon: {
                        Image(shouldAvoidTollRoadsWhileRouting ? "tolls.slash" : "tolls")
                            .foregroundStyle(.secondary)
                    }
                }
                .tint(.accent)
                
                Toggle(isOn: $shouldAvoidUnpavedRoadsWhileRouting) {
                    Label {
                        Text("avoid_unpaved")
                    } icon: {
                        Image(shouldAvoidUnpavedRoadsWhileRouting ? "unpaved.slash" : "unpaved")
                            .foregroundStyle(.secondary)
                    }
                }
                .tint(.accent)
                .disabled(shouldAvoidPavedRoadsWhileRouting)
                
                Toggle(isOn: $shouldAvoidPavedRoadsWhileRouting) {
                    Label {
                        Text("avoid_paved")
                    } icon: {
                        Image(shouldAvoidPavedRoadsWhileRouting ? "paved.slash" : "paved")
                            .foregroundStyle(.secondary)
                    }
                }
                .tint(.accent)
                .disabled(shouldAvoidUnpavedRoadsWhileRouting)
                
                Toggle(isOn: $shouldAvoidMotorwaysWhileRouting) {
                    Label {
                        Text("avoid_motorways")
                    } icon: {
                        Image(shouldAvoidMotorwaysWhileRouting ? "motorways.slash" : "motorways")
                            .foregroundStyle(.secondary)
                    }
                }
                .tint(.accent)
                
                Toggle(isOn: $shouldAvoidFerriesWhileRouting) {
                    Label {
                        Text("avoid_ferry")
                    } icon: {
                        Image(shouldAvoidFerriesWhileRouting ? "ferries.slash" : "ferries")
                            .foregroundStyle(.secondary)
                    }
                }
                .tint(.accent)
                
                Toggle(isOn: $shouldAvoidStepsWhileRouting) {
                    Label {
                        Text("avoid_steps")
                    } icon: {
                        Image(shouldAvoidStepsWhileRouting ? "steps.slash" : "steps")
                            .foregroundStyle(.secondary)
                    }
                }
                .tint(.accent)
            } header: {
                HStack(spacing: 4) {
                    Image(systemName: "slider.horizontal.3")
                        .imageScale(.small)
                    
                    Text("driving_options_title")
                }
            }
        }
        .accentColor(.accent)
        .navigationViewStyle(StackNavigationViewStyle())
        .navigationTitle("prefs_group_route")
        .onAppear {
            hasPerspectiveViewWhileRouting = Settings.hasPerspectiveViewWhileRouting
            hasAutoZoomWhileRouting = Settings.hasAutoZoomWhileRouting
            shouldProvideVoiceRouting = Settings.shouldProvideVoiceRouting
            selectedLanguageForVoiceRouting = Settings.languageForVoiceRouting
            shouldAnnounceStreetnamesWhileVoiceRouting = Settings.shouldAnnounceStreetnamesWhileVoiceRouting
            selectedAnnouncingSpeedTrapsWhileVoiceRouting = Settings.announcingSpeedTrapsWhileVoiceRouting
            shouldAvoidTollRoadsWhileRouting = Settings.shouldAvoidTollRoadsWhileRouting
            shouldAvoidUnpavedRoadsWhileRouting = Settings.shouldAvoidUnpavedRoadsWhileRouting
            shouldAvoidPavedRoadsWhileRouting = Settings.shouldAvoidPavedRoadsWhileRouting
            shouldAvoidFerriesWhileRouting = Settings.shouldAvoidFerriesWhileRouting
            shouldAvoidMotorwaysWhileRouting = Settings.shouldAvoidMotorwaysWhileRouting
            shouldAvoidStepsWhileRouting = Settings.shouldAvoidStepsWhileRouting
        }
        .onChange(of: scenePhase) { _ in
            forceRefreshDate = Date.now
        }
        .onChange(of: hasPerspectiveViewWhileRouting) { changedHasPerspectiveViewWhileRouting in
            Settings.hasPerspectiveViewWhileRouting = changedHasPerspectiveViewWhileRouting
        }
        .onChange(of: hasAutoZoomWhileRouting) { changedHasAutoZoomWhileRouting in
            Settings.hasAutoZoomWhileRouting = changedHasAutoZoomWhileRouting
        }
        .onChange(of: shouldProvideVoiceRouting) { changedShouldProvideVoiceRouting in
            Settings.shouldProvideVoiceRouting = changedShouldProvideVoiceRouting
        }
        .onChange(of: selectedLanguageForVoiceRouting) { changedSelectedLanguageForVoiceRouting in
            if let changedSelectedLanguageForVoiceRouting {
                Settings.languageForVoiceRouting = changedSelectedLanguageForVoiceRouting
            }
        }
        .onChange(of: shouldAnnounceStreetnamesWhileVoiceRouting) { changedShouldAnnounceStreetnamesWhileVoiceRouting in
            Settings.shouldAnnounceStreetnamesWhileVoiceRouting = changedShouldAnnounceStreetnamesWhileVoiceRouting
        }
        .onChange(of: selectedAnnouncingSpeedTrapsWhileVoiceRouting) { changedSelectedAnnouncingSpeedTrapsWhileVoiceRouting in
            Settings.announcingSpeedTrapsWhileVoiceRouting = changedSelectedAnnouncingSpeedTrapsWhileVoiceRouting
        }
        .onChange(of: shouldAvoidTollRoadsWhileRouting) { changedShouldAvoidTollRoadsWhileRouting in
            Settings.shouldAvoidTollRoadsWhileRouting = changedShouldAvoidTollRoadsWhileRouting
        }
        .onChange(of: shouldAvoidUnpavedRoadsWhileRouting) { changedShouldAvoidUnpavedRoadsWhileRouting in
            Settings.shouldAvoidUnpavedRoadsWhileRouting = changedShouldAvoidUnpavedRoadsWhileRouting
            if changedShouldAvoidUnpavedRoadsWhileRouting {
                shouldAvoidPavedRoadsWhileRouting = false
            }
        }
        .onChange(of: shouldAvoidPavedRoadsWhileRouting) { changedShouldAvoidPavedRoadsWhileRouting in
            Settings.shouldAvoidPavedRoadsWhileRouting = changedShouldAvoidPavedRoadsWhileRouting
            if changedShouldAvoidPavedRoadsWhileRouting {
                shouldAvoidUnpavedRoadsWhileRouting = false
            }
        }
        .onChange(of: shouldAvoidFerriesWhileRouting) { changedShouldAvoidFerriesWhileRouting in
            Settings.shouldAvoidFerriesWhileRouting = changedShouldAvoidFerriesWhileRouting
        }
        .onChange(of: shouldAvoidMotorwaysWhileRouting) { changedShouldAvoidMotorwaysWhileRouting in
            Settings.shouldAvoidMotorwaysWhileRouting = changedShouldAvoidMotorwaysWhileRouting
        }
        .onChange(of: shouldAvoidStepsWhileRouting) { changedShouldAvoidStepsWhileRouting in
            Settings.shouldAvoidStepsWhileRouting = changedShouldAvoidStepsWhileRouting
        }
    }
}
