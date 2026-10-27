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
