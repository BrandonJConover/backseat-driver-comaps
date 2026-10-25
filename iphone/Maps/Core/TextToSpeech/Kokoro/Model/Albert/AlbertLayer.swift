//
//  Kokoro-tts-lib
//
import Foundation
import MLX
import MLXNN

class AlbertLayer {
  let attention: AlbertSelfAttention
  let fullLayerLayerNorm: LayerNorm
  let ffn: Linear
  let ffnOutput: Linear

  init(weights: [String: MLXArray], config: AlbertModelArgs, layerNum: Int, innerGroupNum: Int) throws {
    attention = try AlbertSelfAttention(weights: weights, config: config, layerNum: layerNum, innerGroupNum: innerGroupNum)
    ffn = Linear(weight: try requiredAlbertWeight(weights, key: "bert.encoder.albert_layer_groups.\(layerNum).albert_layers.\(innerGroupNum).ffn.weight"),
                 bias: try requiredAlbertWeight(weights, key: "bert.encoder.albert_layer_groups.\(layerNum).albert_layers.\(innerGroupNum).ffn.bias"))
    ffnOutput = Linear(weight: try requiredAlbertWeight(weights, key: "bert.encoder.albert_layer_groups.\(layerNum).albert_layers.\(innerGroupNum).ffn_output.weight"),
                       bias: try requiredAlbertWeight(weights, key: "bert.encoder.albert_layer_groups.\(layerNum).albert_layers.\(innerGroupNum).ffn_output.bias"))
    fullLayerLayerNorm = LayerNorm(dimensions: config.hiddenSize, eps: config.layerNormEps)

    let fullLayerLayerNormWeights = try requiredAlbertWeight(weights, key: "bert.encoder.albert_layer_groups.\(layerNum).albert_layers.\(innerGroupNum).full_layer_layer_norm.weight")
    let fullLayerLayerNormBiases = try requiredAlbertWeight(weights, key: "bert.encoder.albert_layer_groups.\(layerNum).albert_layers.\(innerGroupNum).full_layer_layer_norm.bias")

    guard fullLayerLayerNormWeights.count == config.hiddenSize, fullLayerLayerNormBiases.count == config.hiddenSize else {
      throw AlbertWeightError.shapeMismatch("AlbertLayer FullLayerLayerNorm expects \(config.hiddenSize) elements, got weights:\(fullLayerLayerNormWeights.count) biases:\(fullLayerLayerNormBiases.count)")
    }

    guard let lnWeight = fullLayerLayerNorm.weight, let lnBias = fullLayerLayerNorm.bias else {
      throw AlbertWeightError.shapeMismatch("AlbertLayer FullLayerLayerNorm missing bias or weight parameters")
    }
    for i in 0 ..< config.hiddenSize {
      lnWeight[i] = fullLayerLayerNormWeights[i]
      lnBias[i] = fullLayerLayerNormBiases[i]
    }
  }

  func ffChunk(_ attentionOutput: MLXArray) -> MLXArray {
    var ffnOutputArray = ffn(attentionOutput)
    ffnOutputArray = KokoroActivations.gelu(ffnOutputArray)
    ffnOutputArray = ffnOutput(ffnOutputArray)
    return ffnOutputArray
  }

  func callAsFunction(
    _ hiddenStates: MLXArray,
    attentionMask: MLXArray? = nil
  ) -> MLXArray {
    let attentionOutput = attention(hiddenStates, attentionMask: attentionMask)
    let ffnOutput = ffChunk(attentionOutput)
    let output = fullLayerLayerNorm(ffnOutput + attentionOutput)
    return output
  }
}
