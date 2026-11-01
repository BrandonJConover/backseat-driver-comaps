import CoreLocation
import SwiftUI

/// Backseat Driver's "Where to?" card over the map when there's no route: a big mic to say where,
/// and the places driven to most, one tap from a route. The search bar stays right below it.
struct BackseatWhereToCard: View {
    // MARK: Properties

    /// The scene phase of the environment
    @Environment(\.scenePhase) private var scenePhase


    /// A place driven to before
    struct Place: Identifiable {
        let id: Int
        let label: String
        let latitude: Double
        let longitude: Double
        let minutes: Int
    }


    /// The places driven to most, most first
    @State private var places: [Place] = []


    /// Where the last trip started, to head back to
    @State private var returnTrip: BackseatReturnTrip.Origin?


    /// Routes drawn and saved in Draw mode
    @State private var savedRoutes: [BackseatSavedRoutes.Route] = []


    /// The saved route being renamed, and its new name
    @State private var renaming: BackseatSavedRoutes.Route?
    @State private var newName: String = ""


    /// If the GPS is listening
    @State private var isListening: Bool = false


    /// The size of the mic button
    private let micSize: CGFloat = 60


    /// Under the title: a different jab each time the card comes back
    @State private var tagline: String = Self.taglines[0]


    private static let taglines = [
        "I'll decide how badly you get there.",
        "Somewhere close. I don't have all day.",
        "Pick a place. I'll pick apart how you get there.",
        "Go on. Surprise me.",
    ]


    /// The night-drive palette, as SwiftUI colors
    private enum Palette {
        static let card = Color(uiColor: BackseatTheme.card)
        static let raised = Color(uiColor: BackseatTheme.raised)
        static let line = Color(uiColor: BackseatTheme.line)
        static let text = Color(uiColor: BackseatTheme.text)
        static let muted = Color(uiColor: BackseatTheme.muted)
        static let accent = Color(uiColor: BackseatTheme.accent)
        static let hot = Color(uiColor: BackseatTheme.hot)
        static let ink = Color(uiColor: BackseatTheme.ink)
    }


    /// The actual view
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .center, spacing: 12) {
                VStack(alignment: .leading, spacing: 2) {
                    Text(verbatim: isListening ? "Listening…" : "Where to?")
                        .font(Font(BackseatTheme.number(30)))
                        .foregroundStyle(Palette.text)

                    Text(verbatim: tagline)
                        .font(.subheadline)
                        .foregroundStyle(Palette.muted)
                        .lineLimit(1)
                        .minimumScaleFactor(0.8)
                }

                Spacer(minLength: 0)

                // Draw a route from here with a finger
                Button {
                    BackseatSketch.open()
                } label: {
                    // Labeled: an icon alone didn't say what it does.
                    VStack(spacing: 2) {
                        Image(systemName: "hand.tap")
                            .font(.body.weight(.semibold))
                        Text(verbatim: "Plan")
                            .font(.caption2.weight(.bold))
                    }
                    .foregroundStyle(Palette.text)
                    .frame(width: 52, height: 48)
                    .background(RoundedRectangle(cornerRadius: 14, style: .continuous).fill(Palette.raised))
                    .contentShape(Rectangle())
                }
                .buttonStyle(BackseatPressStyle())
                .accessibilityLabel(Text(verbatim: "Plan route"))
            }

            // The big yellow mic: tap and say where
            Button {
                BackseatListener.shared.toggle()
            } label: {
                HStack(spacing: 12) {
                    Image(systemName: isListening ? "waveform" : "mic.fill")
                        .font(.body.weight(.bold))
                        .foregroundStyle(isListening ? Palette.hot : Palette.accent)
                        .frame(width: 36, height: 36)
                        .background(Circle().fill(Palette.ink))

                    Text(verbatim: isListening ? "Listening. Go ahead." : "Tap and tell me")
                        .font(.headline)

                    Spacer(minLength: 0)
                }
                .foregroundStyle(Palette.ink)
                .padding(.horizontal, 12)
                .frame(height: micSize)
                .background(RoundedRectangle(cornerRadius: 18, style: .continuous).fill(isListening ? Palette.hot : Palette.accent))
                .contentShape(Rectangle())
            }
            .buttonStyle(BackseatPressStyle())
            .accessibilityLabel(Text(verbatim: isListening ? "Stop listening" : "Say where to"))

            // After a trip: one tap back the way you came.
            if let returnTrip {
                Button {
                    BackseatReturnTrip.headBack()
                } label: {
                    HStack(spacing: 12) {
                        Image(systemName: "arrow.uturn.backward")
                            .font(.body.weight(.bold))
                            .foregroundStyle(Palette.accent)

                        VStack(alignment: .leading, spacing: 1) {
                            Text(verbatim: returnTrip.name == "Where you started" ? "Head back" : "Head back to \(returnTrip.name)")
                                .font(.subheadline.weight(.bold))
                                .foregroundStyle(Palette.text)
                                .lineLimit(1)

                            Text(verbatim: "One tap. Even you can manage that.")
                                .font(.caption)
                                .foregroundStyle(Palette.muted)
                                .lineLimit(1)
                        }

                        Spacer(minLength: 0)

                        Text(verbatim: "Go")
                            .font(.subheadline.weight(.heavy))
                            .foregroundStyle(Palette.accent)
                    }
                    .padding(.horizontal, 14)
                    .frame(minHeight: 52)
                    .background(RoundedRectangle(cornerRadius: 16, style: .continuous).strokeBorder(Palette.accent, lineWidth: 1.5))
                    .contentShape(Rectangle())
                }
                .buttonStyle(BackseatPressStyle())
                .accessibilityLabel(Text(verbatim: "Head back to \(returnTrip.name)"))

                // Came on a saved route: the same roads the other way.
                if let saved = returnTrip.savedRoute {
                    Button {
                        BackseatReturnTrip.headBackSameWay()
                    } label: {
                        capsule(symbol: "arrow.triangle.swap", text: "Same way back (\(saved.name))")
                    }
                    .buttonStyle(BackseatPressStyle())
                }
            }

            // Drawn routes saved for next time. Long press to rename or delete.
            if !savedRoutes.isEmpty {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 8) {
                        ForEach(savedRoutes) { route in
                            Button {
                                BackseatSavedRoutes.start(route)
                            } label: {
                                capsule(symbol: "scribble", text: route.name)
                            }
                            .buttonStyle(BackseatPressStyle())
                            .contextMenu {
                                    Button { BackseatSavedRoutes.drive(route, backwards: false) } label: {
                                        Label(title: { Text(verbatim: "Drive it forwards") }, icon: { Image(systemName: "arrow.right") })
                                    }

                                    Button { BackseatSavedRoutes.drive(route, backwards: true) } label: {
                                        Label(title: { Text(verbatim: "Drive it backwards") }, icon: { Image(systemName: "arrow.left") })
                                    }

                                Button {
                                    newName = route.name
                                    renaming = route
                                } label: {
                                    Label(title: { Text(verbatim: "Rename") }, icon: { Image(systemName: "pencil") })
                                }

                                Button(role: .destructive) {
                                    BackseatSavedRoutes.delete(route)
                                } label: {
                                    Label(title: { Text(verbatim: "Delete") }, icon: { Image(systemName: "trash") })
                                }
                            }
                        }
                    }
                }
            }

            // Road-trip basics, one tap from a list of what's nearby.
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach(Self.quickSearches, id: \.key) { quick in
                        Button {
                            Self.search(category: quick.key)
                        } label: {
                            capsule(symbol: quick.symbol, text: L(quick.key))
                        }
                        .buttonStyle(BackseatPressStyle())
                    }
                }
            }

            if places.isEmpty {
                Text(verbatim: "No trips yet. I'm sure you'll get lost somewhere memorable.")
                    .font(.footnote)
                    .foregroundStyle(Palette.muted)
                    .fixedSize(horizontal: false, vertical: true)
            } else {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 8) {
                        ForEach(places) { place in
                            Button {
                                MWMBackseatRewriter.route(
                                    to: CLLocationCoordinate2D(latitude: place.latitude, longitude: place.longitude),
                                    title: place.label)
                            } label: {
                                tile(for: place)
                            }
                            .buttonStyle(BackseatPressStyle())
                        }
                    }
                }
            }
        }
        .padding(16)
        .background {
            RoundedRectangle(cornerRadius: 26, style: .continuous)
                .fill(Palette.card)
                .shadow(color: .black.opacity(0.45), radius: 14, y: 8)
        }
        .environment(\.colorScheme, .dark)
        .alert(Text(verbatim: "Rename route"), isPresented: Binding(get: { renaming != nil }, set: { if !$0 { renaming = nil } })) {
            TextField(text: $newName) {
                Text(verbatim: "Name")
            }

            Button(role: .cancel) {
                renaming = nil
            } label: {
                Text(verbatim: "Cancel")
            }

            Button {
                if let route = renaming, !newName.trimmingCharacters(in: .whitespaces).isEmpty {
                    BackseatSavedRoutes.rename(route, to: newName.trimmingCharacters(in: .whitespaces))
                }
                renaming = nil
            } label: {
                Text(verbatim: "Save")
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: BackseatSavedRoutes.changed)) { _ in reload() }
        .onAppear(perform: reload)
        .onChange(of: scenePhase) { _ in reload() }
        .onReceive(NotificationCenter.default.publisher(for: BackseatListener.listeningChanged)) { _ in
            isListening = BackseatListener.shared.isListening
        }
    }


    /// Nearby searches offered on the card: the stock category keys, so the names are translated.
    private static let quickSearches: [(key: String, symbol: String)] = [
        ("category_fuel", "fuelpump.fill"),
        ("category_eat", "fork.knife"),
        ("category_toilet", "toilet.fill"),
        ("category_parking", "parkingsign"),
    ]


    /// The same search as tapping that category in the search screen.
    private static func search(category key: String) {
        let preferred = AppInfo.shared().languageId
        let language = MWMSearchFrameworkHelper.isLanguageSupported(preferred) ? preferred : "en"
        let query = SearchQuery(L(key, languageCode: language), locale: language, source: .category)
        MWMMapViewControlsManager.manager()?.search(query)
    }


    /// A saved route or the way back: an icon and a name on a dark capsule
    private func capsule(symbol: String, text: String) -> some View {
        HStack(spacing: 6) {
            Image(systemName: symbol)
                .imageScale(.small)
                .foregroundStyle(Palette.accent)

            Text(verbatim: text)
                .lineLimit(1)
                .foregroundStyle(Palette.text)
        }
        .font(.subheadline.weight(.medium))
        .padding(.horizontal, 14)
        .frame(minHeight: 44)
        .background(Capsule().fill(Palette.raised))
        .contentShape(Capsule())
    }


    /// One place: a yellow icon over its name and usual drive time
    private func tile(for place: Place) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Image(systemName: place.label == "Home" ? "house.fill" : place.label == "Work" ? "briefcase.fill" : "mappin")
                .font(.body.weight(.semibold))
                .foregroundStyle(Palette.accent)

            Text(verbatim: place.label)
                .font(.subheadline.weight(.bold))
                .foregroundStyle(Palette.text)
                .lineLimit(1)

            if place.minutes > 0 {
                Text(verbatim: "\(place.minutes) min")
                    .font(.caption)
                    .foregroundStyle(Palette.muted)
            }
        }
        .padding(12)
        .frame(minWidth: 96, minHeight: 76, alignment: .topLeading)
        .background(RoundedRectangle(cornerRadius: 16, style: .continuous).fill(Color(uiColor: BackseatTheme.asphalt)))
        .overlay(RoundedRectangle(cornerRadius: 16, style: .continuous).strokeBorder(Palette.line, lineWidth: 1))
        .contentShape(Rectangle())
    }


    /// Reads the places again, e.g. after a trip
    private func reload() {
        places = MWMBackseatRewriter.pastRoutes(8).enumerated().compactMap { index, route in
            guard let label = route["label"] as? String, !label.isEmpty,
                  let latitude = (route["lat"] as? NSNumber)?.doubleValue,
                  let longitude = (route["lon"] as? NSNumber)?.doubleValue else { return nil }
            return Place(id: index, label: label, latitude: latitude, longitude: longitude,
                         minutes: (route["minutes"] as? NSNumber)?.intValue ?? 0)
        }
        isListening = BackseatListener.shared.isListening
        tagline = Self.taglines.randomElement() ?? tagline
        returnTrip = BackseatReturnTrip.origin
        savedRoutes = BackseatSavedRoutes.all
    }
}
