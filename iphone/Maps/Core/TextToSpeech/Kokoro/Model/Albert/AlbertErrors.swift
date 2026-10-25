import Foundation
import MLX

enum AlbertWeightError: Error, LocalizedError {
    case missingWeight(String)
    case shapeMismatch(String)

    var errorDescription: String? {
        switch self {
        case .missingWeight(let key):
            return "Missing required Albert model weight: \(key)"
        case .shapeMismatch(let message):
            return "Albert model weight shape mismatch: \(message)"
        }
    }
}

func requiredAlbertWeight(_ weights: [String: MLXArray], key: String) throws -> MLXArray {
    guard let weight = weights[key] else {
        throw AlbertWeightError.missingWeight(key)
    }
    return weight
}
