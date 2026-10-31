import CoreLocation
import SwiftUI
import UIKit

/// Plan a route by tapping, point by point, over the map:
/// - Tap a road: the best route from where you are to there. Each tap after that adds the best
///   route from the last point to the new one. The route is rebuilt and shown after every tap.
/// - Tap a dot: make it a stop, make it a drive-through point again, or remove it. The last dot
///   is the end of the trip.
/// - Drag (one finger or two) moves the map, pinch zooms. Only a tap adds a point.
/// - Change a section: rub out a stretch of the route; nearby roads are offered as lettered pins,
///   and tapping one shows the route going that way. "Use this road" keeps it (as a drive-through
///   point between the points on either side).
/// - Undo takes back the last change. Cancel puts the route back how it was. Go starts driving.
/// - More > Save route keeps it for the "Where to?" card, to drive either way.
@objc(MWMRouteSketchOverlay)
final class RouteSketchOverlay: UIView {
  /// Kept for the ObjC callers; point planning reports nothing per stroke.
  @objc var onStroke: ((MWMRouteSketchResult) -> Void)?
  @objc var onErase: (() -> Void)?
  @objc var onDone: (() -> Void)?

  struct PlanPoint: Equatable {
    var location: CLLocation
    /// A real stop (shown, announced on arrival). Otherwise the route just goes through here.
    var isStop: Bool
  }

  /// The tapped points in order. The trip starts at `planOrigin` and ends at the last one.
  private var points: [PlanPoint] = []
  /// Where I was at the first tap: the start of the trip, and of a saved route.
  private var planOrigin: CLLocation?
  /// Undo steps: the points as they were.
  private var history: [[PlanPoint]] = []
  /// CoMaps' limit is 100 stops between start and finish.
  private static let maxPoints = 101

  private weak var mapView: UIView?
  private var dots: [UIView] = []
  private let hint = UILabel()
  private let subHint = UILabel()
  private let hintBox = UIView()
  private var flashing: DispatchWorkItem?
  /// The one finger down: a tap unless it moves, then it's the map's.
  private var pendingTouch: UITouch?
  private var pressStart = CGPoint.zero
  /// The map has the touches until they all lift.
  private var panning = false
  /// Keeps the dots on the map while it moves (panning, and its glide after).
  private var displayLink: CADisplayLink?
  private var followUntil = Date.distantPast
  private let planner = RoutePlannerModel()
  private var plannerTimer: Timer?
  private let cancelButton = RouteSketchOverlay.squareButton("xmark", label: "Cancel")
  private let undoButton = RouteSketchOverlay.pill("Undo", symbol: "arrow.uturn.backward")
  private let goButton = RouteSketchOverlay.pill("Go", symbol: "car.fill", prominent: true)
  private let moreButton = RouteSketchOverlay.squareButton("ellipsis", label: "More")
  private var buttonsRow = UIStackView()

  // Change a section
  private let sectionButton = RouteSketchOverlay.pill("Change a section", symbol: "scissors")
  private let keepButton = RouteSketchOverlay.pill("Use this road", symbol: "checkmark", prominent: true)
  private let endSectionButton = RouteSketchOverlay.pill("Cancel", symbol: "xmark")
  private var sectionRow = UIStackView()
  private var sectionMode = false
  /// The route's line when the change began; `section` indexes into it.
  private var sectionLine: [CLLocation] = []
  private var section: (from: Int, to: Int)?
  private var rubbing: [CLLocation] = []
  private var rubMoved = false
  /// Roads near the rubbed-out stretch to go by instead, and the one being tried.
  private var candidates: [CLLocation] = []
  private var candidatePins: [UIView] = []
  private var trying: Int?
  private let sectionLayer = CAShapeLayer()
  private var coach: UIView?
  private static let coachedKey = "backseat.plan.coached"

  @objc init(mapView: UIView) {
    self.mapView = mapView
    super.init(frame: mapView.bounds)
    backgroundColor = .clear
    isMultipleTouchEnabled = true

    // Guidance, right above the buttons where the eyes already are.
    BackseatTheme.styleCard(hintBox, radius: 16)
    hint.font = BackseatTheme.body(15, weight: .semibold)
    hint.textColor = BackseatTheme.text
    hint.numberOfLines = 0
    hint.textAlignment = .center
    subHint.font = BackseatTheme.body(13)
    subHint.textColor = BackseatTheme.muted
    subHint.numberOfLines = 0
    subHint.textAlignment = .center
    subHint.text = "Drag to move the map, pinch to zoom."
    sectionButton.configuration?.baseBackgroundColor = BackseatTheme.raised
    sectionButton.layer.shadowOpacity = 0
    sectionButton.heightAnchor.constraint(equalToConstant: 44).isActive = true
    sectionButton.addAction(UIAction { [weak self] _ in self?.beginSection() }, for: .touchUpInside)
    let hintStack = UIStackView(arrangedSubviews: [hint, subHint, sectionButton])
    hintStack.axis = .vertical
    hintStack.spacing = 2
    hintStack.setCustomSpacing(10, after: subHint)

    // The rubbed-out stretch: dashed over the route.
    sectionLayer.strokeColor = BackseatTheme.text.withAlphaComponent(0.9).cgColor
    sectionLayer.fillColor = nil
    sectionLayer.lineWidth = 9
    sectionLayer.lineCap = .round
    sectionLayer.lineJoin = .round
    sectionLayer.lineDashPattern = [8, 8]
    layer.addSublayer(sectionLayer)
    hintStack.translatesAutoresizingMaskIntoConstraints = false
    hintBox.translatesAutoresizingMaskIntoConstraints = false
    hintBox.addSubview(hintStack)
    addSubview(hintBox)

    cancelButton.addAction(UIAction { [weak self] _ in self?.cancel() }, for: .touchUpInside)
    undoButton.addAction(UIAction { [weak self] _ in self?.undo() }, for: .touchUpInside)
    goButton.addAction(UIAction { [weak self] _ in self?.go() }, for: .touchUpInside)
    moreButton.showsMenuAsPrimaryAction = true
    let buttons = UIStackView(arrangedSubviews: [cancelButton, undoButton, goButton, moreButton])
    buttons.axis = .horizontal
    buttons.spacing = 8
    buttons.translatesAutoresizingMaskIntoConstraints = false
    addSubview(buttons)
    buttonsRow = buttons
    // While changing a section, these take the buttons' place.
    endSectionButton.addAction(UIAction { [weak self] _ in self?.endSection(keep: false) }, for: .touchUpInside)
    keepButton.addAction(UIAction { [weak self] _ in self?.endSection(keep: true) }, for: .touchUpInside)
    let sectionButtons = UIStackView(arrangedSubviews: [endSectionButton, keepButton])
    sectionButtons.axis = .horizontal
    sectionButtons.spacing = 8
    sectionButtons.isHidden = true
    sectionButtons.translatesAutoresizingMaskIntoConstraints = false
    addSubview(sectionButtons)
    sectionRow = sectionButtons
    keepButton.setContentHuggingPriority(.defaultLow, for: .horizontal)
    endSectionButton.setContentHuggingPriority(.required, for: .horizontal)
    goButton.setContentHuggingPriority(.defaultLow, for: .horizontal)
    undoButton.setContentHuggingPriority(.required, for: .horizontal)

    // The total and the route options, on top, folded to one line.
    let host = UIHostingController(rootView: RoutePlannerPanel(model: planner))
    host.view.backgroundColor = .clear
    host.view.translatesAutoresizingMaskIntoConstraints = false
    if #available(iOS 16.0, *) { host.sizingOptions = .intrinsicContentSize }
    addSubview(host.view)
    plannerTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
      self?.planner.refresh()
    }

    NSLayoutConstraint.activate([
      host.view.topAnchor.constraint(equalTo: safeAreaLayoutGuide.topAnchor, constant: 8),
      host.view.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
      host.view.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12),

      buttons.leadingAnchor.constraint(equalTo: safeAreaLayoutGuide.leadingAnchor, constant: 12),
      buttons.trailingAnchor.constraint(equalTo: safeAreaLayoutGuide.trailingAnchor, constant: -12),
      buttons.bottomAnchor.constraint(equalTo: safeAreaLayoutGuide.bottomAnchor, constant: -12),
      buttons.heightAnchor.constraint(equalToConstant: 52),
      cancelButton.widthAnchor.constraint(equalToConstant: 52),
      moreButton.widthAnchor.constraint(equalToConstant: 52),
      sectionButtons.leadingAnchor.constraint(equalTo: buttons.leadingAnchor),
      sectionButtons.trailingAnchor.constraint(equalTo: buttons.trailingAnchor),
      sectionButtons.bottomAnchor.constraint(equalTo: buttons.bottomAnchor),
      sectionButtons.heightAnchor.constraint(equalToConstant: 52),

      hintBox.leadingAnchor.constraint(equalTo: buttons.leadingAnchor),
      hintBox.trailingAnchor.constraint(equalTo: buttons.trailingAnchor),
      hintBox.bottomAnchor.constraint(equalTo: buttons.topAnchor, constant: -8),
      hintStack.topAnchor.constraint(equalTo: hintBox.topAnchor, constant: 10),
      hintStack.bottomAnchor.constraint(equalTo: hintBox.bottomAnchor, constant: -10),
      hintStack.leadingAnchor.constraint(equalTo: hintBox.leadingAnchor, constant: 14),
      hintStack.trailingAnchor.constraint(equalTo: hintBox.trailingAnchor, constant: -14),
    ])
    update()

    if !UserDefaults.standard.bool(forKey: Self.coachedKey) {
      DispatchQueue.main.async { [weak self] in self?.showCoach() }
    }
  }

  @available(*, unavailable)
  required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

  deinit {
    displayLink?.invalidate()
    plannerTimer?.invalidate()
  }

  // MARK: - Look

  private static func pill(_ title: String, symbol: String?, prominent: Bool = false) -> UIButton {
    var config = UIButton.Configuration.filled()
    config.title = title
    if let symbol {
      config.image = UIImage(systemName: symbol, withConfiguration: UIImage.SymbolConfiguration(weight: .semibold))
    }
    config.imagePadding = 6
    config.baseBackgroundColor = prominent ? BackseatTheme.accent : BackseatTheme.card
    config.baseForegroundColor = prominent ? BackseatTheme.ink : BackseatTheme.text
    config.background.cornerRadius = BackseatTheme.buttonRadius
    config.background.strokeColor = prominent ? nil : BackseatTheme.line
    config.background.strokeWidth = prominent ? 0 : 1
    config.contentInsets = NSDirectionalEdgeInsets(top: 10, leading: 14, bottom: 10, trailing: 14)
    config.titleTextAttributesTransformer = UIConfigurationTextAttributesTransformer { attributes in
      var attributes = attributes
      attributes.font = prominent ? BackseatTheme.number(22) : BackseatTheme.body(16, weight: .semibold)
      return attributes
    }
    let button = UIButton(configuration: config)
    button.layer.shadowOpacity = 0.35
    button.layer.shadowRadius = 6
    button.layer.shadowOffset = CGSize(width: 0, height: 3)
    return button
  }

  private static func squareButton(_ symbol: String, label: String) -> UIButton {
    let button = pill("", symbol: symbol)
    button.configuration?.title = nil
    button.configuration?.contentInsets = .zero
    button.accessibilityLabel = label
    return button
  }

  // MARK: - State

  private var stopCount: Int { points.dropLast().filter(\.isStop).count }
  private var canSave: Bool { planOrigin != nil && !points.isEmpty }

  private func update() {
    undoButton.isEnabled = !history.isEmpty
    undoButton.alpha = history.isEmpty ? 0.45 : 1
    goButton.configuration?.baseBackgroundColor = points.isEmpty ? BackseatTheme.raised : BackseatTheme.accent
    goButton.configuration?.baseForegroundColor = points.isEmpty ? BackseatTheme.muted : BackseatTheme.ink
    moreButton.menu = UIMenu(children: [
      UIAction(title: "Save route", image: UIImage(systemName: "bookmark"),
               attributes: canSave ? [] : .disabled) { [weak self] _ in self?.askToSave() },
      UIAction(title: "Start over", image: UIImage(systemName: "trash"),
               attributes: points.isEmpty ? [.destructive, .disabled] : .destructive) { [weak self] _ in
        self?.startOver()
      },
      UIAction(title: "How it works", image: UIImage(systemName: "questionmark.circle")) { [weak self] _ in
        self?.showCoach()
      },
    ])
    let passing = points.count - 1 - stopCount
    planner.counts = points.isEmpty ? nil
      : [stopCount > 0 ? "\(stopCount) stop\(stopCount == 1 ? "" : "s")" : nil,
         passing > 0 ? "\(passing) point\(passing == 1 ? "" : "s")" : nil].compactMap { $0 }.joined(separator: " · ")
    buttonsRow.isHidden = sectionMode
    sectionRow.isHidden = !sectionMode
    sectionButton.isHidden = sectionMode || points.isEmpty
    keepButton.isEnabled = trying != nil
    keepButton.alpha = trying == nil ? 0.45 : 1
    if flashing == nil { showHint() }
    redraw()
  }

  private func showHint() {
    hint.textColor = BackseatTheme.text
    subHint.isHidden = false
    if sectionMode {
      if section == nil {
        hint.text = "Rub your finger over the part of the route you want to change."
      } else if candidates.isEmpty {
        hint.text = "No other roads close by. Zoom out and rub out a longer stretch."
      } else if let trying {
        hint.text = "That's the route by road \(Self.letter(trying)). Tap another letter to compare, or use this one."
      } else {
        hint.text = "Tap a lettered road to see the route go that way."
      }
      return
    }
    switch points.count {
    case 0: hint.text = "Tap a road to plan the best route there from where you are."
    case 1: hint.text = "Tap the next spot to keep going from there. Tap a dot to make it a stop."
    default: hint.text = "Keep tapping to add on. Tap a dot to make it a stop or remove it."
    }
    hint.textColor = BackseatTheme.text
    subHint.isHidden = false
  }

  /// Says what just happened for a moment, then goes back to the hint.
  private func flash(_ text: String, warn: Bool = false) {
    flashing?.cancel()
    hint.text = text
    hint.textColor = warn ? BackseatTheme.hot : BackseatTheme.accent
    subHint.isHidden = true
    hintBox.transform = CGAffineTransform(scaleX: 0.96, y: 0.96)
    UIView.animate(withDuration: 0.35, delay: 0, usingSpringWithDamping: 0.55, initialSpringVelocity: 0.6,
                   options: [.allowUserInteraction, .beginFromCurrentState]) {
      self.hintBox.transform = .identity
    }
    if warn { UINotificationFeedbackGenerator().notificationOccurred(.warning) }
    UIAccessibility.post(notification: .announcement, argument: text)
    let work = DispatchWorkItem { [weak self] in
      self?.flashing = nil
      self?.showHint()
    }
    flashing = work
    DispatchQueue.main.asyncAfter(deadline: .now() + 2.8, execute: work)
  }

  /// Every change: one Undo step, then the route is planned again through the new points.
  private func change(_ apply: () -> Void) {
    history.append(points)
    apply()
    replan()
    update()
  }

  private func replan() {
    if points.isEmpty {
      // Nothing tapped any more: the route goes back to how it was before planning.
      MWMRouter.cancelSketchSession()
      MWMRouter.beginSketchSession()
      return
    }
    plan(points)
  }

  private func plan(_ points: [PlanPoint]) {
    var stops = IndexSet()
    for (i, point) in points.enumerated() where point.isStop { stops.insert(i) }
    _ = MWMRouter.planRoute(through: points.map(\.location), stops: stops)
    follow(for: 1.5)
  }

  // MARK: - Change a section

  private func beginSection() {
    guard !points.isEmpty else { return }
    let line = MWMRouter.routeLine()
    guard line.count >= 2 else {
      flash("Give the route a second to finish planning.", warn: true)
      return
    }
    sectionLine = line
    sectionMode = true
    UISelectionFeedbackGenerator().selectionChanged()
    flashing?.cancel()
    flashing = nil
    update()
  }

  /// Keep the road being tried, or put the route back as it was.
  private func endSection(keep: Bool) {
    if keep, let trying {
      let via = PlanPoint(location: candidates[trying], isStop: false)
      let at = insertIndex()
      clearSection()
      change { points.insert(via, at: at) }
      UINotificationFeedbackGenerator().notificationOccurred(.success)
      flash("Done. The route goes that way now.")
    } else {
      let wasTrying = trying != nil
      clearSection()
      if wasTrying { replan() }
      update()
    }
  }

  private func clearSection() {
    sectionMode = false
    sectionLine = []
    section = nil
    rubbing = []
    candidates = []
    trying = nil
  }

  /// Shows the route going by candidate `index`. Nothing is kept until "Use this road".
  private func trySection(_ index: Int) {
    guard index < candidates.count else { return }
    var trial = points
    trial.insert(PlanPoint(location: candidates[index], isStop: false), at: insertIndex())
    plan(trial)
    trying = index
    UISelectionFeedbackGenerator().selectionChanged()
    update()
  }

  /// Where a point for the changed section goes in `points`: after the points before the stretch.
  private func insertIndex() -> Int {
    guard let section, !sectionLine.isEmpty else { return max(points.count - 1, 0) }
    let mid = (section.from + section.to) / 2
    func vertex(_ location: CLLocation) -> Int {
      sectionLine.indices.min { sectionLine[$0].distance(from: location) < sectionLine[$1].distance(from: location) } ?? 0
    }
    return points.firstIndex { vertex($0.location) > mid } ?? max(points.count - 1, 0)
  }

  /// The stretch of the route a rub covers.
  private func rubSpan(_ rub: [CLLocation]) -> (from: Int, to: Int)? {
    guard let mapView, sectionLine.count >= 2, rub.count >= 2,
          let span = MWMRouter.eraseSpan(on: sectionLine, eraser: rub, radiusM: 22 * MWMRouter.metersPerPoint(in: mapView)),
          span.count == 2, span[0].intValue <= span[1].intValue, span[1].intValue < sectionLine.count
    else { return nil }
    return (span[0].intValue, span[1].intValue)
  }

  /// Up to four roads to go by instead: on both sides of the stretch, near and farther out, each
  /// moved onto the nearest road. Ones back on the rubbed-out road, or on top of another, are left out.
  private func findCandidates() {
    candidates = []
    guard let section else { return }
    let a = sectionLine[section.from]
    let b = sectionLine[section.to]
    let mid = sectionLine[(section.from + section.to) / 2]
    let metersPerDegree = 111_320.0
    let cosLat = cos(mid.coordinate.latitude * .pi / 180)
    var east = (b.coordinate.longitude - a.coordinate.longitude) * metersPerDegree * cosLat
    var north = (b.coordinate.latitude - a.coordinate.latitude) * metersPerDegree
    let length = hypot(east, north)
    if length < 1 {
      east = 1
      north = 0
    } else {
      east /= length
      north /= length
    }
    func moved(_ p: CLLocation, east e: Double, north n: Double) -> CLLocation {
      CLLocation(latitude: p.coordinate.latitude + n / metersPerDegree,
                 longitude: p.coordinate.longitude + e / (metersPerDegree * cosLat))
    }
    let erased = Array(sectionLine[section.from...section.to])
    let span = max(length, 200)
    for share in [0.4, 0.8] {
      let distance = min(max(share * span, 250), 3000)
      for side in [1.0, -1.0] {
        let raw = moved(mid, east: -north * distance * side, north: east * distance * side)
        let probe = moved(raw, east: east * 20, north: north * 20)
        let road = MWMRouter.snapStroke(toRoads: [raw, probe]).first ?? raw
        let onErased = erased.contains { $0.distance(from: road) < 150 }
        let onOther = candidates.contains { $0.distance(from: road) < 200 }
        if !onErased && !onOther { candidates.append(road) }
      }
    }
  }

  private static func letter(_ index: Int) -> String {
    String(Character(UnicodeScalar(UInt8(65 + min(index, 25)))))
  }

  private func makeCandidatePin() -> UIView {
    let pin = UIView(frame: CGRect(x: 0, y: 0, width: 36, height: 36))
    pin.layer.cornerRadius = 18
    pin.layer.borderWidth = 3
    pin.layer.shadowColor = UIColor.black.cgColor
    pin.layer.shadowOpacity = 0.45
    pin.layer.shadowRadius = 4
    pin.layer.shadowOffset = CGSize(width: 0, height: 2)
    pin.isUserInteractionEnabled = false
    let label = UILabel(frame: pin.bounds)
    label.textAlignment = .center
    label.font = BackseatTheme.number(18)
    pin.addSubview(label)
    addSubview(pin)
    return pin
  }

  /// The candidate pin under a tap, if any.
  private func candidate(at point: CGPoint) -> Int? {
    var best: (index: Int, distance: CGFloat)?
    for (i, pin) in candidatePins.prefix(candidates.count).enumerated() {
      let d = hypot(pin.center.x - point.x, pin.center.y - point.y)
      if d <= 32, d < (best?.distance ?? .infinity) { best = (i, d) }
    }
    return best?.index
  }

  // MARK: - Actions

  private func addPoint(at screen: CGPoint) {
    guard points.count < Self.maxPoints else {
      flash("That's as many points as a route can have.", warn: true)
      return
    }
    if planOrigin == nil || points.isEmpty {
      guard let here = LocationManager.lastLocation() ?? planOrigin else {
        flash("Still finding where you are. Try again in a second.", warn: true)
        return
      }
      planOrigin = here
    }
    guard let location = coordinate(at: screen) else { return }
    change { points.append(PlanPoint(location: location, isStop: false)) }
    UIImpactFeedbackGenerator(style: .light).impactOccurred()
    if points.count == 1 {
      flash("Routing from you to here. Tap the next spot to keep going.")
    }
  }

  /// A tap on a dot: what it is, and what to do with it.
  private func showMenu(forPoint index: Int) {
    guard index < points.count, let presenter = window?.rootViewController?.topmostPresented else { return }
    let isEnd = index == points.count - 1
    let title = isEnd ? "End of the route" : points[index].isStop ? "Stop" : "Drive-through point"
    let message = isEnd ? "The trip finishes here." : points[index].isStop
      ? "The GPS treats this as a stop on the way."
      : "The route goes through here without stopping."
    let sheet = UIAlertController(title: title, message: message, preferredStyle: .actionSheet)
    if !isEnd {
      let makeStop = !points[index].isStop
      sheet.addAction(UIAlertAction(title: makeStop ? "Make it a stop" : "Just drive through", style: .default) {
        [weak self] _ in
        guard let self else { return }
        self.change { self.points[index].isStop = makeStop }
        self.flash(makeStop ? "Marked as a stop." : "Now it's just a point to drive through.")
      })
    }
    sheet.addAction(UIAlertAction(title: "Remove this point", style: .destructive) { [weak self] _ in
      guard let self else { return }
      self.change { self.points.remove(at: index) }
      self.flash(self.points.isEmpty ? "Removed. Back to the route you had." : "Removed. The route skips it now.")
    })
    sheet.addAction(UIAlertAction(title: "Cancel", style: .cancel))
    if let popover = sheet.popoverPresentationController, index < dots.count {
      popover.sourceView = self
      popover.sourceRect = dots[index].frame
    }
    presenter.present(sheet, animated: true)
  }

  private func undo() {
    guard let last = history.popLast() else { return }
    points = last
    replan()
    update()
    flash("Undone.")
  }

  private func startOver() {
    change { points.removeAll() }
    flash("Cleared. Undo brings it back.")
  }

  /// Starts driving the planned route.
  private func go() {
    guard !points.isEmpty else {
      flash("Tap a road first to plan a route.", warn: true)
      return
    }
    close()
    MWMRouter.startWhenReady()
  }

  /// Leaves the route exactly as it was. With changes, asks first.
  private func cancel() {
    guard !history.isEmpty, let presenter = window?.rootViewController?.topmostPresented else {
      MWMRouter.cancelSketchSession()
      close()
      return
    }
    let alert = UIAlertController(title: "Throw away this route?",
                                  message: "The route goes back to how it was.", preferredStyle: .alert)
    alert.addAction(UIAlertAction(title: "Keep planning", style: .cancel))
    alert.addAction(UIAlertAction(title: "Throw away", style: .destructive) { [weak self] _ in
      MWMRouter.cancelSketchSession()
      self?.close()
    })
    presenter.present(alert, animated: true)
  }

  /// "Save route": asks for a name ("To Melissa Drive" suggested). Saved routes drive either way.
  private func askToSave() {
    guard canSave, let planOrigin, let presenter = window?.rootViewController?.topmostPresented else { return }
    let alert = UIAlertController(title: "Save route",
                                  message: "It'll be on the \u{201C}Where to?\u{201D} card, to drive either way.",
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
                               origin: planOrigin, points: self.points)
      UINotificationFeedbackGenerator().notificationOccurred(.success)
      self.flash("Saved. It's on the Where to card now.")
    })
    presenter.present(alert, animated: true)
  }

  private func close() {
    flashing?.cancel()
    plannerTimer?.invalidate()
    plannerTimer = nil
    MWMRouter.setDrawModeOpen(false)
    displayLink?.invalidate()
    displayLink = nil
    removeFromSuperview()
    onDone?()
  }

  // MARK: - Coach

  /// The first time (and from More > How it works): the whole thing on one card.
  private func showCoach() {
    guard coach == nil else { return }
    let dim = TouchSink()
    dim.backgroundColor = UIColor.black.withAlphaComponent(0.5)
    dim.translatesAutoresizingMaskIntoConstraints = false
    let card = UIView()
    BackseatTheme.styleCard(card)
    card.translatesAutoresizingMaskIntoConstraints = false

    let title = UILabel()
    title.text = "Plan your own route"
    title.font = BackseatTheme.number(26)
    title.textColor = BackseatTheme.text
    let rows: [(String, String)] = [
      ("hand.tap", "Tap a road: the best route from where you are to there."),
      ("arrow.triangle.turn.up.right.diamond", "Tap again: the route carries on from the last point to the new one."),
      ("mappin.and.ellipse", "Tap a dot to make it a stop. The rest you just drive through."),
      ("scissors", "Change a section: rub out part of the route, then tap one of the roads offered."),
      ("arrow.uturn.backward", "Undo takes back the last change."),
      ("bookmark", "Save it from the \u{2026} button and drive it either way next time."),
      ("hand.draw", "Drag to move the map, pinch to zoom."),
    ]
    let stack = UIStackView(arrangedSubviews: [title])
    stack.axis = .vertical
    stack.spacing = 12
    stack.setCustomSpacing(16, after: title)
    for (symbol, text) in rows {
      let icon = UIImageView(image: UIImage(systemName: symbol,
                                            withConfiguration: UIImage.SymbolConfiguration(pointSize: 17, weight: .semibold)))
      icon.tintColor = BackseatTheme.accent
      icon.contentMode = .center
      icon.widthAnchor.constraint(equalToConstant: 28).isActive = true
      let label = UILabel()
      label.text = text
      label.font = BackseatTheme.body(15)
      label.textColor = BackseatTheme.text
      label.numberOfLines = 0
      let row = UIStackView(arrangedSubviews: [icon, label])
      row.spacing = 10
      row.alignment = .center
      stack.addArrangedSubview(row)
    }
    let gotIt = Self.pill("Got it", symbol: nil, prominent: true)
    gotIt.heightAnchor.constraint(equalToConstant: 52).isActive = true
    gotIt.addAction(UIAction { [weak self] _ in self?.hideCoach() }, for: .touchUpInside)
    if let lastRow = stack.arrangedSubviews.last { stack.setCustomSpacing(20, after: lastRow) }
    stack.addArrangedSubview(gotIt)
    stack.translatesAutoresizingMaskIntoConstraints = false
    card.addSubview(stack)
    dim.addSubview(card)
    addSubview(dim)
    NSLayoutConstraint.activate([
      dim.topAnchor.constraint(equalTo: topAnchor),
      dim.bottomAnchor.constraint(equalTo: bottomAnchor),
      dim.leadingAnchor.constraint(equalTo: leadingAnchor),
      dim.trailingAnchor.constraint(equalTo: trailingAnchor),
      card.centerYAnchor.constraint(equalTo: dim.centerYAnchor),
      card.centerXAnchor.constraint(equalTo: dim.centerXAnchor),
      card.leadingAnchor.constraint(greaterThanOrEqualTo: dim.safeAreaLayoutGuide.leadingAnchor, constant: 16),
      card.widthAnchor.constraint(lessThanOrEqualToConstant: 440),
      stack.topAnchor.constraint(equalTo: card.topAnchor, constant: 22),
      stack.bottomAnchor.constraint(equalTo: card.bottomAnchor, constant: -18),
      stack.leadingAnchor.constraint(equalTo: card.leadingAnchor, constant: 20),
      stack.trailingAnchor.constraint(equalTo: card.trailingAnchor, constant: -20),
    ])
    let wide = card.widthAnchor.constraint(equalTo: dim.widthAnchor, constant: -32)
    wide.priority = .defaultHigh
    wide.isActive = true
    coach = dim
    dim.alpha = 0
    UIView.animate(withDuration: 0.2) { dim.alpha = 1 }
  }

  private func hideCoach() {
    UserDefaults.standard.set(true, forKey: Self.coachedKey)
    guard let coach else { return }
    self.coach = nil
    UIView.animate(withDuration: 0.2, animations: { coach.alpha = 0 }) { _ in coach.removeFromSuperview() }
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

  /// Puts every dot where it now is on screen: small white for drive-through, numbered orange for
  /// stops, a yellow flag at the end.
  private func redraw() {
    var stopNumber = 0
    for (i, point) in points.enumerated() {
      let dot = i < dots.count ? dots[i] : makeDot()
      if i >= dots.count { dots.append(dot) }
      let label = dot.subviews.compactMap { $0 as? UILabel }.first
      let flag = dot.subviews.compactMap { $0 as? UIImageView }.first
      let isEnd = i == points.count - 1
      let size: CGFloat
      if isEnd {
        size = 34
        dot.backgroundColor = BackseatTheme.accent
        dot.layer.borderColor = BackseatTheme.ink.cgColor
        label?.isHidden = true
        flag?.isHidden = false
      } else if point.isStop {
        stopNumber += 1
        size = 30
        dot.backgroundColor = BackseatTheme.hot
        dot.layer.borderColor = UIColor.white.cgColor
        label?.text = "\(stopNumber)"
        label?.isHidden = false
        flag?.isHidden = true
      } else {
        size = 18
        dot.backgroundColor = .white
        dot.layer.borderColor = BackseatTheme.ink.cgColor
        label?.isHidden = true
        flag?.isHidden = true
      }
      dot.bounds = CGRect(x: 0, y: 0, width: size, height: size)
      dot.layer.cornerRadius = size / 2
      label?.frame = dot.bounds
      flag?.frame = dot.bounds.insetBy(dx: 7, dy: 7)
      dot.center = screenPoint(point.location)
      dot.isHidden = false
      dot.accessibilityLabel = isEnd ? "End of the route" : point.isStop ? "Stop \(stopNumber)" : "Drive-through point"
    }
    for dot in dots.dropFirst(points.count) { dot.isHidden = true }

    // Changing a section: the stretch rubbed out (or being rubbed), and the roads offered.
    let dashed = UIBezierPath()
    if sectionMode, let span = rubbing.isEmpty ? section : (rubSpan(rubbing) ?? section) {
      for (i, location) in sectionLine[span.from...span.to].enumerated() {
        let p = screenPoint(location)
        if i == 0 { dashed.move(to: p) } else { dashed.addLine(to: p) }
      }
    }
    sectionLayer.path = dashed.cgPath
    for (i, road) in candidates.enumerated() {
      let pin = i < candidatePins.count ? candidatePins[i] : makeCandidatePin()
      if i >= candidatePins.count { candidatePins.append(pin) }
      let on = i == trying
      pin.backgroundColor = on ? BackseatTheme.accent : BackseatTheme.card
      pin.layer.borderColor = BackseatTheme.accent.cgColor
      let label = pin.subviews.first as? UILabel
      label?.text = Self.letter(i)
      label?.textColor = on ? BackseatTheme.ink : BackseatTheme.accent
      pin.center = screenPoint(road)
      pin.isHidden = false
      pin.accessibilityLabel = "Road \(Self.letter(i))"
    }
    for pin in candidatePins.dropFirst(candidates.count) { pin.isHidden = true }
  }

  private func makeDot() -> UIView {
    let dot = UIView()
    dot.layer.borderWidth = 3
    dot.layer.shadowColor = UIColor.black.cgColor
    dot.layer.shadowOpacity = 0.4
    dot.layer.shadowRadius = 3
    dot.layer.shadowOffset = CGSize(width: 0, height: 2)
    dot.isUserInteractionEnabled = false
    dot.isAccessibilityElement = true
    let label = UILabel()
    label.textAlignment = .center
    label.font = BackseatTheme.number(15)
    label.textColor = .white
    dot.addSubview(label)
    let flag = UIImageView(image: UIImage(systemName: "flag.checkered"))
    flag.tintColor = BackseatTheme.ink
    flag.contentMode = .scaleAspectFit
    dot.addSubview(flag)
    insertSubview(dot, at: 0)
    return dot
  }

  /// The dot under a tap, if any (generous, so a dot is easy to hit).
  private func dot(at point: CGPoint) -> Int? {
    var best: (index: Int, distance: CGFloat)?
    for (i, dot) in dots.prefix(points.count).enumerated() {
      let d = hypot(dot.center.x - point.x, dot.center.y - point.y)
      if d <= 28, d < (best?.distance ?? .infinity) { best = (i, d) }
    }
    return best?.index
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

  /// Hands the touches to the map from here on, including any finger already down.
  private func startPanning(_ touches: Set<UITouch>, with event: UIEvent?) {
    panning = true
    pendingTouch = nil
    rubbing = []
    let down = (event?.allTouches ?? touches).filter { $0.phase != .ended && $0.phase != .cancelled }
    next?.touchesBegan(down.isEmpty ? touches : down, with: event)
    follow(for: 0)
  }

  override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
    if panning {
      next?.touchesBegan(touches, with: event)
      return
    }
    if (event?.allTouches?.count ?? touches.count) > 1 {
      startPanning(touches, with: event)
      return
    }
    guard let touch = touches.first else { return }
    pendingTouch = touch
    pressStart = touch.location(in: self)
    if sectionMode {
      // One finger rubs out a stretch (or taps a lettered road); two fingers still move the map.
      rubbing = coordinate(at: pressStart).map { [$0] } ?? []
      rubMoved = false
    }
  }

  override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
    if panning {
      next?.touchesMoved(touches, with: event)
      return
    }
    guard let pending = pendingTouch, touches.contains(pending) else { return }
    let at = pending.location(in: self)
    if sectionMode {
      if hypot(at.x - pressStart.x, at.y - pressStart.y) > 10 { rubMoved = true }
      for coalesced in event?.coalescedTouches(for: pending) ?? [pending] {
        if let c = coordinate(at: coalesced.location(in: self)) { rubbing.append(c) }
      }
      redraw()
      return
    }
    // Moved past a tap: the map takes it.
    if hypot(at.x - pressStart.x, at.y - pressStart.y) > 10 {
      startPanning(touches, with: event)
      next?.touchesMoved(touches, with: event)
    }
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
    guard let pending = pendingTouch, touches.contains(pending) else { return }
    pendingTouch = nil
    if sectionMode {
      let rub = rubbing
      rubbing = []
      if !rubMoved {
        if let index = candidate(at: pressStart) { trySection(index) } else { redraw() }
        return
      }
      if let span = rubSpan(rub) {
        // A new stretch: put back the road being tried, then offer roads for this one.
        if trying != nil {
          trying = nil
          replan()
        }
        section = span
        findCandidates()
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
        update()
      } else {
        redraw()
        flash("Rub right over the route line.", warn: true)
      }
      return
    }
    if let index = dot(at: pressStart) {
      UISelectionFeedbackGenerator().selectionChanged()
      showMenu(forPoint: index)
    } else {
      addPoint(at: pressStart)
    }
  }

  override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
    if panning {
      next?.touchesCancelled(touches, with: event)
      panning = false
      follow(for: 1.5)
      return
    }
    pendingTouch = nil
    rubbing = []
    redraw()
  }
}

/// Swallows touches, so nothing under the coach card lands on the map.
private final class TouchSink: UIView {
  override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {}
  override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {}
  override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {}
  override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {}
}

/// Opens route planning from the route preview, the Report stack or the "Where to?" card, and
/// voices the GPS's opinion of the route planned.
@objc final class BackseatSketch: NSObject {
  /// Under this the driver can plan; over it, it's for passengers (8 km/h).
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

  /// A route finished building: a planned one gets judged.
  @objc static func routeReady() {
    say(MWMBackseatRewriter.takeSketchVerdict())
  }

  private static func say(_ line: String) {
    guard !line.isEmpty, MWMTextToSpeech.isTTSEnabled() else { return }
    KokoroVoice.shared.sayReply(line, voiceID: nil) { MWMTextToSpeech.tts().play(line) }
  }

  static func button(title: String = "Plan route") -> UIButton {
    var config = UIButton.Configuration.filled()
    config.image = UIImage(systemName: "hand.tap")
    config.title = title
    config.imagePadding = 6
    config.cornerStyle = .capsule
    config.baseBackgroundColor = BackseatTheme.card
    config.baseForegroundColor = BackseatTheme.text
    config.background.strokeColor = BackseatTheme.line
    config.background.strokeWidth = 1
    let button = UIButton(configuration: config)
    button.addAction(UIAction { _ in open() }, for: .touchUpInside)
    return button
  }

  private static var previewTimer: Timer?

  /// In route preview (a route, not yet driving): a "Plan route" button on the leading edge.
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

/// Routes kept for next time, by name, in Documents/backseat_saved_routes.json.
/// - Planned by tapping (`origin` + `points`): driven either way, through the same points.
/// - Drawn with a finger before that (`items`): rebuilt through the sketch path, as before.
final class BackseatSavedRoutes {
  struct Route: Codable, Identifiable, Equatable {
    var id = UUID()
    var name: String
    /// Old drawn routes: strokes and stops in the order drawn, each a list of [latitude, longitude].
    var items: [[[Double]]]
    var savedAt = Date()
    /// Tapped routes: where planning started, as [latitude, longitude].
    var origin: [Double]?
    /// Tapped routes: each point as [latitude, longitude, 1 for a stop or 0 to drive through].
    var points: [[Double]]?

    var locations: [[CLLocation]] {
      items.map { item in item.map { CLLocation(latitude: $0[0], longitude: $0[1]) } }
    }

    var isPlanned: Bool { origin != nil && !(points ?? []).isEmpty }

    /// The whole way, start to end, with which places are stops (the two ends always are).
    var path: [RouteSketchOverlay.PlanPoint] {
      guard let origin, origin.count >= 2, let points else { return [] }
      return [RouteSketchOverlay.PlanPoint(location: CLLocation(latitude: origin[0], longitude: origin[1]), isStop: true)]
        + points.filter { $0.count >= 2 }.map {
          RouteSketchOverlay.PlanPoint(location: CLLocation(latitude: $0[0], longitude: $0[1]),
                                       isStop: $0.count > 2 && $0[2] != 0)
        }
    }

    /// Old drawn routes: the same roads the other way, ending back at `home`.
    func reversed(to home: CLLocationCoordinate2D) -> [[CLLocation]] {
      locations.reversed().map { $0.reversed() } + [[CLLocation(latitude: home.latitude, longitude: home.longitude)]]
    }
  }

  static let changed = Notification.Name("BackseatSavedRoutesChanged")
  /// Closer than this to an end of a saved route, you're already there.
  private static let atEndMeters: CLLocationDistance = 300

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

  static func save(name: String, origin: CLLocation, points: [RouteSketchOverlay.PlanPoint]) {
    let route = Route(name: name, items: [],
                      origin: [origin.coordinate.latitude, origin.coordinate.longitude],
                      points: points.map { [$0.location.coordinate.latitude, $0.location.coordinate.longitude, $0.isStop ? 1 : 0] })
    write(all + [route])
  }

  static func rename(_ route: Route, to name: String) {
    write(all.map { saved in
      guard saved.id == route.id else { return saved }
      var renamed = saved
      renamed.name = name
      return renamed
    })
  }

  static func delete(_ route: Route) {
    write(all.filter { $0.id != route.id })
  }

  /// Drives a saved route now, from the end you're nearer to.
  static func start(_ route: Route) {
    drive(route, backwards: nil)
  }

  /// Drives a saved route now, straight into navigation. `backwards` nil picks the way from the
  /// end you're nearer to. `end` replaces where the trip finishes (heading back to where you started).
  static func drive(_ route: Route, backwards: Bool?, endingAt end: CLLocationCoordinate2D? = nil) {
    BackseatReturnTrip.usingSavedRoute = route.id
    guard route.isPlanned else {
      // An old drawn route: backwards ends where it began, unless told where.
      if backwards ?? false, let home = end ?? route.locations.first?.first?.coordinate {
        drive(route.reversed(to: home))
      } else {
        drive(route.locations)
      }
      return
    }
    var path = route.path
    let here = LocationManager.lastLocation()
    let reverse = backwards ?? {
      guard let here, let first = path.first, let last = path.last else { return false }
      return here.distance(from: last.location) < here.distance(from: first.location)
    }()
    if reverse { path.reverse() }
    if let end, !path.isEmpty {
      path[path.count - 1] = RouteSketchOverlay.PlanPoint(
        location: CLLocation(latitude: end.latitude, longitude: end.longitude), isStop: true)
    }
    // Starting at the first end: skip it. Starting somewhere else: get onto the route there first.
    if let here, let first = path.first {
      if here.distance(from: first.location) < atEndMeters {
        path.removeFirst()
      } else {
        path[0].isStop = false
      }
    }
    guard !path.isEmpty else { return }
    var stops = IndexSet()
    for (i, point) in path.enumerated() where point.isStop { stops.insert(i) }
    if MWMRouter.isOnRoute() { MWMRouter.stopRouting() }
    MWMRouter.removePoints()
    MWMRouter.startWhenReady()
    _ = MWMRouter.planRoute(through: path.map(\.location), stops: stops)
  }

  /// Drives old sketch items now, from here.
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

/// What the planner panel shows, read from the route once a second (it re-plans in the background
/// after every tap): the trip's total, and the avoid options.
final class RoutePlannerModel: ObservableObject {
  @Published var summary: String = "Tap a road to start"
  /// "2 stops · 3 points", set by the overlay.
  @Published var counts: String?
  @Published var avoidHighways = false
  @Published var avoidTolls = false
  @Published var avoidFerries = false
  @Published var expanded = false

  init() {
    let options = RoutingOptions()
    avoidHighways = options.avoidMotorway
    avoidTolls = options.avoidToll
    avoidFerries = options.avoidFerry
    refresh()
  }

  func refresh() {
    let text: String
    if let total = MWMRouter.plannerSummary(), let s = total["seconds"]?.doubleValue, let m = total["meters"]?.doubleValue {
      text = "\(Self.time(s)) · \(Self.distance(m))"
    } else if MWMRouter.isRoutingActive() {
      text = "Planning…"
    } else {
      text = "Tap a road to start"
    }
    if text != summary { summary = text }
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

  func applyOptions() {
    let options = RoutingOptions()
    options.avoidMotorway = avoidHighways
    options.avoidToll = avoidTolls
    options.avoidFerry = avoidFerries
    options.save()
    MWMRouter.rebuild(withBestRouter: false)
  }
}

/// The planner panel: the trip's total time and distance, what's on it, and the avoid options.
struct RoutePlannerPanel: View {
  @ObservedObject var model: RoutePlannerModel

  var body: some View {
    VStack(alignment: .leading, spacing: 10) {
      Button {
        withAnimation { model.expanded.toggle() }
      } label: {
        HStack(spacing: 8) {
          Image(systemName: "car.fill")
            .foregroundStyle(Color(uiColor: BackseatTheme.accent))

          VStack(alignment: .leading, spacing: 0) {
            Text(verbatim: model.summary)
              .font(Font(BackseatTheme.number(20, weight: .bold)))
              .monospacedDigit()
              .foregroundStyle(Color(uiColor: BackseatTheme.text))

            if let counts = model.counts, !counts.isEmpty {
              Text(verbatim: counts)
                .font(.caption)
                .foregroundStyle(Color(uiColor: BackseatTheme.muted))
            }
          }

          Spacer()

          Text(verbatim: "Options")
            .font(.subheadline.weight(.semibold))
            .foregroundStyle(Color(uiColor: BackseatTheme.muted))

          Image(systemName: model.expanded ? "chevron.up" : "chevron.down")
            .foregroundStyle(Color(uiColor: BackseatTheme.muted))
        }
        .frame(minHeight: 36)
        .contentShape(Rectangle())
      }
      .buttonStyle(.plain)

      if model.expanded {
        ScrollView(.horizontal, showsIndicators: false) {
          HStack(spacing: 8) {
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
    .background(RoundedRectangle(cornerRadius: 18, style: .continuous).fill(Color(uiColor: BackseatTheme.card)))
    .shadow(color: .black.opacity(0.45), radius: 10, y: 6)
    .environment(\.colorScheme, .dark)
  }

  private func chip(_ title: String, symbol: String, on: Bool, action: @escaping () -> Void) -> some View {
    Button(action: action) {
      HStack(spacing: 4) {
        Image(systemName: symbol)
          .imageScale(.small)

        Text(verbatim: title)
      }
      .font(.subheadline.weight(.medium))
      .foregroundStyle(Color(uiColor: on ? BackseatTheme.ink : BackseatTheme.text))
      .padding(.horizontal, 14)
      .frame(minHeight: 40)
      .background(Capsule().fill(Color(uiColor: on ? BackseatTheme.accent : BackseatTheme.raised)))
      .contentShape(Capsule())
    }
    .buttonStyle(.plain)
  }
}
