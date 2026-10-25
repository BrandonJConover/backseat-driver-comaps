// Copyright 2025-2026 Brandon J Conover. Apache-2.0 license.
//
//  DecoderErrors.swift
//  AudiobookStudioApp
//
//  Shared error type and safe weight accessor for Kokoro decoder building blocks.
//  Matches the AlbertErrors.swift pattern used in the Albert module.

import Foundation
import MLX

enum DecoderWeightError: Error, LocalizedError {
    case missingWeight(String)

    var errorDescription: String? {
        switch self {
        case .missingWeight(let key):
            return "Missing required Kokoro decoder weight: \(key)"
        }
    }
}

/// Safely retrieves a required weight from the weight dictionary.
/// Throws `DecoderWeightError.missingWeight` instead of force-unwrapping.
func requiredDecoderWeight(_ weights: [String: MLXArray], key: String) throws -> MLXArray {
    guard let weight = weights[key] else {
        throw DecoderWeightError.missingWeight(key)
    }
    return weight
}
