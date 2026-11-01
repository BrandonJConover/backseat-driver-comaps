import UIKit

/// Backseat Driver's mic button on the map: tap and say "take me to work" (or answer the license
/// plate game). The navigation panel has its own while a route is being driven.
@objc final class BackseatTalkButton: NSObject {
  /// The map's own mic button, from before the "Where to?" card had one.
  static let showsMapButton = false

  static let navigationVisibilityChanged = Notification.Name("BackseatNavigationVisibilityChanged")

  /// The round yellow mic.
  static func make() -> UIButton {
    let button = UIButton(configuration: .filled())
    BackseatTheme.styleTalkButton(button)
    button.accessibilityLabel = "Talk to Backseat Driver"
    button.addAction(UIAction { _ in BackseatListener.shared.toggle() }, for: .touchUpInside)
    followListening(button)
    return button
  }

  /// Turns `button` hot orange while the GPS is listening, yellow otherwise.
  static func followListening(_ button: UIButton) {
    NotificationCenter.default.addObserver(forName: BackseatListener.listeningChanged, object: nil, queue: .main) {
      [weak button] _ in
      guard let button else { return }
      let listening = BackseatListener.shared.isListening
      button.configuration?.baseBackgroundColor = listening ? BackseatTheme.hot : BackseatTheme.accent
      button.configuration?.baseForegroundColor = BackseatTheme.ink
    }
  }

  /// Puts the mic on the leading edge of the map, halfway down, out of the way of the other controls.
  @objc static func install(in view: UIView) {
    BackseatListener.prepare()
    BackseatHooks.install()
    BackseatReturnTrip.install()
    BackseatDriveOverlay.install(in: view)
    BackseatCaption.install(in: view)
    BackseatQuiz.install(in: view)
    BackseatArrivalCard.install()
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

/// While a route is followed: a speed card (a real SPEED LIMIT sign next to your speed, which goes
/// orange when over), and square Report and Draw buttons, on the leading edge of the map. Added on
/// top of CoMaps' own navigation views; remove the install call to take it out.
final class BackseatDriveOverlay: NSObject {
  private static var stack: UIStackView?
  private static var sign: BackseatSpeedLimitSign?
  private static var speedLabel: UILabel?
  private static var unitLabel: UILabel?
  private static var speedCard: UIView?
  private static var draw: UIButton?
  private static var timer: Timer?

  static func install(in view: UIView) {
    let sign = BackseatSpeedLimitSign()
    let speedLabel = UILabel()
    speedLabel.font = BackseatTheme.number(40)
    speedLabel.textColor = BackseatTheme.text
    speedLabel.textAlignment = .center
    let unitLabel = UILabel()
    unitLabel.font = BackseatTheme.body(12, weight: .semibold)
    unitLabel.textColor = BackseatTheme.muted
    unitLabel.textAlignment = .center
    let speedColumn = UIStackView(arrangedSubviews: [speedLabel, unitLabel])
    speedColumn.axis = .vertical
    speedColumn.alignment = .center
    speedColumn.spacing = -4
    let speedRow = UIStackView(arrangedSubviews: [sign, speedColumn])
    speedRow.axis = .horizontal
    speedRow.alignment = .center
    speedRow.spacing = 10
    speedRow.translatesAutoresizingMaskIntoConstraints = false
    let speedCard = UIView()
    BackseatTheme.styleCard(speedCard, radius: BackseatTheme.buttonRadius)
    speedCard.layer.borderWidth = 2
    speedCard.layer.borderColor = UIColor.clear.cgColor
    speedCard.addSubview(speedRow)
    speedCard.isAccessibilityElement = true
    NSLayoutConstraint.activate([
      speedRow.topAnchor.constraint(equalTo: speedCard.topAnchor, constant: 8),
      speedRow.bottomAnchor.constraint(equalTo: speedCard.bottomAnchor, constant: -8),
      speedRow.leadingAnchor.constraint(equalTo: speedCard.leadingAnchor, constant: 8),
      speedRow.trailingAnchor.constraint(equalTo: speedCard.trailingAnchor, constant: -12),
      speedColumn.widthAnchor.constraint(greaterThanOrEqualToConstant: 44),
    ])

    let report = BackseatTheme.iconButton(symbol: "exclamationmark.triangle.fill", label: "Report police ahead")
    BackseatMotion.addPress(to: report)
    report.addAction(UIAction { _ in
      MWMBackseatRewriter.runTool("report_police", argument: "", spoken: "report police")
    }, for: .touchUpInside)

    // Drawing a route: the driver only while (nearly) stopped; otherwise it says it's for passengers.
    let draw = BackseatTheme.iconButton(symbol: "scribble", label: "Draw route")
    BackseatMotion.addPress(to: draw)
    draw.addAction(UIAction { _ in BackseatSketch.open() }, for: .touchUpInside)

    // CoMaps keeps its search and bookmarks buttons halfway down the leading edge while navigating,
    // so the speed card sits low on that side, just above the GPS's face, and Report and Draw join
    // the mic in the trailing column, above it.
    speedCard.translatesAutoresizingMaskIntoConstraints = false
    speedCard.isHidden = true
    view.addSubview(speedCard)
    let stack = UIStackView(arrangedSubviews: [draw, report])
    stack.axis = .vertical
    stack.spacing = 10
    stack.alignment = .trailing
    stack.translatesAutoresizingMaskIntoConstraints = false
    stack.isHidden = true
    view.addSubview(stack)
    let guide = view.safeAreaLayoutGuide
    NSLayoutConstraint.activate([
      speedCard.leadingAnchor.constraint(equalTo: guide.leadingAnchor, constant: 12),
      speedCard.bottomAnchor.constraint(equalTo: guide.bottomAnchor, constant: -(BackseatCaption.bottomInset + 72)),
      // Lined up with the mic, which is the map-position button's width and sits right above it.
      stack.trailingAnchor.constraint(equalTo: guide.trailingAnchor, constant: -16),
      stack.bottomAnchor.constraint(equalTo: guide.bottomAnchor, constant: -196),
    ])
    self.stack = stack
    self.sign = sign
    self.speedLabel = speedLabel
    self.unitLabel = unitLabel
    self.speedCard = speedCard
    self.draw = draw

    NotificationCenter.default.addObserver(forName: BackseatTalkButton.navigationVisibilityChanged, object: nil,
                                           queue: .main) { note in
      let navigating = (note.object as? Bool) ?? false
      stack.isHidden = !navigating
      speedCard.isHidden = !navigating
      timer?.invalidate()
      timer = nil
      if navigating {
        update()
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { _ in update() }
      }
    }
  }

  /// Under this far from the turn, it's "turn now".
  private static let turnNowMeters = 40.0
  private static var wasNearTurn = false

  /// Two knocks you can feel as the turn arrives, once per turn.
  private static func checkTurnNow() {
    let meters = MWMBackseatRewriter.metersToNextTurn
    let near = meters > 0 && meters < turnNowMeters
    if near && !wasNearTurn { BackseatHaptics.turnNow() }
    wasNearTurn = near
  }

  /// The limit on the sign, your speed beside it: orange digits and an orange edge when over.
  private static func update() {
    // The Patience meter under the GPS's face: 10 when calm, down to 0 when it's had enough.
    BackseatCaption.setPatience(10 - (MWMBackseatRewriter.frustration + 5) / 10)
    guard let speedLabel, let speedCard else { return }
    let speedMps = max(LocationManager.lastLocation()?.speed ?? 0, 0)
    let speed = Measure(asSpeed: speedMps)
    let limitMps = MWMBackseatRewriter.speedLimitMps
    sign?.setLimit(limitMps > 0 ? Measure(asSpeed: limitMps).valueAsString : nil)
    speedLabel.text = speed.valueAsString
    unitLabel?.text = speed.unit
    let over = limitMps > 0 && speedMps > limitMps
    speedLabel.textColor = over ? BackseatTheme.hot : BackseatTheme.text
    speedCard.layer.borderColor = (over ? BackseatTheme.hot : UIColor.clear).cgColor
    BackseatMotion.breathe(speedCard.layer, on: over)
    checkTurnNow()
    speedCard.accessibilityLabel = limitMps > 0
      ? "\(speed.valueAsString) \(speed.unit), limit \(Measure(asSpeed: limitMps).valueAsString)"
      : "\(speed.valueAsString) \(speed.unit)"
    let drawLabel = speedMps < BackseatSketch.parkedMps ? "Draw route" : "Draw route (passenger)"
    if draw?.accessibilityLabel != drawLabel {
      draw?.accessibilityLabel = drawLabel
    }
  }
}
