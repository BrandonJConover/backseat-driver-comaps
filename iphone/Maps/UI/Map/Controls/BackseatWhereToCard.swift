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
        .onAppear(perform: reload)
        .onChange(of: scenePhase) { _ in reload() }
        .onReceive(NotificationCenter.default.publisher(for: BackseatListener.listeningChanged)) { _ in
            isListening = BackseatListener.shared.isListening
        }
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
    }
}
