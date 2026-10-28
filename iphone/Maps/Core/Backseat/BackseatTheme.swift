import UIKit

/// Backseat Driver's own look: night asphalt surfaces, a road-sign yellow, and an orange for when
/// the GPS has had enough. Only Backseat views and the driving-screen overrides use it; stock CoMaps
/// screens keep the app theme.
/// Mockup: https://claude.ai/artifact/VfJtFoH2w1EYEQAe64JvH7
enum BackseatTheme {
  // MARK: Colors (the same in light and dark: the driving screen is always "night drive")

  /// The darkest surface, behind everything.
  static let asphalt = UIColor(red: 0x15 / 255, green: 0x17 / 255, blue: 0x1A / 255, alpha: 1)
  /// Cards: the turn card, the bottom panel, round buttons.
  static let card = UIColor(red: 0x1F / 255, green: 0x22 / 255, blue: 0x26 / 255, alpha: 1)
  /// Chips and tracks on top of a card.
  static let raised = UIColor(red: 0x2A / 255, green: 0x2E / 255, blue: 0x33 / 255, alpha: 1)
  /// Hairlines and empty meter segments.
  static let line = UIColor(red: 0x3A / 255, green: 0x3F / 255, blue: 0x45 / 255, alpha: 1)
  /// Main text on dark.
  static let text = UIColor(red: 0xF4 / 255, green: 0xF1 / 255, blue: 0xEA / 255, alpha: 1)
  /// Secondary text on dark (7:1 on `card`).
  static let muted = UIColor(red: 0xA7 / 255, green: 0xAD / 255, blue: 0xB4 / 255, alpha: 1)
  /// Road-sign yellow: the turn arrow box, the mic, the route, primary buttons.
  static let accent = UIColor(red: 0xFF / 255, green: 0xC9 / 255, blue: 0x33 / 255, alpha: 1)
  /// Over the limit, out of patience.
  static let hot = UIColor(red: 0xFF / 255, green: 0x6B / 255, blue: 0x3D / 255, alpha: 1)
  /// Patience left.
  static let calm = UIColor(red: 0x7B / 255, green: 0xD8 / 255, blue: 0x8F / 255, alpha: 1)
  /// Text and icons on `accent` or on the white speech bubble.
  static let ink = asphalt

  // MARK: Type (SF Pro condensed for numbers and headings, like highway signs)

  static func number(_ size: CGFloat, weight: UIFont.Weight = .heavy) -> UIFont {
    UIFont.systemFont(ofSize: size, weight: weight, width: .condensed)
  }

  static func body(_ size: CGFloat, weight: UIFont.Weight = .regular) -> UIFont {
    UIFont.systemFont(ofSize: size, weight: weight)
  }

  // MARK: Shapes

  static let cardRadius: CGFloat = 22
  static let buttonRadius: CGFloat = 16

  /// A dark rounded card with a soft drop shadow, for anything floating over the map.
  static func styleCard(_ view: UIView, radius: CGFloat = cardRadius) {
    view.backgroundColor = card
    view.layer.cornerRadius = radius
    view.layer.cornerCurve = .continuous
    view.layer.shadowColor = UIColor.black.cgColor
    view.layer.shadowOpacity = 0.45
    view.layer.shadowRadius = 14
    view.layer.shadowOffset = CGSize(width: 0, height: 8)
  }

  /// A 48 pt square map button: dark card, light SF Symbol, hairline border.
  static func iconButton(symbol: String, label: String) -> UIButton {
    var config = UIButton.Configuration.filled()
    config.image = UIImage(systemName: symbol,
                           withConfiguration: UIImage.SymbolConfiguration(pointSize: 18, weight: .semibold))
    config.baseBackgroundColor = card
    config.baseForegroundColor = text
    config.background.cornerRadius = buttonRadius
    config.background.strokeColor = line
    config.background.strokeWidth = 1
    let button = UIButton(configuration: config)
    button.accessibilityLabel = label
    NSLayoutConstraint.activate([
      button.widthAnchor.constraint(equalToConstant: 48),
      button.heightAnchor.constraint(equalToConstant: 48),
    ])
    return button
  }

  /// The big round yellow mic. Turns hot orange while listening (see BackseatTalkButton.followListening).
  static func styleTalkButton(_ button: UIButton) {
    var config = button.configuration ?? .filled()
    config.image = UIImage(systemName: "mic.fill",
                           withConfiguration: UIImage.SymbolConfiguration(pointSize: 22, weight: .bold))
    config.cornerStyle = .capsule
    config.baseBackgroundColor = accent
    config.baseForegroundColor = ink
    button.configuration = config
    button.layer.shadowColor = accent.cgColor
    button.layer.shadowOpacity = 0.35
    button.layer.shadowRadius = 8
    button.layer.shadowOffset = .zero
  }
}

/// A US-style SPEED LIMIT sign: white, black border, the limit in big condensed digits.
/// Hidden when the limit is unknown.
final class BackseatSpeedLimitSign: UIView {
  private let caption = UILabel()
  private let value = UILabel()

  override init(frame: CGRect) {
    super.init(frame: frame)
    backgroundColor = .white
    layer.cornerRadius = 6
    layer.cornerCurve = .continuous
    layer.borderColor = UIColor.black.cgColor
    layer.borderWidth = 2

    caption.text = "SPEED\nLIMIT"
    caption.numberOfLines = 2
    caption.textAlignment = .center
    caption.font = .systemFont(ofSize: 7, weight: .heavy)
    caption.textColor = .black
    value.textAlignment = .center
    value.font = BackseatTheme.number(26)
    value.textColor = .black
    value.adjustsFontSizeToFitWidth = true
    value.minimumScaleFactor = 0.6

    let stack = UIStackView(arrangedSubviews: [caption, value])
    stack.axis = .vertical
    stack.alignment = .center
    stack.spacing = 0
    stack.translatesAutoresizingMaskIntoConstraints = false
    addSubview(stack)
    NSLayoutConstraint.activate([
      widthAnchor.constraint(equalToConstant: 46),
      heightAnchor.constraint(equalToConstant: 58),
      stack.centerXAnchor.constraint(equalTo: centerXAnchor),
      stack.centerYAnchor.constraint(equalTo: centerYAnchor),
      stack.leadingAnchor.constraint(greaterThanOrEqualTo: leadingAnchor, constant: 3),
      stack.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -3),
    ])
    isAccessibilityElement = true
  }

  required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

  /// "35", or nil to hide the sign.
  func setLimit(_ limit: String?) {
    isHidden = limit == nil
    value.text = limit
    accessibilityLabel = limit.map { "Speed limit \($0)" }
  }
}

/// The same palette for Objective-C (the stock turn card in MWMNavigationInfoView.mm).
@objc(MWMBackseatTheme) final class BackseatThemeObjC: NSObject {
  @objc static var accent: UIColor { BackseatTheme.accent }
  @objc static var card: UIColor { BackseatTheme.card }
  @objc static var text: UIColor { BackseatTheme.text }
  @objc static var muted: UIColor { BackseatTheme.muted }
  @objc static var ink: UIColor { BackseatTheme.ink }
  @objc static func numberFont(_ size: CGFloat) -> UIFont { BackseatTheme.number(size) }
  @objc static func bodyFont(_ size: CGFloat, weight: UIFont.Weight) -> UIFont { BackseatTheme.body(size, weight: weight) }
  @objc static func styleCard(_ view: UIView, radius: CGFloat) { BackseatTheme.styleCard(view, radius: radius) }
}
