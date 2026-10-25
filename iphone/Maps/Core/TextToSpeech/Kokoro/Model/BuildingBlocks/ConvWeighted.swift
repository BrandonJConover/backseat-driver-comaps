//
//  Kokoro-tts-lib
//
import Foundation
import MLX
import MLXNN

func computeNorm(
  x: MLXArray,
  p: Int,
  dim: [Int]? = nil,
  keepdim: Bool = false
) -> MLXArray {
  guard p == 1 || p == 2 else {
    // Unsupported p-norm. In current usage computeNorm is always called with p=2,
    // so this path is unreachable. Fall back to L2 norm instead of crashing.
    assertionFailure("computeNorm: unsupported p=\(p), falling back to L2 norm")
    let allAxes = Array(0 ..< x.ndim)
    return MLX.sqrt(MLX.sum(x * x, axes: allAxes, keepDims: keepdim))
  }

  let dimensions: [Int]
  if let dim = dim {
    dimensions = dim
  } else {
    dimensions = Array(0 ..< x.ndim)
  }

  if p == 1 {
    // L1 norm
    return MLX.sum(MLX.abs(x), axes: dimensions, keepDims: keepdim)
  } else {
    // L2 norm
    return MLX.sqrt(MLX.sum(x * x, axes: dimensions, keepDims: keepdim))
  }
}

func weightNorm(
  weightV: MLXArray,
  weightG: MLXArray,
  dim: Int? = nil
) -> MLXArray {
  let rank = weightV.shape.count

  var axes: [Int]

  if let dim = dim {
    var adjustedDim = dim
    if dim < 0 {
      adjustedDim += rank
    }

    axes = Array(0 ..< rank)
    if adjustedDim != -1 {
      axes.removeAll(where: { $0 == adjustedDim })
    }
  } else {
    axes = Array(0 ..< rank)
  }

  let normV = computeNorm(x: weightV, p: 2, dim: axes, keepdim: true)

  // Add epsilon for numerical stability
  let normalizedWeight = weightV / (normV + 1e-7)
  return normalizedWeight * weightG
}

/// Conv1d with weight normalization
class ConvWeighted: Module {
  var weightG: MLXArray
  var weightV: MLXArray
  var bias: MLXArray?

  let stride: Int
  let padding: Int
  let dilation: Int
  let outputPadding: Int
  let groups: Int

  init(
    weightG: MLXArray,
    weightV: MLXArray,
    bias: MLXArray?,
    stride: Int = 1,
    padding: Int = 1,
    dilation: Int = 1,
    outputPadding: Int = 0,
    groups: Int = 1
  ) {
    self.stride = stride
    self.padding = padding
    self.dilation = dilation
    self.outputPadding = outputPadding
    self.groups = groups

    self.weightG = weightG
    self.weightV = weightV
    self.bias = bias

    super.init()
  }
  
  /// The weight-normalized kernel, computed once. It was rebuilt from
  /// `weightV`/`weightG` on every call — a pass over every decoder weight
  /// per sentence, cheap on the GPU but most of the ~0.4 s each sentence
  /// cost on the CPU before its length counted (device timings 2026-10-10).
  /// The weights never change, so the result is kept, computed (no pending
  /// work for the background), and the two source arrays are released so
  /// memory doesn't grow.
  private var normalizedWeight: MLXArray?

  private func effectiveWeight() -> MLXArray {
    if let normalizedWeight { return normalizedWeight }
    let weight = weightNorm(weightV: weightV, weightG: weightG, dim: 0)
    weight.eval()
    normalizedWeight = weight
    weightV = MLXArray(Float(0))
    weightG = MLXArray(Float(0))
    return weight
  }

  /// The normalized kernel ([out, taps, in], MLX layout) and the bias, for
  /// callers that fold a per-channel scale into the kernel
  /// (`AdaINResBlock1.fusedChannelsLast`).
  func kernelAndBias() -> (weight: MLXArray, bias: MLXArray?) {
    (effectiveWeight(), bias?.reshaped([-1]))
  }

  public func callAsFunction(_ x: MLXArray, conv: (MLXArray, MLXArray, Int, Int, Int, Int, StreamOrDevice) -> MLXArray) -> MLXArray {
    let weight = effectiveWeight()
    bias = bias?.reshaped([1, 1, -1])

    func applyConv(x: MLXArray, weightToUse: MLXArray) -> MLXArray {
      let result = conv(
        x,
        weightToUse,
        self.stride,
        padding,
        dilation,
        groups,
        .default
      )

      if let bias = bias {
        return result + bias
      }
      return result
    }

    if x.shape.last == weight.shape.last || groups > 1 {
      return applyConv(x: x, weightToUse: weight)
    } else {
      return applyConv(x: x, weightToUse: weight.transposed())
    }
  }
  
  public func callAsFunction(_ x: MLXArray, conv: (MLXArray, MLXArray, Int, Int, Int, Int, Int, StreamOrDevice) -> MLXArray) -> MLXArray {
    let weight = effectiveWeight()
    bias = bias?.reshaped([1, 1, -1])

    func applyConv(x: MLXArray, weightToUse: MLXArray) -> MLXArray {
      let result = conv(
        x,
        weightToUse,
        self.stride,
        padding,
        dilation,
        outputPadding,
        groups,
        .default
      )

      if let bias = bias {
        return result + bias
      }
      return result
    }

    if x.shape.last == weight.shape.last || groups > 1 {
      return applyConv(x: x, weightToUse: weight)
    } else {
      return applyConv(x: x, weightToUse: weight.transposed())
    }
  }
}

/// 1-D convolutions that stay fast on the CPU.
///
/// MLX's CPU backend sends dilated, transposed (input-dilated and flipped)
/// and grouped 1-D convolutions to a naive loop; only plain ones use a
/// matrix multiply. Kokoro's decoder is built from exactly those, which made
/// background (CPU) synthesis ~6× slower than real time (2026-10-10). On the
/// CPU these run as K shifted matrix multiplies (or elementwise products for
/// depthwise); measured with MLX at Kokoro's decoder shapes: dilated 330–400×
/// faster, transposed ~45×, results equal to 1e-6. The GPU path is unchanged.
enum KokoroConv {
  private static var onCPU: Bool { Device.defaultDevice() == .cpu }

  static func conv1d(_ x: MLXArray, _ w: MLXArray, stride: Int, padding: Int, dilation: Int,
                     groups: Int, stream: StreamOrDevice) -> MLXArray {
    if onCPU, dilation != 1 || groups != 1,
       let y = shiftedMatmul(x, w, padLow: padding, padHigh: padding, stride: stride,
                             dilation: dilation, groups: groups) {
      return y
    }
    return MLX.conv1d(x, w, stride: stride, padding: padding, dilation: dilation, groups: groups, stream: stream)
  }

  static func convTransposed1d(_ x: MLXArray, _ w: MLXArray, stride: Int, padding: Int, dilation: Int,
                               outputPadding: Int, groups: Int, stream: StreamOrDevice) -> MLXArray {
    if onCPU, let y = transposedAsConv(x, w, stride: stride, padding: padding, dilation: dilation,
                                       outputPadding: outputPadding, groups: groups) {
      return y
    }
    return MLX.convTransposed1d(x, w, stride: stride, padding: padding, dilation: dilation,
                                outputPadding: outputPadding, groups: groups, stream: stream)
  }

  /// Correlation like `MLX.conv1d` (x [N, L, C], w [O, K, C/groups]) as a
  /// sum over kernel taps. nil for shapes it doesn't cover (MLX handles them).
  static func shiftedMatmul(_ x: MLXArray, _ w: MLXArray, padLow: Int, padHigh: Int, stride: Int,
                            dilation: Int, groups: Int) -> MLXArray? {
    guard stride == 1, x.ndim == 3, w.ndim == 3, padLow >= 0, padHigh >= 0 else { return nil }
    let channels = x.dim(2)
    let (outChannels, taps, groupChannels) = (w.dim(0), w.dim(1), w.dim(2))
    let depthwise = groups == channels && groupChannels == 1 && outChannels == channels
    guard (groups == 1 && groupChannels == channels) || depthwise else { return nil }
    let xp = padLow > 0 || padHigh > 0
      ? MLX.padded(x, widths: [IntOrPair(0), IntOrPair([padLow, padHigh]), IntOrPair(0)])
      : x
    let length = xp.dim(1) - dilation * (taps - 1)
    guard length > 0 else { return nil }
    var y: MLXArray?
    for tap in 0 ..< taps {
      let start = tap * dilation
      let window = xp[0..., start ..< start + length, 0...]
      let term = depthwise
        ? window * w[0..., tap, 0]
        : MLX.matmul(window, w[0..., tap, 0...].transposed())
      y = y.map { $0 + term } ?? term
    }
    return y
  }

  /// Correlation of `x` [L, C] with `w` [O, K, C] (stride 1), each tap
  /// added inside the matrix multiply (`addMM`) instead of as a separate
  /// pass over the output, starting from `initial` (e.g. the residual) when
  /// given.
  static func accumulated(_ x: MLXArray, _ w: MLXArray, padding: Int, dilation: Int,
                          initial: MLXArray?) -> MLXArray {
    let xp = padding > 0
      ? MLX.padded(x, widths: [IntOrPair([padding, padding]), IntOrPair(0)])
      : x
    let taps = w.dim(1)
    let length = xp.dim(0) - dilation * (taps - 1)
    var y = initial
    for tap in 0 ..< taps {
      let start = tap * dilation
      let window = xp[start ..< start + length, 0...]
      let kernel = w[0..., tap, 0...].transposed()
      y = y.map { MLX.addMM($0, window, kernel) } ?? MLX.matmul(window, kernel)
    }
    return y!
  }

  /// `MLX.convTransposed1d` as a correlation over the zero-stuffed input with
  /// the kernel flipped — what MLX itself does, minus the slow CPU loop.
  static func transposedAsConv(_ x: MLXArray, _ w: MLXArray, stride: Int, padding: Int, dilation: Int,
                               outputPadding: Int, groups: Int) -> MLXArray? {
    guard x.ndim == 3, w.ndim == 3, stride >= 1 else { return nil }
    let (batch, length, channels) = (x.dim(0), x.dim(1), x.dim(2))
    let taps = w.dim(1)
    var stuffed = x
    if stride > 1 {
      let zeros = MLXArray.zeros([batch, length, stride - 1, channels], dtype: x.dtype)
      stuffed = MLX.concatenated([x.expandedDimensions(axis: 2), zeros], axis: 2)
        .reshaped([batch, length * stride, channels])[0..., 0 ..< (length - 1) * stride + 1, 0...]
    }
    let reversed = MLXArray((0 ..< taps).reversed().map { Int32($0) })
    let flipped = w.take(reversed, axis: 1)
    let low = dilation * (taps - 1) - padding
    guard low >= 0 else { return nil }
    return shiftedMatmul(stuffed, flipped, padLow: low, padHigh: low + outputPadding, stride: 1,
                         dilation: dilation, groups: groups)
  }
}

