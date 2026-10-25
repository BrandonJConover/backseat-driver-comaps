//
//  Kokoro-tts-lib
//
import Foundation
import OSLog

struct KokoroConfig : Decodable {
  static var config: KokoroConfig?

  struct IstftnetConfig : Decodable {
    let upsampleKernelSizes: [Int]
    let upsampleRates: [Int]
    let genIstftHopSize: Int
    let genIstftNFFT: Int
    let resblockDilationSizes: [[Int]]
    let resblockKernelSizes: [Int]
    let upsampleInitialChannel: Int
    
    enum CodingKeys : String, CodingKey {
      case upsampleKernelSizes = "upsample_kernel_sizes"
      case upsampleRates = "upsample_rates"
      case genIstftHopSize = "gen_istft_hop_size"
      case genIstftNFFT = "gen_istft_n_fft"
      case resblockDilationSizes = "resblock_dilation_sizes"
      case resblockKernelSizes = "resblock_kernel_sizes"
      case upsampleInitialChannel = "upsample_initial_channel"
    }
  }
  
  struct Plbert: Decodable {
    let hiddenSize: Int
    let numAttentionHeads: Int
    let intermediateSize: Int
    let maxPositionEmbeddings: Int
    let numHiddenLayers: Int
    let dropout: Double
    
    enum CodingKeys : String, CodingKey {
      case hiddenSize = "hidden_size"
      case numAttentionHeads = "num_attention_heads"
      case intermediateSize = "intermediate_size"
      case maxPositionEmbeddings = "max_position_embeddings"
      case numHiddenLayers = "num_hidden_layers"
      case dropout = "dropout"
    }
  }
  
  let istftNet: IstftnetConfig
  let dimIn: Int
  let dropout: Double
  let hiddenDim: Int
  let maxConvDim: Int
  let maxDur: Int
  let multispeaker: Bool
  let nLayer: Int
  let nMels: Int
  let nToken: Int
  let styleDim: Int
  let textEncoderKernelSize: Int
  let plbert: Plbert
  let vocab: [String: Int]
  
  enum CodingKeys : String, CodingKey {
    case istftNet = "istftnet"
    case dimIn = "dim_in"
    case dropout = "dropout"
    case hiddenDim = "hidden_dim"
    case maxConvDim = "max_conv_dim"
    case maxDur = "max_dur"
    case multispeaker = "multispeaker"
    case nLayer = "n_layer"
    case nMels = "n_mels"
    case nToken = "n_token"
    case styleDim = "style_dim"
    case textEncoderKernelSize = "text_encoder_kernel_size"
    case plbert = "plbert"
    case vocab = "vocab"
  }
  
  enum ConfigError: Error, LocalizedError {
    case configNotFound
    case invalidData
    case decodeFailed(String)

    var errorDescription: String? {
      switch self {
      case .configNotFound:
        return "Could not find Kokoro config.json in bundle"
      case .invalidData:
        return "Could not convert config.json to UTF-8 data"
      case .decodeFailed(let reason):
        return "Failed to decode config.json: \(reason)"
      }
    }
  }

  static func loadConfig() throws -> KokoroConfig {
    // Try multiple locations for the config file so bundled and downloaded
    // Kokoro layouts can share the same loader.
    var filePath: String? = nil

    if let path = Bundle.main.path(forResource: "config", ofType: "json") {
      filePath = path
      AppLogger.kokoro.info("🔊 KokoroConfig: Found config.json in bundle root")
    }

    if filePath == nil, let bundleURL = Bundle.main.resourceURL {
      let candidatePaths: [(path: String, label: String)] = [
        (bundleURL.appendingPathComponent("KokoroAssets/config.json").path, "KokoroAssets"),
        (bundleURL.appendingPathComponent("KokoroAssets/models/config.json").path, "KokoroAssets/models"),
        (bundleURL.appendingPathComponent("KokoroTTS/Models/config.json").path, "KokoroTTS/Models")
      ]

      if let match = candidatePaths.first(where: { FileManager.default.fileExists(atPath: $0.path) }) {
        filePath = match.path
        AppLogger.kokoro.info("🔊 KokoroConfig: Found config.json in \(match.label, privacy: .public)")
      }
    }

    guard let finalPath = filePath else {
      AppLogger.kokoro.error("🔊 KokoroConfig: ❌ Could not find config.json in bundle")
      throw ConfigError.configNotFound
    }

    let configJSON = try String(contentsOf: URL(fileURLWithPath: finalPath), encoding: .utf8)
    guard let jsonData = configJSON.data(using: .utf8) else {
      throw ConfigError.invalidData
    }
    let config = try JSONDecoder().decode(KokoroConfig.self, from: jsonData)
    KokoroConfig.config = config
    return config
  }
}
