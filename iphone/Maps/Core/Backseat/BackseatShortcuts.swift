import AppIntents

/// "Navigate with Backseat": iOS keeps Siri's own directions for Apple Maps (outside the EU), so this
/// is how Siri gets to the GPS. Siri asks where to, and the answer goes in as if said to the GPS.
@available(iOS 16.0, *)
struct BackseatNavigateIntent: AppIntent {
  static let title: LocalizedStringResource = "Navigate with Backseat Driver"
  static let openAppWhenRun = true

  @Parameter(title: "Destination", requestValueDialog: "Where to? Try to be specific. I know that's hard.")
  var destination: String

  @MainActor
  func perform() async throws -> some IntentResult {
    MWMBackseatRewriter.driverSaid("take me to \(destination)")
    return .result()
  }
}

@available(iOS 16.0, *)
struct BackseatHomeIntent: AppIntent {
  static let title: LocalizedStringResource = "Go home with Backseat Driver"
  static let openAppWhenRun = true

  @MainActor
  func perform() async throws -> some IntentResult {
    MWMBackseatRewriter.driverSaid("take me home")
    return .result()
  }
}

@available(iOS 16.0, *)
struct BackseatWorkIntent: AppIntent {
  static let title: LocalizedStringResource = "Go to work with Backseat Driver"
  static let openAppWhenRun = true

  @MainActor
  func perform() async throws -> some IntentResult {
    MWMBackseatRewriter.driverSaid("take me to work")
    return .result()
  }
}

@available(iOS 16.0, *)
struct BackseatShortcuts: AppShortcutsProvider {
  static var appShortcuts: [AppShortcut] {
    AppShortcut(intent: BackseatNavigateIntent(),
                phrases: ["Navigate with \(.applicationName)", "Get directions with \(.applicationName)",
                          "Start \(.applicationName)"],
                shortTitle: "Navigate", systemImageName: "car.side")
    AppShortcut(intent: BackseatHomeIntent(),
                phrases: ["Take me home with \(.applicationName)", "\(.applicationName) take me home"],
                shortTitle: "Go home", systemImageName: "house")
    AppShortcut(intent: BackseatWorkIntent(),
                phrases: ["Take me to work with \(.applicationName)", "\(.applicationName) take me to work"],
                shortTitle: "Go to work", systemImageName: "briefcase")
  }
}
