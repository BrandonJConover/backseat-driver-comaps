import AppIntents

/// "Hey Siri, navigate with Backseat Driver." iOS keeps "Hey Siri, directions to…" for Apple Maps
/// (outside the EU there's no default navigation app setting), so these are the ways in that Apple
/// allows: App Shortcuts Siri knows without any setup, the Shortcuts app and the Action Button.
/// Each one opens the app and hands the place to the same "take me to…" path as the mic button, so
/// home and work route straight away and anything else is searched and read out.

/// Siri asks "Where to?", then the app takes it from there.
struct BackseatNavigateIntent: AppIntent {
  static var title: LocalizedStringResource = "Take Me Somewhere"
  static var description = IntentDescription("Opens Backseat Driver and starts directions to a place you name.")
  static var openAppWhenRun: Bool = true

  @Parameter(title: "Destination", requestValueDialog: IntentDialog("Where to? And don't say Apple Maps."))
  var destination: String

  static var parameterSummary: some ParameterSummary {
    Summary("Take me to \(\.$destination)")
  }

  @MainActor
  func perform() async throws -> some IntentResult {
    BackseatSiri.navigate(to: destination)
    return .result()
  }
}

enum BackseatSavedPlace: String, AppEnum {
  case home
  case work

  static var typeDisplayRepresentation: TypeDisplayRepresentation = "Saved Place"
  static var caseDisplayRepresentations: [BackseatSavedPlace: DisplayRepresentation] = [
    .home: "home",
    .work: "work",
  ]
}

/// "Hey Siri, take me home with Backseat Driver": one sentence, no follow-up question.
struct BackseatGoToSavedPlaceIntent: AppIntent {
  static var title: LocalizedStringResource = "Take Me Home or to Work"
  static var description = IntentDescription("Opens Backseat Driver and starts directions home or to work.")
  static var openAppWhenRun: Bool = true

  @Parameter(title: "Place", default: .home)
  var place: BackseatSavedPlace

  static var parameterSummary: some ParameterSummary {
    Summary("Take me to \(\.$place)")
  }

  @MainActor
  func perform() async throws -> some IntentResult {
    BackseatSiri.navigate(to: place.rawValue)
    return .result()
  }
}

/// Phrases Siri recognizes as soon as the app is installed. Each must name the app; Info.plist's
/// INAlternativeAppNames lets "Backseat" and "Backseat Driver" stand in for the Debug build's name.
struct BackseatShortcuts: AppShortcutsProvider {
  static var appShortcuts: [AppShortcut] {
    AppShortcut(intent: BackseatGoToSavedPlaceIntent(),
                phrases: [
                  "Take me \(\.$place) with \(.applicationName)",
                  "Take me to \(\.$place) with \(.applicationName)",
                  "\(.applicationName) take me \(\.$place)",
                  "\(.applicationName) take me to \(\.$place)",
                  "Drive \(\.$place) with \(.applicationName)",
                  "Navigate \(\.$place) with \(.applicationName)",
                  "Navigate to \(\.$place) with \(.applicationName)",
                ],
                shortTitle: "Take Me Home",
                systemImageName: "house.fill")
    AppShortcut(intent: BackseatNavigateIntent(),
                phrases: [
                  "Navigate with \(.applicationName)",
                  "Get directions with \(.applicationName)",
                  "Take me somewhere with \(.applicationName)",
                  "\(.applicationName) take me somewhere",
                  "Drive with \(.applicationName)",
                  "Start \(.applicationName)",
                ],
                shortTitle: "Take Me Somewhere",
                systemImageName: "car.fill")
  }
}

/// Waits for the map to be up (Siri can cold-launch the app), then says "take me to <place>" for
/// the driver.
@objc final class BackseatSiri: NSObject {
  private static let retryEvery: TimeInterval = 0.5
  /// About 20 s. After that it goes anyway: the search waits for the map by itself.
  private static let maxTries = 40
  private static var pending: String?

  static func navigate(to destination: String) {
    let place = destination.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !place.isEmpty else { return }
    BackseatLog.event("siri.navigate", ["place": place])
    let waiting = pending != nil
    pending = place
    if !waiting { deliverWhenReady(tries: maxTries) }
  }

  private static func deliverWhenReady(tries: Int) {
    guard let place = pending else { return }
    let app = MapsAppDelegate.theApp()
    let mapShowing = app.mapViewController?.viewIfLoaded?.window != nil
    guard (app.isDrapeEngineCreated && mapShowing) || tries <= 0 else {
      DispatchQueue.main.asyncAfter(deadline: .now() + retryEvery) { deliverWhenReady(tries: tries - 1) }
      return
    }
    pending = nil
    // Siri gives orders, so the GPS hears "take me to", not "please".
    if !MWMBackseatRewriter.runTool("navigate_to", argument: place, spoken: "take me to \(place)") {
      BackseatVoiceSearch.start(place)
    }
  }
}
