import CoreLocation
import UIKit

/// Draw-your-own route: a see-through layer over the map. One finger draws along the roads you
/// want; two fingers pan and zoom the map without ending the drawing. A stroke that starts where
/// the last one ended carries that line on, so a route can run past the screen edge: draw, pan,
/// keep going. Undo takes strokes back one at a time. Each stroke reshapes the route
/// (MWMRouter+Sketch).
@objc(MWMRouteSketchOverlay)
final class RouteSketchOverlay: UIView {
  @objc var onStroke: ((MWMRouteSketchResult) -> Void)?
  @objc var onErase: (() -> Void)?
  @objc var onDone: (() -> Void)?

  private weak var mapView: UIView?
  private let strokeLayer = CAShapeLayer()
  private var points: [CGPoint] = []
  /// The stroke being drawn, on the map, so it survives panning.
  private var coordinates: [CLLocation] = []
  /// The line so far (all strokes that continued one another), for the next stroke to carry on.
  private var line: [CLLocation] = []
  /// This stroke started where the line ended.
  private var continuing = false
  /// Two fingers are down: the map gets the touches until they all lift.
  private var panning = false
  private let path = UIBezierPath()
  private let undoButton = RouteSketchOverlay.pill("Undo")
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
    hint.text = "Drag along the roads you want. Two fingers move the map; start where you left off to keep going. End on empty map to add a stop."
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

    undoButton.configuration?.image = UIImage(systemName: "arrow.uturn.backward")
    undoButton.configuration?.imagePadding = 6
    undoButton.addAction(UIAction { [weak self] _ in
      guard let self else { return }
      MWMRouter.undoSketch()
      // The line before the undone stroke isn't known any more; the next stroke starts fresh.
      self.line = []
      self.updateErase()
    }, for: .touchUpInside)
    eraseButton.addAction(UIAction { [weak self] _ in
      guard let self else { return }
      if MWMRouter.clearSketch() {
        self.onErase?()
      }
      self.line = []
      self.updateErase()
    }, for: .touchUpInside)
    doneButton.addAction(UIAction { [weak self] _ in
      guard let self else { return }
      MWMRouter.forgetSketchHistory()
      self.removeFromSuperview()
      self.onDone?()
    }, for: .touchUpInside)
    let buttons = UIStackView(arrangedSubviews: [undoButton, eraseButton, doneButton])
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
      undoButton.heightAnchor.constraint(equalToConstant: 44),
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
    undoButton.isHidden = !MWMRouter.canUndoSketch()
  }

  /// How close to the end of the line a new stroke must start to carry it on.
  private static let continueWithin: CGFloat = 44

  private func coordinate(at point: CGPoint) -> CLLocation? {
    guard let mapView else { return nil }
    let c = MWMRouter.coordinate(at: convert(point, to: mapView), in: mapView)
    return CLLocation(latitude: c.latitude, longitude: c.longitude)
  }

  // Two fingers go to the map (pan, pinch) for as long as they're down; the stroke in progress, if
  // any, is dropped rather than drawn.

  override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
    if panning || (event?.allTouches?.count ?? touches.count) > 1 {
      if !panning {
        panning = true
        points = []
        coordinates = []
        fadeStroke()
      }
      next?.touchesBegan(touches, with: event)
      return
    }
    guard let touch = touches.first else { return }
    let start = touch.location(in: self)
    strokeLayer.removeAllAnimations()
    strokeLayer.opacity = 1
    points = [start]
    coordinates = coordinate(at: start).map { [$0] } ?? []
    continuing = false
    if let end = line.last, let mapView {
      let endOnScreen = convert(MWMRouter.point(for: end.coordinate, in: mapView), from: mapView)
      continuing = hypot(endOnScreen.x - start.x, endOnScreen.y - start.y) <= Self.continueWithin
    }
    path.removeAllPoints()
    path.move(to: start)
    strokeLayer.path = path.cgPath
  }

  override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
    if panning {
      next?.touchesMoved(touches, with: event)
      return
    }
    guard !points.isEmpty, let touch = touches.first else { return }
    for coalesced in event?.coalescedTouches(for: touch) ?? [touch] {
      let p = coalesced.location(in: self)
      points.append(p)
      if let c = coordinate(at: p) { coordinates.append(c) }
      path.addLine(to: p)
    }
    strokeLayer.path = path.cgPath
  }

  override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
    if panning {
      next?.touchesEnded(touches, with: event)
      let stillDown = (event?.allTouches ?? []).filter { $0.phase != .ended && $0.phase != .cancelled }
      if stillDown.isEmpty { panning = false }
      return
    }
    defer {
      points = []
      coordinates = []
    }
    guard coordinates.count >= 2, let mapView else {
      fadeStroke()
      return
    }
    let previous = continuing ? line : []
    let result = MWMRouter.applySketchCoordinates(coordinates, previous: previous, in: mapView)
    if result.contains(.applied) {
      line = previous + coordinates
    }
    fadeStroke()
    updateErase()
    onStroke?(result)
  }

  override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
    if panning {
      next?.touchesCancelled(touches, with: event)
      panning = false
      return
    }
    points = []
    coordinates = []
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
    MWMRouter.forgetSketchHistory()
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
