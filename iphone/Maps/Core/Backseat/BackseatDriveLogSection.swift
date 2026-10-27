import SwiftUI
import UIKit

/// Settings > Backseat Driver > Drive log: keep it on or off, share it, delete it.
/// Drop it into the Backseat Driver settings Form as `BackseatDriveLogSection(voiceState: ...)`.
struct BackseatDriveLogSection: View {
  /// Voice facts for the export header, e.g. ["kokoro": "ready", "crashGuard": "ok", "voice": "Echo"].
  var voiceState: () -> [String: String] = { [:] }

  @State private var enabled = BackseatLog.enabled
  @State private var shareURL: URL?
  @State private var confirmDelete = false
  @State private var exportFailed = false

  var body: some View {
    Section {
      Toggle("Keep a drive log", isOn: $enabled)
        .onChange(of: enabled) { _, on in BackseatLog.enabled = on }
      Button("Share drive log") {
        if let url = BackseatLog.exportLog(extra: voiceState()) {
          shareURL = url
        } else {
          exportFailed = true
        }
      }
      Button("Delete drive log", role: .destructive) { confirmDelete = true }
    } header: {
      Text("Drive log")
    } footer: {
      Text("A record of what the GPS said and why on recent drives, so problems can be tracked down. It stays on this iPhone and is only sent if you share it.")
    }
    .sheet(item: $shareURL) { url in
      ActivitySheet(items: [url])
    }
    .confirmationDialog("Delete the drive log?", isPresented: $confirmDelete, titleVisibility: .visible) {
      Button("Delete", role: .destructive) { BackseatLog.clear() }
    }
    .alert("Couldn't make the drive log file", isPresented: $exportFailed) {
      Button("OK", role: .cancel) {}
    }
  }
}

extension URL: @retroactive Identifiable {
  public var id: String { absoluteString }
}

/// The system share sheet: AirDrop, Messages, Mail, Save to Files.
private struct ActivitySheet: UIViewControllerRepresentable {
  let items: [Any]

  func makeUIViewController(context: Context) -> UIActivityViewController {
    UIActivityViewController(activityItems: items, applicationActivities: nil)
  }

  func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}
