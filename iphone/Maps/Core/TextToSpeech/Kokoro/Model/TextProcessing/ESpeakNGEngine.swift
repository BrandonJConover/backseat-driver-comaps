//
//  ESpeakNGEngine.swift
//  Backseat Driver links no espeak-ng: this is the stub EpubToM4B uses when ESpeakNG.xcframework
//  is missing. MisakiSwift is the phonemizer; these types only carry its dialect.
//

import Foundation

// ============================================================================
// Stub ESpeakNGEngine when ESpeakNG.xcframework is not linked.
// MisakiPhonemizer (the primary phonemizer) still references these types.
// ============================================================================

public final class ESpeakNGEngine {
  enum ESpeakNGEngineError: Error {
    case dataBundleNotFound
    case couldNotInitialize
    case languageNotFound
    case internalError
    case languageNotSet
    case couldNotPhonemize
  }

  public enum LanguageDialect: String, CaseIterable {
    case none = ""
    case enUS = "en-us"
    case enGB = "en-gb"

    public var displayName: String {
      switch self {
      case .none: return "Default"
      case .enUS: return "American English"
      case .enGB: return "British English"
      }
    }
  }

  private var _dialect: LanguageDialect = .none

  var currentDialect: LanguageDialect {
    return _dialect
  }

  init() throws {
    AppLogger.kokoro.debug("ESpeakNG C library not available — using MisakiSwift as primary phonemizer")
    throw ESpeakNGEngineError.couldNotInitialize
  }

  func setLanguage(for voice: TTSVoice) throws {
    throw ESpeakNGEngineError.couldNotInitialize
  }

  func setLanguage(dialect: LanguageDialect) throws {
    throw ESpeakNGEngineError.couldNotInitialize
  }

  func phonemize(text: String) throws -> String {
    throw ESpeakNGEngineError.couldNotInitialize
  }

  enum Constants {
    static let voice2Language: [TTSVoice: LanguageDialect] = [
      .afHeart: .enUS, .afAlloy: .enUS, .afAoede: .enUS, .afBella: .enUS,
      .afJessica: .enUS, .afKore: .enUS, .afNicole: .enUS, .afNova: .enUS,
      .afRiver: .enUS, .afSarah: .enUS, .afSky: .enUS,
      .amAdam: .enUS, .amEcho: .enUS, .amEric: .enUS, .amFenrir: .enUS,
      .amLiam: .enUS, .amMichael: .enUS, .amOnyx: .enUS, .amPuck: .enUS,
      .amSanta: .enUS,
      .bfAlice: .enGB, .bfEmma: .enGB, .bfIsabella: .enGB, .bfLily: .enGB,
      .bmDaniel: .enGB, .bmFable: .enGB, .bmGeorge: .enGB, .bmLewis: .enGB,
      .hfAlpha: .enUS, .hfBeta: .enUS, .hmOmega: .enUS, .hmPsi: .enUS,
    ]
  }
}

