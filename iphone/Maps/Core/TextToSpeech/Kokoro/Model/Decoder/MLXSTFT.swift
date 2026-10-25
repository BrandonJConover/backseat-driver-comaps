//
//  Kokoro-tts-lib
//
import Foundation
import MLX
import MLXFFT
import MLXNN

// MARK: - Processing Errors

enum KokoroProcessingError: Error, LocalizedError {
    case unsupportedWindowType(String)
    case invalidWindowType
    case invalidPadMode(String)
    case inputTooShort
    case invalidInputDimensions(String)
    case invalidInterpolationArguments(String)
    case runningStatsUnavailable
    case unsupportedSpatialDimensions(Int)

    var errorDescription: String? {
        switch self {
        case .unsupportedWindowType(let name):
            return "Unsupported window type: \(name). Only 'hann' is supported."
        case .invalidWindowType:
            return "Window must be a String or MLXArray."
        case .invalidPadMode(let mode):
            return "Invalid pad mode: \(mode). Use 'constant' or 'reflect'."
        case .inputTooShort:
            return "Input audio is too short for the requested STFT parameters."
        case .invalidInputDimensions(let msg):
            return msg
        case .invalidInterpolationArguments(let msg):
            return msg
        case .runningStatsUnavailable:
            return "Running statistics not available for InstanceNorm."
        case .unsupportedSpatialDimensions(let dims):
            return "Only 1D interpolation is currently supported; got \(dims)D."
        }
    }
}

// Hanning window implementation to replace np.hanning
func hanning(length: Int) -> MLXArray {
  if length == 1 {
    return MLXArray(1.0)
  }

  let n = MLXArray(Array(stride(from: Float(1 - length), to: Float(length), by: 2.0)))
  let factor = .pi / Float(length - 1)
  return 0.5 + 0.5 * cos(n * factor)
}

// Unwrap implementation to replace np.unwrap
func unwrap(p: MLXArray) -> MLXArray {
  let period: Float = 2.0 * .pi
  let discont: Float = period / 2.0

  let pDiff1 = p[0..., 0 ..< p.shape[1] - 1]
  let pDiff2 = p[0..., 1 ..< p.shape[1]]

  let pDiff = pDiff2 - pDiff1

  let intervalHigh: Float = period / 2.0
  let intervalLow: Float = -intervalHigh

  var pDiffMod = pDiff - intervalLow
  pDiffMod = (((pDiffMod % period) + period) % period) + intervalLow

  let ddSignArray = MLX.where(pDiff .> 0, intervalHigh, pDiffMod)

  pDiffMod = MLX.where(pDiffMod .== intervalLow, ddSignArray, pDiffMod)

  var phCorrect = pDiffMod - pDiff
  phCorrect = MLX.where(abs(pDiff) .< discont, MLXArray(0.0), phCorrect)

  return MLX.concatenated([p[0..., 0 ..< 1], p[0..., 1...] + phCorrect.cumsum(axis: 1)], axis: 1)
}

func mlxStft(
  x: MLXArray,
  nFft: Int = 800,
  hopLength: Int? = nil,
  winLength: Int? = nil,
  window: Any = "hann",
  center: Bool = true,
  padMode: String = "reflect"
) throws -> MLXArray {
  let hopLen = hopLength ?? nFft / 4
  let winLen = winLength ?? nFft

  var w: MLXArray
  if let windowStr = window as? String {
    if windowStr.lowercased() == "hann" {
      w = hanning(length: winLen + 1)[0 ..< winLen]
    } else {
      throw KokoroProcessingError.unsupportedWindowType(windowStr)
    }
  } else if let windowArray = window as? MLXArray {
    w = windowArray
  } else {
    throw KokoroProcessingError.invalidWindowType
  }

  if w.shape[0] < nFft {
    let padSize = nFft - w.shape[0]
    w = MLX.concatenated([w, MLXArray.zeros([padSize])], axis: 0)
  }

  func pad(_ x: MLXArray, padding: Int, padMode: String = "reflect") throws -> MLXArray {
    if padMode == "constant" {
      return MLX.padded(x, width: [padding, padding])
    } else if padMode == "reflect" {
      let prefix = x[1 ..< padding + 1][.stride(by: -1)]
      let suffix = x[-(padding + 1) ..< -1][.stride(by: -1)]
      return MLX.concatenated([prefix, x, suffix])
    } else {
      throw KokoroProcessingError.invalidPadMode(padMode)
    }
  }

  var xArray = x

  if center {
    xArray = try pad(xArray, padding: nFft / 2, padMode: padMode)
  }

  let numFrames = 1 + (xArray.shape[0] - nFft) / hopLen
  if numFrames <= 0 {
    throw KokoroProcessingError.inputTooShort
  }

  let shape: [Int] = [numFrames, nFft]
  let strides: [Int] = [hopLen, 1]

  let frames = MLX.asStrided(xArray, shape, strides: strides)

  let spec = MLXFFT.rfft(frames * w)
  return spec.transposed(1, 0)
}

func mlxIstft(
  x: MLXArray,
  hopLength: Int? = nil,
  winLength: Int? = nil,
  window: Any = "hann"
) throws -> MLXArray {
  let winLen = winLength ?? ((x.shape[1] - 1) * 2)
  let hopLen = hopLength ?? (winLen / 4)

  var w: MLXArray
  if let windowStr = window as? String {
    if windowStr.lowercased() == "hann" {
      w = hanning(length: winLen + 1)[0 ..< winLen]
    } else {
      throw KokoroProcessingError.unsupportedWindowType(windowStr)
    }
  } else if let windowArray = window as? MLXArray {
    w = windowArray
  } else {
    throw KokoroProcessingError.invalidWindowType
  }

  if w.shape[0] < winLen {
    w = MLX.concatenated([w, MLXArray.zeros([winLen - w.shape[0]])], axis: 0)
  }

  let xTransposed = x.transposed(1, 0)
  let t = (xTransposed.shape[0] - 1) * hopLen + winLen
  let windowModLen = 20 / 5

  let wSquared = w * w
  let totalWsquared = MLX.concatenated(Array(repeating: wSquared, count: t / winLen))

  let output = MLXFFT.irfft(xTransposed, axis: 1) * w

  var outputs: [MLXArray] = []
  var windowSums: [MLXArray] = []

  for i in 0 ..< windowModLen {
    let outputStride = output[.stride(from: i, by: windowModLen), .ellipsis].reshaped([-1])
    let windowSumArray = totalWsquared[0 ..< outputStride.shape[0]]

    outputs.append(MLX.concatenated([
      MLXArray.zeros([i * hopLen]),
      outputStride,
      MLXArray.zeros([max(0, t - i * hopLen - outputStride.shape[0])]),
    ]))

    windowSums.append(MLX.concatenated([
      MLXArray.zeros([i * hopLen]),
      windowSumArray,
      MLXArray.zeros([max(0, t - i * hopLen - windowSumArray.shape[0])]),
    ]))
  }

  var reconstructed = outputs[0]
  var windowSum = windowSums[0]
  for i in 1 ..< windowModLen {
    reconstructed += outputs[i]
    windowSum += windowSums[i]
  }

  reconstructed =
    reconstructed[winLen / 2 ..< (reconstructed.shape[0] - winLen / 2)] /
    windowSum[winLen / 2 ..< (reconstructed.shape[0] - winLen / 2)]

  return reconstructed
}

class MLXSTFT {
  let filterLength: Int
  let hopLength: Int
  let winLength: Int
  let window: String

  init(filterLength: Int = 800, hopLength: Int = 200, winLength: Int = 800, window: String = "hann") {
    self.filterLength = filterLength
    self.hopLength = hopLength
    self.winLength = winLength
    self.window = window
  }

  func transform(inputData: MLXArray) throws -> (MLXArray, MLXArray) {
    var audioArray = inputData
    if audioArray.ndim == 1 {
      audioArray = audioArray.expandedDimensions(axis: 0)
    }

    var magnitudes: [MLXArray] = []
    var phases: [MLXArray] = []

    for batchIdx in 0 ..< audioArray.shape[0] {
      // Compute STFT
      let stft = try mlxStft(
        x: audioArray[batchIdx],
        nFft: filterLength,
        hopLength: hopLength,
        winLength: winLength,
        window: window,
        center: true,
        padMode: "reflect"
      )

      let magnitude = MLX.abs(stft)

      // Replaces np.angle()
      let phase = MLX.atan2(stft.imaginaryPart(), stft.realPart())

      magnitudes.append(magnitude)
      phases.append(phase)
    }

    let magnitudesStacked = MLX.stacked(magnitudes, axis: 0)
    let phasesStacked = MLX.stacked(phases, axis: 0)

    return (magnitudesStacked, phasesStacked)
  }

  func inverse(magnitude: MLXArray, phase: MLXArray) throws -> MLXArray {
    var reconstructed: [MLXArray] = []

    for batchIdx in 0 ..< magnitude.shape[0] {
      let phaseCont = unwrap(p: phase[batchIdx])

      // Combine magnitude and phase
      let stft = magnitude[batchIdx] * MLX.exp(MLXArray(real: 0, imaginary: 1) * phaseCont)

      // Inverse STFT
      let audio = try mlxIstft(
        x: stft,
        hopLength: hopLength,
        winLength: winLength,
        window: window
      )
      reconstructed.append(audio)
    }

    let reconstructedStacked = MLX.stacked(reconstructed, axis: 0)
    return reconstructedStacked.expandedDimensions(axis: 1)
  }
}
