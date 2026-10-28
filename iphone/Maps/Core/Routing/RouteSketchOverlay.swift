import CoreLocation
import UIKit

/// Draw-your-own route: a see-through layer over the map where the driver sketches the way they
/// want to go before anything is routed.
/// - One finger highlights roads. Lifting the finger doesn't route anything; draw again to add more.
/// - Two fingers pan and pinch-zoom the map, and the drawing stays put on the map.
/// - A long press (half a second, held still) drops a stop pin.
/// - Undo takes back the last stroke or pin. Erase clears everything, including an earlier drawn route.
/// - Go builds the route from all of it, in the order drawn (MWMRouter+Sketch), and closes.
@objc(MWMRouteSketchOverlay)
final class RouteSketchOverlay: UIView {
  @objc var onStroke: ((MWMRouteSketchResult) -> Void)?
  @objc var onErase: (() -> Void)?
  @objc var onDone: (() -> Void)?

  /// A stroke (two or more coordinates) or a stop (one), in the order drawn.
  private var items: [[CLLocation]] = []
  /// Erase also takes away the route drawn earlier; it happens on Go, so Cancel leaves it be.
  private var eraseRoute = false
  /// How things were before each change, for Undo (strokes, stops and erases alike).
  private var history: [(items: [[CLLocation]], eraseRoute: Bool)] = []
  /// Cancel was tapped once with a drawing: the next tap discards it.
  private var confirmingCancel = false
  private weak var mapView: UIView?
  private let drawingLayer = CAShapeLayer()
  private let currentLayer = CAShapeLayer()
  private var pins: [UIView] = []
  /// The stroke under the finger, on the map so it survives panning.
  private var current: [CLLocation] = []
  /// Two fingers are down: the map gets the touches until they all lift.
  private var panning = false
  /// Keeps the drawing on the map while it moves (panning, and its glide after).
  private var displayLink: CADisplayLink?
  private var followUntil = Date.distantPast
  private let undoButton = RouteSketchOverlay.pill("Undo", symbol: "arrow.uturn.backward")
  private let eraseButton = RouteSketchOverlay.pill("Erase", symbol: "trash")
  private let cancelButton = RouteSketchOverlay.pill("Cancel", symbol: "xmark")
  private let saveButton = RouteSketchOverlay.pill("Save", symbol: "bookmark")
  private let goButton = RouteSketchOverlay.pill("Go", symbol: "car.fill", prominent: true)

  @objc init(mapView: UIView) {
    self.mapView = mapView
    super.init(frame: mapView.bounds)
    backgroundColor = .clear
    isMultipleTouchEnabled = true

    for layer in [drawingLayer, currentLayer] {
      layer.strokeColor = UIColor.systemOrange.cgColor
      layer.fillColor = nil
      layer.lineWidth = 6
      layer.lineCap = .round
      layer.lineJoin = .round
      self.layer.addSublayer(layer)
    }
    drawingLayer.opacity = 0.8

    let hint = UILabel()
    hint.text = "One finger highlights roads. Two fingers move and zoom. Hold to drop a stop. Tap Go when done."
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

    undoButton.addAction(UIAction { [weak self] _ in self?.undo() }, for: .touchUpInside)
    eraseButton.addAction(UIAction { [weak self] _ in self?.erase() }, for: .touchUpInside)
    cancelButton.addAction(UIAction { [weak self] _ in self?.cancel() }, for: .touchUpInside)
    saveButton.addAction(UIAction { [weak self] _ in self?.askToSave() }, for: .touchUpInside)
    goButton.addAction(UIAction { [weak self] _ in self?.go() }, for: .touchUpInside)
    let buttons = UIStackView(arrangedSubviews: [cancelButton, undoButton, eraseButton, saveButton, goButton])
    buttons.axis = .horizontal
    buttons.spacing = 8
    buttons.translatesAutoresizingMaskIntoConstraints = false
    addSubview(buttons)

    let press = UILongPressGestureRecognizer(target: self, action: #selector(longPressed(_:)))
    press.minimumPressDuration = 0.5
    press.allowableMovement = 10
    addGestureRecognizer(press)

    NSLayoutConstraint.activate([
      hintBox.topAnchor.constraint(equalTo: safeAreaLayoutGuide.topAnchor, constant: 12),
      hintBox.centerXAnchor.constraint(equalTo: centerXAnchor),
      hintBox.leadingAnchor.constraint(greaterThanOrEqualTo: leadingAnchor, constant: 16),
      hint.topAnchor.constraint(equalTo: hintBox.topAnchor, constant: 8),
      hint.bottomAnchor.constraint(equalTo: hintBox.bottomAnchor, constant: -8),
      hint.leadingAnchor.constraint(equalTo: hintBox.leadingAnchor, constant: 12),
      hint.trailingAnchor.constraint(equalTo: hintBox.trailingAnchor, constant: -12),
      buttons.centerXAnchor.constraint(equalTo: centerXAnchor),
      buttons.leadingAnchor.constraint(greaterThanOrEqualTo: leadingAnchor, constant: 12),
      buttons.bottomAnchor.constraint(equalTo: safeAreaLayoutGuide.bottomAnchor, constant: -16),
    ] + [undoButton, eraseButton, cancelButton, saveButton, goButton].map { $0.heightAnchor.constraint(equalToConstant: 44) })
    updateButtons()
  }

  @available(*, unavailable)
  required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

  deinit { displayLink?.invalidate() }

  private static func pill(_ title: String, symbol: String?, prominent: Bool = false) -> UIButton {
    var config = UIButton.Configuration.filled()
    config.title = title
    if let symbol { config.image = UIImage(systemName: symbol) }
    config.imagePadding = 6
    config.baseBackgroundColor = prominent ? .systemOrange : .systemBackground
    config.baseForegroundColor = prominent ? .white : .label
    config.background.cornerRadius = 22
    config.contentInsets = NSDirectionalEdgeInsets(top: 10, leading: 14, bottom: 10, trailing: 14)
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

  private var hasChanges: Bool { !items.isEmpty || eraseRoute }

  private func updateButtons() {
    undoButton.isHidden = history.isEmpty
    eraseButton.isHidden = items.isEmpty && (eraseRoute || !MWMRouter.hasSketch())
    saveButton.isHidden = items.isEmpty
    goButton.isEnabled = hasChanges
    goButton.alpha = hasChanges ? 1 : 0.5
    confirmingCancel = false
    cancelButton.configuration?.title = "Cancel"
    cancelButton.configuration?.baseForegroundColor = .label
  }

  private func change(_ apply: () -> Void) {
    history.append((items, eraseRoute))
    apply()
    redraw()
    updateButtons()
  }

  // MARK: - Actions

  private func undo() {
    guard let last = history.popLast() else { return }
    items = last.items
    eraseRoute = last.eraseRoute
    redraw()
    updateButtons()
  }

  private func erase() {
    current.removeAll()
    change {
      items.removeAll()
      if MWMRouter.hasSketch() { eraseRoute = true }
    }
  }

  /// Nothing touches the route until here.
  private func go() {
    guard let mapView, hasChanges else { return }
    if items.isEmpty {
      if MWMRouter.clearSketch() { onErase?() }
    } else {
      onStroke?(MWMRouter.buildSketch(items, in: mapView))
    }
    close()
  }

  /// Leaves the route exactly as it was. With a drawing, the first tap asks (right on the button).
  private func cancel() {
    if hasChanges && !confirmingCancel {
      confirmingCancel = true
      cancelButton.configuration?.title = "Discard drawing?"
      cancelButton.configuration?.baseForegroundColor = .systemRed
      DispatchQueue.main.asyncAfter(deadline: .now() + 4) { [weak self] in
        guard let self, self.confirmingCancel else { return }
        self.updateButtons()
      }
      return
    }
    close()
  }

  /// "Save route": asks for a name ("To Melissa Drive" suggested) and keeps the drawing.
  private func askToSave() {
    guard !items.isEmpty, let presenter = window?.rootViewController?.topmostPresented else { return }
    let alert = UIAlertController(title: "Save route", message: "Find it on the \u{201C}Where to?\u{201D} card next time.",
                                  preferredStyle: .alert)
    alert.addTextField { field in
      field.text = BackseatSavedRoutes.suggestedName()
      field.clearButtonMode = .whileEditing
      field.autocapitalizationType = .words
    }
    alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
    alert.addAction(UIAlertAction(title: "Save", style: .default) { [weak self, weak alert] _ in
      guard let self else { return }
      let name = alert?.textFields?.first?.text?.trimmingCharacters(in: .whitespaces) ?? ""
      BackseatSavedRoutes.save(name: name.isEmpty ? BackseatSavedRoutes.suggestedName() : name, items: self.items)
      UINotificationFeedbackGenerator().notificationOccurred(.success)
    })
    presenter.present(alert, animated: true)
  }

  private func close() {
    displayLink?.invalidate()
    displayLink = nil
    removeFromSuperview()
    onDone?()
  }

  @objc private func longPressed(_ press: UILongPressGestureRecognizer) {
    guard press.state == .began, let stop = coordinate(at: press.location(in: self)) else { return }
    current.removeAll()
    change { items.append([stop]) }
    UIImpactFeedbackGenerator(style: .medium).impactOccurred()
  }

  // MARK: - Map position

  private func coordinate(at point: CGPoint) -> CLLocation? {
    guard let mapView else { return nil }
    let c = MWMRouter.coordinate(at: convert(point, to: mapView), in: mapView)
    return CLLocation(latitude: c.latitude, longitude: c.longitude)
  }

  private func screenPoint(_ location: CLLocation) -> CGPoint {
    guard let mapView else { return .zero }
    return convert(MWMRouter.point(for: location.coordinate, in: mapView), from: mapView)
  }

  private func path(for stroke: [CLLocation]) -> UIBezierPath {
    let path = UIBezierPath()
    for (i, location) in stroke.enumerated() {
      let p = screenPoint(location)
      if i == 0 { path.move(to: p) } else { path.addLine(to: p) }
    }
    return path
  }

  /// Puts every stroke and pin where it now is on screen.
  private func redraw() {
    let all = UIBezierPath()
    var stopNumber = 0
    var pinIndex = 0
    for item in items {
      if item.count == 1 {
        stopNumber += 1
        let pin = pinIndex < pins.count ? pins[pinIndex] : makePin()
        if pinIndex >= pins.count { pins.append(pin) }
        (pin.subviews.first as? UILabel)?.text = "\(stopNumber)"
        pin.center = screenPoint(item[0])
        pin.isHidden = false
        pinIndex += 1
      } else {
        all.append(path(for: item))
      }
    }
    for pin in pins[pinIndex...] { pin.isHidden = true }
    drawingLayer.path = all.cgPath
    currentLayer.path = path(for: current).cgPath
  }

  private func makePin() -> UIView {
    let pin = UIView(frame: CGRect(x: 0, y: 0, width: 28, height: 28))
    pin.backgroundColor = .systemOrange
    pin.layer.cornerRadius = 14
    pin.layer.borderColor = UIColor.white.cgColor
    pin.layer.borderWidth = 3
    pin.isUserInteractionEnabled = false
    let label = UILabel(frame: pin.bounds)
    label.textAlignment = .center
    label.font = .systemFont(ofSize: 13, weight: .bold)
    label.textColor = .white
    pin.addSubview(label)
    insertSubview(pin, at: 0)
    return pin
  }

  /// Redraws every frame while the map moves, and for a moment after (its glide).
  private func follow(for seconds: TimeInterval) {
    followUntil = max(followUntil, Date().addingTimeInterval(seconds))
    guard displayLink == nil else { return }
    let link = CADisplayLink(target: self, selector: #selector(tick))
    link.add(to: .main, forMode: .common)
    displayLink = link
  }

  @objc private func tick() {
    redraw()
    if !panning && Date() > followUntil {
      displayLink?.invalidate()
      displayLink = nil
    }
  }

  // MARK: - Touches

  override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
    if panning || (event?.allTouches?.count ?? touches.count) > 1 {
      if !panning {
        // A second finger: this is a pan or a pinch, not a stroke.
        panning = true
        current.removeAll()
        follow(for: 0)
      }
      next?.touchesBegan(touches, with: event)
      return
    }
    guard let touch = touches.first, let start = coordinate(at: touch.location(in: self)) else { return }
    current = [start]
    redraw()
  }

  override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
    if panning {
      next?.touchesMoved(touches, with: event)
      return
    }
    guard !current.isEmpty, let touch = touches.first else { return }
    for coalesced in event?.coalescedTouches(for: touch) ?? [touch] {
      if let c = coordinate(at: coalesced.location(in: self)) { current.append(c) }
    }
    redraw()
  }

  override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
    if panning {
      next?.touchesEnded(touches, with: event)
      let stillDown = (event?.allTouches ?? []).filter { $0.phase != .ended && $0.phase != .cancelled }
      if stillDown.isEmpty {
        panning = false
        follow(for: 1.5)
      }
      return
    }
    // Lifting the finger only finishes the highlight; nothing is routed until Go.
    let stroke = current
    current.removeAll()
    if stroke.count >= 2 {
      change { items.append(stroke) }
    } else {
      redraw()
    }
  }

  override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
    if panning {
      next?.touchesCancelled(touches, with: event)
      panning = false
      follow(for: 1.5)
      return
    }
    // The long press took over (a stop), or the system interrupted: drop the stroke.
    current.removeAll()
    redraw()
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

/// Drawn routes kept for next time: the strokes and stops from Draw mode, by name, in
/// Documents/backseat_saved_routes.json. Starting one rebuilds it through the same sketch path, so it
/// drives (and gets judged) like a freshly drawn route.
final class BackseatSavedRoutes {
  struct Route: Codable, Identifiable, Equatable {
    var id = UUID()
    var name: String
    /// Strokes and stops in the order drawn, each a list of [latitude, longitude].
    var items: [[[Double]]]
    var savedAt = Date()

    var locations: [[CLLocation]] {
      items.map { item in item.map { CLLocation(latitude: $0[0], longitude: $0[1]) } }
    }

    /// The same roads the other way, ending back at `home`.
    func reversed(to home: CLLocationCoordinate2D) -> [[CLLocation]] {
      locations.reversed().map { $0.reversed() } + [[CLLocation(latitude: home.latitude, longitude: home.longitude)]]
    }
  }

  static let changed = Notification.Name("BackseatSavedRoutesChanged")

  private static var fileURL: URL {
    FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
      .appendingPathComponent("backseat_saved_routes.json")
  }

  static var all: [Route] {
    guard let data = try? Data(contentsOf: fileURL) else { return [] }
    return (try? JSONDecoder().decode([Route].self, from: data)) ?? []
  }

  private static func write(_ routes: [Route]) {
    if let data = try? JSONEncoder().encode(routes) {
      try? data.write(to: fileURL, options: .atomic)
    }
    NotificationCenter.default.post(name: changed, object: nil)
  }

  static func save(name: String, items: [[CLLocation]]) {
    let route = Route(name: name, items: items.map { $0.map { [$0.coordinate.latitude, $0.coordinate.longitude] } })
    write(all + [route])
  }

  static func rename(_ route: Route, to name: String) {
    write(all.map { $0.id == route.id ? Route(id: $0.id, name: name, items: $0.items, savedAt: $0.savedAt) : $0 })
  }

  static func delete(_ route: Route) {
    write(all.filter { $0.id != route.id })
  }

  /// Drives a saved route now: rebuilt from its sketch, straight into navigation.
  static func start(_ route: Route) {
    BackseatReturnTrip.usingSavedRoute = route.id
    drive(route.locations)
  }

  /// Drives sketch items now, from here.
  static func drive(_ items: [[CLLocation]]) {
    guard let mapView = MapViewController.shared()?.mapView else { return }
    if MWMRouter.isOnRoute() { MWMRouter.stopRouting() }
    MWMRouter.removePoints()
    MWMRouter.startWhenReady()
    MWMRouter.buildSketch(items, in: mapView)
  }

  /// "To Melissa Drive": the route's destination, if it has one with a name.
  static func suggestedName() -> String {
    if let finish = MWMRouter.finishPoint(), !finish.title.isEmpty, !finish.isMyPosition {
      return "To \(finish.title)"
    }
    return "My route"
  }
}

private extension UIViewController {
  /// The view controller on top of whatever this one presents.
  var topmostPresented: UIViewController {
    presentedViewController?.topmostPresented ?? self
  }
}
