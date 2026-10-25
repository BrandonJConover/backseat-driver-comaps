//
//  Kokoro-tts-lib
//
import Foundation
import MLX
import MLXNN
import OSLog

// NOTE: Do NOT use folder name "Resources" in iOS projects - it causes code signing issues.
// Use "KokoroAssets", "Models", or other names instead.

// Utility class for loading and preprocessing the weights for the model
class WeightLoader {
  private init() {}

  private static let activeKokoroModelIdDefaultsKey = "activeKokoroModelId"
  private static let defaultModelFileName = "kokoro-v1_0.safetensors"
  // q8/q4 entries were removed along with their ModelDownloadManager listings:
  // this loader has no dequantization support, so those files could never load.
  private static let modelFileNameById: [String: String] = [
    "kokoro-82m-bf16": "kokoro-v1_0-bf16.safetensors"
  ]

  private static func candidateModelFileNames() -> [String] {
    var candidates = [defaultModelFileName]

    if let activeModelId = UserDefaults.standard.string(forKey: activeKokoroModelIdDefaultsKey),
       let activeFileName = modelFileNameById[activeModelId] {
      candidates.insert(activeFileName, at: 0)
    }

    for fileName in modelFileNameById.values where !candidates.contains(fileName) {
      candidates.append(fileName)
    }

    return candidates
  }

  enum WeightLoaderError: Error, LocalizedError {
    case modelNotFound
    case loadFailed(String)

    var errorDescription: String? {
      switch self {
      case .modelNotFound:
        return "Could not find Kokoro model weights (.safetensors) in bundle or downloaded model directories"
      case .loadFailed(let reason):
        return "Failed to load model weights: \(reason)"
      }
    }
  }

  static func loadWeights() throws -> [String: MLXArray] {
    // MLX's Metal allocator aborts the whole process in the iOS Simulator
    // (mlx::core::metal::Device::Device() -> abort). MTLCreateSystemDefaultDevice()
    // succeeds there, so a Metal-availability check does NOT catch this — the
    // guard has to be the compile-time environment. Fail as a normal error so
    // callers fall back gracefully instead of taking the app down.
    #if targetEnvironment(simulator)
    throw WeightLoaderError.loadFailed("Kokoro MLX weights cannot be loaded in the iOS Simulator")
    #else
    let fileManager = FileManager.default
    let documentsDirectory = fileManager.urls(for: .documentDirectory, in: .userDomainMask).first
    let appSupportDirectory = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask).first

    for fileName in candidateModelFileNames() {
      // 1. Direct bundle lookup (for files in root)
      if let bundlePath = Bundle.main.path(forResource: (fileName as NSString).deletingPathExtension,
                                           ofType: (fileName as NSString).pathExtension) {
        AppLogger.kokoro.info("WeightLoader: Found model in bundle root")
        return try loadWeights(at: bundlePath)
      }

      // 2. Check KokoroAssets subdirectory
      if let bundleURL = Bundle.main.resourceURL {
        let bundleCandidates = [
          bundleURL.appendingPathComponent("KokoroAssets/\(fileName)").path,
          bundleURL.appendingPathComponent("KokoroAssets/models/\(fileName)").path,
          bundleURL.appendingPathComponent("KokoroTTS/Models/\(fileName)").path
        ]

        if let path = bundleCandidates.first(where: { fileManager.fileExists(atPath: $0) }) {
          AppLogger.kokoro.info("WeightLoader: Found model in bundle resources")
          return try loadWeights(at: path)
        }
      }

      // 3. Check downloaded model directories
      let localCandidates = [
        documentsDirectory?.appendingPathComponent("kokoro_models/\(fileName)").path,
        appSupportDirectory?.appendingPathComponent("kokoro_models/\(fileName)").path
      ].compactMap { $0 }

      if let path = localCandidates.first(where: { fileManager.fileExists(atPath: $0) }) {
        AppLogger.kokoro.info("WeightLoader: Found downloaded model")
        return try loadWeights(at: path)
      }
    }

    AppLogger.kokoro.error("WeightLoader: Could not find kokoro model weights in bundle or downloaded directories")
    throw WeightLoaderError.modelNotFound
    #endif
  }

  private static func loadWeights(at finalPath: String) throws -> [String: MLXArray] {
    let weights: [String: MLXArray]
    do {
      weights = try MLX.loadArrays(url: URL(fileURLWithPath: finalPath))
    } catch {
      AppLogger.kokoro.error("WeightLoader: Failed to load model weights: \(error.localizedDescription, privacy: .public)")
      throw WeightLoaderError.loadFailed(error.localizedDescription)
    }

    // Quantized exports (8-bit/4-bit) store packed weights with .scales/.biases
    // sidecar tensors that this loader cannot dequantize. Fail with a clear
    // message instead of letting the shape guards crash deep in model init —
    // stale installs may still have an 8-bit file under the default name.
    if weights.keys.contains(where: { $0.hasSuffix(".scales") }) {
      AppLogger.kokoro.error("WeightLoader: \(finalPath, privacy: .public) contains quantized weights, which are not supported")
      throw WeightLoaderError.loadFailed(
        "This model file contains quantized weights, which this app cannot load. Please re-download the Kokoro model from Settings.")
    }

    var sanitizedWeights: [String: MLXArray] = [:]

    for (key, value) in weights {
      if key.hasPrefix("bert") {
        if key.contains("position_ids") {
          continue
        }
        sanitizedWeights[key] = value
      } else if key.hasPrefix("predictor") {
        if key.contains("F0_proj.weight") {
          sanitizedWeights[key] = value.transposed(0, 2, 1)
        } else if key.contains("N_proj.weight") {
          sanitizedWeights[key] = value.transposed(0, 2, 1)
        } else if key.contains("weight_v") {
          if checkArrayShape(arr: value) {
            sanitizedWeights[key] = value
          } else {
            sanitizedWeights[key] = value.transposed(0, 2, 1)
          }
        } else {
          sanitizedWeights[key] = value
        }
      } else if key.hasPrefix("text_encoder") {
        if key.contains("weight_v") {
          if checkArrayShape(arr: value) {
            sanitizedWeights[key] = value
          } else {
            sanitizedWeights[key] = value.transposed(0, 2, 1)
          }
        } else {
          sanitizedWeights[key] = value
        }
      } else if key.hasPrefix("decoder") {
        if key.contains("noise_convs"), key.hasSuffix(".weight") {
          sanitizedWeights[key] = value.transposed(0, 2, 1)
        } else if key.contains("weight_v") {
          if checkArrayShape(arr: value) {
            sanitizedWeights[key] = value
          } else {
            sanitizedWeights[key] = value.transposed(0, 2, 1)
          }
        } else {
          sanitizedWeights[key] = value
        }
      }
    }

    return sanitizedWeights
  }

  private static func checkArrayShape(arr: MLXArray) -> Bool {
    guard arr.shape.count == 3 else { return true }

    let outChannels = arr.shape[0]
    let kH = arr.shape[1]
    let kW = arr.shape[2]

    return (outChannels >= kH) && (outChannels >= kW) && (kH == kW)
  }

  static func legacyLoadWeights() throws -> [String: MLXArray] {
    guard let finalPath = Bundle.main.path(forResource: "kokoro-v1_0", ofType: "safetensors") else {
      throw WeightLoaderError.modelNotFound
    }
    return try loadWeights(at: finalPath)
  }
}
