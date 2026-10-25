//
//  Kokoro-tts-lib
//
import Foundation
import MLX
import MLXNN

class AdaIN1d {
  private let norm: InstanceNorm1d
  private let fc: Linear

  public init(styleDim _: Int, numFeatures: Int, fcWeight: MLXArray, fcBias: MLXArray) {
    norm = InstanceNorm1d(numFeatures: numFeatures, affine: false)
    fc = Linear(weight: fcWeight, bias: fcBias)
  }

  public func callAsFunction(_ x: MLXArray, s: MLXArray) throws -> MLXArray {
    let h = fc(s)
    let hExpanded = h.expandedDimensions(axes: [2])
    let split = hExpanded.split(parts: 2, axis: 1)
    let gamma = split[0]
    let beta = split[1]

    let normalized = try norm(x)
    return (1 + gamma) * normalized + beta
  }

  /// `callAsFunction` for channels-last input ([N, L, C]): instance norm
  /// over L per channel (no affine, no running stats), then style scale and
  /// shift. See `AdaINResBlock1.channelsLast`.
  /// AdaIN of `x` [L, C] (one item) as `x * scale + shift`, per channel:
  /// the same values as `channelsLast` with two passes over `x` instead of
  /// four.
  func scaleShift(_ x: MLXArray, s: MLXArray) -> (scale: MLXArray, shift: MLXArray) {
    let h = fc(s)
    let channels = h.dim(-1) / 2
    let gamma = h[0, 0 ..< channels]
    let beta = h[0, channels...]
    let mean = MLX.mean(x, axes: [0])
    let variance = MLX.variance(x, axes: [0])
    let scale = (1 + gamma) * MLX.rsqrt(variance + norm.eps)
    return (scale, beta - mean * scale)
  }

  func channelsLast(_ x: MLXArray, s: MLXArray) -> MLXArray {
    let h = fc(s)
    let channels = h.dim(-1) / 2
    let gamma = h[0..., 0 ..< channels].expandedDimensions(axis: 1)
    let beta = h[0..., channels...].expandedDimensions(axis: 1)
    let mean = MLX.mean(x, axes: [1], keepDims: true)
    let variance = MLX.variance(x, axes: [1], keepDims: true)
    let normalized = (x - mean) / MLX.sqrt(variance + norm.eps)
    return (1 + gamma) * normalized + beta
  }
}
