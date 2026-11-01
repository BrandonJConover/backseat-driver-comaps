import SwiftUI
import UIKit

/// "Are you actually intending to walk there?" (Brandon's words) as a Backseat card instead of a
/// stock alert: the GPS's unimpressed face, how far the walk is, and two big answers. It waits for
/// an answer, like the alert did. Shown by +[MWMBackseatRewriter walkQuestionAllowsStart].
@objc final class BackseatWalkQuestion: NSObject {
  private static weak var shown: UIViewController?

  @objc static var isShowing: Bool { shown != nil }

  /// `meters`: the walking route's length, 0 when it isn't known.
  @objc static func present(meters: Double, onYes: @escaping () -> Void, onNo: @escaping () -> Void) {
    guard shown == nil, let map = MapViewController.shared() else { return }
    var top: UIViewController = map
    while let next = top.presentedViewController { top = next }
    var host: UIHostingController<BackseatWalkQuestionView>?
    let answer: (Bool) -> Void = { yes in
      host?.dismiss(animated: true) { yes ? onYes() : onNo() }
    }
    host = UIHostingController(rootView: BackseatWalkQuestionView(meters: meters, answer: answer))
    guard let host else { return }
    host.view.backgroundColor = BackseatTheme.asphalt
    host.overrideUserInterfaceStyle = .dark
    host.isModalInPresentation = true
    if let sheet = host.sheetPresentationController {
      sheet.detents = [.custom { _ in 340 }]
      sheet.preferredCornerRadius = 28
    }
    shown = host
    BackseatHaptics.sigh()
    top.present(host, animated: true)
  }

  static func present(onYes: @escaping () -> Void, onNo: @escaping () -> Void) {
    present(meters: 5400, onYes: onYes, onNo: onNo)
  }
}

struct BackseatWalkQuestionView: View {
  let meters: Double
  let answer: (Bool) -> Void

  private var distance: String? {
    guard meters > 0 else { return nil }
    let formatter = MeasurementFormatter()
    formatter.unitOptions = .naturalScale
    formatter.unitStyle = .long
    formatter.numberFormatter.maximumFractionDigits = 1
    return formatter.string(from: Measurement(value: meters, unit: UnitLength.meters))
  }

  /// At a 5 km/h walk: "about 1 hr 5 min".
  private var walkTime: String? {
    guard meters > 0 else { return nil }
    let formatter = DateComponentsFormatter()
    formatter.allowedUnits = meters >= 5000 ? [.hour, .minute] : [.minute]
    formatter.unitsStyle = .short
    return formatter.string(from: meters / 5000 * 3600)
  }

  var body: some View {
    VStack(alignment: .leading, spacing: 18) {
      HStack(alignment: .center, spacing: 14) {
        BackseatFaceRepresentable(mood: 2, size: 52)
          .frame(width: 52, height: 52)
          .accessibilityHidden(true)
        VStack(alignment: .leading, spacing: 4) {
          Text(verbatim: "Are you actually intending to walk there?")
            .font(Font(BackseatTheme.number(26)))
            .foregroundStyle(Color(uiColor: BackseatTheme.text))
            .fixedSize(horizontal: false, vertical: true)
          if let distance, let walkTime {
            Text(verbatim: "\(distance) on foot. About \(walkTime).")
              .font(.subheadline.weight(.medium))
              .foregroundStyle(Color(uiColor: BackseatTheme.muted))
          }
        }
      }
      HStack(spacing: 12) {
        answerButton(title: "Yes", detail: "I'm walking", symbol: "figure.walk", primary: false) { answer(true) }
        answerButton(title: "No", detail: "I'll drive", symbol: "car.fill", primary: true) { answer(false) }
      }
    }
    .padding(.horizontal, 22)
    .padding(.top, 28)
    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
    .background(Color(uiColor: BackseatTheme.asphalt))
    .environment(\.colorScheme, .dark)
  }

  private func answerButton(title: String, detail: String, symbol: String, primary: Bool,
                            action: @escaping () -> Void) -> some View {
    Button(action: action) {
      VStack(spacing: 6) {
        Image(systemName: symbol).font(.system(size: 22, weight: .semibold))
        Text(verbatim: title).font(Font(BackseatTheme.number(24)))
        Text(verbatim: detail).font(.caption.weight(.semibold)).opacity(0.75)
      }
      .frame(maxWidth: .infinity, minHeight: 112)
      .foregroundStyle(Color(uiColor: primary ? BackseatTheme.ink : BackseatTheme.text))
      .background(
        RoundedRectangle(cornerRadius: BackseatTheme.buttonRadius + 4, style: .continuous)
          .fill(Color(uiColor: primary ? BackseatTheme.accent : BackseatTheme.raised))
      )
    }
    .buttonStyle(BackseatPressStyle())
    .accessibilityLabel(Text(verbatim: "\(title), \(detail)"))
  }
}
