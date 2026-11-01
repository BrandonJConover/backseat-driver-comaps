@objc(MWMRouteStartButton)
final class RouteStartButton: UIButton {
  @objc func statePrepare() {
    isHidden = false
    isEnabled = false
  }

  @objc func stateError() {
    isHidden = true
    isEnabled = false
  }

  @objc func stateReady() {
    isHidden = false
    isEnabled = true
  }

  @objc func stateHidden() {
    isHidden = true
    isEnabled = true
  }

  override func applyTheme() {
    super.applyTheme()
    setBackgroundImage(UIColor.linkBlue().getImage(), for: .normal)
    setBackgroundImage(UIColor.linkBlueHighlighted().getImage(), for: .highlighted)
    backseatStyle()
  }

  /// Backseat: the same road-sign Start as the place card. Heavy condensed dark label, a dark
  /// arrow, a pill, and a squeeze on touch.
  private var backseatStyled = false

  private func backseatStyle() {
    titleLabel?.font = BackseatTheme.number(22)
    setTitleColor(BackseatTheme.ink, for: .normal)
    setTitleColor(BackseatTheme.ink, for: .highlighted)
    setTitleColor(BackseatTheme.ink.withAlphaComponent(0.45), for: .disabled)
    let arrow = UIImage(systemName: "location.north.fill",
                        withConfiguration: UIImage.SymbolConfiguration(pointSize: 15, weight: .heavy))
    setImage(arrow?.withTintColor(BackseatTheme.ink, renderingMode: .alwaysOriginal), for: .normal)
    setImagePadding(8)
    clipsToBounds = true
    layer.cornerCurve = .continuous
    guard !backseatStyled else { return }
    backseatStyled = true
    BackseatMotion.addPress(to: self)
  }

  override func layoutSubviews() {
    super.layoutSubviews()
    layer.cornerRadius = bounds.height / 2
  }
}
