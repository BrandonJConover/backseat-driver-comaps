import SwiftUI
import UIKit

/// The report card when a trip ends: the grade stamped big, the engine's quip, how the two halves
/// scored, what went wrong, and one big "Head back". Reads +[MWMBackseatRewriter lastTripReport].
@objc final class BackseatArrivalCard: NSObject {
  /// How long to wait after the trip ends for its report to show up.
  private static let reportWait: TimeInterval = 6

  private static var tripStartedAt: Date?

  static func install() {
    NotificationCenter.default.addObserver(forName: BackseatTalkButton.navigationVisibilityChanged, object: nil,
                                           queue: .main) { note in
      if (note.object as? Bool) ?? false {
        tripStartedAt = Date()
      } else if let started = tripStartedAt {
        tripStartedAt = nil
        waitForReport(newerThan: started, until: Date().addingTimeInterval(reportWait))
      }
    }
  }

  /// A cancelled trip has no report, so nothing shows.
  private static func waitForReport(newerThan started: Date, until deadline: Date) {
    if let report = TripReport(MWMBackseatRewriter.lastTripReport), report.at >= started {
      show(report)
      return
    }
    guard Date() < deadline else { return }
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { waitForReport(newerThan: started, until: deadline) }
  }

  static func show(_ report: TripReport) {
    guard let controller = MapViewController.shared(), controller.presentedViewController == nil else { return }
    var host: UIHostingController<BackseatArrivalCardView>?
    let view = BackseatArrivalCardView(report: report, headBack: BackseatReturnTrip.origin) {
      host?.dismiss(animated: true)
    }
    host = UIHostingController(rootView: view)
    guard let host else { return }
    host.view.backgroundColor = BackseatTheme.asphalt
    host.overrideUserInterfaceStyle = .dark
    if let sheet = host.sheetPresentationController {
      sheet.detents = [.large()]
      sheet.prefersGrabberVisible = true
      sheet.preferredCornerRadius = 28
    }
    controller.present(host, animated: true)
  }

  /// The trip report, from the dictionary the rewriter hands over.
  struct TripReport {
    let at: Date
    let letter: String
    let quip: String
    let destination: String
    let instructionsScore: Int
    let drivingScore: Int
    let onTime: Int
    let late: Int
    let missed: Int
    let wrongWay: Int
    let reroutes: Int
    let speedingSeconds: Int
    let maxOverLimit: Int
    let hardBrakes: Int
    let hardAccels: Int
    let expectedMin: Int
    let actualMin: Int
    let previousLetter: String?
    let grudge: String?

    init?(_ dict: [String: Any]?) {
      guard let dict, let letter = dict["letter"] as? String, !letter.isEmpty else { return nil }
      func int(_ key: String) -> Int { (dict[key] as? NSNumber)?.intValue ?? 0 }
      func text(_ key: String) -> String? {
        guard let value = dict[key] as? String, !value.isEmpty else { return nil }
        return value
      }
      at = Date(timeIntervalSince1970: (dict["at"] as? NSNumber)?.doubleValue ?? 0)
      self.letter = letter
      quip = text("quip") ?? "You made it. Somehow."
      destination = text("destination") ?? "your destination"
      instructionsScore = int("instructionsScore")
      drivingScore = int("drivingScore")
      onTime = int("onTime")
      late = int("late")
      missed = int("missed")
      wrongWay = int("wrongWay")
      reroutes = int("reroutes")
      speedingSeconds = int("speedingSeconds")
      maxOverLimit = int("maxOverLimit")
      hardBrakes = int("hardBrakes")
      hardAccels = int("hardAccels")
      expectedMin = int("expectedMin")
      actualMin = int("actualMin")
      previousLetter = text("previousLetter")
      grudge = text("grudge")
    }

    /// The charge sheet, worst first. Empty for a clean trip.
    var offenses: [String] {
      var list: [String] = []
      if missed > 0 { list.append(missed == 1 ? "Missed a turn" : "Missed \(missed) turns") }
      if wrongWay > 0 { list.append(wrongWay == 1 ? "Turned the wrong way once" : "Turned the wrong way \(wrongWay) times") }
      if late > 0 { list.append(late == 1 ? "One turn, barely" : "\(late) turns, barely") }
      if reroutes > 0 { list.append(reroutes == 1 ? "Made me reroute once" : "Made me reroute \(reroutes) times") }
      if speedingSeconds >= 60 {
        list.append("Speeding for \(speedingSeconds / 60) min, up to \(maxOverLimit) over")
      } else if speedingSeconds > 0 {
        list.append("Speeding for \(speedingSeconds) s, up to \(maxOverLimit) over")
      }
      if hardBrakes > 0 { list.append(hardBrakes == 1 ? "1 brake check" : "\(hardBrakes) brake checks") }
      if hardAccels > 0 { list.append(hardAccels == 1 ? "1 launch off the line" : "\(hardAccels) launches off the line") }
      return list
    }

    /// "11 min (I said 9) · down from a B last time"
    var subline: String {
      var parts: [String] = []
      if actualMin > 0 {
        parts.append(expectedMin > 0 && expectedMin != actualMin ? "\(actualMin) min (I said \(expectedMin))" : "\(actualMin) min")
      }
      if let previousLetter, previousLetter != letter {
        let better = Self.rank(letter) > Self.rank(previousLetter)
        parts.append("\(better ? "up" : "down") from \(Self.article(previousLetter)) \(previousLetter) last time")
      }
      return parts.joined(separator: " · ")
    }

    var isGood: Bool { Self.rank(letter) >= Self.rank("B-") }

    /// A+ is highest, F lowest.
    private static func rank(_ letter: String) -> Int {
      let base = ["F": 0, "D": 3, "C": 6, "B": 9, "A": 12][String(letter.prefix(1))] ?? 0
      return base + (letter.hasSuffix("+") ? 1 : letter.hasSuffix("-") ? -1 : 0)
    }

    private static func article(_ letter: String) -> String {
      letter.hasPrefix("A") || letter.hasPrefix("F") ? "an" : "a"
    }

    var shareText: String {
      var text = "Backseat Driver gave me \(Self.article(letter)) \(letter) driving to \(destination). \"\(quip)\""
      if let worst = offenses.first { text += " Exhibit A: \(worst.lowercased())." }
      return text
    }
  }
}

struct BackseatArrivalCardView: View {
  let report: BackseatArrivalCard.TripReport
  let headBack: BackseatReturnTrip.Origin?
  let dismiss: () -> Void

  @State private var sharing = false
  /// The stamp slams down and the bars fill once the card is up.
  @State private var landed = false

  private enum Palette {
    static let card = Color(uiColor: BackseatTheme.card)
    static let line = Color(uiColor: BackseatTheme.line)
    static let text = Color(uiColor: BackseatTheme.text)
    static let muted = Color(uiColor: BackseatTheme.muted)
    static let accent = Color(uiColor: BackseatTheme.accent)
    static let hot = Color(uiColor: BackseatTheme.hot)
    static let calm = Color(uiColor: BackseatTheme.calm)
    static let ink = Color(uiColor: BackseatTheme.ink)
  }

  var body: some View {
    ScrollView {
      VStack(alignment: .leading, spacing: 18) {
        header
        scores
        if !report.offenses.isEmpty { chargeSheet }
        if let grudge = report.grudge {
          Text(verbatim: grudge)
            .font(.subheadline)
            .foregroundStyle(Palette.text.opacity(0.85))
            .padding(14)
            .frame(maxWidth: .infinity, alignment: .leading)
            .overlay(RoundedRectangle(cornerRadius: 16, style: .continuous)
              .strokeBorder(Palette.line, style: StrokeStyle(lineWidth: 1, dash: [5, 4])))
        }
        buttons
      }
      .padding(.horizontal, 18)
      .padding(.top, 28)
      .padding(.bottom, 24)
    }
    .background(Color(uiColor: BackseatTheme.asphalt).ignoresSafeArea())
    .onAppear {
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) {
        withAnimation(.spring(response: 0.38, dampingFraction: 0.55)) { landed = true }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.12) { BackseatHaptics.stamp(good: report.isGood) }
      }
    }
    .sheet(isPresented: $sharing) { ShareSheet(text: report.shareText) }
  }

  private var stampColor: Color { report.isGood ? Palette.calm : Palette.hot }

  private var header: some View {
    HStack(alignment: .center, spacing: 16) {
      Text(verbatim: report.letter)
        .font(Font(BackseatTheme.number(60)))
        .foregroundStyle(stampColor)
        .frame(width: 104, height: 104)
        .overlay(Circle().strokeBorder(stampColor, lineWidth: 4))
        .rotationEffect(.degrees(landed ? -8 : -24))
        .scaleEffect(landed ? 1 : 2.4)
        .opacity(landed ? 1 : 0)
        .accessibilityLabel(Text(verbatim: "Grade \(report.letter)"))

      VStack(alignment: .leading, spacing: 4) {
        Text(verbatim: "Arrived at \(report.destination)".uppercased())
          .font(.caption.weight(.bold))
          .tracking(0.8)
          .foregroundStyle(Palette.muted)
          .lineLimit(1)
        Text(verbatim: report.quip)
          .font(Font(BackseatTheme.number(28)))
          .foregroundStyle(Palette.text)
          .fixedSize(horizontal: false, vertical: true)
        if !report.subline.isEmpty {
          Text(verbatim: report.subline)
            .font(.subheadline)
            .foregroundStyle(Palette.muted)
        }
      }
    }
  }

  private var scores: some View {
    VStack(spacing: 12) {
      bar("Following directions", report.instructionsScore, color: report.instructionsScore >= 70 ? Palette.accent : Palette.hot)
      bar("Actual driving", report.drivingScore, color: report.drivingScore >= 70 ? Palette.accent : Palette.hot)
    }
    .padding(16)
    .background(RoundedRectangle(cornerRadius: 20, style: .continuous).fill(Palette.card))
  }

  private func bar(_ title: String, _ score: Int, color: Color) -> some View {
    VStack(spacing: 6) {
      HStack {
        Text(verbatim: title)
        Spacer()
        Text(verbatim: "\(score)").monospacedDigit()
      }
      .font(.subheadline.weight(.semibold))
      .foregroundStyle(Palette.text)
      GeometryReader { geo in
        ZStack(alignment: .leading) {
          Capsule().fill(Palette.line)
          Capsule().fill(color)
            .frame(width: landed ? geo.size.width * CGFloat(min(max(score, 0), 100)) / 100 : 0)
            .animation(.easeOut(duration: 0.9).delay(0.5), value: landed)
        }
      }
      .frame(height: 8)
    }
    .accessibilityElement(children: .combine)
  }

  private var chargeSheet: some View {
    VStack(alignment: .leading, spacing: 8) {
      Text(verbatim: "THE CHARGES")
        .font(.caption.weight(.bold))
        .tracking(0.8)
        .foregroundStyle(Palette.muted)
      ForEach(report.offenses, id: \.self) { offense in
        HStack(alignment: .firstTextBaseline, spacing: 10) {
          Circle().fill(Palette.hot).frame(width: 7, height: 7)
          Text(verbatim: offense)
            .font(.body)
            .foregroundStyle(Palette.text)
        }
      }
    }
    .padding(16)
    .frame(maxWidth: .infinity, alignment: .leading)
    .background(RoundedRectangle(cornerRadius: 20, style: .continuous).fill(Palette.card))
  }

  private var buttons: some View {
    VStack(spacing: 10) {
      if let headBack {
        Button {
          dismiss()
          BackseatReturnTrip.headBack()
        } label: {
          HStack(spacing: 12) {
            Image(systemName: "arrow.uturn.backward").font(.body.weight(.heavy))
            Text(verbatim: "Head back").font(.title3.weight(.heavy))
            Spacer(minLength: 0)
            Text(verbatim: headBack.name == "Where you started" ? "where you started" : "to \(headBack.name)")
              .font(.subheadline.weight(.bold))
              .lineLimit(1)
          }
          .foregroundStyle(Palette.ink)
          .padding(.horizontal, 20)
          .frame(height: 64)
          .background(RoundedRectangle(cornerRadius: 20, style: .continuous).fill(Palette.accent))
          .contentShape(Rectangle())
        }
        .buttonStyle(BackseatPressStyle())
      }
      HStack(spacing: 10) {
        secondary(report.isGood ? "Brag about it" : "Share my shame") { sharing = true }
        secondary("Done", action: dismiss)
      }
    }
    .padding(.top, 4)
  }

  private func secondary(_ title: String, action: @escaping () -> Void) -> some View {
    Button(action: action) {
      Text(verbatim: title)
        .font(.subheadline.weight(.bold))
        .foregroundStyle(Palette.text)
        .frame(maxWidth: .infinity, minHeight: 50)
        .background(RoundedRectangle(cornerRadius: 16, style: .continuous).fill(Palette.card))
        .overlay(RoundedRectangle(cornerRadius: 16, style: .continuous).strokeBorder(Palette.line, lineWidth: 1))
        .contentShape(Rectangle())
    }
    .buttonStyle(BackseatPressStyle())
  }
}

/// The system share sheet with the grade as text.
private struct ShareSheet: UIViewControllerRepresentable {
  let text: String

  func makeUIViewController(context: Context) -> UIActivityViewController {
    UIActivityViewController(activityItems: [text], applicationActivities: nil)
  }

  func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}
