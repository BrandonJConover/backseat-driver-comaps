import SwiftUI
import UIKit

/// App-wide look for the screens CoMaps draws itself: Settings, search, bookmarks, menus and alerts.
/// Always dark, with yellow tint and sign-like condensed titles. Call `apply(to:)` once the window
/// exists (MapsAppDelegate), and again for any window CarPlay or a scene creates.
@objc(MWMBackseatAppearance) final class BackseatAppearance: NSObject {
  @objc static func apply(to window: UIWindow?) {
    guard let window else { return }
    window.overrideUserInterfaceStyle = .dark
    window.tintColor = BackseatTheme.accent
    window.backgroundColor = BackseatTheme.asphalt
  }

  /// UIKit appearance proxies. Call once at launch, before the first screen is made.
  @objc static func applyProxies() {
    let titleFont = BackseatTheme.number(20, weight: .bold)
    let largeTitleFont = BackseatTheme.number(38)

    let bar = UINavigationBarAppearance()
    bar.configureWithOpaqueBackground()
    bar.backgroundColor = BackseatTheme.asphalt
    bar.shadowColor = .clear
    bar.titleTextAttributes = [.font: titleFont, .foregroundColor: BackseatTheme.text]
    bar.largeTitleTextAttributes = [.font: largeTitleFont, .foregroundColor: BackseatTheme.text]
    let navigation = UINavigationBar.appearance()
    navigation.standardAppearance = bar
    navigation.scrollEdgeAppearance = bar
    navigation.compactAppearance = bar
    navigation.tintColor = BackseatTheme.accent

    UISwitch.appearance().onTintColor = BackseatTheme.accent
    UISegmentedControl.appearance().selectedSegmentTintColor = BackseatTheme.accent
    UISegmentedControl.appearance().setTitleTextAttributes([.foregroundColor: BackseatTheme.ink], for: .selected)
    UIPageControl.appearance().currentPageIndicatorTintColor = BackseatTheme.accent

    // Grouped lists (Settings and friends) on asphalt with card rows.
    UITableView.appearance(whenContainedInInstancesOf: [UINavigationController.self]).backgroundColor = BackseatTheme.asphalt
    UICollectionView.appearance(whenContainedInInstancesOf: [UINavigationController.self]).backgroundColor = BackseatTheme.asphalt

    // The search field: a raised dark capsule with a yellow caret.
    let field = UITextField.appearance(whenContainedInInstancesOf: [UISearchBar.self])
    field.backgroundColor = BackseatTheme.raised
    field.textColor = BackseatTheme.text
    field.tintColor = BackseatTheme.accent
  }
}

extension View {
  /// A Backseat-styled SwiftUI list: asphalt behind card rows, yellow controls.
  func backseatList() -> some View {
    self
      .scrollContentBackground(.hidden)
      .background(Color(uiColor: BackseatTheme.asphalt))
      .tint(Color(uiColor: BackseatTheme.accent))
      .environment(\.colorScheme, .dark)
  }
}

/// The top of Settings > Backseat Driver: the GPS's face, its name, and a jab, so the page opens with
/// some personality instead of a list.
struct BackseatSettingsHero: View {
  private static let jabs = [
    "Your harshest critic. Adjust me, if you must.",
    "Settings. As if you'll read them.",
    "Change whatever you like. I'll still judge you.",
    "Tweak away. My opinion of you is fixed.",
  ]

  @State private var jab = jabs[0]

  var body: some View {
    HStack(spacing: 16) {
      BackseatFaceRepresentable()
        .frame(width: 56, height: 56)
        .accessibilityHidden(true)

      VStack(alignment: .leading, spacing: 4) {
        Text(verbatim: "Backseat Driver")
          .font(Font(BackseatTheme.number(28)))
          .foregroundStyle(Color(uiColor: BackseatTheme.text))
        Text(verbatim: jab)
          .font(.subheadline)
          .foregroundStyle(Color(uiColor: BackseatTheme.muted))
          .fixedSize(horizontal: false, vertical: true)
      }
    }
    .padding(.vertical, 8)
    .listRowBackground(Color(uiColor: BackseatTheme.card))
    .onAppear { jab = Self.jabs.randomElement() ?? jab }
  }
}

/// The driving screen's face at any size, for SwiftUI screens (Settings, onboarding, the walk question).
struct BackseatFaceRepresentable: UIViewRepresentable {
  /// Patience left, 0-10: 10 is calm, 0 is fed up.
  var mood = 4
  var size: CGFloat = 56

  func makeUIView(context: Context) -> UIView {
    let holder = UIView()
    let face = BackseatFaceView()
    face.translatesAutoresizingMaskIntoConstraints = false
    face.transform = CGAffineTransform(scaleX: size / 44.0, y: size / 44.0)
    face.setMood(mood)
    holder.addSubview(face)
    NSLayoutConstraint.activate([
      face.centerXAnchor.constraint(equalTo: holder.centerXAnchor),
      face.centerYAnchor.constraint(equalTo: holder.centerYAnchor),
    ])
    return holder
  }

  func updateUIView(_ view: UIView, context: Context) {
    (view.subviews.first as? BackseatFaceView)?.setMood(mood)
  }
}

/// A settings-row icon on a little rounded tile, like the system Settings app: road-sign yellow for
/// Backseat Driver's own rows, a raised grey for the stock ones.
struct BackseatRowIcon: View {
  let symbol: String
  var stock = false

  var body: some View {
    Image(systemName: symbol)
      .font(.system(size: 14, weight: .bold))
      .foregroundStyle(Color(uiColor: stock ? BackseatTheme.text : BackseatTheme.ink))
      .frame(width: 30, height: 30)
      .background(RoundedRectangle(cornerRadius: 8, style: .continuous)
        .fill(Color(uiColor: stock ? BackseatTheme.raised : BackseatTheme.accent)))
      .accessibilityHidden(true)
  }
}
