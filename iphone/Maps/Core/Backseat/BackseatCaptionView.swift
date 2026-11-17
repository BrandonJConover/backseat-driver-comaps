import UIKit

/// While driving: the GPS's face in the corner, a Patience meter under it, and a speech bubble with
/// whatever it just said, so a missed line can be read at the next light. Tap the face to see the
/// last line again.
/// Feed it from the speech queue (`said`) and the engine's frustration (`setPatience`).
@objc final class BackseatCaption: NSObject {
  /// How far above the safe-area bottom the face sits, clear of the navigation panel.
  static var bottomInset: CGFloat = 132

  private static var view: BackseatCaptionView?
  /// The same face on the CarPlay map. Its bubble only opens while the car is stopped, so the line
  /// can be read at a light and never pulls the driver's eyes to the dash while moving.
  private static weak var carView: BackseatCaptionView?
  /// Slower than this (m/s), the car counts as stopped.
  private static let carStoppedMps = 2.0
  private static var lastLine = ""
  private static var patience = 10

  static func install(in parent: UIView) {
    let caption = BackseatCaptionView()
    caption.translatesAutoresizingMaskIntoConstraints = false
    caption.isHidden = true
    parent.addSubview(caption)
    NSLayoutConstraint.activate([
      caption.leadingAnchor.constraint(equalTo: parent.safeAreaLayoutGuide.leadingAnchor, constant: 12),
      // The trailing edge stays clear of the map-position button.
      caption.trailingAnchor.constraint(lessThanOrEqualTo: parent.safeAreaLayoutGuide.trailingAnchor, constant: -72),
      caption.bottomAnchor.constraint(equalTo: parent.safeAreaLayoutGuide.bottomAnchor, constant: -bottomInset),
    ])
    caption.onFaceTap = { if !lastLine.isEmpty { caption.show(lastLine) } }
    caption.setPatience(patience)
    view = caption

    NotificationCenter.default.addObserver(forName: BackseatTalkButton.navigationVisibilityChanged, object: nil,
                                           queue: .main) { note in
      let navigating = (note.object as? Bool) ?? false
      caption.isHidden = !navigating
      if !navigating { caption.hideBubble(animated: false) }
    }
  }

  /// Puts the face and Patience meter on the CarPlay map, at the driver's side bottom corner. The
  /// car screen's own view controller calls this when it loads; a new one replaces the old.
  @objc static func installOnCar(in parent: UIView) {
    carView?.removeFromSuperview()
    let caption = BackseatCaptionView()
    caption.isQuiet = true
    caption.translatesAutoresizingMaskIntoConstraints = false
    caption.isHidden = !BackseatCarTrip.isNavigating
    parent.addSubview(caption)
    NSLayoutConstraint.activate([
      caption.leadingAnchor.constraint(equalTo: parent.safeAreaLayoutGuide.leadingAnchor, constant: 8),
      caption.trailingAnchor.constraint(lessThanOrEqualTo: parent.safeAreaLayoutGuide.trailingAnchor, constant: -96),
      caption.bottomAnchor.constraint(equalTo: parent.safeAreaLayoutGuide.bottomAnchor, constant: -8),
    ])
    caption.onFaceTap = { if !lastLine.isEmpty && carStopped { caption.show(lastLine) } }
    caption.setPatience(patience)
    carView = caption
    if !carObserverInstalled {
      carObserverInstalled = true
      NotificationCenter.default.addObserver(forName: BackseatTalkButton.navigationVisibilityChanged, object: nil,
                                             queue: .main) { note in
        let navigating = (note.object as? Bool) ?? false
        carView?.isHidden = !navigating
        if !navigating { carView?.hideBubble(animated: false) }
      }
    }
  }

  private static var carObserverInstalled = false

  /// A fix with no speed (-1, from Wi-Fi or a weak GPS) counts as moving: better a shut bubble than
  /// one that opens at 60 mph.
  private static var carStopped: Bool {
    guard let speed = LocationManager.lastLocation()?.speed, speed >= 0 else { return false }
    return speed < carStoppedMps
  }

  /// Posted on the main thread when a line starts playing; the object is the line. The first-launch
  /// disclaimer follows it to know which paragraph is being read.
  static let lineStarted = Notification.Name("BackseatLineStarted")

  /// A line just started playing (Kokoro or the system voice).
  @objc static func said(_ line: String) {
    let text = line.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !text.isEmpty else { return }
    DispatchQueue.main.async {
      lastLine = text
      view?.show(text)
      if carStopped { carView?.show(text) } else { carView?.nudge() }
      NotificationCenter.default.post(name: lineStarted, object: text)
    }
  }

  /// 0 (gone) to 10 (tolerating you).
  @objc static func setPatience(_ left: Int) {
    DispatchQueue.main.async {
      patience = min(max(left, 0), 10)
      view?.setPatience(patience)
      carView?.setPatience(patience)
    }
  }
}

final class BackseatCaptionView: UIView {
  var onFaceTap: (() -> Void)?
  /// No haptics: the car screen's copy of the face, while the phone's buzzes.
  var isQuiet = false

  private let face = BackseatFaceView()
  private let meter = BackseatPatienceMeter()
  private let bubble = UIView()
  private let label = UILabel()
  private var hideWork: DispatchWorkItem?
  private var patience = 10

  override init(frame: CGRect) {
    super.init(frame: frame)

    bubble.backgroundColor = BackseatTheme.text
    bubble.layer.cornerRadius = 18
    bubble.layer.cornerCurve = .continuous
    // A speech-bubble corner pointing at the face.
    bubble.layer.maskedCorners = [.layerMinXMinYCorner, .layerMaxXMinYCorner, .layerMaxXMaxYCorner]
    bubble.layer.shadowColor = UIColor.black.cgColor
    bubble.layer.shadowOpacity = 0.4
    bubble.layer.shadowRadius = 12
    bubble.layer.shadowOffset = CGSize(width: 0, height: 6)
    bubble.alpha = 0

    label.font = BackseatTheme.body(15, weight: .medium)
    label.textColor = BackseatTheme.ink
    label.numberOfLines = 3
    label.lineBreakMode = .byTruncatingTail
    label.translatesAutoresizingMaskIntoConstraints = false
    bubble.addSubview(label)

    let faceColumn = UIStackView(arrangedSubviews: [face, meter])
    faceColumn.axis = .vertical
    faceColumn.alignment = .center
    faceColumn.spacing = 6

    let row = UIStackView(arrangedSubviews: [faceColumn, bubble])
    row.axis = .horizontal
    row.alignment = .bottom
    row.spacing = 10
    row.translatesAutoresizingMaskIntoConstraints = false
    addSubview(row)

    NSLayoutConstraint.activate([
      row.topAnchor.constraint(equalTo: topAnchor),
      row.bottomAnchor.constraint(equalTo: bottomAnchor),
      row.leadingAnchor.constraint(equalTo: leadingAnchor),
      row.trailingAnchor.constraint(equalTo: trailingAnchor),
      label.topAnchor.constraint(equalTo: bubble.topAnchor, constant: 10),
      label.bottomAnchor.constraint(equalTo: bubble.bottomAnchor, constant: -10),
      label.leadingAnchor.constraint(equalTo: bubble.leadingAnchor, constant: 14),
      label.trailingAnchor.constraint(equalTo: bubble.trailingAnchor, constant: -14),
      // The bubble sits level with the face, not the meter.
      bubble.bottomAnchor.constraint(equalTo: face.bottomAnchor),
    ])

    face.addGestureRecognizer(UITapGestureRecognizer(target: self, action: #selector(faceTapped)))
    face.isAccessibilityElement = true
    face.accessibilityTraits = .button
    face.accessibilityHint = "Shows the last thing it said"
  }

  required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

  /// Shows the bubble for about as long as it takes to read it, then fades it out.
  func show(_ text: String) {
    label.text = text
    bubble.accessibilityLabel = text
    // Springs out of the face: grows from its corner.
    let wasHidden = bubble.alpha < 0.01
    if wasHidden {
      bubble.transform = CGAffineTransform(translationX: -16, y: 8).scaledBy(x: 0.6, y: 0.6)
    }
    UIView.animate(withDuration: 0.5, delay: 0, usingSpringWithDamping: 0.62, initialSpringVelocity: 0.4,
                   options: [.allowUserInteraction, .beginFromCurrentState]) {
      self.bubble.alpha = 1
      self.bubble.transform = .identity
    }
    if !wasHidden { BackseatMotion.pop(bubble) }
    hideWork?.cancel()
    let words = text.split(separator: " ").count
    let seconds = min(max(Double(words) * 0.45, 5), 12)
    let work = DispatchWorkItem { [weak self] in self?.hideBubble(animated: true) }
    hideWork = work
    DispatchQueue.main.asyncAfter(deadline: .now() + seconds, execute: work)
  }

  func hideBubble(animated: Bool) {
    hideWork?.cancel()
    if animated {
      UIView.animate(withDuration: 0.35, delay: 0, options: [.curveEaseIn, .beginFromCurrentState]) {
        self.bubble.alpha = 0
        self.bubble.transform = CGAffineTransform(translationX: -8, y: 4).scaledBy(x: 0.9, y: 0.9)
      }
    } else {
      bubble.alpha = 0
    }
  }

  func setPatience(_ left: Int) {
    let dropped = left < patience
    patience = left
    meter.setLevel(left, animated: true)
    face.setMood(left)
    if dropped && !isHidden {
      BackseatMotion.shake(face)
      if !isQuiet { BackseatHaptics.sigh() }
    }
  }

  /// A line played while the bubble stays shut: the face reacts so the car screen feels alive.
  func nudge() {
    guard !isHidden else { return }
    hideBubble(animated: true)
    BackseatMotion.pop(face)
  }

  @objc private func faceTapped() {
    onFaceTap?()
  }

  /// Taps on empty space go through to the map.
  override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
    let inFace = face.point(inside: convert(point, to: face), with: event)
    let inBubble = bubble.alpha > 0 && bubble.point(inside: convert(point, to: bubble), with: event)
    return inFace || inBubble
  }
}

/// The GPS's face: two eyebrows and a mouth that get angrier as patience runs out, in a ring the
/// colour of its mood.
final class BackseatFaceView: UIView {
  private let features = CAShapeLayer()

  override init(frame: CGRect) {
    super.init(frame: frame)
    backgroundColor = BackseatTheme.raised
    layer.cornerRadius = 22
    layer.borderWidth = 3
    features.strokeColor = BackseatTheme.text.cgColor
    features.fillColor = UIColor.clear.cgColor
    features.lineWidth = 2.2
    features.lineCap = .round
    layer.addSublayer(features)
    NSLayoutConstraint.activate([
      widthAnchor.constraint(equalToConstant: 44),
      heightAnchor.constraint(equalToConstant: 44),
    ])
    setMood(10)
  }

  required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

  override func layoutSubviews() {
    super.layoutSubviews()
    features.frame = bounds
  }

  func setMood(_ left: Int) {
    let color = BackseatPatienceMeter.color(for: left)
    layer.borderColor = color.cgColor
    // Drawn in a 44 pt box: brows tilt in and the mouth flattens, then frowns.
    let path = UIBezierPath()
    let tilt: CGFloat = left >= 7 ? 0 : left >= 4 ? 1.5 : 3.5
    path.move(to: CGPoint(x: 14, y: 17 - tilt)); path.addLine(to: CGPoint(x: 19, y: 18 + tilt * 0.4))
    path.move(to: CGPoint(x: 30, y: 17 - tilt)); path.addLine(to: CGPoint(x: 25, y: 18 + tilt * 0.4))
    let mouthBend: CGFloat = left >= 7 ? 3 : left >= 4 ? 0 : -3
    path.move(to: CGPoint(x: 16, y: 28))
    path.addQuadCurve(to: CGPoint(x: 28, y: 28), controlPoint: CGPoint(x: 22, y: 28 + mouthBend * 2))
    features.path = path.cgPath
    accessibilityLabel = "Backseat Driver, " + BackseatPatienceMeter.mood(for: left)
  }
}

/// Ten little segments: green while it's tolerating you, yellow when it's sighing, orange when it's
/// running low.
final class BackseatPatienceMeter: UIView {
  private var segments: [UIView] = []

  static func color(for left: Int) -> UIColor {
    left >= 7 ? BackseatTheme.calm : left >= 4 ? BackseatTheme.accent : BackseatTheme.hot
  }

  static func mood(for left: Int) -> String {
    left >= 7 ? "tolerating you" : left >= 4 ? "sighing" : left >= 1 ? "running low on patience" : "out of patience"
  }

  override init(frame: CGRect) {
    super.init(frame: frame)
    let stack = UIStackView()
    stack.axis = .horizontal
    stack.spacing = 1.5
    stack.distribution = .fillEqually
    stack.translatesAutoresizingMaskIntoConstraints = false
    for _ in 0..<10 {
      let segment = UIView()
      segment.layer.cornerRadius = 1.5
      segments.append(segment)
      stack.addArrangedSubview(segment)
    }
    addSubview(stack)
    NSLayoutConstraint.activate([
      widthAnchor.constraint(equalToConstant: 44),
      heightAnchor.constraint(equalToConstant: 5),
      stack.topAnchor.constraint(equalTo: topAnchor),
      stack.bottomAnchor.constraint(equalTo: bottomAnchor),
      stack.leadingAnchor.constraint(equalTo: leadingAnchor),
      stack.trailingAnchor.constraint(equalTo: trailingAnchor),
    ])
    isAccessibilityElement = true
    setLevel(10)
  }

  required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

  func setLevel(_ left: Int, animated: Bool = false) {
    let color = Self.color(for: left)
    for (i, segment) in segments.enumerated() {
      let target = i < left ? color : BackseatTheme.line
      // Segments drain one after another, right to left.
      let delay = animated ? Double(segments.count - i) * 0.03 : 0
      UIView.animate(withDuration: animated ? 0.25 : 0, delay: delay) {
        segment.backgroundColor = target
      }
    }
    accessibilityLabel = "Patience \(left) of 10"
  }
}
