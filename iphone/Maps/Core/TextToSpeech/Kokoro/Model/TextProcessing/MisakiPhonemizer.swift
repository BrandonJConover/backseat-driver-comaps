//
//  MisakiPhonemizer.swift
//  AudiobookStudioApp
//
//  MisakiSwift-based phonemizer for Kokoro TTS.
//  Uses the official Misaki G2P library (the same G2P Kokoro was trained with)
//  for higher quality phonemization than raw espeak-ng.
//
//  Advantages over espeak-ng alone:
//  - Dictionary-based lookup for common words (gold + silver tiers)
//  - Neural network fallback (BART transformer via MLX) for unknown words
//  - Context-aware POS tagging via Apple's NaturalLanguage framework
//  - Correct American vs British phoneme sets matching Kokoro's training data
//  - Inline phoneme override syntax: [word](/IPA/)
//

import Foundation
import OSLog
#if canImport(MisakiSwift)
import MisakiSwift
#endif

/// Protocol for phonemizers so KokoroTTS can swap implementations
protocol KokoroPhonemizer {
  /// The current dialect being used
  var currentDialect: ESpeakNGEngine.LanguageDialect { get }

  /// Set dialect based on a built-in TTSVoice (auto-detects from voice prefix)
  func setLanguage(for voice: TTSVoice) throws

  /// Set dialect explicitly (for custom voices using user preference)
  func setLanguage(dialect: ESpeakNGEngine.LanguageDialect) throws

  /// Convert text to Kokoro-compatible phoneme string
  func phonemize(text: String) throws -> String
}

// Make ESpeakNGEngine conform to our protocol (it already has all these methods)
extension ESpeakNGEngine: KokoroPhonemizer {}

struct KokoroPhonemeSegment: Equatable {
  let text: String
  let phonemes: String
  let tokenCount: Int
}

/// Host-memory accumulator for variable-duration Kokoro segment output.
/// Keeping this Foundation-only makes the critical shape behavior testable
/// without initializing MLX or a Metal device.
struct KokoroAudioSegmentAccumulator {
  private(set) var samples: [Float] = []

  @discardableResult
  mutating func append(_ segment: [Float]) -> Bool {
    guard !segment.isEmpty else { return false }
    samples.append(contentsOf: segment)
    return true
  }
}

enum KokoroPhonemeSegmenter {
  typealias Phonemize = (String) throws -> String
  typealias Tokenize = (String) -> [Int]

  static func segment(
    text: String,
    maxTokenCount: Int,
    phonemize: Phonemize,
    tokenize: Tokenize
  ) throws -> [KokoroPhonemeSegment] {
    let limit = max(1, maxTokenCount)
    let normalizedText = normalizedSynthesisText(text)
    guard !normalizedText.isEmpty else { return [] }

    var segments: [KokoroPhonemeSegment] = []
    var pending = ""

    func measuredSegment(_ candidate: String) throws -> KokoroPhonemeSegment? {
      let clean = candidate.trimmingCharacters(in: .whitespacesAndNewlines)
      guard !clean.isEmpty else { return nil }
      let phonemes = try phonemize(clean)
      return KokoroPhonemeSegment(text: clean, phonemes: phonemes, tokenCount: tokenize(phonemes).count)
    }

    func flushPending() throws {
      guard let segment = try measuredSegment(pending) else {
        pending = ""
        return
      }
      segments.append(segment)
      pending = ""
    }

    func appendUnits(_ units: [String]) throws {
      for unit in units {
        guard let unitSegment = try measuredSegment(unit) else { continue }

        if unitSegment.tokenCount > limit {
          try flushPending()
          segments.append(contentsOf: try splitOversizedUnit(
            unitSegment.text,
            limit: limit,
            phonemize: phonemize,
            tokenize: tokenize
          ))
          continue
        }

        let candidate = pending.isEmpty ? unitSegment.text : "\(pending) \(unitSegment.text)"
        guard let candidateSegment = try measuredSegment(candidate) else { continue }

        if candidateSegment.tokenCount <= limit {
          pending = candidateSegment.text
        } else {
          try flushPending()
          pending = unitSegment.text
        }
      }
    }

    for sentence in sentenceChunks(from: normalizedText) {
      guard let sentenceSegment = try measuredSegment(sentence) else { continue }
      if sentenceSegment.tokenCount > limit {
        try flushPending()
        try appendUnits(wordChunks(from: sentenceSegment.text))
      } else {
        try appendUnits([sentenceSegment.text])
      }
    }

    try flushPending()
    return segments
  }

  /// EPUB text frequently contains non-breaking spacing characters around
  /// ellipses and dialogue punctuation. They are visually indistinguishable
  /// from spaces, but some phonemizers tokenize them differently and can
  /// produce unstable segment boundaries. Normalize only spacing here; the
  /// spoken words and punctuation remain unchanged.
  static func normalizedSynthesisText(_ text: String) -> String {
    text
      .replacingOccurrences(of: "\u{00A0}", with: " ")
      .replacingOccurrences(of: "\u{202F}", with: " ")
      .replacingOccurrences(of: "\u{2007}", with: " ")
      .trimmingCharacters(in: .whitespacesAndNewlines)
  }

  private static func splitOversizedUnit(
    _ text: String,
    limit: Int,
    phonemize: Phonemize,
    tokenize: Tokenize
  ) throws -> [KokoroPhonemeSegment] {
    var segments: [KokoroPhonemeSegment] = []
    var pending = ""

    func measuredSegment(_ candidate: String) throws -> KokoroPhonemeSegment? {
      let clean = candidate.trimmingCharacters(in: .whitespacesAndNewlines)
      guard !clean.isEmpty else { return nil }
      let phonemes = try phonemize(clean)
      return KokoroPhonemeSegment(text: clean, phonemes: phonemes, tokenCount: tokenize(phonemes).count)
    }

    func flushPending() throws {
      guard let segment = try measuredSegment(pending) else {
        pending = ""
        return
      }
      segments.append(segment)
      pending = ""
    }

    for character in text {
      let candidate = pending + String(character)
      guard let candidateSegment = try measuredSegment(candidate) else { continue }
      if candidateSegment.tokenCount <= limit {
        pending = candidateSegment.text
      } else {
        try flushPending()
        pending = String(character)
      }
    }

    try flushPending()
    return segments
  }

  private static func sentenceChunks(from text: String) -> [String] {
    let pattern = #"[^.!?;:]+[.!?;:]+["'”’»)\]]*|[^.!?;:]+$"#
    return regexChunks(pattern: pattern, text: text)
  }

  private static func wordChunks(from text: String) -> [String] {
    regexChunks(pattern: #"[,—-]?\S+\s*"#, text: text)
  }

  private static func regexChunks(pattern: String, text: String) -> [String] {
    guard let regex = try? NSRegularExpression(pattern: pattern) else { return [text] }
    let range = NSRange(text.startIndex..., in: text)
    let chunks = regex.matches(in: text, range: range).compactMap { match -> String? in
      guard let matchRange = Range(match.range, in: text) else { return nil }
      return String(text[matchRange]).trimmingCharacters(in: .whitespacesAndNewlines)
    }
    return chunks.isEmpty ? [text] : chunks
  }
}

/// MisakiSwift-based phonemizer for higher quality G2P.
/// Falls back to ESpeakNGEngine if MisakiSwift is unavailable.
final class MisakiPhonemizer: KokoroPhonemizer {

  enum MisakiPhonemizerError: Error, LocalizedError {
    case notInitialized
    case languageNotSet
    case phonemizationFailed(String)

    var errorDescription: String? {
      switch self {
      case .notInitialized:
        return "Misaki phonemizer not initialized"
      case .languageNotSet:
        return "Language/dialect not set"
      case .phonemizationFailed(let reason):
        return "Phonemization failed: \(reason)"
      }
    }
  }

  /// The underlying MisakiSwift G2P instance (nil if library unavailable)
  private var g2p: AnyObject?

  /// Whether MisakiSwift is available
  private(set) var isMisakiAvailable: Bool = false

  /// Current dialect
  private var _dialect: ESpeakNGEngine.LanguageDialect = .none

  /// Whether current dialect is British
  private var isBritish: Bool { _dialect == .enGB }

  /// Fallback espeak-ng engine
  private let espeakFallback: ESpeakNGEngine?

  var currentDialect: ESpeakNGEngine.LanguageDialect {
    return _dialect
  }

  init(espeakFallback: ESpeakNGEngine? = nil) {
    self.espeakFallback = espeakFallback
    initializeMisaki(british: false)
  }

  // MARK: - MisakiSwift Integration

  /// Initialize MisakiSwift G2P with the given dialect.
  private func initializeMisaki(british: Bool) {
    do {
      let g2pInstance = try MisakiG2PWrapper(british: british)
      // Run Misaki's neural fallback (a small BART model for words outside
      // its dictionary) once now, on whichever device is current. Its setup
      // leaves lazy MLX work tied to that device; if the first unknown word
      // came after the app went to the background, that work would be sent
      // to the GPU, which iOS refuses, and MLX aborts the app (crashes
      // 2026-10-10 06:57 and 06:58).
      _ = try? KokoroMLXErrors.capture { try g2pInstance.phonemize(text: "Zorqavelith") }
      self.g2p = g2pInstance
      self.isMisakiAvailable = true
      AppLogger.kokoro.info("MisakiPhonemizer: Initialized MisakiSwift G2P (british=\(british, privacy: .public))")
    } catch {
      self.g2p = nil
      self.isMisakiAvailable = false
      AppLogger.kokoro.warning("MisakiPhonemizer: MisakiSwift unavailable, using espeak-ng fallback: \(error.localizedDescription, privacy: .public)")
    }
  }

  // MARK: - KokoroPhonemizer Protocol

  func setLanguage(for voice: TTSVoice) throws {
    let dialect = ESpeakNGEngine.Constants.voice2Language[voice] ?? .enUS
    try setLanguage(dialect: dialect)
  }

  func setLanguage(dialect: ESpeakNGEngine.LanguageDialect) throws {
    guard dialect != .none else {
      throw MisakiPhonemizerError.languageNotSet
    }

    let wasBritish = isBritish
    _dialect = dialect

    // Reinitialize Misaki if dialect changed between American and British
    if isBritish != wasBritish {
      initializeMisaki(british: isBritish)
    }

    // Also set the fallback engine's language
    try? espeakFallback?.setLanguage(dialect: dialect)
  }

  func phonemize(text: String) throws -> String {
    guard _dialect != .none else {
      throw MisakiPhonemizerError.languageNotSet
    }

    guard !text.isEmpty else {
      return ""
    }

    // Try MisakiSwift first
    if isMisakiAvailable, let wrapper = g2p as? MisakiG2PWrapper {
      do {
        let phonemes = try wrapper.phonemize(text: text)
        if !phonemes.isEmpty {
          return phonemes
        }
      } catch {
        AppLogger.kokoro.warning("MisakiPhonemizer: Misaki failed, falling back to espeak-ng: \(error.localizedDescription, privacy: .public)")
      }
    }

    // Fall back to espeak-ng
    if let espeak = espeakFallback {
      return try espeak.phonemize(text: text)
    }

    throw MisakiPhonemizerError.notInitialized
  }

  // MARK: - Pronunciation Override Support

  /// Detect whether a pronunciation string is IPA notation.
  /// Returns true if it contains IPA-specific characters or is wrapped in slashes.
  static func isIPAPronunciationPublic(_ text: String) -> Bool {
    return isIPAPronunciation(text)
  }

  /// Internal IPA detection implementation.
  private static func isIPAPronunciation(_ text: String) -> Bool {
    // Wrapped in slashes: /IPA/
    if text.hasPrefix("/") && text.hasSuffix("/") {
      return true
    }

    // Contains IPA-specific Unicode characters
    let ipaIndicators: Set<Swift.Character> = [
      "ə", "ɪ", "ʊ", "ɛ", "ɔ", "æ", "ʌ", "ɑ", "ɒ",  // Vowels
      "ʃ", "ʒ", "θ", "ð", "ŋ", "ɹ", "ɡ",              // Consonants
      "ˈ", "ˌ",                                           // Stress marks
      "ː",                                                 // Length mark
      "ʤ", "ʧ",                                           // Affricates
    ]

    return text.contains(where: { ipaIndicators.contains($0) })
  }
}

// MARK: - MisakiSwift Wrapper

/// Thin wrapper around MisakiSwift's EnglishG2P to isolate the import.
/// This class handles the actual MisakiSwift API calls.
private class MisakiG2PWrapper {
  private let british: Bool

  // The MisakiSwift G2P engine
  private var _engine: Any?

  init(british: Bool) throws {
    self.british = british
    try initializeEngine()
  }

  private func initializeEngine() throws {
    #if canImport(MisakiSwift)
    // Verify MisakiSwift's resource bundle exists before constructing EnglishG2P.
    // EnglishG2P's EnglishFallbackNetwork force-unwraps Bundle.module resource
    // lookups — if the resource bundle is missing, the process crashes instead of
    // throwing. Pre-checking avoids that.
    let misakiBundle = Bundle(for: MisakiSwift.EnglishG2P.self)
    let resourceBundleName = "MisakiSwift_MisakiSwift"
    let resourceBundleURL = misakiBundle.url(forResource: resourceBundleName, withExtension: "bundle")
        ?? Bundle.main.url(forResource: resourceBundleName, withExtension: "bundle")
    guard let bundleURL = resourceBundleURL else {
        AppLogger.kokoro.error("MisakiG2PWrapper: MisakiSwift resource bundle not found")
        throw MisakiPhonemizer.MisakiPhonemizerError.notInitialized
    }
    // Verify critical resources inside the bundle. The fork ships them under
    // "MisakiResources" (older layouts used "Resources" or the bundle root) —
    // checking only "Resources" made this guard reject a perfectly good bundle,
    // which nil'd the phonemizer and silently forced every utterance onto the
    // iOS system voices.
    let resBundle = Bundle(url: bundleURL)
    func hasResource(_ name: String, _ ext: String) -> Bool {
        for sub in ["MisakiResources", "Resources"] {
            if resBundle?.url(forResource: name, withExtension: ext, subdirectory: sub) != nil { return true }
        }
        return resBundle?.url(forResource: name, withExtension: ext) != nil
    }
    let hasGold = hasResource(british ? "gb_gold" : "us_gold", "json")
    let hasBart = hasResource(british ? "gb_bart" : "us_bart", "safetensors")
    guard hasGold && hasBart else {
        AppLogger.kokoro.error("MisakiG2PWrapper: MisakiSwift resources incomplete (gold=\(hasGold, privacy: .public), bart=\(hasBart, privacy: .public))")
        throw MisakiPhonemizer.MisakiPhonemizerError.notInitialized
    }
    AppLogger.kokoro.info("MisakiG2PWrapper: Resources verified")
    _engine = MisakiSwift.EnglishG2P(british: british)
    #else
    throw MisakiPhonemizer.MisakiPhonemizerError.notInitialized
    #endif
  }

  func phonemize(text: String) throws -> String {
    #if canImport(MisakiSwift)
    guard let engine = _engine as? MisakiSwift.EnglishG2P else {
      throw MisakiPhonemizer.MisakiPhonemizerError.notInitialized
    }
    let (phonemes, _) = engine.phonemize(text: text)
    return phonemes
    #else
    throw MisakiPhonemizer.MisakiPhonemizerError.notInitialized
    #endif
  }
}
