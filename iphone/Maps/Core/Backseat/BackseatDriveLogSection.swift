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
  @State private var uploads = BackseatLogUploader.enabled
  @State private var hasToken = BackseatLogUploader.hasToken
  @State private var token = ""

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
      Toggle("Upload drive logs", isOn: $uploads)
        .disabled(!hasToken)
        .onChange(of: uploads) { _, on in
          BackseatLogUploader.enabled = on
          if on { BackseatLogUploader.shared.uploadPending() }
        }
      SecureField(hasToken ? "GitHub log token (saved)" : "GitHub log token", text: $token)
        .textInputAutocapitalization(.never)
        .autocorrectionDisabled()
        .onSubmit {
          guard !token.isEmpty else { return }
          BackseatLogToken.set(token)
          token = ""
          hasToken = true
          BackseatLogUploader.shared.uploadPending()
        }
      if hasToken {
        Button("Upload now") { BackseatLogUploader.shared.uploadNow() }
        Button("Remove log token", role: .destructive) {
          BackseatLogToken.set("")
          hasToken = false
        }
      }
      Button("Delete drive log", role: .destructive) { confirmDelete = true }
    } header: {
      Text("Drive log")
    } footer: {
      Text("A record of what the GPS said and why on recent drives, so problems can be tracked down. It stays on this iPhone unless you share it or add a log token, which uploads each drive, including its GPS track, to your private GitHub repository.")
    }
    // The engine may have changed it since this view was made (or it was made before the engine).
    .onAppear { enabled = BackseatLog.enabled }
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
