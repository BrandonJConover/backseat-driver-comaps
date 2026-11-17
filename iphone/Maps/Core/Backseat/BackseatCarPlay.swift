import CarPlay
import CoreLocation
import UIKit

/// Whether a trip is on, for the Backseat pieces that follow `navigationVisibilityChanged` (hands-free
/// mic, the face and Patience meter, rough-road bumps, the quiz, "Head back", the report card).
/// On the phone the navigation panel posts it. A trip on the CarPlay screen hides that panel, so
/// the car screen posts it through here instead; posting only on a change keeps the two in step.
@objc final class BackseatCarTrip: NSObject {
  private(set) static var isNavigating = false
  private static var installed = false

  /// Follows the phone's own posts. Called when the Backseat UI installs, and again (harmlessly)
  /// before the car screen posts anything.
  @objc static func install() {
    guard !installed else { return }
    installed = true
    NotificationCenter.default.addObserver(forName: BackseatTalkButton.navigationVisibilityChanged, object: nil,
                                           queue: .main) { note in
      isNavigating = (note.object as? Bool) ?? false
    }
  }

  /// A trip started (or moved) onto the car screen.
  static func started() {
    install()
    guard !isNavigating else { return }
    BackseatLog.event("carplay.trip", ["phase": "start"])
    post(true)
  }

  /// The car screen's trip arrived or was cancelled.
  static func ended() {
    install()
    guard isNavigating else { return }
    BackseatLog.event("carplay.trip", ["phase": "end"])
    post(false)
  }

  private static func post(_ navigating: Bool) {
    isNavigating = navigating
    NotificationCenter.default.post(name: BackseatTalkButton.navigationVisibilityChanged, object: navigating)
  }
}

/// Backseat Driver on the CarPlay screen: the mic, one-tap "Head back", and the trip's grade when it
/// ends.
@objc final class BackseatCarPlay: NSObject {
  // MARK: Mic

  private static var micButtons = NSHashTable<AnyObject>.weakObjects()
  private static var micObserverInstalled = false

  /// A map button that opens the mic, like the yellow one on the phone. It turns into a waveform
  /// while the GPS is listening.
  static func micMapButton() -> CPMapButton {
    let button = CPMapButton { _ in BackseatListener.shared.toggle() }
    button.image = micImage()
    track(button)
    return button
  }

  /// The same, for a navigation bar.
  static func micBarButton() -> CPBarButton {
    let button = CPBarButton(image: micImage() ?? UIImage()) { _ in BackseatListener.shared.toggle() }
    track(button)
    return button
  }

  private static func micImage() -> UIImage? {
    UIImage(systemName: BackseatListener.shared.isListening ? "waveform" : "mic.fill")
  }

  private static func track(_ button: AnyObject) {
    micButtons.add(button)
    guard !micObserverInstalled else { return }
    micObserverInstalled = true
    NotificationCenter.default.addObserver(forName: BackseatListener.listeningChanged, object: nil, queue: .main) { _ in
      let image = micImage()
      for button in micButtons.allObjects {
        if let map = button as? CPMapButton { map.image = image }
        if let bar = button as? CPBarButton, let image { bar.image = image }
      }
    }
  }

  // MARK: Head back

  /// "Head back to <where the last trip started>" for the top of the car's destination list, or nil
  /// when there's nowhere to head back to.
  static func headBackSection() -> CPListSection? {
    guard let origin = BackseatReturnTrip.origin else { return nil }
    let item = CPListItem(text: "Head back to \(origin.name)", detailText: "Starts right away",
                          image: UIImage(systemName: "arrow.uturn.backward.circle.fill"))
    item.handler = { _, completion in
      route(to: origin.coordinate, title: origin.name)
      completion()
    }
    return CPListSection(items: [item])
  }

  // MARK: Routes

  /// Drives there now. +[MWMBackseatRewriter routeTo:title:] starts it on the car screen while that's
  /// up (CarPlay's own router, straight into navigation), else on the phone.
  private static func route(to coordinate: CLLocationCoordinate2D, title: String) {
    MWMBackseatRewriter.route(to: coordinate, title: title)
  }

  // MARK: Grade on arrival

  /// How long after arriving the grade may still replace the plain "You have arrived" alert.
  private static let gradeWait: TimeInterval = 10
  private static var gradeObserver: NSObjectProtocol?

  /// Swaps the car screen's arrival alert for the trip's grade and quip once the report is in.
  static func awaitGrade(on mapTemplate: CPMapTemplate, done: CPAlertAction) {
    if let gradeObserver { NotificationCenter.default.removeObserver(gradeObserver) }
    // routeDidFinish ends the trip before it shows the arrival alert, and a report that was already in is
    // posted right then, before this listens: take that one.
    if let ready = BackseatArrivalCard.lastReady, Date().timeIntervalSince(ready.at) < 2 {
      stopAwaitingGrade()
      showGrade(ready.report, on: mapTemplate, done: done)
      return
    }
    let deadline = Date().addingTimeInterval(gradeWait)
    gradeObserver = NotificationCenter.default.addObserver(forName: BackseatArrivalCard.reportReady, object: nil,
                                                           queue: .main) { [weak mapTemplate] note in
      stopAwaitingGrade()
      guard Date() < deadline, let mapTemplate, let report = note.object as? BackseatArrivalCard.TripReport else {
        return
      }
      showGrade(report, on: mapTemplate, done: done)
    }
    DispatchQueue.main.asyncAfter(deadline: .now() + gradeWait) { stopAwaitingGrade() }
  }

  private static func stopAwaitingGrade() {
    if let gradeObserver { NotificationCenter.default.removeObserver(gradeObserver) }
    gradeObserver = nil
  }

  private static func showGrade(_ report: BackseatArrivalCard.TripReport, on mapTemplate: CPMapTemplate,
                                done: CPAlertAction) {
    var headBack: CPAlertAction?
    if let origin = BackseatReturnTrip.origin {
      headBack = CPAlertAction(title: "Head back", style: .default) { _ in
        mapTemplate.dismissNavigationAlert(animated: true) { _ in }
        route(to: origin.coordinate, title: origin.name)
      }
    }
    let alert = CPNavigationAlert(titleVariants: ["Grade: \(report.letter)", report.letter],
                                  subtitleVariants: [report.quip],
                                  image: nil,
                                  primaryAction: done,
                                  secondaryAction: headBack,
                                  duration: 0)
    mapTemplate.dismissNavigationAlert(animated: false) { _ in
      mapTemplate.present(navigationAlert: alert, animated: true)
    }
  }
}
