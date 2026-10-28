import CoreLocation
import SwiftUI
import UIKit

/// Draw-your-own route: a see-through layer over the map where the driver sketches the way they
/// want to go before anything is routed.
/// - Press on the blue route line (or a white via handle) and drag: the route re-plans through your
///   finger, live. Long-press a via handle to remove it.
/// - One finger elsewhere highlights roads. Lifting the finger doesn't route anything; draw again to add more.
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
  /// Undo steps: the drawing as it was, or a change already made to the route (a drag).
  /// Erase and reconnect: a rubbed-out stretch of the route line (vertex indexes into `line`), and
  /// the drawn way across it once there is one.
  private struct Cut {
    var from: Int
    var to: Int
    var fill: [CLLocation]?
  }
  /// The route's line when editing began; frozen while there are cuts, so their indexes hold.
  private var line: [CLLocation] = []
  private var cuts: [Cut] = []
  private enum Tool { case pen, eraser }
  private var tool = Tool.pen
  /// The eraser's path while rubbing, and the stretch it has rubbed out so far.
  private var rubbing: [CLLocation] = []
  private let cutsLayer = CAShapeLayer()
  private let cutEndsLayer = CAShapeLayer()
  private let penButton = RouteSketchOverlay.toolButton("pencil.tip")
  private let eraserButton = RouteSketchOverlay.toolButton("eraser")
  private let hint = UILabel()
  private let defaultHint = "Drag the blue line to change roads, or draw with one finger. Two fingers move and zoom. Hold to drop a stop."
  /// Pressing on the route: drawing unless the finger stays still, then it drags the route.
  private var holdTimer: Timer?
  private var pressStart = CGPoint.zero
  private var pressOnHandle = false

  private enum Step {
    case drawing(items: [[CLLocation]], eraseRoute: Bool, cuts: [Cut])
    case route
  }
  private var history: [Step] = []
  /// The route itself was changed here (dragged or a via removed); Cancel puts it back.
  private var routeChanged = false
  /// A finger is dragging the route line, and whether it has moved yet.
  private var dragging = false
  private var dragMoved = false
  private var dragStart = CGPoint.zero
  private let handlesLayer = CAShapeLayer()
  private let planner = RoutePlannerModel()
  private var plannerHost: UIHostingController<RoutePlannerPanel>?
  private var plannerTimer: Timer?
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
    handlesLayer.fillColor = UIColor.white.cgColor
    handlesLayer.strokeColor = UIColor.systemBlue.cgColor
    handlesLayer.lineWidth = 3
    layer.addSublayer(handlesLayer)

    hint.text = defaultHint
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
    // Pen and eraser, on the right edge.
    for layer in [cutsLayer, cutEndsLayer] {
      self.layer.insertSublayer(layer, below: drawingLayer)
    }
    cutsLayer.strokeColor = UIColor.white.cgColor
    cutsLayer.fillColor = nil
    cutsLayer.lineWidth = 9
    cutsLayer.lineCap = .round
    cutsLayer.lineDashPattern = [8, 8]
    cutEndsLayer.lineWidth = 3
    cutEndsLayer.strokeColor = UIColor.white.cgColor
    penButton.addAction(UIAction { [weak self] _ in self?.select(.pen) }, for: .touchUpInside)
    eraserButton.addAction(UIAction { [weak self] _ in self?.select(.eraser) }, for: .touchUpInside)
    let tools = UIStackView(arrangedSubviews: [penButton, eraserButton])
    tools.axis = .vertical
    tools.spacing = 8
    tools.translatesAutoresizingMaskIntoConstraints = false
    addSubview(tools)
    NSLayoutConstraint.activate([
      tools.trailingAnchor.constraint(equalTo: safeAreaLayoutGuide.trailingAnchor, constant: -12),
      tools.centerYAnchor.constraint(equalTo: centerYAnchor),
      penButton.widthAnchor.constraint(equalToConstant: 48),
      penButton.heightAnchor.constraint(equalToConstant: 48),
      eraserButton.widthAnchor.constraint(equalToConstant: 48),
      eraserButton.heightAnchor.constraint(equalToConstant: 48),
    ])
    line = MWMRouter.routeLine()

    let buttons = UIStackView(arrangedSubviews: [cancelButton, undoButton, eraseButton, saveButton, goButton])
    buttons.axis = .horizontal
    buttons.spacing = 8
    buttons.translatesAutoresizingMaskIntoConstraints = false
    addSubview(buttons)

    let press = UILongPressGestureRecognizer(target: self, action: #selector(longPressed(_:)))
    press.minimumPressDuration = 0.5
    press.allowableMovement = 10
    addGestureRecognizer(press)

    // The planner: total, stops and options, on top.
    planner.routeChanged = { [weak self] in self?.routeWasChanged() }
    planner.addStop = { [weak self] in self?.addStopBySearch() }
    let host = UIHostingController(rootView: RoutePlannerPanel(model: planner))
    host.view.backgroundColor = .clear
    host.view.translatesAutoresizingMaskIntoConstraints = false
    if #available(iOS 16.0, *) { host.sizingOptions = .intrinsicContentSize }
    addSubview(host.view)
    plannerHost = host
    plannerTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
      guard let self else { return }
      self.planner.refresh()
      // The route can re-plan (drags, stops); its line is followed until something is cut out of it.
      if self.cuts.isEmpty { self.line = MWMRouter.routeLine() }
      self.redraw()
    }

    NSLayoutConstraint.activate([
      host.view.topAnchor.constraint(equalTo: safeAreaLayoutGuide.topAnchor, constant: 8),
      host.view.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
      host.view.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12),
      hintBox.topAnchor.constraint(equalTo: host.view.bottomAnchor, constant: 8),
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

  deinit {
    displayLink?.invalidate()
    plannerTimer?.invalidate()
  }

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

  private var hasChanges: Bool { !items.isEmpty || eraseRoute || routeChanged || !cuts.isEmpty }
  private var gapOpen: Bool { cuts.contains { $0.fill == nil } }

  private static func toolButton(_ symbol: String) -> UIButton {
    var config = UIButton.Configuration.filled()
    config.image = UIImage(systemName: symbol)
    config.baseBackgroundColor = .systemBackground
    config.baseForegroundColor = .label
    config.cornerStyle = .medium
    let button = UIButton(configuration: config)
    button.layer.shadowOpacity = 0.2
    button.layer.shadowRadius = 3
    button.layer.shadowOffset = .zero
    return button
  }

  private func select(_ tool: Tool) {
    self.tool = tool
    updateButtons()
  }

  private func updateButtons() {
    penButton.configuration?.baseBackgroundColor = tool == .pen ? .systemOrange : .systemBackground
    penButton.configuration?.baseForegroundColor = tool == .pen ? .white : .label
    eraserButton.configuration?.baseBackgroundColor = tool == .eraser ? .systemOrange : .systemBackground
    eraserButton.configuration?.baseForegroundColor = tool == .eraser ? .white : .label
    undoButton.isHidden = history.isEmpty
    eraseButton.isHidden = items.isEmpty && (eraseRoute || !MWMRouter.hasSketch())
    saveButton.isHidden = items.isEmpty && cuts.isEmpty
    // An erased stretch has to be drawn across before the route can be planned.
    let ready = hasChanges && !gapOpen
    goButton.isEnabled = ready
    goButton.alpha = ready ? 1 : 0.5
    hint.text = gapOpen ? "Connect the gap: draw from one end of the dashed stretch to the other."
      : tool == .eraser ? "Rub out the part of the blue route you don't want." : defaultHint
    confirmingCancel = false
    cancelButton.configuration?.title = "Cancel"
    cancelButton.configuration?.baseForegroundColor = .label
  }

  private func change(_ apply: () -> Void) {
    history.append(.drawing(items: items, eraseRoute: eraseRoute, cuts: cuts))
    apply()
    redraw()
    updateButtons()
  }

  // MARK: - Actions

  private func undo() {
    guard let last = history.popLast() else { return }
    switch last {
    case .drawing(let previousItems, let previousErase, let previousCuts):
      items = previousItems
      eraseRoute = previousErase
      cuts = previousCuts
    case .route:
      MWMRouter.undoSketch()
      follow(for: 1.5)
    }
    redraw()
    updateButtons()
  }

  /// The route was changed directly (a drag, a via removed): one Undo step.
  private func routeWasChanged() {
    history.append(.route)
    routeChanged = true
    follow(for: 1.5)
    updateButtons()
  }

  private func erase() {
    current.removeAll()
    change {
      items.removeAll()
      cuts.removeAll()
      if MWMRouter.hasSketch() { eraseRoute = true }
    }
  }

  /// Nothing touches the route until here.
  private func go() {
    guard let mapView, hasChanges, !gapOpen else { return }
    // Reconnected stretches first, then anything drawn freehand.
    let fills = cuts.compactMap { cut in cut.fill.map { ["from": cut.from, "to": cut.to, "stroke": $0] as [String: Any] } }
    if !fills.isEmpty {
      onStroke?(MWMRouter.applyRouteFills(fills, line: line, in: mapView))
    }
    if items.isEmpty {
      if eraseRoute, MWMRouter.clearSketch() { onErase?() }
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
    MWMRouter.cancelSketchSession()
    close()
  }

  /// "Save route": asks for a name ("To Melissa Drive" suggested) and keeps the drawing.
  private func askToSave() {
    guard !items.isEmpty || !cuts.isEmpty, !gapOpen,
          let presenter = window?.rootViewController?.topmostPresented else { return }
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
      BackseatSavedRoutes.save(name: name.isEmpty ? BackseatSavedRoutes.suggestedName() : name,
                               items: self.editedLine.map { [$0] + self.items } ?? self.items)
      UINotificationFeedbackGenerator().notificationOccurred(.success)
    })
    presenter.present(alert, animated: true)
  }

  /// The route as edited (the kept line with each cut drawn across), for Save: a saved route is the
  /// result, not the edits. Nil without cuts.
  private var editedLine: [CLLocation]? {
    guard !cuts.isEmpty, !gapOpen, !line.isEmpty else { return nil }
    var out: [CLLocation] = []
    var next = 0
    for cut in cuts.sorted(by: { $0.from < $1.from }) {
      if cut.from > next { out += line[next..<cut.from] }
      out += cut.fill ?? []
      next = cut.to + 1
    }
    if next < line.count { out += line[next...] }
    return out
  }

  /// "+ Add stop": keeps what's been done, closes Draw mode and opens search; a place picked there
  /// is added with the place card's "Add stop".
  private func addStopBySearch() {
    if !items.isEmpty, let mapView {
      onStroke?(MWMRouter.buildSketch(items, in: mapView))
    }
    close()
    MapViewController.shared()?.searchManager.startSearching(isRouting: true)
  }

  private func close() {
    holdTimer?.invalidate()
    plannerTimer?.invalidate()
    plannerTimer = nil
    MWMRouter.setDrawModeOpen(false)
    displayLink?.invalidate()
    displayLink = nil
    removeFromSuperview()
    onDone?()
  }

  @objc private func longPressed(_ press: UILongPressGestureRecognizer) {
    guard press.state == .began, let mapView else { return }
    let at = press.location(in: self)
    // Held still on a via handle: remove it. (Held on the line itself is the start of a drag.)
    if dragging && !dragMoved && !pressOnHandle { return }
    if dragging && !dragMoved {
      dragging = false
      MWMRouter.endRouteDrag()
      if MWMRouter.removeVia(at: convert(at, to: mapView), in: mapView) {
        routeWasChanged()
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
      }
      return
    }
    guard let stop = coordinate(at: at) else { return }
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
    let handles = UIBezierPath()
    if let mapView {
      for value in MWMRouter.viaHandles(in: mapView) {
        let p = convert(value.cgPointValue, from: mapView)
        handles.append(UIBezierPath(ovalIn: CGRect(x: p.x - 7, y: p.y - 7, width: 14, height: 14)))
      }
    }
    handlesLayer.path = handles.cgPath
    // Erased stretches: dashed over the route, with their ends marked (red until drawn across).
    let dashed = UIBezierPath()
    let ends = UIBezierPath()
    var anyOpen = false
    for cut in cuts where cut.to < line.count {
      dashed.append(path(for: Array(line[cut.from...cut.to])))
      if let fill = cut.fill { all.append(path(for: fill)) } else { anyOpen = true }
      for end in [line[cut.from], line[cut.to]] {
        let p = screenPoint(end)
        ends.append(UIBezierPath(ovalIn: CGRect(x: p.x - 9, y: p.y - 9, width: 18, height: 18)))
      }
    }
    if !rubbing.isEmpty, let span = rubSpan(), span.to < line.count {
      dashed.append(path(for: Array(line[span.from...span.to])))
    }
    cutsLayer.path = dashed.cgPath
    cutEndsLayer.path = ends.cgPath
    cutEndsLayer.fillColor = (anyOpen ? UIColor.systemRed : UIColor.systemGreen).cgColor
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
    if !panning && !dragging && Date() > followUntil {
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
    guard let touch = touches.first, let mapView else { return }
    let at = touch.location(in: self)
    if tool == .eraser {
      rubbing = coordinate(at: at).map { [$0] } ?? []
      redraw()
      return
    }
    pressStart = at
    // On a via handle: drag it straight away.
    let onHandle = MWMRouter.viaHandles(in: mapView).contains {
      let p = convert($0.cgPointValue, from: mapView)
      return hypot(p.x - at.x, p.y - at.y) <= 28
    }
    if onHandle, cuts.isEmpty, MWMRouter.beginRouteDrag(at: convert(at, to: mapView), in: mapView) {
      startDrag(at: at, onHandle: true)
      return
    }
    guard let start = coordinate(at: at) else { return }
    current = [start]
    redraw()
    // On the route line: a stroke, unless the finger holds still a moment, which drags the route.
    holdTimer?.invalidate()
    if cuts.isEmpty {
      holdTimer = Timer.scheduledTimer(withTimeInterval: 0.35, repeats: false) { [weak self] _ in
        guard let self, let mapView = self.mapView, self.current.count <= 2 else { return }
        if MWMRouter.beginRouteDrag(at: self.convert(self.pressStart, to: mapView), in: mapView) {
          self.current.removeAll()
          self.startDrag(at: self.pressStart, onHandle: false)
        }
      }
    }
  }

  private func startDrag(at point: CGPoint, onHandle: Bool) {
    dragging = true
    dragMoved = false
    dragStart = point
    pressOnHandle = onHandle
    UISelectionFeedbackGenerator().selectionChanged()
    follow(for: 0)
    redraw()
  }

  /// Meters a finger can be off and still mean a spot on the route.
  private var reachM: Double {
    guard let mapView else { return 30 }
    return 30 * MWMRouter.metersPerPoint(in: mapView)
  }

  /// The stretch of the route line rubbed out so far.
  private func rubSpan() -> (from: Int, to: Int)? {
    guard let mapView, !line.isEmpty, !rubbing.isEmpty,
          let span = MWMRouter.eraseSpan(on: line, eraser: rubbing, radiusM: 22 * MWMRouter.metersPerPoint(in: mapView)),
          span.count == 2 else { return nil }
    return (span[0].intValue, span[1].intValue)
  }

  override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
    if panning {
      next?.touchesMoved(touches, with: event)
      return
    }
    if dragging, let touch = touches.first, let mapView {
      let at = touch.location(in: self)
      if !dragMoved && hypot(at.x - dragStart.x, at.y - dragStart.y) < 8 { return }
      dragMoved = true
      MWMRouter.moveRouteDrag(to: convert(at, to: mapView), in: mapView)
      return
    }
    if tool == .eraser, let touch = touches.first {
      for coalesced in event?.coalescedTouches(for: touch) ?? [touch] {
        if let c = coordinate(at: coalesced.location(in: self)) { rubbing.append(c) }
      }
      redraw()
      return
    }
    guard !current.isEmpty, let touch = touches.first else { return }
    if hypot(touch.location(in: self).x - pressStart.x, touch.location(in: self).y - pressStart.y) > 8 {
      holdTimer?.invalidate()
    }
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
    if dragging {
      dragging = false
      MWMRouter.endRouteDrag()
      if dragMoved {
        routeWasChanged()
      } else {
        // A tap on the line, not a drag: take the via it made back out.
        MWMRouter.undoSketch()
        follow(for: 0.5)
      }
      return
    }
    holdTimer?.invalidate()
    if tool == .eraser {
      let span = rubSpan()
      rubbing.removeAll()
      if let span {
        // A new cut swallows any it overlaps, and their drawn ways across go with them.
        change {
          var merged = Cut(from: span.from, to: span.to, fill: nil)
          cuts.removeAll { cut in
            let overlaps = cut.from <= merged.to && cut.to >= merged.from
            if overlaps {
              merged.from = min(merged.from, cut.from)
              merged.to = max(merged.to, cut.to)
            }
            return overlaps
          }
          cuts.append(merged)
          cuts.sort { $0.from < $1.from }
        }
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
      } else {
        redraw()
      }
      return
    }
    // Lifting the finger only finishes the highlight; nothing is routed until Go. The highlight
    // moves onto the roads traced, and those snapped points are what Go routes through.
    let stroke = current.count >= 2 ? MWMRouter.snapStroke(toRoads: current) : current
    current.removeAll()
    guard stroke.count >= 2 else {
      redraw()
      return
    }
    // From one end of an erased stretch to the other: that's the way across.
    if !line.isEmpty {
      for (i, cut) in cuts.enumerated() where cut.fill == nil {
        if let way = MWMRouter.matchGap(on: line, from: cut.from, to: cut.to, stroke: stroke, reachM: reachM) {
          change { cuts[i].fill = way }
          return
        }
      }
      // From the route back onto the route, nothing erased: it replaces the stretch between.
      if !gapOpen, let span = MWMRouter.strokeAcrossRoute(line, stroke: stroke, reachM: reachM), span.count == 2 {
        let cut = Cut(from: span[0].intValue, to: span[1].intValue, fill: nil)
        let way = MWMRouter.matchGap(on: line, from: cut.from, to: cut.to, stroke: stroke, reachM: reachM * 3) ?? stroke
        let overlaps = cuts.contains { $0.from <= cut.to && $0.to >= cut.from }
        if !overlaps {
          change {
            cuts.append(Cut(from: cut.from, to: cut.to, fill: way))
            cuts.sort { $0.from < $1.from }
          }
          return
        }
      }
    }
    change { items.append(stroke) }
  }

  override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
    if panning {
      next?.touchesCancelled(touches, with: event)
      panning = false
      follow(for: 1.5)
      return
    }
    if dragging {
      // The long press took over (removing a via), or the system interrupted.
      dragging = false
      MWMRouter.endRouteDrag()
      redraw()
      return
    }
    // The long press took over (a stop), or the system interrupted: drop the stroke.
    holdTimer?.invalidate()
    current.removeAll()
    rubbing.removeAll()
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
    MWMRouter.beginSketchSession()
    MWMRouter.setDrawModeOpen(true)
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

/// What the Draw mode planner panel shows, read from the route once a second (it re-plans in the
/// background after every edit).
final class RoutePlannerModel: ObservableObject {
  struct Stop: Identifiable, Equatable {
    let id: Int
    let title: String
    let kind: String
    let leg: String?
  }

  @Published var stops: [Stop] = []
  @Published var summary: String = "Planning…"
  @Published var roundTrip = false
  @Published var avoidHighways = false
  @Published var avoidTolls = false
  @Published var avoidFerries = false
  @Published var expanded = true

  /// Called after the panel changed the route itself, for Undo.
  var routeChanged: () -> Void = {}
  /// "+ Add stop": search for a place.
  var addStop: () -> Void = {}

  init() {
    let options = RoutingOptions()
    avoidHighways = options.avoidMotorway
    avoidTolls = options.avoidToll
    avoidFerries = options.avoidFerry
    refresh()
  }

  func refresh() {
    let rows = MWMRouter.plannerStops()
    let next = rows.enumerated().map { index, row -> Stop in
      let seconds = (row["legSeconds"] as? NSNumber)?.doubleValue
      let meters = (row["legMeters"] as? NSNumber)?.doubleValue
      let leg = seconds.map { s in [Self.time(s), meters.map(Self.distance)].compactMap { $0 }.joined(separator: " · ") }
      return Stop(id: index, title: row["title"] as? String ?? "", kind: row["kind"] as? String ?? "stop", leg: leg)
    }
    if next != stops { stops = next }
    if let total = MWMRouter.plannerSummary(), let s = total["seconds"]?.doubleValue, let m = total["meters"]?.doubleValue {
      let text = "\(Self.time(s)) · \(Self.distance(m))"
      if text != summary { summary = text }
    } else if summary != "Planning…" {
      summary = "Planning…"
    }
    let round = MWMRouter.isRoundTrip
    if round != roundTrip { roundTrip = round }
  }

  /// "1 h 5 min", "12 min".
  static func time(_ seconds: Double) -> String {
    let minutes = max(1, Int((seconds / 60).rounded()))
    return minutes < 60 ? "\(minutes) min" : "\(minutes / 60) h \(minutes % 60) min"
  }

  /// Miles or kilometers, as the phone's region uses.
  static func distance(_ meters: Double) -> String {
    let formatter = MeasurementFormatter()
    formatter.unitOptions = .providedUnit
    formatter.numberFormatter.maximumFractionDigits = 1
    let metric = Locale.current.measurementSystem == .metric
    let measurement = Measurement(value: meters, unit: UnitLength.meters)
      .converted(to: metric ? .kilometers : .miles)
    return formatter.string(from: measurement)
  }

  // MARK: Actions

  func move(from source: IndexSet, to destination: Int) {
    guard let from = source.first else { return }
    // List's destination counts the row's old place; the router wants where it ends up.
    let to = destination > from ? destination - 1 : destination
    guard from > 0, to > 0 else { return }
    MWMRouter.movePlannerStop(from: from, to: to)
    routeChanged()
    refresh()
  }

  func delete(_ offsets: IndexSet) {
    for index in offsets.sorted(by: >) where index > 0 {
      MWMRouter.removePlannerStop(at: index)
      routeChanged()
    }
    refresh()
  }

  func optimize() {
    MWMRouter.optimizePlannerStops()
    routeChanged()
    refresh()
  }

  func setRoundTrip(_ on: Bool) {
    MWMRouter.isRoundTrip = on
    routeChanged()
    refresh()
  }

  func applyOptions() {
    let options = RoutingOptions()
    options.avoidMotorway = avoidHighways
    options.avoidToll = avoidTolls
    options.avoidFerry = avoidFerries
    options.save()
    MWMRouter.rebuild(withBestRouter: false)
  }

  var canOptimize: Bool { stops.filter { $0.kind == "stop" }.count >= 2 }
}

/// Draw mode's planner, MapQuest style: the trip's total, its stops in order (drag to reorder,
/// swipe to delete, time for each leg), and the trip options.
struct RoutePlannerPanel: View {
  @ObservedObject var model: RoutePlannerModel

  var body: some View {
    VStack(alignment: .leading, spacing: 8) {
      Button {
        withAnimation { model.expanded.toggle() }
      } label: {
        HStack {
          Image(systemName: "car.fill")
            .foregroundStyle(.orange)

          Text(verbatim: model.summary)
            .font(.headline)
            .monospacedDigit()

          Spacer()

          Image(systemName: model.expanded ? "chevron.up" : "chevron.down")
            .foregroundStyle(.secondary)
        }
        .contentShape(Rectangle())
      }
      .buttonStyle(.plain)

      if model.expanded {
        List {
          ForEach(model.stops) { stop in
            HStack(spacing: 10) {
              Text(verbatim: Self.letter(for: stop.id))
                .font(.caption.weight(.bold))
                .foregroundStyle(.white)
                .frame(width: 22, height: 22)
                .background(Circle().fill(stop.kind == "start" ? Color.green : stop.kind == "finish" ? Color.red : Color.orange))

              Text(verbatim: stop.title)
                .lineLimit(1)

              Spacer(minLength: 4)

              if let leg = stop.leg {
                Text(verbatim: leg)
                  .font(.caption)
                  .foregroundStyle(.secondary)
                  .monospacedDigit()
              }
            }
            .moveDisabled(stop.kind == "start")
            .deleteDisabled(stop.kind == "start" || model.stops.count <= 2)
          }
          .onMove(perform: model.move)
          .onDelete(perform: model.delete)
        }
        .listStyle(.plain)
        .environment(\.editMode, .constant(.active))
        .frame(height: min(CGFloat(model.stops.count) * 44, 176))

        ScrollView(.horizontal, showsIndicators: false) {
          HStack(spacing: 8) {
            chip("Add stop", symbol: "plus", on: false) { model.addStop() }
            chip("Optimize order", symbol: "wand.and.stars", on: false) { model.optimize() }
              .disabled(!model.canOptimize)
              .opacity(model.canOptimize ? 1 : 0.4)
            chip("Round trip", symbol: "arrow.triangle.2.circlepath", on: model.roundTrip) {
              model.setRoundTrip(!model.roundTrip)
            }
            chip("No highways", symbol: "road.lanes", on: model.avoidHighways) {
              model.avoidHighways.toggle()
              model.applyOptions()
            }
            chip("No tolls", symbol: "dollarsign.circle", on: model.avoidTolls) {
              model.avoidTolls.toggle()
              model.applyOptions()
            }
            chip("No ferries", symbol: "ferry", on: model.avoidFerries) {
              model.avoidFerries.toggle()
              model.applyOptions()
            }
          }
        }
      }
    }
    .padding(12)
    .background(RoundedRectangle(cornerRadius: 18, style: .continuous).fill(Color(uiColor: .systemBackground)))
    .shadow(radius: 3)
  }

  /// A, B, C… down the list.
  private static func letter(for index: Int) -> String {
    let scalar = UnicodeScalar(UInt8(65 + min(index, 25)))
    return String(Character(scalar))
  }

  private func chip(_ title: String, symbol: String, on: Bool, action: @escaping () -> Void) -> some View {
    Button(action: action) {
      HStack(spacing: 4) {
        Image(systemName: symbol)
          .imageScale(.small)

        Text(verbatim: title)
      }
      .font(.subheadline.weight(.medium))
      .foregroundStyle(on ? Color.white : Color.primary)
      .padding(.horizontal, 12)
      .frame(minHeight: 36)
      .background(Capsule().fill(on ? Color.orange : Color(uiColor: .secondarySystemBackground)))
    }
    .buttonStyle(.plain)
  }
}
