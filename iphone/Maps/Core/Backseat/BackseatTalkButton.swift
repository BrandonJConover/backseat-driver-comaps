import UIKit

/// Backseat Driver's mic button on the map: tap and say "take me to work" (or answer the license
/// plate game). The navigation panel has its own while a route is being driven.
@objc final class BackseatTalkButton: NSObject {
  /// The map's own mic button, from before the "Where to?" card had one.
  static let showsMapButton = false

  static let navigationVisibilityChanged = Notification.Name("BackseatNavigationVisibilityChanged")

  /// A round mic button styled like the other map buttons.
  static func make() -> UIButton {
    var config = UIButton.Configuration.filled()
    config.image = UIImage(systemName: "mic.fill")
    config.cornerStyle = .capsule
    config.baseBackgroundColor = .systemBackground
    config.baseForegroundColor = .label
    let button = UIButton(configuration: config)
    button.accessibilityLabel = "Talk to Backseat Driver"
    button.layer.shadowColor = UIColor.black.cgColor
    button.layer.shadowOpacity = 0.25
    button.layer.shadowRadius = 3
    button.layer.shadowOffset = CGSize(width: 0, height: 1)
    button.addAction(UIAction { _ in BackseatListener.shared.toggle() }, for: .touchUpInside)
    followListening(button)
    return button
  }

  /// Turns `button` red while the GPS is listening.
  static func followListening(_ button: UIButton) {
    NotificationCenter.default.addObserver(forName: BackseatListener.listeningChanged, object: nil, queue: .main) {
      [weak button] _ in
      guard let button else { return }
      let listening = BackseatListener.shared.isListening
      button.configuration?.baseBackgroundColor = listening ? .systemRed : .systemBackground
      button.configuration?.baseForegroundColor = listening ? .white : .label
    }
  }

  /// Puts the mic on the leading edge of the map, halfway down, out of the way of the other controls.
  @objc static func install(in view: UIView) {
    BackseatListener.prepare()
    BackseatHooks.install()
    BackseatDriveOverlay.install(in: view)
    BackseatSketch.installPreviewButton(in: view)
    // The "Where to?" card has the mic now.
    if !showsMapButton { return }
    let button = make()
    button.translatesAutoresizingMaskIntoConstraints = false
    view.addSubview(button)
    NSLayoutConstraint.activate([
      button.widthAnchor.constraint(equalToConstant: 48),
      button.heightAnchor.constraint(equalToConstant: 48),
      button.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor, constant: 12),
      button.centerYAnchor.constraint(equalTo: view.safeAreaLayoutGuide.centerYAnchor),
    ])
    NotificationCenter.default.addObserver(forName: navigationVisibilityChanged, object: nil, queue: .main) {
      [weak button] note in
      button?.isHidden = (note.object as? Bool) ?? false
    }
  }
}

/// While a route is followed: a big speed-against-the-limit pill that turns red when over, and a
/// one-tap Report button, on the leading edge of the map. Added on top of CoMaps' own navigation
/// views, which it leaves alone; remove the install call to take it out.
final class BackseatDriveOverlay: NSObject {
  private static var stack: UIStackView?
  private static var pill: UILabel?
  private static var draw: UIButton?
  private static var timer: Timer?

  static func install(in view: UIView) {
    let pill = UILabel()
    pill.font = .monospacedDigitSystemFont(ofSize: 22, weight: .bold)
    pill.textAlignment = .center
    pill.layer.cornerRadius = 22
    pill.layer.masksToBounds = true
    pill.layer.borderWidth = 3
    pill.accessibilityLabel = "Speed and speed limit"

    var config = UIButton.Configuration.filled()
    config.image = UIImage(systemName: "exclamationmark.triangle.fill")
    config.title = "Report"
    config.imagePadding = 6
    config.cornerStyle = .capsule
    config.baseBackgroundColor = .systemBackground
    config.baseForegroundColor = .label
    let report = UIButton(configuration: config)
    report.accessibilityLabel = "Report police ahead"
    report.addAction(UIAction { _ in
      MWMBackseatRewriter.runTool("report_police", argument: "", spoken: "report police")
    }, for: .touchUpInside)

    // Drawing a route: the driver only while (nearly) stopped; otherwise it says it's for passengers.
    let draw = BackseatSketch.button()

    let stack = UIStackView(arrangedSubviews: [pill, report, draw])
    stack.axis = .vertical
    stack.spacing = 10
    stack.alignment = .leading
    stack.translatesAutoresizingMaskIntoConstraints = false
    stack.isHidden = true
    view.addSubview(stack)
    NSLayoutConstraint.activate([
      pill.heightAnchor.constraint(equalToConstant: 44),
      pill.widthAnchor.constraint(greaterThanOrEqualToConstant: 110),
      report.heightAnchor.constraint(equalToConstant: 44),
      draw.heightAnchor.constraint(equalToConstant: 44),
      stack.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor, constant: 12),
      stack.centerYAnchor.constraint(equalTo: view.safeAreaLayoutGuide.centerYAnchor),
    ])
    self.stack = stack
    self.pill = pill
    self.draw = draw

    NotificationCenter.default.addObserver(forName: BackseatTalkButton.navigationVisibilityChanged, object: nil,
                                           queue: .main) { note in
      let navigating = (note.object as? Bool) ?? false
      stack.isHidden = !navigating
      timer?.invalidate()
      timer = nil
      if navigating {
        update()
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { _ in update() }
      }
    }
  }

  /// "34 / 35 mph": red with white text when over the limit.
  private static func update() {
    guard let pill else { return }
    let speedMps = max(LocationManager.lastLocation()?.speed ?? 0, 0)
    let speed = Measure(asSpeed: speedMps)
    let limitMps = MWMBackseatRewriter.speedLimitMps
    var text = speed.valueAsString
    if limitMps > 0 {
      text += " / " + Measure(asSpeed: limitMps).valueAsString
    }
    pill.text = "  \(text) \(speed.unit)  "
    let over = limitMps > 0 && speedMps > limitMps
    pill.backgroundColor = over ? .systemRed : .systemBackground
    pill.textColor = over ? .white : .label
    pill.layer.borderColor = (over ? UIColor.white : UIColor.systemRed).cgColor
    let drawTitle = speedMps < BackseatSketch.parkedMps ? "Draw route" : "Draw route (passenger)"
    if draw?.configuration?.title != drawTitle {
      draw?.configuration?.title = drawTitle
    }
  }
}
