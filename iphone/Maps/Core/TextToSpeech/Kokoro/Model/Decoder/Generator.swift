//
//  Kokoro-tts-lib
//
import Foundation
import MLX
import MLXNN

class Generator {
  let numKernels: Int
  let numUpsamples: Int
  let mSource: SourceModuleHnNSF
  let f0Upsample: Upsample
  let postNFFt: Int
  var noiseConvs: [Conv1dInference]
  var noiseRes: [AdaINResBlock1]
  var ups: [ConvWeighted]
  var resBlocks: [AdaINResBlock1]
  let convPost: ConvWeighted
  let reflectionPad: ReflectionPad1d
  let stft: MLXSTFT

  init(weights: [String: MLXArray],
       styleDim: Int,
       resblockKernelSizes: [Int],
       upsampleRates: [Int],
       upsampleInitialChannel: Int,
       resblockDilationSizes: [[Int]],
       upsampleKernelSizes: [Int],
       genIstftNFft: Int,
       genIstftHopSize: Int) throws
  {
    numKernels = resblockKernelSizes.count
    numUpsamples = upsampleRates.count

    let upsampleScaleNum = MLX.product(MLXArray(upsampleRates)) * genIstftHopSize
    let upsampleScaleNumVal: Int = upsampleScaleNum.item()

    mSource = try SourceModuleHnNSF(
      weights: weights,
      samplingRate: KokoroTTS.Constants.samplingRate,
      upsampleScale: upsampleScaleNum.item(),
      harmonicNum: 8,
      voicedThreshold: 10
    )

    f0Upsample = Upsample(scaleFactor: .float(Float(upsampleScaleNumVal)))

    noiseConvs = []
    noiseRes = []
    ups = []

    for (i, (u, k)) in zip(upsampleRates, upsampleKernelSizes).enumerated() {
      ups.append(
        ConvWeighted(
          weightG: try requiredDecoderWeight(weights, key: "decoder.generator.ups.\(i).weight_g"),
          weightV: try requiredDecoderWeight(weights, key: "decoder.generator.ups.\(i).weight_v"),
          bias: try requiredDecoderWeight(weights, key: "decoder.generator.ups.\(i).bias"),
          stride: u,
          padding: (k - u) / 2
        )
      )
    }

    resBlocks = []
    for i in 0 ..< ups.count {
      let ch = upsampleInitialChannel / Int(pow(2.0, Double(i + 1)))
      for (j, (k, d)) in zip(resblockKernelSizes, resblockDilationSizes).enumerated() {
        resBlocks.append(
          try AdaINResBlock1(
            weights: weights,
            weightPrefixKey: "decoder.generator.resblocks.\((i * resblockKernelSizes.count) + j)",
            channels: ch,
            kernelSize: k,
            dilation: d,
            styleDim: styleDim
          )
        )
      }

      let cCur = ch
      if i + 1 < upsampleRates.count {
        let strideF0: Int = MLX.product(MLXArray(upsampleRates)[(i + 1)...]).item()
        noiseConvs.append(
          Conv1dInference(
            inputChannels: genIstftNFft + 2,
            outputChannels: cCur,
            kernelSize: strideF0 * 2,
            stride: strideF0,
            padding: (strideF0 + 1) / 2,
            weight: try requiredDecoderWeight(weights, key: "decoder.generator.noise_convs.\(i).weight"),
            bias: try requiredDecoderWeight(weights, key: "decoder.generator.noise_convs.\(i).bias")
          )
        )

        noiseRes.append(
          try AdaINResBlock1(
            weights: weights,
            weightPrefixKey: "decoder.generator.noise_res.\(i)",
            channels: cCur,
            kernelSize: 7,
            dilation: [1, 3, 5],
            styleDim: styleDim
          )
        )
      } else {
        noiseConvs.append(
          Conv1dInference(
            inputChannels: genIstftNFft + 2,
            outputChannels: cCur,
            kernelSize: 1,
            weight: try requiredDecoderWeight(weights, key: "decoder.generator.noise_convs.\(i).weight"),
            bias: try requiredDecoderWeight(weights, key: "decoder.generator.noise_convs.\(i).bias")
          )
        )
        noiseRes.append(
          try AdaINResBlock1(
            weights: weights,
            weightPrefixKey: "decoder.generator.noise_res.\(i)",
            channels: cCur,
            kernelSize: 11,
            dilation: [1, 3, 5],
            styleDim: styleDim
          )
        )
      }
    }

    postNFFt = genIstftNFft

    convPost = ConvWeighted(
      weightG: try requiredDecoderWeight(weights, key: "decoder.generator.conv_post.weight_g"),
      weightV: try requiredDecoderWeight(weights, key: "decoder.generator.conv_post.weight_v"),
      bias: try requiredDecoderWeight(weights, key: "decoder.generator.conv_post.bias"),
      stride: 1,
      padding: 3
    )

    reflectionPad = ReflectionPad1d(padding: (1, 0))

    stft = MLXSTFT(
      filterLength: genIstftNFft,
      hopLength: genIstftHopSize,
      winLength: genIstftNFft
    )
  }

  func callAsFunction(_ x: MLXArray, _ s: MLXArray, _ F0Curve: MLXArray) throws -> MLXArray {
    var f0New = F0Curve[.newAxis, 0..., 0...].transposed(0, 2, 1)
    f0New = f0Upsample(f0New)
    KokoroStageClock.mark("gen.f0up", [f0New])

    var (harSource, _, _) = try mSource(f0New)
    KokoroStageClock.mark("gen.source", [harSource])

    harSource = MLX.squeezed(harSource.transposed(0, 2, 1), axis: 1)
    let (harSpec, harPhase) = try stft.transform(inputData: harSource)
    
    var har = MLX.concatenated([harSpec, harPhase], axis: 1)
    har = MLX.swappedAxes(har, 2, 1)
    KokoroStageClock.mark("gen.source+stft", [har])
        
    var newX = x
    for i in 0 ..< numUpsamples {
      newX = KokoroLeakyReLU(negativeSlope: 0.1)(newX)
      var xSource = noiseConvs[i](har)
      xSource = MLX.swappedAxes(xSource, 2, 1)
      xSource = try noiseRes[i](xSource, s)
      KokoroStageClock.mark("gen.noise\(i)", [xSource])

      newX = MLX.swappedAxes(newX, 2, 1)
      newX = ups[i](newX, conv: KokoroConv.convTransposed1d)
      KokoroStageClock.mark("gen.ups\(i)", [newX])
      newX = MLX.swappedAxes(newX, 2, 1)

      if i == numUpsamples - 1 {
        newX = reflectionPad(newX)
      }
      newX = newX + xSource
      
      var xs: MLXArray?
      for j in 0 ..< numKernels {
        if xs == nil {
          xs = try resBlocks[i * numKernels + j](newX, s)
        } else {
          let temp = try resBlocks[i * numKernels + j](newX, s)
          xs = xs! + temp
        }
      }
      newX = xs! / numKernels
      KokoroStageClock.mark("gen.res\(i)", [newX])
    }
    
    newX = KokoroLeakyReLU(negativeSlope: 0.01)(newX)

    newX = MLX.swappedAxes(newX, 2, 1)
    newX = convPost(newX, conv: KokoroConv.conv1d)
    newX = MLX.swappedAxes(newX, 2, 1)
    
    KokoroStageClock.mark("gen.post", [newX])
    let spec = MLX.exp(newX[0..., 0 ..< (postNFFt / 2 + 1), 0...])
    let phase = MLX.sin(newX[0..., (postNFFt / 2 + 1)..., 0...])

    let result = try stft.inverse(magnitude: spec, phase: phase)
    KokoroStageClock.mark("gen.istft", [result])
    return result
  }
}
