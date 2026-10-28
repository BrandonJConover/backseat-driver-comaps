import CoreLocation
import SwiftUI

/// Backseat Driver's "Where to?" card over the map when there's no route: a big mic to say where,
/// and the places driven to most, one tap from a route. The search bar stays right below it.
struct BackseatWhereToCard: View {
    // MARK: Properties

    /// The color scheme of the environment
    @Environment(\.colorScheme) private var colorScheme


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


    /// The actual view
    var body: some View {
        HStack(spacing: 12) {
            Button {
                BackseatListener.shared.toggle()
            } label: {
                Image(systemName: isListening ? "waveform" : "mic.fill")
                    .font(.title2.weight(.semibold))
                    .foregroundStyle(.white)
                    .frame(width: micSize, height: micSize)
                    .background(Circle().fill(isListening ? Color.red : Color.accentColor))
                    .shadow(radius: 2)
            }
            .buttonStyle(.plain)
            .accessibilityLabel(Text(verbatim: isListening ? "Stop listening" : "Say where to"))

            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text(verbatim: isListening ? "Listening…" : "Where to?")
                        .font(.headline)

                    Spacer(minLength: 0)

                    // Draw a route from here with a finger
                    Button {
                        BackseatSketch.open()
                    } label: {
                        Label {
                            Text(verbatim: "Draw route")
                        } icon: {
                            Image(systemName: "scribble")
                        }
                        .font(.subheadline.weight(.medium))
                        .padding(.horizontal, 12)
                        .frame(minHeight: 36)
                        .background(Capsule().fill(Color(uiColor: .secondarySystemBackground)))
                        .contentShape(Capsule())
                    }
                    .buttonStyle(.plain)
                }

                // After a trip: one tap back the way you came.
                if let returnTrip {
                    Button {
                        BackseatReturnTrip.headBack()
                    } label: {
                        HStack(spacing: 6) {
                            Image(systemName: "arrow.uturn.backward")
                                .imageScale(.small)

                            Text(verbatim: returnTrip.name == "Where you started" ? "Head back" : "Head back to \(returnTrip.name)")
                                .lineLimit(1)
                        }
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(.white)
                        .padding(.horizontal, 14)
                        .frame(minHeight: 44)
                        .background(Capsule().fill(Color.accentColor))
                        .contentShape(Capsule())
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(Text(verbatim: "Head back to \(returnTrip.name)"))

                    // Came on a saved route: the same roads the other way.
                    if let saved = returnTrip.savedRoute {
                        Button {
                            BackseatReturnTrip.headBackSameWay()
                        } label: {
                            capsule(symbol: "arrow.triangle.swap", text: "Same way back (\(saved.name))")
                        }
                        .buttonStyle(.plain)
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
                                .buttonStyle(.plain)
                                .contextMenu {
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

                if places.isEmpty {
                    Text(verbatim: "No trips yet. I'm sure you'll get lost somewhere memorable.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
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
                                    chip(for: place)
                                }
                                .buttonStyle(.plain)
                            }
                        }
                    }
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(12)
        .background {
            RoundedRectangle(cornerRadius: 24, style: .continuous)
                .fill(Color(uiColor: .systemBackground).opacity(colorScheme == .dark ? 0.92 : 0.97))
                .shadow(radius: 3)
        }
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


    /// A saved route or the way back: an icon and a name on a quiet capsule
    private func capsule(symbol: String, text: String) -> some View {
        HStack(spacing: 6) {
            Image(systemName: symbol)
                .imageScale(.small)

            Text(verbatim: text)
                .lineLimit(1)
        }
        .font(.subheadline.weight(.medium))
        .padding(.horizontal, 14)
        .frame(minHeight: 44)
        .background(Capsule().fill(Color(uiColor: .secondarySystemBackground)))
        .contentShape(Capsule())
    }


    /// One place: its icon, name and usual drive time
    private func chip(for place: Place) -> some View {
        HStack(spacing: 6) {
            Image(systemName: place.label == "Home" ? "house.fill" : place.label == "Work" ? "briefcase.fill" : "mappin")
                .imageScale(.small)

            Text(verbatim: place.minutes > 0 ? "\(place.label) · \(place.minutes) min" : place.label)
                .lineLimit(1)
        }
        .font(.subheadline.weight(.medium))
        .padding(.horizontal, 14)
        .frame(minHeight: 44)
        .background(Capsule().fill(Color(uiColor: .secondarySystemBackground)))
        .contentShape(Capsule())
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
        returnTrip = BackseatReturnTrip.origin
        savedRoutes = BackseatSavedRoutes.all
    }
}
