//
//  Kokoro-tts-lib
//
import Foundation
import MLX
import MLXNN

// Utility class for loading voices
class VoiceLoader {
  private init() {}

  enum VoiceLoaderError: Error, LocalizedError {
    case voiceFileNotFound(String)
    case parseError(String)
    case loadError(String, Error)
    case valueConversionError(String)
    case shapeMismatch(String)

    var errorDescription: String? {
      switch self {
      case .voiceFileNotFound(let voiceId):
        return "Voice file '\(voiceId).json' not found in bundle"
      case .parseError(let voiceId):
        return "Failed to parse voice file '\(voiceId).json'"
      case .loadError(let voiceId, let error):
        return "Failed to load voice file '\(voiceId).json': \(error.localizedDescription)"
      case .valueConversionError(let detail):
        return "Could not convert voice data value: \(detail)"
      case .shapeMismatch(let detail):
        return "Voice array shape mismatch: \(detail)"
      }
    }
  }

  /// Load voice with throwing error handling (safer than fatalError)
  static func tryLoadVoice(_ voice: TTSVoice) throws -> MLXArray {
    // MLX's Metal allocator aborts the whole process in the iOS Simulator
    // (mlx::core::metal::Device::Device() -> abort). Fail as a normal error
    // instead so callers degrade gracefully; Kokoro is unavailable there anyway.
    #if targetEnvironment(simulator)
    throw VoiceLoaderError.parseError("MLX voice loading is unavailable in the iOS Simulator")
    #endif

    let file = voice.rawValue
    let ext = "json"

    // Try multiple locations for the voice file
    var filePath: String? = nil

    // 1. Direct bundle lookup (for files in root)
    if let path = Bundle.main.path(forResource: file, ofType: ext) {
      filePath = path
      AppLogger.voiceModel.debug("Found \(file).\(ext) in bundle root")
    }

    // 2. Check KokoroAssets subdirectory in bundle
    if filePath == nil, let bundleURL = Bundle.main.resourceURL {
      let assetsPath = bundleURL.appendingPathComponent("KokoroAssets/\(file).\(ext)").path
      if FileManager.default.fileExists(atPath: assetsPath) {
        filePath = assetsPath
        AppLogger.voiceModel.debug("Found \(file).\(ext) in KokoroAssets")
      }
    }

    // 3. Check KokoroAssets/models subdirectory
    if filePath == nil, let bundleURL = Bundle.main.resourceURL {
      let assetsModelsPath = bundleURL.appendingPathComponent("KokoroAssets/models/\(file).\(ext)").path
      if FileManager.default.fileExists(atPath: assetsModelsPath) {
        filePath = assetsModelsPath
        AppLogger.voiceModel.debug("Found \(file).\(ext) in KokoroAssets/models")
      }
    }

    // 4. Check KokoroTTS/Models subdirectory
    if filePath == nil, let bundleURL = Bundle.main.resourceURL {
      let ttsModelsPath = bundleURL.appendingPathComponent("KokoroTTS/Models/\(file).\(ext)").path
      if FileManager.default.fileExists(atPath: ttsModelsPath) {
        filePath = ttsModelsPath
        AppLogger.voiceModel.debug("Found \(file).\(ext) in KokoroTTS/Models")
      }
    }

    guard let finalPath = filePath else {
      AppLogger.voiceModel.error("Could not find voice file \(file).\(ext) in bundle")
      throw VoiceLoaderError.voiceFileNotFound(file)
    }

    do {
      guard let result = try read3DArrayFromJson(file: finalPath, shape: [510, 1, 256]) else {
        AppLogger.voiceModel.error("Failed to parse voice file \(file).\(ext)")
        throw VoiceLoaderError.parseError(file)
      }
      AppLogger.voiceModel.info("Loaded voice \(file) successfully")
      return result
    } catch let error as VoiceLoaderError {
      throw error
    } catch {
      AppLogger.voiceModel.error("Failed to load voice file \(file).\(ext): \(error.localizedDescription)")
      throw VoiceLoaderError.loadError(file, error)
    }
  }

  static func loadVoice(_ voice: TTSVoice) -> MLXArray {
    do {
      return try tryLoadVoice(voice)
    } catch {
      AppLogger.voiceModel.error("loadVoice failed for '\(voice.rawValue)': \(error.localizedDescription). Returning zeroed embedding.")
      // Return a zeroed embedding rather than crashing — results in silent audio
      // which is recoverable, unlike a fatalError crash.
      return MLXArray.zeros([510, 1, 256])
    }
  }

  private static func read3DArrayFromJson(file: String, shape: [Int]) throws -> MLXArray? {
    guard shape.count == 3 else { return nil }

    let data = try Data(contentsOf: URL(fileURLWithPath: file))
    let jsonObject = try JSONSerialization.jsonObject(with: data, options: [])

    var aa = Array(repeating: Float(0.0), count: shape[0] * shape[1] * shape[2])
    var aaIndex = 0

    if let nestedArray = jsonObject as? [[[Any]]] {
      guard nestedArray.count == shape[0] else { return nil }
      for a in 0 ..< nestedArray.count {
        guard nestedArray[a].count == shape[1] else { return nil }
        for b in 0 ..< nestedArray[a].count {
          guard nestedArray[a][b].count == shape[2] else { return nil }
          for c in 0 ..< nestedArray[a][b].count {
            if let n = nestedArray[a][b][c] as? Double {
              aa[aaIndex] = Float(n)
              aaIndex += 1
            } else {
              AppLogger.voiceModel.warning("Could not convert value at [\(a)][\(b)][\(c)] to Double, returning nil")
              return nil
            }
          }
        }
      }
    } else {
      return nil
    }

    guard aaIndex == shape[0] * shape[1] * shape[2] else {
      AppLogger.voiceModel.warning("Array size mismatch: got \(aaIndex), expected \(shape[0] * shape[1] * shape[2]). Returning nil.")
      return nil
    }

    return MLXArray(aa).reshaped(shape)
  }

  /// Load a voice by string ID directly from bundle (bypasses TTSVoice enum)
  /// Used for file-backed voices such as the bundled Kokoro fits (`*_k`)
  static func tryLoadVoiceById(_ voiceId: String) throws -> MLXArray {
    // MLX's Metal allocator aborts the whole process in the iOS Simulator
    // (mlx::core::metal::Device::Device() -> abort). Fail as a normal error
    // instead so callers degrade gracefully; Kokoro is unavailable there anyway.
    #if targetEnvironment(simulator)
    throw VoiceLoaderError.parseError("MLX voice loading is unavailable in the iOS Simulator")
    #endif

    let file = voiceId
    let ext = "json"

    var filePath: String? = nil

    if let path = Bundle.main.path(forResource: file, ofType: ext) {
      filePath = path
    }

    if filePath == nil, let bundleURL = Bundle.main.resourceURL {
      let searchPaths = [
        "KokoroAssets/\(file).\(ext)",
        "KokoroAssets/models/\(file).\(ext)",
        "KokoroTTS/Models/\(file).\(ext)"
      ]
      for subpath in searchPaths {
        let fullPath = bundleURL.appendingPathComponent(subpath).path
        if FileManager.default.fileExists(atPath: fullPath) {
          filePath = fullPath
          break
        }
      }
    }

    if let finalPath = filePath {
      do {
        guard let result = try read3DArrayFromJson(file: finalPath, shape: [510, 1, 256]) else {
          throw VoiceLoaderError.parseError(file)
        }
        AppLogger.voiceModel.info("Loaded voice \(file) by ID from JSON")
        return result
      } catch let error as VoiceLoaderError {
        throw error
      } catch {
        throw VoiceLoaderError.loadError(file, error)
      }
    }

    throw VoiceLoaderError.voiceFileNotFound(file)
  }

  /// Check if a voice file is available in the bundle
  static func isVoiceAvailable(_ voiceId: String) -> Bool {
    // 1. Direct bundle lookup
    if Bundle.main.path(forResource: voiceId, ofType: "json") != nil {
      return true
    }

    // 2. Bundle subdirectories
    if let bundleURL = Bundle.main.resourceURL {
      let paths = [
        bundleURL.appendingPathComponent("KokoroAssets/\(voiceId).json").path,
        bundleURL.appendingPathComponent("KokoroAssets/models/\(voiceId).json").path,
        bundleURL.appendingPathComponent("KokoroTTS/Models/\(voiceId).json").path
      ]
      for path in paths {
        if FileManager.default.fileExists(atPath: path) {
          return true
        }
      }
    }

    return false
  }

  /// Load a custom voice from a file path (for imported custom voices)
  static func loadCustomVoice(from filePath: URL) -> MLXArray? {
    AppLogger.voiceModel.debug("Loading custom voice from \(filePath.path)")

    guard FileManager.default.fileExists(atPath: filePath.path) else {
      AppLogger.voiceModel.warning("Custom voice file not found at \(filePath.path)")
      return nil
    }

    do {
      return try read3DArrayFromJson(file: filePath.path, shape: [510, 1, 256])
    } catch {
      AppLogger.voiceModel.error("Failed to load custom voice: \(error)")
      return nil
    }
  }
}
