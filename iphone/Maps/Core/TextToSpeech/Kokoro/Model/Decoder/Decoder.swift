//
//  Kokoro-tts-lib
//
import Foundation
import MLX
import MLXNN

class KokoroDecoder {
  private let encode: AdainResBlk1d
  private var decode: [AdainResBlk1d] = []
  private let F0Conv: ConvWeighted
  private let NConv: ConvWeighted
  private let asrRes: [ConvWeighted]
  private let generator: Generator

  init(
    weights: [String: MLXArray],
    dimIn: Int,
    styleDim: Int,
    dimOut _: Int,
    resblockKernelSizes: [Int],
    upsampleRates: [Int],
    upsampleInitialChannel: Int,
    resblockDilationSizes: [[Int]],
    upsampleKernelSizes: [Int],
    genIstftNFft: Int,
    genIstftHopSize: Int
  ) throws {
    encode = try AdainResBlk1d(weights: weights, weightKeyPrefix: "decoder.encode", dimIn: dimIn + 2, dimOut: 1024, styleDim: styleDim)

    decode.append(try AdainResBlk1d(weights: weights, weightKeyPrefix: "decoder.decode.0", dimIn: 1024 + 2 + 64, dimOut: 1024, styleDim: styleDim))
    decode.append(try AdainResBlk1d(weights: weights, weightKeyPrefix: "decoder.decode.1", dimIn: 1024 + 2 + 64, dimOut: 1024, styleDim: styleDim))
    decode.append(try AdainResBlk1d(weights: weights, weightKeyPrefix: "decoder.decode.2", dimIn: 1024 + 2 + 64, dimOut: 1024, styleDim: styleDim))
    decode.append(try AdainResBlk1d(weights: weights, weightKeyPrefix: "decoder.decode.3", dimIn: 1024 + 2 + 64, dimOut: 512, styleDim: styleDim, upsample: "true"))

    F0Conv = ConvWeighted(
      weightG: try requiredDecoderWeight(weights, key: "decoder.F0_conv.weight_g"),
      weightV: try requiredDecoderWeight(weights, key: "decoder.F0_conv.weight_v"),
      bias: try requiredDecoderWeight(weights, key: "decoder.F0_conv.bias"),
      stride: 2,
      padding: 1,
      groups: 1
    )
    NConv = ConvWeighted(
      weightG: try requiredDecoderWeight(weights, key: "decoder.N_conv.weight_g"),
      weightV: try requiredDecoderWeight(weights, key: "decoder.N_conv.weight_v"),
      bias: try requiredDecoderWeight(weights, key: "decoder.N_conv.bias"),
      stride: 2,
      padding: 1,
      groups: 1
    )

    asrRes = [ConvWeighted(
      weightG: try requiredDecoderWeight(weights, key: "decoder.asr_res.0.weight_g"),
      weightV: try requiredDecoderWeight(weights, key: "decoder.asr_res.0.weight_v"),
      bias: try requiredDecoderWeight(weights, key: "decoder.asr_res.0.bias"),
      padding: 0
    )]

    generator = try Generator(
      weights: weights,
      styleDim: styleDim,
      resblockKernelSizes: resblockKernelSizes,
      upsampleRates: upsampleRates,
      upsampleInitialChannel: upsampleInitialChannel,
      resblockDilationSizes: resblockDilationSizes,
      upsampleKernelSizes: upsampleKernelSizes,
      genIstftNFft: genIstftNFft,
      genIstftHopSize: genIstftHopSize
    )
  }

  func callAsFunction(asr: MLXArray, F0Curve: MLXArray, N: MLXArray, s: MLXArray) throws -> MLXArray {
    let F0CurveSwapped = MLX.swappedAxes(F0Curve.reshaped([F0Curve.shape[0], 1, F0Curve.shape[1]]), 2, 1)
    let F0 = MLX.swappedAxes(F0Conv(F0CurveSwapped, conv: KokoroConv.conv1d), 2, 1)

    let NSwapped = MLX.swappedAxes(N.reshaped([N.shape[0], 1, N.shape[1]]), 2, 1)
    let NProcessed = MLX.swappedAxes(NConv(NSwapped, conv: KokoroConv.conv1d), 2, 1)

    KokoroStageClock.mark("dec.f0n", [F0, NProcessed])
    var x = MLX.concatenated([asr, F0, NProcessed], axis: 1)
    x = try encode(x: x, s: s)
    KokoroStageClock.mark("dec.encode", [x])

    let asrResidual = MLX.swappedAxes(asrRes[0](MLX.swappedAxes(asr, 2, 1), conv: KokoroConv.conv1d), 2, 1)
    var res = true

    for block in decode {
      if res {
        x = MLX.concatenated([x, asrResidual, F0, NProcessed], axis: 1)
      }
      x = try block(x: x, s: s)

      if block.upsampleType != "none" {
        res = false
      }
    }

    KokoroStageClock.mark("dec.decode", [x])
    return try generator(x, s, F0Curve)
  }
}
