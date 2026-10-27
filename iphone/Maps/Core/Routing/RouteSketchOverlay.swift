import UIKit

/// Draw-your-own route: a see-through layer over the map. One finger draws along the roads you
/// want; two fingers still move the map. Each stroke reshapes the route (MWMRouter+Sketch).
@objc(MWMRouteSketchOverlay)
final class RouteSketchOverlay: UIView {
  @objc var onStroke: ((MWMRouteSketchResult) -> Void)?
  @objc var onErase: (() -> Void)?
  @objc var onDone: (() -> Void)?

  private weak var mapView: UIView?
  private let strokeLayer = CAShapeLayer()
  private var points: [CGPoint] = []
  private let path = UIBezierPath()
  private let eraseButton = RouteSketchOverlay.pill("Erase drawing")
  private let doneButton = RouteSketchOverlay.pill("Done")

  @objc init(mapView: UIView) {
    self.mapView = mapView
    super.init(frame: mapView.bounds)
    backgroundColor = .clear
    isMultipleTouchEnabled = true

    strokeLayer.strokeColor = UIColor.systemOrange.cgColor
    strokeLayer.fillColor = nil
    strokeLayer.lineWidth = 6
    strokeLayer.lineCap = .round
    strokeLayer.lineJoin = .round
    layer.addSublayer(strokeLayer)

    let hint = UILabel()
    hint.text = "Drag along the roads you want. End on empty map to add a stop."
    hint.font = .preferredFont(forTextStyle: .subheadline)
    hint.textColor = .white
    hint.numberOfLines = 0
    hint.textAlignment = .center
    let hintBox = UIView()
    hintBox.backgroundColor = UIColor.black.withAlphaComponent(0.7)
    hintBox.layer.cornerRadius = 10
    hintBox.translatesAutoresizingMaskIntoConstraints = false
    hint.translatesAutoresizingMaskIntoConstraints = false
    hintBox.addSubview(hint)
    addSubview(hintBox)

    eraseButton.addAction(UIAction { [weak self] _ in
      guard let self else { return }
      if MWMRouter.clearSketch() {
        self.onErase?()
      }
      self.updateErase()
    }, for: .touchUpInside)
    doneButton.addAction(UIAction { [weak self] _ in
      guard let self else { return }
      self.removeFromSuperview()
      self.onDone?()
    }, for: .touchUpInside)
    let buttons = UIStackView(arrangedSubviews: [eraseButton, doneButton])
    buttons.axis = .horizontal
    buttons.spacing = 12
    buttons.translatesAutoresizingMaskIntoConstraints = false
    addSubview(buttons)

    NSLayoutConstraint.activate([
      hintBox.topAnchor.constraint(equalTo: safeAreaLayoutGuide.topAnchor, constant: 12),
      hintBox.centerXAnchor.constraint(equalTo: centerXAnchor),
      hintBox.leadingAnchor.constraint(greaterThanOrEqualTo: leadingAnchor, constant: 16),
      hint.topAnchor.constraint(equalTo: hintBox.topAnchor, constant: 8),
      hint.bottomAnchor.constraint(equalTo: hintBox.bottomAnchor, constant: -8),
      hint.leadingAnchor.constraint(equalTo: hintBox.leadingAnchor, constant: 12),
      hint.trailingAnchor.constraint(equalTo: hintBox.trailingAnchor, constant: -12),
      buttons.centerXAnchor.constraint(equalTo: centerXAnchor),
      buttons.bottomAnchor.constraint(equalTo: safeAreaLayoutGuide.bottomAnchor, constant: -16),
      eraseButton.heightAnchor.constraint(equalToConstant: 44),
      doneButton.heightAnchor.constraint(equalToConstant: 44),
    ])
    updateErase()
  }

  @available(*, unavailable)
  required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

  private static func pill(_ title: String) -> UIButton {
    var config = UIButton.Configuration.filled()
    config.title = title
    config.baseBackgroundColor = .systemBackground
    config.baseForegroundColor = .label
    config.background.cornerRadius = 22
    config.contentInsets = NSDirectionalEdgeInsets(top: 10, leading: 18, bottom: 10, trailing: 18)
    config.titleTextAttributesTransformer = UIConfigurationTextAttributesTransformer { attributes in
      var attributes = attributes
      attributes.font = .preferredFont(forTextStyle: .headline)
      return attributes
    }
    let button = UIButton(configuration: config)
    button.layer.shadowOpacity = 0.2
    button.layer.shadowRadius = 3
    button.layer.shadowOffset = .zero
    return button
  }

  private func updateErase() {
    eraseButton.isHidden = !MWMRouter.hasSketch()
  }

  // Two fingers move the map underneath; only the buttons and one-finger drawing stay here.
  override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
    if let event, (event.allTouches?.count ?? 0) > 1 {
      return false
    }
    return super.point(inside: point, with: event)
  }

  override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
    guard touches.count == 1, let touch = touches.first else { return }
    strokeLayer.removeAllAnimations()
    strokeLayer.opacity = 1
    points = [touch.location(in: self)]
    path.removeAllPoints()
    path.move(to: points[0])
    strokeLayer.path = path.cgPath
  }

  override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
    guard !points.isEmpty, let touch = touches.first else { return }
    for coalesced in event?.coalescedTouches(for: touch) ?? [touch] {
      let p = coalesced.location(in: self)
      points.append(p)
      path.addLine(to: p)
    }
    strokeLayer.path = path.cgPath
  }

  override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
    defer { points = [] }
    guard points.count >= 2, let mapView else {
      fadeStroke()
      return
    }
    let inMap = points.map { NSValue(cgPoint: convert($0, to: mapView)) }
    let result = MWMRouter.applySketch(inMap, in: mapView)
    fadeStroke()
    updateErase()
    onStroke?(result)
  }

  override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
    points = []
    fadeStroke()
  }

  private func fadeStroke() {
    let fade = CABasicAnimation(keyPath: "opacity")
    fade.fromValue = 1
    fade.toValue = 0
    fade.duration = 0.6
    strokeLayer.opacity = 0
    strokeLayer.add(fade, forKey: "fade")
  }
}

/// Opens the drawing layer from the route preview, the Report stack or the "Where to?" card, and
/// voices the GPS's opinion of what was drawn.
@objc final class BackseatSketch: NSObject {
  /// Under this the driver can draw; over it, it's for passengers (8 km/h).
  static let parkedMps = 8 / 3.6

  static func open() {
    guard let controller = MapViewController.shared() else { return }
    guard !(controller.view.subviews.contains { $0 is RouteSketchOverlay }) else { return }
    let overlay = RouteSketchOverlay(mapView: controller.mapView)
    overlay.frame = controller.view.bounds
    overlay.autoresizingMask = [.flexibleWidth, .flexibleHeight]
    overlay.onErase = { say(MWMBackseatRewriter.sketchCleared()) }
    controller.view.addSubview(overlay)
  }

  /// A route finished building: a drawn one gets judged.
  @objc static func routeReady() {
    say(MWMBackseatRewriter.takeSketchVerdict())
  }

  private static func say(_ line: String) {
    guard !line.isEmpty, MWMTextToSpeech.isTTSEnabled() else { return }
    KokoroVoice.shared.sayReply(line, voiceID: nil) { MWMTextToSpeech.tts().play(line) }
  }

  static func button(title: String = "Draw route") -> UIButton {
    var config = UIButton.Configuration.filled()
    config.image = UIImage(systemName: "scribble")
    config.title = title
    config.imagePadding = 6
    config.cornerStyle = .capsule
    config.baseBackgroundColor = .systemBackground
    config.baseForegroundColor = .label
    let button = UIButton(configuration: config)
    button.addAction(UIAction { _ in open() }, for: .touchUpInside)
    return button
  }

  private static var previewTimer: Timer?

  /// In route preview (a route, not yet driving): a "Draw route" button on the leading edge.
  static func installPreviewButton(in view: UIView) {
    let button = button()
    button.translatesAutoresizingMaskIntoConstraints = false
    button.isHidden = true
    view.addSubview(button)
    NSLayoutConstraint.activate([
      button.heightAnchor.constraint(equalToConstant: 44),
      button.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor, constant: 12),
      button.centerYAnchor.constraint(equalTo: view.safeAreaLayoutGuide.centerYAnchor),
    ])
    previewTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak button] _ in
      button?.isHidden = !(MWMRouter.isRoutingActive() && !MWMRouter.isOnRoute())
    }
  }
}
