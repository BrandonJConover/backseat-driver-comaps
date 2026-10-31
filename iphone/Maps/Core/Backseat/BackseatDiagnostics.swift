import Foundation
import MetricKit

/// Crash, CPU and hang reports from the system (delivered the next launch), into the drive log so
/// they're there with the drive. Keeps the newest raw payloads next to it, too.
final class BackseatDiagnostics: NSObject, MXMetricManagerSubscriber {
  static let shared = BackseatDiagnostics()
  private static let handledKey = "backseat.diagnosticsHandledUntil"
  private static let keptPayloads = 10
  private static let stackLimit = 64 * 1024

  func install() {
    MXMetricManager.shared.add(self)
    didReceive(MXMetricManager.shared.pastDiagnosticPayloads)
  }

  func didReceive(_ payloads: [MXDiagnosticPayload]) {
    var handledUntil = UserDefaults.standard.double(forKey: Self.handledKey)
    for payload in payloads where payload.timeStampEnd.timeIntervalSince1970 > handledUntil {
      handledUntil = max(handledUntil, payload.timeStampEnd.timeIntervalSince1970)
      save(payload)
      for crash in payload.crashDiagnostics ?? [] {
        log("crash", crash, ["exception_type": crash.exceptionType?.stringValue ?? "",
                             "exception_code": crash.exceptionCode?.stringValue ?? "",
                             "signal": crash.signal?.stringValue ?? "",
                             "reason": crash.terminationReason ?? "",
                             "vm_region": crash.virtualMemoryRegionInfo ?? ""], stack: crash.callStackTree)
      }
      for cpu in payload.cpuExceptionDiagnostics ?? [] {
        log("cpu", cpu, ["cpu_s": String(cpu.totalCPUTime.converted(to: .seconds).value),
                         "sampled_s": String(cpu.totalSampledTime.converted(to: .seconds).value)], stack: cpu.callStackTree)
      }
      for hang in payload.hangDiagnostics ?? [] {
        log("hang", hang, ["hang_s": String(hang.hangDuration.converted(to: .seconds).value)], stack: hang.callStackTree)
      }
    }
    UserDefaults.standard.set(handledUntil, forKey: Self.handledKey)
  }

  private func log(_ event: String, _ diagnostic: MXDiagnostic, _ fields: [String: String], stack: MXCallStackTree) {
    var all = fields
    all["app"] = diagnostic.applicationVersion
    all["build"] = diagnostic.metaData.applicationBuildVersion
    all["stack"] = Self.trimmed(stack.jsonRepresentation())
    BackseatLog.event(event, all)
  }

  /// The whole tree, or when that's too much the thread at fault, cut to the limit.
  private static func trimmed(_ tree: Data) -> String {
    var data = tree
    if data.count > stackLimit, var json = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
       let threads = json["callStacks"] as? [[String: Any]] {
      json["callStacks"] = threads.filter { $0["threadAttributed"] as? Bool == true }
      data = (try? JSONSerialization.data(withJSONObject: json)) ?? data
    }
    return String(decoding: data.prefix(stackLimit), as: UTF8.self)
  }

  private func save(_ payload: MXDiagnosticPayload) {
    let directory = URL(fileURLWithPath: BackseatLog.directory)
    let stamp = Int(payload.timeStampEnd.timeIntervalSince1970)
    try? payload.jsonRepresentation().write(to: directory.appendingPathComponent("backseat_diag_\(stamp).json"))
    let files = (try? FileManager.default.contentsOfDirectory(atPath: directory.path)) ?? []
    let saved = files.filter { $0.hasPrefix("backseat_diag_") && $0.hasSuffix(".json") }.sorted()
    for old in saved.dropLast(Self.keptPayloads) {
      try? FileManager.default.removeItem(at: directory.appendingPathComponent(old))
    }
  }
}
