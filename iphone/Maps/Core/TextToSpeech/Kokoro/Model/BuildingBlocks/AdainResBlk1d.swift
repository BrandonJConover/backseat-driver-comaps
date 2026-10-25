//
//  Kokoro-tts-lib
//
import Foundation
import MLX
import MLXNN

class AdainResBlk1d {
  let actv: KokoroLeakyReLU
  let dimIn: Int
  let upsampleType: String
  let upsample: UpSample1d
  let learned_sc: Bool
  let pool: Module

  var conv1: ConvWeighted!
  var conv2: ConvWeighted!
  var norm1: AdaIN1d!
  var norm2: AdaIN1d!
  var conv1x1: ConvWeighted?

  init(
    weights: [String: MLXArray],
    weightKeyPrefix: String,
    dimIn: Int,
    dimOut: Int,
    styleDim: Int = 64,
    actv: KokoroLeakyReLU = KokoroLeakyReLU(negativeSlope: 0.2),
    upsample: String = "none"
  ) throws {
    self.actv = actv
    self.dimIn = dimIn
    upsampleType = upsample
    self.upsample = UpSample1d(layerType: upsample)
    learned_sc = dimIn != dimOut

    if upsample == "none" {
      pool = Identity()
    } else {
      pool = ConvWeighted(
        weightG: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".pool.weight_g"),
        weightV: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".pool.weight_v"),
        bias: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".pool.bias"),
        stride: 2,
        padding: 1,
        groups: dimIn
      )
    }

    try buildWeights(weights: weights, weightKeyPrefix: weightKeyPrefix, dimIn: dimIn, dimOut: dimOut, styleDim: styleDim)
  }

  func buildWeights(weights: [String: MLXArray], weightKeyPrefix: String, dimIn: Int, dimOut _: Int, styleDim: Int) throws {
    conv1 = ConvWeighted(
      weightG: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".conv1.weight_g"),
      weightV: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".conv1.weight_v"),
      bias: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".conv1.bias"),
      stride: 1,
      padding: 1
    )

    conv2 = ConvWeighted(
      weightG: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".conv2.weight_g"),
      weightV: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".conv2.weight_v"),
      bias: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".conv2.bias"),
      stride: 1,
      padding: 1
    )

    norm1 = AdaIN1d(
      styleDim: styleDim,
      numFeatures: dimIn,
      fcWeight: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".norm1.fc.weight"),
      fcBias: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".norm1.fc.bias")
    )

    norm2 = AdaIN1d(
      styleDim: styleDim,
      numFeatures: dimIn,
      fcWeight: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".norm2.fc.weight"),
      fcBias: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".norm2.fc.bias")
    )

    if learned_sc {
      conv1x1 = ConvWeighted(
        weightG: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".conv1x1.weight_g"),
        weightV: try requiredDecoderWeight(weights, key: weightKeyPrefix + ".conv1x1.weight_v"),
        bias: nil,
        stride: 1,
        padding: 0
      )
    }
  }

  func shortcut(_ x: MLXArray) -> MLXArray {
    var x = MLX.swappedAxes(x, 2, 1)
    x = upsample(x)
    x = MLX.swappedAxes(x, 2, 1)

    if let conv1x1 = conv1x1 {
      x = MLX.swappedAxes(x, 2, 1)
      x = conv1x1(x, conv: KokoroConv.conv1d)
      x = MLX.swappedAxes(x, 2, 1)
    }

    return x
  }

  func residual(_ x: MLXArray, _ s: MLXArray) throws -> MLXArray {
    var x = try norm1(x, s: s)
    x = actv(x)

    x = MLX.swappedAxes(x, 2, 1)
    if upsampleType != "none" {
      if let idPool = pool as? Identity {
        x = idPool(x)
      } else if let convPool = pool as? ConvWeighted {
        x = convPool(x, conv: KokoroConv.convTransposed1d)
      }
      x = MLX.padded(x, widths: [IntOrPair([0, 0]), IntOrPair([1, 0]), IntOrPair([0, 0])])
    }
    x = MLX.swappedAxes(x, 2, 1)

    x = MLX.swappedAxes(x, 2, 1)
    x = conv1(x, conv: KokoroConv.conv1d)
    x = MLX.swappedAxes(x, 2, 1)

    x = try norm2(x, s: s)
    x = actv(x)

    x = MLX.swappedAxes(x, 2, 1)
    x = conv2(x, conv: KokoroConv.conv1d)
    x = MLX.swappedAxes(x, 2, 1)

    return x
  }

  func callAsFunction(x: MLXArray, s: MLXArray) throws -> MLXArray {
    let out = try residual(x, s)
    let result = (out + shortcut(x)) / sqrt(2.0)
    return result
  }
}

// MARK: - Activations that run on the CPU

/// MLXNN's `gelu` and `LeakyReLU` are `compile(shapeless:)` functions. MLX on
/// iOS has no CPU compiler (mlx-swift leaves out backend/cpu/compiled.cpp:
/// "[Compiled::eval_cpu] CPU compilation not supported on the platform"), so
/// any compiled function fails on the CPU — and the CPU is the only device
/// Kokoro may use in the background. That failure killed live playback
/// (2026-10-09) and stopped background narration after each sentence.
/// These are the same formulas as plain operations, which run on the GPU
/// and the CPU alike.
enum KokoroActivations {
    /// Exact GELU, as MLXNN.gelu: x · (1 + erf(x / √2)) / 2.
    static func gelu(_ x: MLXArray) -> MLXArray {
        x * (1 + MLX.erf(x / Float(2).squareRoot())) / 2
    }

    /// As MLXNN.leakyRelu: max(slope · x, x).
    static func leakyRelu(_ x: MLXArray, negativeSlope: Float) -> MLXArray {
        MLX.maximum(negativeSlope * x, x)
    }
}

/// LeakyReLU without `compile` (see `KokoroActivations`).
final class KokoroLeakyReLU: Module, UnaryLayer {
    let negativeSlope: Float

    init(negativeSlope: Float = 0.01) {
        self.negativeSlope = negativeSlope
        super.init()
    }

    func callAsFunction(_ x: MLXArray) -> MLXArray {
        KokoroActivations.leakyRelu(x, negativeSlope: negativeSlope)
    }
}

