//
//  Kokoro-tts-lib
//
import Foundation
import MLX
import MLXNN

class AdaINResBlock1 {
  var convs1: [ConvWeighted] = []
  var convs2: [ConvWeighted] = []
  var adain1: [AdaIN1d] = []
  var adain2: [AdaIN1d] = []
  var alpha1: [MLXArray] = []
  var alpha2: [MLXArray] = []

  private func getPadding(kernelSize: Int, dilation: Int = 1) -> Int {
    return Int((kernelSize * dilation - dilation) / 2)
  }

  init(
    weights: [String: MLXArray],
    weightPrefixKey: String,
    channels: Int,
    kernelSize: Int = 3,
    dilation: [Int] = [1, 3, 5],
    styleDim: Int = 64
  ) throws {
    for i in 0 ..< 3 {
      let dilationValue = dilation[i]
      let conv = ConvWeighted(
        weightG: try requiredDecoderWeight(weights, key: weightPrefixKey + ".convs1.\(i).weight_g"),
        weightV: try requiredDecoderWeight(weights, key: weightPrefixKey + ".convs1.\(i).weight_v"),
        bias: try requiredDecoderWeight(weights, key: weightPrefixKey + ".convs1.\(i).bias"),
        stride: 1,
        padding: getPadding(kernelSize: kernelSize, dilation: dilationValue),
        dilation: dilationValue
      )
      convs1.append(conv)
    }

    for i in 0 ..< convs1.count {
      let conv = ConvWeighted(
        weightG: try requiredDecoderWeight(weights, key: weightPrefixKey + ".convs2.\(i).weight_g"),
        weightV: try requiredDecoderWeight(weights, key: weightPrefixKey + ".convs2.\(i).weight_v"),
        bias: try requiredDecoderWeight(weights, key: weightPrefixKey + ".convs2.\(i).bias"),
        stride: 1,
        padding: getPadding(kernelSize: kernelSize, dilation: 1),
        dilation: 1
      )
      convs2.append(conv)
    }

    for i in 0 ..< convs1.count {
      adain1.append(AdaIN1d(
        styleDim: styleDim,
        numFeatures: channels,
        fcWeight: try requiredDecoderWeight(weights, key: weightPrefixKey + ".adain1.\(i).fc.weight"),
        fcBias: try requiredDecoderWeight(weights, key: weightPrefixKey + ".adain1.\(i).fc.bias")
      ))

      adain2.append(AdaIN1d(
        styleDim: styleDim,
        numFeatures: channels,
        fcWeight: try requiredDecoderWeight(weights, key: weightPrefixKey + ".adain2.\(i).fc.weight"),
        fcBias: try requiredDecoderWeight(weights, key: weightPrefixKey + ".adain2.\(i).fc.bias")
      ))
    }

    for i in 0 ..< convs1.count {
      alpha1.append(try requiredDecoderWeight(weights, key: weightPrefixKey + ".alpha1.\(i)"))
      alpha2.append(try requiredDecoderWeight(weights, key: weightPrefixKey + ".alpha2.\(i)"))
    }
  }

  func callAsFunction(_ x: MLXArray, _ s: MLXArray) throws -> MLXArray {
    if Device.defaultDevice() == .cpu {
      if Self.useFusedCPUPath, let fused = fusedChannelsLast(x, s) { return fused }
      return channelsLast(x, s)
    }
    var result = x

    for i in 0 ..< convs1.count {
      let c1 = convs1[i]
      let c2 = convs2[i]
      let n1 = adain1[i]
      let n2 = adain2[i]
      let a1 = alpha1[i]
      let a2 = alpha2[i]

      var xt = try n1(result, s: s)
      xt = xt + (1 / a1) * MLX.square(MLX.sin(a1 * xt))

      xt = MLX.swappedAxes(xt, 2, 1)
      xt = c1(xt, conv: KokoroConv.conv1d)
      xt = MLX.swappedAxes(xt, 2, 1)

      xt = try n2(xt, s: s)
      xt = xt + (1 / a2) * MLX.square(MLX.sin(a2 * xt))

      xt = MLX.swappedAxes(xt, 2, 1)
      xt = c2(xt, conv: KokoroConv.conv1d)
      xt = MLX.swappedAxes(xt, 2, 1)

      result = xt + result
    }
    return result
  }

  /// The same block for the CPU, kept channels-last ([N, L, C], what the
  /// convolutions take) from start to finish: one transpose in and one out
  /// instead of four per step, so the norms and activations run over
  /// contiguous memory. With `square` instead of `pow(2)` (MLX's CPU pow is
  /// a per-element libm call), 680 → 254 ms per block at Kokoro's last
  /// upsample stage in MLX 0.31.1, same values to 3e-4 (2026-10-10). This
  /// stage was ~70% of background synthesis time.
  private func channelsLast(_ x: MLXArray, _ s: MLXArray) -> MLXArray {
    var result = MLX.swappedAxes(x, 2, 1)
    for i in 0 ..< convs1.count {
      let a1 = MLX.swappedAxes(alpha1[i], 1, 2)
      let a2 = MLX.swappedAxes(alpha2[i], 1, 2)

      var xt = adain1[i].channelsLast(result, s: s)
      xt = xt + (1 / a1) * MLX.square(MLX.sin(a1 * xt))
      xt = convs1[i](xt, conv: KokoroConv.conv1d)

      xt = adain2[i].channelsLast(xt, s: s)
      xt = xt + (1 / a2) * MLX.square(MLX.sin(a2 * xt))
      xt = convs2[i](xt, conv: KokoroConv.conv1d)

      result = xt + result
    }
    return MLX.swappedAxes(result, 2, 1)
  }

  /// Diagnostic switch (launch with `-KokoroUnfusedResBlock YES`) to compare
  /// the fused CPU block with the plain one on a device.
  static let useFusedCPUPath = !UserDefaults.standard.bool(forKey: "KokoroUnfusedResBlock")

  /// Kernels with Snake's 1/alpha folded into their input channels, made
  /// once (CPU path only).
  private var foldedKernels: [(first: MLXArray, second: MLXArray, secondBias: MLXArray?)]?

  private func kernelsWithAlphaFolded() -> [(first: MLXArray, second: MLXArray, secondBias: MLXArray?)] {
    if let foldedKernels { return foldedKernels }
    var made: [(first: MLXArray, second: MLXArray, secondBias: MLXArray?)] = []
    for i in 0 ..< convs1.count {
      let first = convs1[i].kernelAndBias().weight * (1 / alpha1[i]).reshaped([1, 1, -1])
      let (secondWeight, secondBias) = convs2[i].kernelAndBias()
      let second = secondWeight * (1 / alpha2[i]).reshaped([1, 1, -1])
      MLX.eval(first, second)
      made.append((first, second, secondBias))
    }
    foldedKernels = made
    return made
  }

  /// `channelsLast` with fewer passes over memory (2026-10-10). On the CPU
  /// MLX can't fuse element-wise operations, so each one is a full pass over
  /// [L, C]; at the last upsample stage half the block's time was AdaIN and
  /// Snake, not the convolutions. Same values (to ~1e-5), 1.3–1.4× less CPU
  /// in MLX 0.31.1 on the Mac:
  /// - AdaIN is `x * scale + shift` with per-channel scale and shift.
  /// - Snake `y + sin²(αy)/α` is computed on `u = αy` as `u + sin²(u)`; the
  ///   1/α goes into the next convolution's kernel.
  /// - The first convolution's bias is dropped: the AdaIN after it removes
  ///   any per-channel constant.
  /// - Kernel taps and the residual are summed inside `addMM`.
  /// nil for a batch of more than one (the plain path handles it).
  private func fusedChannelsLast(_ x: MLXArray, _ s: MLXArray) -> MLXArray? {
    guard x.ndim == 3, x.dim(0) == 1 else { return nil }
    let kernels = kernelsWithAlphaFolded()
    // [L, C]. Not `swappedAxes(...)[0]`: MLX 0.31.1's CPU path returns wrong
    // data for an integer index into a size-1 axis of a transposed array
    // (the DurationEncoder bug; on device this block came out 3.5x quieter).
    let channelsLastInput = MLX.swappedAxes(x, 2, 1)
    var result = channelsLastInput[0 ..< 1].reshaped([channelsLastInput.dim(1), channelsLastInput.dim(2)])
    for i in 0 ..< convs1.count {
      let a1 = alpha1[i].reshaped([-1])
      let a2 = alpha2[i].reshaped([-1])

      let (scale1, shift1) = adain1[i].scaleShift(result, s: s)
      var u = result * (a1 * scale1) + a1 * shift1
      let hidden = KokoroConv.accumulated(u + MLX.square(MLX.sin(u)), kernels[i].first,
                                          padding: convs1[i].padding, dilation: convs1[i].dilation,
                                          initial: nil)

      let (scale2, shift2) = adain2[i].scaleShift(hidden, s: s)
      u = hidden * (a2 * scale2) + a2 * shift2
      let residual = kernels[i].secondBias.map { result + $0 } ?? result
      result = KokoroConv.accumulated(u + MLX.square(MLX.sin(u)), kernels[i].second,
                                      padding: convs2[i].padding, dilation: convs2[i].dilation,
                                      initial: residual)
    }
    return MLX.swappedAxes(result.expandedDimensions(axis: 0), 2, 1)
  }
}
