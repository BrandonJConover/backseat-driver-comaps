//
//  Kokoro-tts-lib
//
import Foundation
import MLX
import MLXNN
import OSLog

// Available Kokoro voices - 32 English + 22 multi-language
public enum TTSVoice: String, CaseIterable {
  // American Female
  case afHeart = "af_heart"
  case afAlloy = "af_alloy"
  case afAoede = "af_aoede"
  case afBella = "af_bella"
  case afJessica = "af_jessica"
  case afKore = "af_kore"
  case afNicole = "af_nicole"
  case afNova = "af_nova"
  case afRiver = "af_river"
  case afSarah = "af_sarah"
  case afSky = "af_sky"
  // American Male
  case amAdam = "am_adam"
  case amEcho = "am_echo"
  case amEric = "am_eric"
  case amFenrir = "am_fenrir"
  case amLiam = "am_liam"
  case amMichael = "am_michael"
  case amOnyx = "am_onyx"
  case amPuck = "am_puck"
  case amSanta = "am_santa"
  // British Female
  case bfAlice = "bf_alice"
  case bfEmma = "bf_emma"
  case bfIsabella = "bf_isabella"
  case bfLily = "bf_lily"
  // British Male
  case bmDaniel = "bm_daniel"
  case bmFable = "bm_fable"
  case bmGeorge = "bm_george"
  case bmLewis = "bm_lewis"
  // Greek Letter Voices (distinct character voices)
  case hfAlpha = "hf_alpha"
  case hfBeta = "hf_beta"
  case hmOmega = "hm_omega"
  case hmPsi = "hm_psi"
  // Spanish
  case efDora = "ef_dora"
  case emAlex = "em_alex"
  case emSanta = "em_santa"
  // French
  case ffSiwis = "ff_siwis"
  // Italian
  case ifSara = "if_sara"
  case imNicola = "im_nicola"
  // Japanese
  case jfAlpha = "jf_alpha"
  case jfGongitsune = "jf_gongitsune"
  case jfNezumi = "jf_nezumi"
  case jfTebukuro = "jf_tebukuro"
  case jmKumo = "jm_kumo"
  // Portuguese
  case pfDora = "pf_dora"
  case pmAlex = "pm_alex"
  case pmSanta = "pm_santa"
  // Chinese Mandarin
  case zfXiaobei = "zf_xiaobei"
  case zfXiaoni = "zf_xiaoni"
  case zfXiaoxiao = "zf_xiaoxiao"
  case zfXiaoyi = "zf_xiaoyi"
  case zmYunjian = "zm_yunjian"
  case zmYunxi = "zm_yunxi"
  case zmYunxia = "zm_yunxia"
  case zmYunyang = "zm_yunyang"

  /// Create from voice ID string
  static func from(_ id: String) -> TTSVoice? {
    return TTSVoice(rawValue: id)
  }
}

/// Tiny generic LRU keyed by voice id, used to cache decoded voice embeddings.
///
/// Generic over the value so the eviction policy can be unit-tested with plain
/// values — instantiating MLXArray traps in the iOS Simulator, so tests must
/// never touch the real embedding type.
///
/// Not thread-safe on its own; KokoroTTS's callers are already serialized
/// through KokoroEngine's synthesis executor.
final class VoiceEmbeddingLRU<Value> {
  let capacity: Int
  private var storage: [String: Value] = [:]
  /// Least-recently-used first, most-recently-used last.
  private var recency: [String] = []

  init(capacity: Int) {
    self.capacity = max(1, capacity)
  }

  var count: Int { storage.count }

  /// Keys ordered least-recently-used first (exposed for tests).
  var keysByRecency: [String] { recency }

  /// Returns the cached value and marks the key as most recently used.
  func value(forKey key: String) -> Value? {
    guard let value = storage[key] else { return nil }
    touch(key)
    return value
  }

  /// Inserts (or refreshes) a value, evicting the least-recently-used entry
  /// when over capacity.
  func insert(_ value: Value, forKey key: String) {
    if storage.updateValue(value, forKey: key) != nil {
      touch(key)
      return
    }
    recency.append(key)
    while storage.count > capacity {
      let evicted = recency.removeFirst()
      storage.removeValue(forKey: evicted)
    }
  }

  func removeAll() {
    storage.removeAll()
    recency.removeAll()
  }

  private func touch(_ key: String) {
    if let index = recency.firstIndex(of: key) {
      recency.remove(at: index)
    }
    recency.append(key)
  }
}

// Main class, encapsulates the whole Kokoro text-to-speech pipeline
public class KokoroTTS {
  enum KokoroTTSError: Error, LocalizedError {
    case tooManyTokens
    case voiceLoadFailed(String)
    case phonemizationFailed(String)
    case missingWeight(String)

    var errorDescription: String? {
      switch self {
      case .tooManyTokens:
        return "Text too long: exceeds maximum token count"
      case .voiceLoadFailed(let voiceId):
        return "Failed to load voice: \(voiceId)"
      case .phonemizationFailed(let reason):
        return "Phonemization failed: \(reason)"
      case .missingWeight(let key):
        return "Missing required model weight: \(key)"
      }
    }
  }

  private let bert: CustomAlbert!
  private let bertEncoder: Linear!
  private let durationEncoder: DurationEncoder!
  private let predictorLSTM: LSTM!
  private let durationProj: Linear!
  private let prosodyPredictor: ProsodyPredictor!
  private let textEncoder: TextEncoder!
  private let decoder: KokoroDecoder!
  // Primary phonemizer: MisakiSwift (higher quality, matches Kokoro training data)
  // Falls back to espeak-ng if MisakiSwift is unavailable
  private(set) var phonemizer: KokoroPhonemizer?
  private var chosenVoice: TTSVoice?
  private var voice: MLXArray!

  /// Cache of decoded voice embeddings keyed by voice id. Each embedding is
  /// [510][1][256] float32 ≈ 0.5MB, so 8 entries ≈ 4MB — cheap insurance
  /// against re-parsing a ~130k-value JSON on EVERY narrator/character voice
  /// switch (previously paid once per sentence in alternating dialogue).
  /// Lives inside KokoroTTS, so dropping the instance (memory-warning unload)
  /// drops the cache with it. Keys are namespaced per load path so an enum
  /// voice, a custom file, and a file-backed (`*_k`) voice can never collide.
  private let voiceCache = VoiceEmbeddingLRU<MLXArray>(capacity: 8)

  init() throws {
    AppLogger.kokoro.debug("KokoroTTS.init: Loading weights...")
    let sanitizedWeights = try WeightLoader.loadWeights()
    // Materialize every weight now. MLX records lazy work on the stream that
    // was the default when it was created; a weight transform left pending
    // from a foreground (GPU) load would otherwise run on the GPU the first
    // time a background (CPU) synthesis needs it — iOS refuses that Metal
    // work and MLX aborts the app (crashes 2026-10-10 06:57 and 06:58).
    MLX.eval(Array(sanitizedWeights.values))
    AppLogger.kokoro.info("KokoroTTS.init: Loaded \(sanitizedWeights.count, privacy: .public) weight tensors")
    let config = try KokoroConfig.loadConfig()
    AppLogger.kokoro.debug("KokoroTTS.init: Config loaded")

    // Helper function to safely extract weights
    let requiredWeight: (String) throws -> MLXArray = { key in
      guard let weight = sanitizedWeights[key] else {
        throw KokoroTTSError.missingWeight(key)
      }
      return weight
    }

    bert = try CustomAlbert(weights: sanitizedWeights,
                            config: AlbertModelArgs(
                              numHiddenLayers: config.plbert.numHiddenLayers,
                              numAttentionHeads: config.plbert.numAttentionHeads,
                              hiddenSize: config.plbert.hiddenSize,
                              intermediateSize: config.plbert.intermediateSize,
                              vocabSize: config.nToken))
    bertEncoder = Linear(weight: try requiredWeight("bert_encoder.weight"), bias: try requiredWeight("bert_encoder.bias"))
    durationEncoder = try DurationEncoder(weights: sanitizedWeights, dModel: config.hiddenDim, styDim: config.styleDim, nlayers: config.nLayer)

    predictorLSTM = LSTM(
      inputSize: config.hiddenDim + config.styleDim,
      hiddenSize: config.hiddenDim / 2,
      wxForward: try requiredWeight("predictor.lstm.weight_ih_l0"),
      whForward: try requiredWeight("predictor.lstm.weight_hh_l0"),
      biasIhForward: try requiredWeight("predictor.lstm.bias_ih_l0"),
      biasHhForward: try requiredWeight("predictor.lstm.bias_hh_l0"),
      wxBackward: try requiredWeight("predictor.lstm.weight_ih_l0_reverse"),
      whBackward: try requiredWeight("predictor.lstm.weight_hh_l0_reverse"),
      biasIhBackward: try requiredWeight("predictor.lstm.bias_ih_l0_reverse"),
      biasHhBackward: try requiredWeight("predictor.lstm.bias_hh_l0_reverse")
    )

    durationProj = Linear(
      weight: try requiredWeight("predictor.duration_proj.linear_layer.weight"),
      bias: try requiredWeight("predictor.duration_proj.linear_layer.bias")
    )

    prosodyPredictor = try ProsodyPredictor(
      weights: sanitizedWeights,
      styleDim: config.styleDim,
      dHid: config.hiddenDim
    )

    textEncoder = try TextEncoder(
      weights: sanitizedWeights,
      channels: config.hiddenDim,
      kernelSize: config.textEncoderKernelSize,
      depth: config.nLayer,
      nSymbols: config.nToken
    )

    decoder = try KokoroDecoder(
      weights: sanitizedWeights,
      dimIn: config.hiddenDim,
      styleDim: config.styleDim,
      dimOut: config.nMels,
      resblockKernelSizes: config.istftNet.resblockKernelSizes,
      upsampleRates: config.istftNet.upsampleRates,
      upsampleInitialChannel: config.istftNet.upsampleInitialChannel,
      resblockDilationSizes: config.istftNet.resblockDilationSizes,
      upsampleKernelSizes: config.istftNet.upsampleKernelSizes,
      genIstftNFft: config.istftNet.genIstftNFFT,
      genIstftHopSize: config.istftNet.genIstftHopSize
    )
    AppLogger.kokoro.debug("KokoroTTS.init: Neural network layers created")

    // Initialize phonemizer: prefer MisakiSwift (matches Kokoro's training G2P),
    // with espeak-ng as fallback for robustness
    var espeakEngine: ESpeakNGEngine?
    do {
      espeakEngine = try ESpeakNGEngine()
      AppLogger.kokoro.info("KokoroTTS: ESpeakNG initialized (available as fallback)")
    } catch {
      AppLogger.kokoro.warning("KokoroTTS: ESpeakNG unavailable: \(error.localizedDescription, privacy: .public)")
    }

    let misakiPhonemizer = MisakiPhonemizer(espeakFallback: espeakEngine)
    if misakiPhonemizer.isMisakiAvailable {
      phonemizer = misakiPhonemizer
      AppLogger.kokoro.info("KokoroTTS: Using MisakiSwift as primary phonemizer (with espeak-ng fallback)")
    } else if let espeak = espeakEngine {
      phonemizer = espeak
      AppLogger.kokoro.warning("KokoroTTS: Using ESpeakNG as phonemizer (MisakiSwift unavailable)")
    } else {
      phonemizer = nil
      AppLogger.kokoro.error("KokoroTTS: No phonemizer available")
    }
  }

  /// One short synthesis right after loading, so every array the model
  /// derives lazily is computed now, on the device that loaded it (see
  /// KokoroEngine.initializeKokoroTTS). A failure is logged, not fatal.
  func warmUp() {
    _ = try? KokoroMLXErrors.capture {
      let audio = try generateAudio(voice: .afHeart, text: "Hello.", speed: 1.0)
      audio.eval()
      return audio
    }
  }

  /// Diagnostic for `KokoroEngine.runCPUParityTestIfRequested`: runs the
  /// duration path's layers, and the basic operations inside them, on the
  /// GPU and on the CPU with identical inputs, and reports the largest
  /// difference of each. Call in the foreground (it uses the GPU).
  func cpuParityReport() -> [String] {
    var lines: [String] = []
    MLXRandom.seed(11)
    let seq = MLXRandom.normal([97, 640]) * 0.3
    let seq3 = seq.expandedDimensions(axis: 0)
    let style = MLXRandom.normal([1, 128]) * 0.2
    let w = MLXRandom.normal([1024, 640]) * 0.05
    let wh = MLXRandom.normal([1024, 256]) * 0.05
    let b = MLXRandom.normal([1024])
    let h = MLXRandom.normal([1, 256])
    let normIn = MLXRandom.normal([1, 97, 512])
    MLX.eval(seq, style, w, wh, b, h, normIn)

    func compare(_ name: String, _ f: () -> MLXArray) {
      let gpu = f(); gpu.eval()
      let cpu = Device.withDefaultDevice(.cpu) { () -> MLXArray in let r = f(); r.eval(); return r }
      guard gpu.shape == cpu.shape else { lines.append("\(name) SHAPE \(gpu.shape) vs \(cpu.shape)"); return }
      let diff = MLX.abs(gpu.asType(.float32) - cpu.asType(.float32)).max().item(Float.self)
      let size = MLX.abs(gpu.asType(.float32)).mean().item(Float.self)
      lines.append(String(format: "%@ %.1e (|x| %.3f)", name, diff, size))
    }
    compare("addMM3d") { MLX.addMM(b, seq3, w.transposed()) }
    compare("addMM2d") { MLX.addMM(b, seq, w.transposed()) }
    compare("matmulT") { MLX.matmul(h, wh.transposed()) }
    compare("sliceRow") { MLX.addMM(b, seq3, w.transposed())[0..., 50, 0...] }
    compare("split+gates") {
      let g = MLX.split(MLX.addMM(b, seq3, w.transposed())[0..., 50, 0...], parts: 4, axis: -1)
      return MLX.sigmoid(g[0]) * MLX.tanh(g[2]) + MLX.sigmoid(g[1])
    }
    compare("sigmoid") { MLX.sigmoid(seq * 4) }
    compare("tanh") { MLX.tanh(seq * 4) }
    compare("variance") { MLX.variance(normIn, axes: [-1], keepDims: true) }
    compare("mean") { MLX.mean(normIn, axes: [-1], keepDims: true) }
    compare("stack") { MLX.stacked([seq, seq * 2], axis: -2) }
    compare("sliceUpdate") {
      let pad = MLXArray.zeros([1, 512, 120])
      pad[0 ..< 1, 0 ..< 512, 0 ..< 97] = normIn.transposed(0, 2, 1)
      return pad
    }
    if let lstm = durationEncoder.lstms.first as? LSTM {
      compare("durLSTM") { lstm(seq).0 }
    }
    if let ada = durationEncoder.lstms.dropFirst().first as? AdaLayerNorm {
      compare("durAdaLN") { ada(normIn, style) }
    }
    compare("predictorLSTM") { predictorLSTM(seq).0 }
    // The duration encoder's own glue: a transposed slice fed to addMM, the
    // style broadcast, the mask, and the whole encoder.
    let dEn = MLXRandom.normal([1, 512, 97]) * 0.3
    let lengths = MLXArray(97)
    let mask = (MLXArray(0 ..< 97) + 1 .> lengths).expandedDimensions(axes: [0])
    MLX.eval(dEn, mask)
    let glued = MLX.concatenated([dEn, MLX.broadcast(style.expandedDimensions(axis: -1), to: [1, 128, 97])], axis: 1)
    compare("addMMTransposedSlice") {
      MLX.addMM(b, glued.transposed(0, 2, 1)[0].expandedDimensions(axis: 0), w.transposed())
    }
    compare("lstmTransposedSlice") { predictorLSTM(glued.transposed(0, 2, 1)[0]).0 }
    compare("durationEncoder") { durationEncoder(dEn, style: style, textLengths: lengths, m: mask) }
    return lines
  }

  public func generateAudio(voice: TTSVoice, text: String, speed: Float = 1.0) throws -> MLXArray {
    AppLogger.kokoro.debug("KokoroTTS.generateAudio: voice=\(voice.rawValue, privacy: .public), speed=\(speed, privacy: .public)")

    // Check if phonemizer is available
    guard let phonemizer = self.phonemizer else {
      AppLogger.kokoro.error("KokoroTTS: Phonemizer not available")
      throw KokoroTTSError.phonemizationFailed("Phonemizer not initialized")
    }

    try prepareVoice(voice)

    BenchmarkTimer.reset()
    BenchmarkTimer.startTimer(Constants.bm_timeToFirstAudio)
    BenchmarkTimer.startTimer(Constants.bm_TTS)
    let audio = try generateSegmentedAudio(text: text, phonemizer: phonemizer, speed: speed)

    BenchmarkTimer.stopTimer(Constants.bm_TTS)
    BenchmarkTimer.print()

    // Evaluate the audio array to ensure computation is complete
    audio.eval()

    AppLogger.kokoro.info("KokoroTTS.generateAudio: Audio generated, samples=\(audio.shape.reduce(1, *), privacy: .public)")

    // Clear GPU cache after generation to free memory
    Memory.clearCache()

    return audio
  }

  /// Loads `voice` (from the embedding LRU or from disk) and points the
  /// phonemizer at its language. Idempotent and cheap when the voice is
  /// unchanged, so callers may invoke it once per batch without penalty.
  func prepareVoice(_ voice: TTSVoice) throws {
    guard let phonemizer = self.phonemizer else {
      throw KokoroTTSError.phonemizationFailed("Phonemizer not initialized")
    }
    guard chosenVoice != voice else { return }

    let cacheKey = "enum:\(voice.rawValue)"
    if let cached = voiceCache.value(forKey: cacheKey) {
      // Cache hit: no JSON re-parse and no Memory.clearCache() — the GPU
      // cache clear was only ever needed to offset the fresh-load churn.
      self.voice = cached
      AppLogger.kokoro.debug("KokoroTTS: Voice \(voice.rawValue, privacy: .public) served from embedding cache")
    } else {
      // Clear GPU cache before loading a new voice to prevent memory buildup
      Memory.clearCache()
      AppLogger.kokoro.debug("KokoroTTS: Loading voice \(voice.rawValue, privacy: .public)")
      do {
        let loaded = try VoiceLoader.tryLoadVoice(voice)
        loaded.eval()  // cache only computed arrays (see init)
        voiceCache.insert(loaded, forKey: cacheKey)
        self.voice = loaded
        AppLogger.kokoro.info("KokoroTTS: Voice \(voice.rawValue, privacy: .public) loaded successfully")
      } catch {
        AppLogger.kokoro.error("KokoroTTS: Failed to load voice \(voice.rawValue, privacy: .public): \(error.localizedDescription, privacy: .public)")
        throw KokoroTTSError.voiceLoadFailed(voice.rawValue)
      }
    }
    // Language must track the active voice on EVERY switch, cached or not.
    try phonemizer.setLanguage(for: voice)
    chosenVoice = voice
    currentCustomVoicePath = nil
    currentDynamicVoiceId = nil
  }

  /// The phoneme segments (each under `Constants.maxTokenCount`) that the
  /// MLX path would synthesize for `text` in `voice`'s language (or the
  /// narrator dialect for file-backed voices) — the same splitter, phonemizer
  /// and pronunciation rules — for another renderer of the same model (Core
  /// ML, 2026-10-15). Leaves the next MLX call to re-prepare its voice.
  func phonemeSegments(for text: String, voice: TTSVoice?, dialect: ESpeakNGEngine.LanguageDialect) throws -> [String] {
    guard let phonemizer = self.phonemizer else {
      throw KokoroTTSError.phonemizationFailed("Phonemizer not initialized")
    }
    if let voice { try phonemizer.setLanguage(for: voice) } else { try phonemizer.setLanguage(dialect: dialect) }
    chosenVoice = nil
    currentDynamicVoiceId = nil
    currentCustomVoicePath = nil
    return try KokoroPhonemeSegmenter.segment(
      text: text,
      maxTokenCount: Constants.maxTokenCount,
      phonemize: { try phonemizer.phonemize(text: $0) },
      tokenize: Tokenizer.tokenize(phonemizedText:)
    ).map(\.phonemes)
  }

  /// Phonemizes and tokenizes text with the already-prepared voice WITHOUT
  /// synthesizing. Used by batched sentence synthesis to learn the exact token
  /// span of each sentence before combining them into one model call.
  func phonemeTokens(for text: String) throws -> [Int] {
    guard let phonemizer = self.phonemizer else {
      throw KokoroTTSError.phonemizationFailed("Phonemizer not initialized")
    }
    let phonemes = try phonemizer.phonemize(text: text)
    return Tokenizer.tokenize(phonemizedText: phonemes)
  }

  /// Synthesizes an explicit phoneme-token sequence and returns host-memory
  /// samples plus the model's predicted frame count for every padded token.
  ///
  /// The caller must guarantee `tokenIds.count <= Constants.maxTokenCount`;
  /// this bypasses `generateSegmentedAudio` on purpose so one call maps to one
  /// model forward and the duration map stays aligned with the input tokens.
  func synthesizeTokenizedAudio(
    voice: TTSVoice,
    tokenIds: [Int],
    speed: Float
  ) throws -> KokoroTokenizedAudio {
    try prepareVoice(voice)
    guard !tokenIds.isEmpty else {
      throw KokoroTTSError.phonemizationFailed("No phoneme tokens generated")
    }
    guard tokenIds.count <= Constants.maxTokenCount else {
      throw KokoroTTSError.tooManyTokens
    }

    BenchmarkTimer.reset()
    BenchmarkTimer.startTimer(Constants.bm_timeToFirstAudio)
    BenchmarkTimer.startTimer(Constants.bm_TTS)
    let synthesized = try synthesizePhonemes(inputIds: tokenIds, speed: speed)
    BenchmarkTimer.stopTimer(Constants.bm_TTS)
    BenchmarkTimer.print()

    synthesized.audio.eval()
    let samples = synthesized.audio.asArray(Float.self)
    Memory.clearCache()
    return KokoroTokenizedAudio(samples: samples, paddedFrameCounts: synthesized.tokenDurations)
  }

  /// Host-memory form of a tokenized synthesis result.
  struct KokoroTokenizedAudio {
    let samples: [Float]
    /// Frame count for every padded token: `[leadingPad, real0, …, realN-1, trailingPad]`.
    let paddedFrameCounts: [Int]
  }

  /// Clear all cached data to free memory
  public func clearCache() {
    Memory.clearCache()
    voiceCache.removeAll()
    voice = nil
    chosenVoice = nil
    currentCustomVoicePath = nil
    // Must also reset the dynamic-voice marker: leaving it set while `voice`
    // is nil would make the next generateAudioWithVoiceId call for the same
    // id skip the reload and crash on the nil embedding.
    currentDynamicVoiceId = nil
  }

  // Track custom voice path for caching
  private var currentCustomVoicePath: URL?
  private var currentDynamicVoiceId: String?

  /// Generate audio using a custom voice file (for imported voices)
  /// - Parameters:
  ///   - voicePath: Path to the custom voice file
  ///   - text: Text to synthesize
  ///   - speed: Speech speed multiplier
  ///   - dialect: Optional dialect override (defaults to American English)
  func generateAudioWithCustomVoice(voicePath: URL, text: String, speed: Float = 1.0, dialect: ESpeakNGEngine.LanguageDialect = .enUS) throws -> MLXArray {
    // Check if phonemizer is available
    guard let phonemizer = self.phonemizer else {
      AppLogger.kokoro.error("KokoroTTS: Phonemizer not available for custom voice")
      throw KokoroTTSError.phonemizationFailed("Phonemizer not initialized")
    }

    // Track if we're switching from a standard voice to custom voice
    // This is needed to reset the language dialect even if custom path is the same
    let wasUsingStandardVoice = chosenVoice != nil
    let isNewCustomVoice = currentCustomVoicePath != voicePath

    if isNewCustomVoice {
      let cacheKey = "custom:\(voicePath.path)"
      if let cached = voiceCache.value(forKey: cacheKey) {
        self.voice = cached
        AppLogger.kokoro.debug("KokoroTTS: Custom voice served from embedding cache")
      } else {
        // Clear GPU cache before loading a new voice to prevent memory buildup
        Memory.clearCache()
        guard let customVoice = VoiceLoader.loadCustomVoice(from: voicePath) else {
          throw KokoroTTSError.voiceLoadFailed(voicePath.lastPathComponent)
        }
        customVoice.eval()  // cache only computed arrays (see init)
        voiceCache.insert(customVoice, forKey: cacheKey)
        self.voice = customVoice
      }
      currentCustomVoicePath = voicePath
    }

    // Set the dialect for phonemization
    // This uses the user's preferred dialect for custom voices
    // Ensures British phonemes from character voices don't bleed into narrator text
    if wasUsingStandardVoice || isNewCustomVoice {
      try phonemizer.setLanguage(dialect: dialect)
    }

    // Reset the chosen voice so we don't accidentally reuse a bundled voice
    chosenVoice = nil

    BenchmarkTimer.reset()
    BenchmarkTimer.startTimer(Constants.bm_timeToFirstAudio)
    BenchmarkTimer.startTimer(Constants.bm_TTS)
    let audio = try generateSegmentedAudio(text: text, phonemizer: phonemizer, speed: speed)

    BenchmarkTimer.stopTimer(Constants.bm_TTS)
    BenchmarkTimer.print()

    // Evaluate the audio array to ensure computation is complete
    audio.eval()

    // Clear GPU cache after generation to free memory
    Memory.clearCache()

    return audio
  }

  /// Generate audio using a voice loaded by string ID (for file-backed voices such as the `*_k` fits)
  /// This bypasses the TTSVoice enum, loading the voice JSON directly from the bundle
  func generateAudioWithVoiceId(_ voiceId: String, text: String, speed: Float = 1.0, dialect: ESpeakNGEngine.LanguageDialect = .enUS) throws -> MLXArray {
    guard let phonemizer = self.phonemizer else {
      throw KokoroTTSError.phonemizationFailed("Phonemizer not initialized")
    }

    // Load voice if different from current
    let needsLoad = (chosenVoice != nil) || (currentDynamicVoiceId != voiceId)
    if needsLoad {
      let cacheKey = "dynamic:\(voiceId)"
      if let cached = voiceCache.value(forKey: cacheKey) {
        self.voice = cached
        currentDynamicVoiceId = voiceId
        AppLogger.kokoro.debug("KokoroTTS: Dynamic voice \(voiceId, privacy: .public) served from embedding cache")
      } else {
        // Clear GPU cache before loading a new voice to prevent memory buildup
        Memory.clearCache()
        do {
          let loaded = try VoiceLoader.tryLoadVoiceById(voiceId)
          loaded.eval()  // cache only computed arrays (see init)
          voiceCache.insert(loaded, forKey: cacheKey)
          self.voice = loaded
          currentDynamicVoiceId = voiceId
        } catch {
          throw KokoroTTSError.voiceLoadFailed(voiceId)
        }
      }
      // Language must track the active voice on EVERY switch, cached or not.
      try phonemizer.setLanguage(dialect: dialect)
    }

    chosenVoice = nil
    currentCustomVoicePath = nil

    // Generate audio using the loaded voice (same pipeline as generateAudio)
    BenchmarkTimer.reset()
    BenchmarkTimer.startTimer(Constants.bm_timeToFirstAudio)
    BenchmarkTimer.startTimer(Constants.bm_TTS)
    let audio = try generateSegmentedAudio(text: text, phonemizer: phonemizer, speed: speed)

    BenchmarkTimer.stopTimer(Constants.bm_TTS)
    BenchmarkTimer.print()

    audio.eval()
    Memory.clearCache()

    return audio
  }

  private func generateSegmentedAudio(text: String, phonemizer: KokoroPhonemizer, speed: Float) throws -> MLXArray {
    BenchmarkTimer.startTimer(Constants.bm_Phonemize, Constants.bm_TTS)
    let phonemizeStart = Date()
    let segments = try KokoroPhonemeSegmenter.segment(
      text: text,
      maxTokenCount: Constants.maxTokenCount,
      phonemize: { try phonemizer.phonemize(text: $0) },
      tokenize: Tokenizer.tokenize(phonemizedText:)
    )
    BenchmarkTimer.stopTimer(Constants.bm_Phonemize)
    if false {
      PerfTimings.record("kokoro cpu phonemize", seconds: Date().timeIntervalSince(phonemizeStart),
                         "\(text.count) chars, \(segments.count) segment(s)")
    }

    guard !segments.isEmpty else {
      throw KokoroTTSError.phonemizationFailed("No phonemes generated")
    }

    if segments.count > 1 {
      AppLogger.kokoro.info("KokoroTTS: Split synthesis into \(segments.count, privacy: .public) segments under \(Constants.maxTokenCount, privacy: .public) phoneme tokens")
    }

    if segments.count == 1, let segment = segments.first {
      return try synthesizePhonemes(
        inputIds: Tokenizer.tokenize(phonemizedText: segment.phonemes),
        speed: speed
      ).audio
    }

    // Do not retain several lazy MLX graphs and concatenate them on the GPU.
    // Real-device crashes from long paragraphs ended in mlx_concatenate_axis:
    // variable-duration lazy results reached MLX's fatal shape assertion before
    // Swift could catch an error. Materialize each segment
    // to a flat host buffer, release its graph, and build one 1-D result.
    var accumulator = KokoroAudioSegmentAccumulator()
    for segment in segments {
      let samples: [Float] = try autoreleasepool {
        let audio = try synthesizePhonemes(
          inputIds: Tokenizer.tokenize(phonemizedText: segment.phonemes),
          speed: speed
        ).audio
        audio.eval()
        return audio.asArray(Float.self)
      }

      guard accumulator.append(samples) else {
        throw KokoroTTSError.phonemizationFailed("A segmented synthesis result was empty")
      }
      Memory.clearCache()
    }

    guard !accumulator.samples.isEmpty else {
      throw KokoroTTSError.phonemizationFailed("No audio generated")
    }
    return MLXArray(accumulator.samples)
  }

  /// Raw result of one model forward: the audio plus the predicted frame count
  /// of every padded input token. The durations are what let batched synthesis
  /// split one call's audio back into per-sentence pieces.
  struct TokenSynthesis {
    let audio: MLXArray
    /// Frame count for `[leadingPad, real0, …, realN-1, trailingPad]`.
    let tokenDurations: [Int]
  }

  /// Frame→token index for every audio frame: token `t` repeated
  /// `durations[t]` times, in token order (the CPU equivalent of concatenating
  /// `MLX.repeated(MLXArray([t]), count: durations[t])`). Non-positive
  /// durations contribute no frames. Pure Swift so it is unit-testable
  /// without MLX.
  static func alignmentIndices(durations: [Int]) -> [Int] {
    var indices: [Int] = []
    indices.reserveCapacity(durations.reduce(0) { $0 + max(0, $1) })
    for (token, count) in durations.enumerated() where count > 0 {
      indices.append(contentsOf: repeatElement(token, count: count))
    }
    return indices
  }

  /// Row-major `[tokens, frames]` one-hot alignment matrix: entry
  /// `[alignmentIndices[f], f]` is 1, everything else 0. `frameCount` is the
  /// total number of frames (sum of the positive durations).
  static func alignmentMatrix(durations: [Int]) -> (values: [Float], frameCount: Int) {
    let indices = alignmentIndices(durations: durations)
    let frameCount = indices.count
    var values = [Float](repeating: 0.0, count: frameCount * durations.count)
    for (frame, token) in indices.enumerated() {
      values[token * frameCount + frame] = 1.0
    }
    return (values, frameCount)
  }

  nonisolated(unsafe) static var minimumStyleRow = 0

  private func synthesizePhonemes(inputIds: [Int], speed: Float) throws -> TokenSynthesis {
    guard !inputIds.isEmpty else {
      throw KokoroTTSError.phonemizationFailed("No phoneme tokens generated")
    }
    guard inputIds.count <= Constants.maxTokenCount else {
      throw KokoroTTSError.tooManyTokens
    }

    let paddedInputIdsBase = [0] + inputIds + [0]
    let paddedInputIds = MLXArray(paddedInputIdsBase).expandedDimensions(axes: [0])

    let inputLengths = MLXArray(paddedInputIds.dim(-1))
    // `inputLengths` is a scalar holding the padded length, so its max is that
    // same length; read it from the Swift array instead of a GPU round trip.
    let inputLengthMax: Int = paddedInputIdsBase.count
    var textMask = MLXArray(0 ..< inputLengthMax)
    textMask = textMask + 1 .> inputLengths
    textMask = textMask.expandedDimensions(axes: [0])
    // Same predicate as `textMask` (`i + 1 > length`), evaluated on the CPU so
    // building the attention mask does not force a GPU sync.
    let swiftTextMask: [Bool] = (0 ..< inputLengthMax).map { $0 + 1 > inputLengthMax }
    let swiftTextMaskInt = swiftTextMask.map { !$0 ? 1 : 0 }
    let attentionMask = MLXArray(swiftTextMaskInt).reshaped(textMask.shape)

    let clock = KokoroStageClock.parityLabel.map { KokoroStageClock(label: $0) }
      ?? (false ? KokoroStageClock() : nil)
    KokoroStageClock.current = clock
    BenchmarkTimer.startTimer(Constants.bm_bert, Constants.bm_TTS)
    let (bertDur, _) = bert(paddedInputIds, attentionMask: attentionMask)
    let dEn = bertEncoder(bertDur).transposed(0, 2, 1)
    BenchmarkTimer.stopTimer(Constants.bm_bert, [dEn])
    clock?.mark("bert", [dEn])

    BenchmarkTimer.startTimer(Constants.bm_duration, Constants.bm_TTS)
    // The voice's style row is chosen by input length; very short inputs
    // ("A.", "Seven.") get the shortest rows. `minimumStyleRow` lets a
    // probe compare them with a longer row (0 = upstream behaviour).
    let styleRow = min(max(inputIds.count - 1, Self.minimumStyleRow), self.voice.dim(0) - 1)
    let refS = self.voice[styleRow, 0 ... 1, 0...]
    let s = refS[0 ... 1, 128...]
    let d = durationEncoder(dEn, style: s, textLengths: inputLengths, m: textMask)
    clock?.mark("durationEncoder", [d])
    let (x, _) = predictorLSTM(d)
    clock?.mark("predictorLSTM", [x])
    let duration = durationProj(x)
    clock?.mark("durationLSTM", [duration])
    let durationSigmoid = MLX.sigmoid(duration).sum(axis: -1) / speed
    let predDur = MLX.clip(durationSigmoid.round(), min: 1).asType(.int32)[0]
    // Predicted frame count per padded token, in token order. Exposed to the
    // batched path so it can map sentence token spans onto audio samples.
    let tokenDurations: [Int] = predDur.asArray(Int32.self).map { Int($0) }
    clock?.record(durations: tokenDurations)

    // Token→frame alignment built on the CPU from the durations already read
    // above (one GPU sync total instead of one `.item()` per token and per
    // frame). Values, shape [tokens, frames] and float32 dtype are unchanged.
    let alignment = KokoroTTS.alignmentMatrix(durations: tokenDurations)
    let predAlnTrg = MLXArray(alignment.values).reshaped([paddedInputIds.shape[1], alignment.frameCount])
    let predAlnTrgBatched = predAlnTrg.expandedDimensions(axis: 0)
    let en = d.transposed(0, 2, 1).matmul(predAlnTrgBatched)
    BenchmarkTimer.stopTimer(Constants.bm_duration, [en, s])
    clock?.mark("alignment", [en])

    BenchmarkTimer.startTimer(Constants.bm_prosody, Constants.bm_TTS)
    let (F0Pred, NPred) = try prosodyPredictor.F0NTrain(x: en, s: s)
    clock?.mark("prosody", [F0Pred, NPred])
    let tEn = textEncoder(paddedInputIds, inputLengths: inputLengths, m: textMask)
    let asr = MLX.matmul(tEn, predAlnTrg)
    clock?.mark("textEncoder", [asr])
    BenchmarkTimer.stopTimer(Constants.bm_prosody, [asr, F0Pred, NPred])

    BenchmarkTimer.startTimer(Constants.bm_decoder, Constants.bm_TTS)
    let audio = try decoder(asr: asr, F0Curve: F0Pred, N: NPred, s: refS[0 ... 1, 0 ... 127])[0]
    BenchmarkTimer.stopTimer(Constants.bm_decoder, [audio])
    clock?.mark("decoder", [audio])
    clock?.finish(tokens: inputIds.count)
    return TokenSynthesis(audio: audio, tokenDurations: tokenDurations)
  }

  struct Constants {
    static let maxTokenCount = 510
    static let samplingRate = 24000
    /// Space token that separates sentences combined into one batched model
    /// call. Read from the loaded vocab; the `16` fallback is the upstream
    /// Kokoro id and only applies if the config somehow has not loaded yet (it
    /// always has by the time synthesis runs).
    static var separatorTokenId: Int { KokoroConfig.config?.vocab[" "] ?? 16 }
    
    static let bm_TTS = "TTSAudio"
    static let bm_Phonemize = "Phonemize"
    static let bm_bert = "BERT"
    static let bm_duration = "Duration"
    static let bm_prosody = "Prosody"
    static let bm_decoder = "Decoder"
    static let bm_timeToFirstAudio = "TimeToFirstAudio"
  }
}

/// Per-stage wall time of one CPU synthesis, written to Timings.json so the
/// slow stage on the phone's CPU can be found. Each mark computes the stage's
/// arrays first; only used while Kokoro runs on the CPU (the GPU path is
/// unchanged).
final class KokoroStageClock {
  /// The clock of the synthesis in progress (synthesis is serialized), so
  /// the decoder can mark its own sub-stages.
  nonisolated(unsafe) static var current: KokoroStageClock?

  static func mark(_ stage: String, _ arrays: [MLXArray]) { current?.mark(stage, arrays) }

  /// Set for a CPU/GPU parity run (`KokoroEngine.runCPUParityTestIfRequested`):
  /// every synthesis gets a clock with this label, and each mark also
  /// records a checksum of the stage's output.
  nonisolated(unsafe) static var parityLabel: String?

  private var last = Date()
  private let started = Date()
  private var parts: [String] = []
  private var checksums: [String] = []
  private let label: String?

  init(label: String? = nil) { self.label = label }

  func mark(_ stage: String, _ arrays: [MLXArray] = []) {
    if !arrays.isEmpty { MLX.eval(arrays) }
    let now = Date()
    parts.append(String(format: "%@ %.2f", stage, now.timeIntervalSince(last)))
    if label != nil, let first = arrays.first {
      let values = first.asType(.float32)
      checksums.append(String(format: "%@ %.5f/%.5f", stage,
                              MLX.abs(values).mean().item(Float.self), values.mean().item(Float.self)))
    }
    last = now
  }

  func record(durations: [Int]) {
    guard let label else { return }
    PerfTimings.record("kokoro parity \(label) durations", seconds: 0,
                       "sum \(durations.reduce(0, +)): " + durations.map(String.init).joined(separator: " "))
  }

  func finish(tokens: Int) {
    if Self.current === self { Self.current = nil }
    if let label {
      PerfTimings.record("kokoro parity \(label) stages", seconds: Date().timeIntervalSince(started),
                         checksums.joined(separator: ", "))
      return
    }
    PerfTimings.record("kokoro cpu stages", seconds: Date().timeIntervalSince(started),
                       "\(tokens) tokens: " + parts.joined(separator: ", "))
  }
}
