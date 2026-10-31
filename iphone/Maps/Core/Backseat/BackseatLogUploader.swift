import Foundation
import Network
import Security
import UIKit

/// The GitHub token for log uploads. Only ever in the Keychain.
enum BackseatLogToken {
  private static let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
                                             kSecAttrService as String: "backseat.github-log-token"]

  static var value: String? {
    var result: AnyObject?
    var read = query
    read[kSecReturnData as String] = true
    guard SecItemCopyMatching(read as CFDictionary, &result) == errSecSuccess, let data = result as? Data else { return nil }
    return String(data: data, encoding: .utf8)
  }

  static func set(_ token: String) {
    SecItemDelete(query as CFDictionary)
    guard !token.isEmpty else { return }
    var add = query
    add[kSecValueData as String] = Data(token.utf8)
    add[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlock
    SecItemAdd(add as CFDictionary, nil)
  }
}

/// Sends each finished drive's part of the log to the driver's private logs repo, so a drive can be
/// looked at without the phone. Nothing is sent without a token. What's waiting survives restarts and
/// only leaves the queue on a 201.
final class BackseatLogUploader {
  static let shared = BackseatLogUploader()

  private static let repoURL = "https://api.github.com/repos/BrandonJConover/backseat-driver-logs/contents/"
  private static let enabledKey = "backseat.uploadEnabled"
  private static let sentUntilKey = "backseat.uploadedUntil"
  private static let backlog: TimeInterval = 3 * 24 * 3600
  private static let diagnosticEvents = ["crash", "cpu", "hang"]
  private static let checkEvery: TimeInterval = 60
  private static let minBackoff: TimeInterval = 60
  private static let maxBackoff: TimeInterval = 3600
  private static let maxQueued = 60

  private struct Pending: Codable {
    var repoPath: String
    var file: String
    var message: String
    /// GitHub refused the path because it exists: the next try uses another name.
    var attempt = 0
  }

  private struct Line {
    let text: Substring
    let time: Substring
    let event: Substring
  }

  private let work = DispatchQueue(label: "backseat.log-upload")
  private let monitor = NWPathMonitor()
  private var timer: Timer?
  private var sending = false
  private var backoff: TimeInterval = 0
  private var retryAt = Date.distantPast
  private var logOffset: UInt64 = 0

  private var folder: URL {
    let url = URL(fileURLWithPath: BackseatLog.directory).appendingPathComponent("backseat_upload_pending")
    try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    return url
  }
  private var queueFile: URL { folder.appendingPathComponent("queue.json") }

  static var hasToken: Bool { !(BackseatLogToken.value ?? "").isEmpty }

  /// On by default once there's a token.
  static var enabled: Bool {
    get { UserDefaults.standard.object(forKey: enabledKey) as? Bool ?? true }
    set { UserDefaults.standard.set(newValue, forKey: enabledKey) }
  }

  func install() {
    monitor.pathUpdateHandler = { [weak self] path in
      if path.status == .satisfied { self?.uploadPending() }
    }
    monitor.start(queue: work)
    timer = Timer.scheduledTimer(withTimeInterval: Self.checkEvery, repeats: true) { [weak self] _ in self?.checkForDriveEnd() }
    for name in [UIApplication.didBecomeActiveNotification, UIApplication.didEnterBackgroundNotification] {
      NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in self?.uploadPending() }
    }
    uploadPending()
  }

  /// Queues finished drives and sends what's waiting.
  func uploadPending() {
    work.async { [self] in
      guard Self.enabled, Self.hasToken else { return }
      queueFinishedDrives()
      sendNext()
    }
  }

  /// The button: everything not sent yet, plus the whole log of the last few days.
  func uploadNow() {
    work.async { [self] in
      guard Self.hasToken else { return }
      backoff = 0
      retryAt = .distantPast
      queueFinishedDrives()
      queueWholeLog()
      sendNext()
    }
  }

  /// Reads only what's new in the current log, so this is cheap during a drive.
  private func checkForDriveEnd() {
    work.async { [self] in
      guard let url = logFiles().last, let handle = try? FileHandle(forReadingFrom: url) else { return }
      defer { try? handle.close() }
      let size = (try? handle.seekToEnd()) ?? 0
      if size < logOffset { logOffset = 0 }
      try? handle.seek(toOffset: logOffset)
      let fresh = (try? handle.readToEnd()) ?? Data()
      logOffset = size
      if String(decoding: fresh, as: UTF8.self).contains("\"ev\":\"drive.end\"") { uploadPending() }
    }
  }

  /// Oldest first.
  private func logFiles() -> [URL] {
    let directory = URL(fileURLWithPath: BackseatLog.directory)
    let rolled = (1...4).reversed().map { directory.appendingPathComponent("backseat_drive_log.\($0).jsonl") }
    return (rolled + [directory.appendingPathComponent("backseat_drive_log.jsonl")])
      .filter { FileManager.default.fileExists(atPath: $0.path) }
  }

  /// {"t":"2026-10-27T12:42:04.123Z","dt":"12.3","ev":"say",...
  private func parse(_ text: Substring) -> Line? {
    guard text.hasPrefix("{\"t\":\""), let cue = text.range(of: "\"ev\":\"") else { return nil }
    let timeStart = text.index(text.startIndex, offsetBy: 6)
    guard let timeEnd = text[timeStart...].firstIndex(of: "\""),
          let eventEnd = text[cue.upperBound...].firstIndex(of: "\"") else { return nil }
    return Line(text: text, time: text[timeStart..<timeEnd], event: text[cue.upperBound..<eventEnd])
  }

  private func logLines(since: String) -> [Line] {
    var lines: [Line] = []
    for url in logFiles() {
      guard let text = try? String(contentsOf: url, encoding: .utf8) else { continue }
      for raw in text.split(separator: "\n") {
        if let line = parse(raw), line.time > since { lines.append(line) }
      }
    }
    return lines
  }

  private static func stamp(_ date: Date) -> String {
    let format = ISO8601DateFormatter()
    format.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
    return format.string(from: date)
  }

  /// A piece of the log for each drive that ended, or was cut off by a new one. Reports from between
  /// drives go on their own. A drive still going stays unsent, and comes back whole next time.
  private func queueFinishedDrives() {
    let since = UserDefaults.standard.string(forKey: Self.sentUntilKey) ?? Self.stamp(Date().addingTimeInterval(-Self.backlog))
    var sentUntil = since
    var drive: [Line] = []
    var between: [Line] = []
    for line in logLines(since: since) {
      if line.event == "drive.start" {
        if let last = drive.last {
          queueDrive(drive, suffix: "-cut-off")
          sentUntil = String(last.time)
        }
        drive = [line]
      } else if !drive.isEmpty {
        drive.append(line)
        if line.event == "drive.end" {
          queueDrive(drive, suffix: "")
          sentUntil = String(line.time)
          drive = []
        }
      } else {
        if Self.diagnosticEvents.contains(String(line.event)) { between.append(line) }
        sentUntil = String(line.time)
      }
    }
    if let first = between.first {
      enqueue(between.map(\.text).joined(separator: "\n") + "\n", name: "\(pathStamp(first))-diagnostics",
              message: "diagnostics \(first.time)")
    }
    UserDefaults.standard.set(sentUntil, forKey: Self.sentUntilKey)
  }

  private func queueDrive(_ lines: [Line], suffix: String) {
    guard let first = lines.first else { return }
    var to = "drive"
    if let cue = first.text.range(of: "\"to\":\""), let end = first.text[cue.upperBound...].firstIndex(of: "\"") {
      to = String(first.text[cue.upperBound..<end])
    }
    let safe = String(to.unicodeScalars.map { CharacterSet.alphanumerics.contains($0) ? Character($0) : "-" })
    enqueue(lines.map(\.text).joined(separator: "\n") + "\n", name: "\(pathStamp(first))-\(safe)\(suffix)",
            message: "drive \(first.time)")
  }

  /// "2026-10-31/014706", from the line's own UTC timestamp.
  private func pathStamp(_ line: Line) -> String {
    let parts = line.time.split(separator: "T")
    guard parts.count == 2 else { return "unknown/\(Int(Date().timeIntervalSince1970))" }
    return "\(parts[0])/" + parts[1].prefix(8).replacingOccurrences(of: ":", with: "")
  }

  private func queueWholeLog() {
    let lines = logLines(since: Self.stamp(Date().addingTimeInterval(-Self.backlog)))
    guard let first = lines.first else { return }
    enqueue(lines.map(\.text).joined(separator: "\n") + "\n",
            name: "\(pathStamp(first))-whole-log-\(Int(Date().timeIntervalSince1970))",
            message: "whole log since \(first.time)")
  }

  private func loadQueue() -> [Pending] {
    guard let data = try? Data(contentsOf: queueFile) else { return [] }
    return (try? JSONDecoder().decode([Pending].self, from: data)) ?? []
  }

  private func saveQueue(_ queue: [Pending]) {
    if let data = try? JSONEncoder().encode(queue) { try? data.write(to: queueFile, options: .atomic) }
  }

  private func enqueue(_ text: String, name: String, message: String) {
    var queue = loadQueue()
    let file = UUID().uuidString + ".jsonl"
    guard (try? Data(text.utf8).write(to: folder.appendingPathComponent(file))) != nil else { return }
    queue.append(Pending(repoPath: "drives/\(name).jsonl", file: file, message: message))
    // Oldest go first if the repo stays out of reach for weeks.
    while queue.count > Self.maxQueued {
      try? FileManager.default.removeItem(at: folder.appendingPathComponent(queue.removeFirst().file))
    }
    saveQueue(queue)
  }

  private func sendNext() {
    guard !sending, Date() >= retryAt, let token = BackseatLogToken.value, !token.isEmpty,
          let next = loadQueue().first else { return }
    guard let data = try? Data(contentsOf: folder.appendingPathComponent(next.file)) else {
      saveQueue(Array(loadQueue().dropFirst()))
      sendNext()
      return
    }
    let path = next.attempt == 0 ? next.repoPath : next.repoPath.replacingOccurrences(of: ".jsonl", with: "-\(next.attempt).jsonl")
    guard let url = URL(string: Self.repoURL + path) else { return }
    var request = URLRequest(url: url)
    request.httpMethod = "PUT"
    request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
    request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
    let body = ["message": next.message, "content": data.base64EncodedString(), "branch": "main"]
    request.httpBody = try? JSONSerialization.data(withJSONObject: body)
    sending = true
    URLSession.shared.dataTask(with: request) { [self] _, response, error in
      work.async { [self] in
        sending = false
        finished(next, path: path, status: (response as? HTTPURLResponse)?.statusCode ?? 0, failed: error != nil)
      }
    }.resume()
  }

  private func finished(_ item: Pending, path: String, status: Int, failed: Bool) {
    var queue = loadQueue()
    if status == 201 {
      queue.removeAll { $0.file == item.file }
      saveQueue(queue)
      try? FileManager.default.removeItem(at: folder.appendingPathComponent(item.file))
      BackseatLog.event("upload.ok", ["path": path])
      backoff = 0
      sendNext()
      return
    }
    BackseatLog.event("upload.fail", ["path": path, "status": String(status), "network": failed ? "yes" : "no"])
    // 422: that name is taken, which only a repeat of an earlier upload would do.
    if status == 422, let at = queue.firstIndex(where: { $0.file == item.file }), item.attempt < 3 {
      queue[at].attempt += 1
      saveQueue(queue)
      sendNext()
      return
    }
    backoff = min(Self.maxBackoff, max(Self.minBackoff, backoff * 2))
    retryAt = Date().addingTimeInterval(backoff)
    work.asyncAfter(deadline: .now() + backoff) { [self] in sendNext() }
  }
}
