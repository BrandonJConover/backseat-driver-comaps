import SwiftUI
import UIKit

/// Debug builds only: opens one screen on launch so CI can screenshot it in the simulator
/// (.github/workflows/backseat-ui-tour.yaml). Launch with `-BackseatUITour <screen>`; add
/// `-backseat.onboardingDone YES` to land on the map instead of the first-launch flow.
/// Screens: onboarding2, onboarding3, home, search, place, settings, backseatSettings, arrival,
/// arrivalGood, quiz, quizAnswered, walk, caption.
@objc final class BackseatUITour: NSObject {
  @objc static var screen: String? {
    #if DEBUG
    return UserDefaults.standard.string(forKey: "BackseatUITour")
    #else
    return nil
    #endif
  }

  @objc static var active: Bool { screen != nil }

  /// From the map's viewDidAppear. True when the tour took over the launch.
  @objc static func run(from host: UIViewController) -> Bool {
    #if DEBUG
    // onboarding1 is the real first launch: leave it alone (just keep the permission prompts away).
    guard let screen, screen != "onboarding1", !ran else { return false }
    ran = true
    BackseatLog.event("uitour", ["screen": screen])
    // A fresh install opens on the whole world; go to the simulator's location first, so search has
    // results and the map looks like it does on a phone.
    DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
      if FrameworkHelper.currentZoomLevel() < 10 { FrameworkHelper.switchMyPositionMode() }
    }
    DispatchQueue.main.asyncAfter(deadline: .now() + 4) { open(screen, from: host) }
    return true
    #else
    return false
    #endif
  }

  #if DEBUG
  private static var ran = false

  private static func open(_ screen: String, from host: UIViewController) {
    switch screen {
    case "onboarding2": BackseatOnboarding.presentForTour(from: host, screen: .voices)
    case "onboarding3": BackseatOnboarding.presentForTour(from: host, screen: .aboutYou)
    case "search": search("coffee", select: false)
    case "place": search("coffee", select: true)
    case "settings": NotificationCenter.default.post(name: MapControls.presentSettingsNotificationName, object: nil)
    case "backseatSettings":
      let controller = UIHostingController(rootView: NavigationView { SettingsBackseatView() }.navigationViewStyle(.stack))
      host.present(controller, animated: true)
    case "arrival": showArrival(good: false)
    case "arrivalGood": showArrival(good: true)
    case "quiz": BackseatQuiz.showSample(answered: false)
    case "quizAnswered": BackseatQuiz.showSample(answered: true)
    case "walk": BackseatWalkQuestion.present(onYes: {}, onNo: {})
    case "caption":
      BackseatCaption.setPatience(3)
      BackseatCaption.said("Oh good, another left turn you'll take personally.")
    default: break
    }
  }

  private static func search(_ text: String, select: Bool) {
    let query = SearchQuery(text, source: .deeplink, autoSelectFirstResult: select)
    MWMMapViewControlsManager.manager()?.search(query)
  }

  private static func showArrival(good: Bool) {
    let report: [String: Any] = good ? [
      "at": Date().timeIntervalSince1970, "letter": "A-", "quip": "Fine. That was almost competent.",
      "destination": "Work", "instructionsScore": 94, "drivingScore": 88, "onTime": 9, "late": 0, "missed": 0,
      "wrongWay": 0, "reroutes": 0, "speedingSeconds": 0, "maxOverLimit": 0, "hardBrakes": 1, "hardAccels": 0,
      "expectedMin": 14, "actualMin": 13, "previousLetter": "B",
    ] : [
      "at": Date().timeIntervalSince1970, "letter": "D+", "quip": "You arrived. The curb did not deserve that.",
      "destination": "Trader Joe's", "instructionsScore": 61, "drivingScore": 48, "onTime": 6, "late": 2, "missed": 1,
      "wrongWay": 1, "reroutes": 2, "speedingSeconds": 140, "maxOverLimit": 12, "hardBrakes": 3, "hardAccels": 1,
      "expectedMin": 9, "actualMin": 13, "previousLetter": "C",
      "grudge": "Still not over the roundabout on Tuesday.",
    ]
    if let trip = BackseatArrivalCard.TripReport(report) { BackseatArrivalCard.show(trip) }
  }
  #endif
}
