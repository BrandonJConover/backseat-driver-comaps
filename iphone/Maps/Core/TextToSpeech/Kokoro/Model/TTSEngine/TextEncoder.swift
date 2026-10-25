// SPDX-License-Identifier: Apache-2.0
//
//  Kokoro-tts-lib
//
import Foundation
import MLX
import MLXNN

// Text encoder
class TextEncoder {
  let embedding: Embedding
  let cnn: [[Module]]
  let lstm: LSTM

  init(weights: [String: MLXArray], channels: Int, kernelSize: Int, depth: Int, nSymbols _: Int, actv: Module = KokoroLeakyReLU(negativeSlope: 0.2)) throws {
    embedding = Embedding(weight: try requiredDecoderWeight(weights, key: "text_encoder.embedding.weight"))
    let padding = (kernelSize - 1) / 2

    var cnnLayers: [[Module]] = []
    for i in 0 ..< depth {
      cnnLayers.append([
        ConvWeighted(
          weightG: try requiredDecoderWeight(weights, key: "text_encoder.cnn.\(i).0.weight_g"),
          weightV: try requiredDecoderWeight(weights, key: "text_encoder.cnn.\(i).0.weight_v"),
          bias: try requiredDecoderWeight(weights, key: "text_encoder.cnn.\(i).0.bias"),
          padding: padding
        ),
        LayerNormInference(
          weight: try requiredDecoderWeight(weights, key: "text_encoder.cnn.\(i).1.gamma"),
          bias: try requiredDecoderWeight(weights, key: "text_encoder.cnn.\(i).1.beta")
        ),
        actv,
      ])
    }
    cnn = cnnLayers

    lstm = LSTM(
      inputSize: channels,
      hiddenSize: channels / 2,
      wxForward: try requiredDecoderWeight(weights, key: "text_encoder.lstm.weight_ih_l0"),
      whForward: try requiredDecoderWeight(weights, key: "text_encoder.lstm.weight_hh_l0"),
      biasIhForward: try requiredDecoderWeight(weights, key: "text_encoder.lstm.bias_ih_l0"),
      biasHhForward: try requiredDecoderWeight(weights, key: "text_encoder.lstm.bias_hh_l0"),
      wxBackward: try requiredDecoderWeight(weights, key: "text_encoder.lstm.weight_ih_l0_reverse"),
      whBackward: try requiredDecoderWeight(weights, key: "text_encoder.lstm.weight_hh_l0_reverse"),
      biasIhBackward: try requiredDecoderWeight(weights, key: "text_encoder.lstm.bias_ih_l0_reverse"),
      biasHhBackward: try requiredDecoderWeight(weights, key: "text_encoder.lstm.bias_hh_l0_reverse")
    )
  }

  public func callAsFunction(_ x: MLXArray, inputLengths _: MLXArray, m: MLXArray) -> MLXArray {
    var x = embedding(x)
    x = x.transposed(0, 2, 1)
    let mask = m.expandedDimensions(axis: 1)
    x = MLX.where(mask, 0.0, x)

    for convBlock in cnn {
      for layer in convBlock {
        if layer is ConvWeighted || layer is LayerNormInference {
          x = MLX.swappedAxes(x, 2, 1)
          if let convWeighted = layer as? ConvWeighted {
            x = convWeighted(x, conv: KokoroConv.conv1d)
          } else if let layer = layer as? LayerNormInference {
            x = layer(x)
          }
          x = MLX.swappedAxes(x, 2, 1)
        } else if let layer = layer as? KokoroLeakyReLU {
          x = layer(x)
        } else {
          // Unknown layer type. The TextEncoder init only adds ConvWeighted,
          // LayerNormInference, and KokoroLeakyReLU, so this branch is unreachable
          // in normal usage. Skip the layer instead of crashing.
          assertionFailure("TextEncoder: unsupported layer type \(type(of: layer))")
        }
        x = MLX.where(mask, 0.0, x)
      }
    }

    x = MLX.swappedAxes(x, 2, 1)
    let (lstmOutput, _) = lstm(x)
    x = MLX.swappedAxes(lstmOutput, 2, 1)

    let xPad = MLX.zeros([x.shape[0], x.shape[1], mask.shape[mask.shape.count - 1]])
    xPad._updateInternal(x)

    return MLX.where(mask, 0.0, xPad)
  }
}
