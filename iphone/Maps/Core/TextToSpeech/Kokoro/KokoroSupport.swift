// Backseat Driver: the few app-level helpers the Kokoro model code (ported from EpubToM4B) expects.

import Foundation
import MLX
import os

enum AppLogger {
  static let kokoro = os.Logger(subsystem: "com.brandonjconover.backseatdriver", category: "Kokoro")
  static let voiceModel = os.Logger(subsystem: "com.brandonjconover.backseatdriver", category: "KokoroVoice")
}

/// EpubToM4B records stage timings to a file; Backseat Driver doesn't need them.
enum PerfTimings {
  static func record(_ name: String, seconds: TimeInterval, _ detail: String = "") {}
}

/// Runs MLX work with mlx-swift's error collection on, so a C++ MLX failure becomes a Swift error
/// (and the line goes to the system voice) instead of terminating the app.
enum KokoroMLXErrors {
  struct Failure: LocalizedError {
    let message: String
    var errorDescription: String? { "Kokoro (MLX) failed: \(message)" }
  }

  static func capture<T>(_ body: () throws -> T) throws -> T {
    do {
      return try MLX.withError { try body() }
    } catch {
      throw Failure(message: String(describing: error))
    }
  }
}
