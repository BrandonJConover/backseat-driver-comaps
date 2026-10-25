//
//  Kokoro-tts-lib
//
import Foundation
import MLX
import MLXNN

class DurationEncoder {
  var lstms: [Module] = []

  init(weights: [String: MLXArray], dModel: Int, styDim: Int, nlayers: Int) throws {
    for i in 0 ..< nlayers * 2 {
      if i % 2 == 0 {
        lstms.append(
          LSTM(inputSize: dModel + styDim,
               hiddenSize: dModel / 2,
               wxForward: try requiredDecoderWeight(weights, key: "predictor.text_encoder.lstms.\(i).weight_ih_l0"),
               whForward: try requiredDecoderWeight(weights, key: "predictor.text_encoder.lstms.\(i).weight_hh_l0"),
               biasIhForward: try requiredDecoderWeight(weights, key: "predictor.text_encoder.lstms.\(i).bias_ih_l0"),
               biasHhForward: try requiredDecoderWeight(weights, key: "predictor.text_encoder.lstms.\(i).bias_hh_l0"),
               wxBackward: try requiredDecoderWeight(weights, key: "predictor.text_encoder.lstms.\(i).weight_ih_l0_reverse"),
               whBackward: try requiredDecoderWeight(weights, key: "predictor.text_encoder.lstms.\(i).weight_hh_l0_reverse"),
               biasIhBackward: try requiredDecoderWeight(weights, key: "predictor.text_encoder.lstms.\(i).bias_ih_l0_reverse"),
               biasHhBackward: try requiredDecoderWeight(weights, key: "predictor.text_encoder.lstms.\(i).bias_hh_l0_reverse"))
        )
      } else {
        lstms.append(AdaLayerNorm(weight: try requiredDecoderWeight(weights, key: "predictor.text_encoder.lstms.\(i).fc.weight"),
                                  bias: try requiredDecoderWeight(weights, key: "predictor.text_encoder.lstms.\(i).fc.bias")))
      }
    }
  }

  func callAsFunction(_ x: MLXArray, style: MLXArray, textLengths _: MLXArray, m: MLXArray) -> MLXArray {
    var x = x.transposed(2, 0, 1)
    let s = MLX.broadcast(style, to: [x.shape[0], x.shape[1], style.shape[style.shape.count - 1]])
    x = MLX.concatenated([x, s], axis: -1)
    x = MLX.where(m.expandedDimensions(axes: [-1]).transposed(1, 0, 2), MLXArray.zeros(like: x), x)
    x = x.transposed(1, 2, 0)

    for block in lstms {
      if let adaLayerNorm = block as? AdaLayerNorm {
        x = adaLayerNorm(x.transposed(0, 2, 1), style).transposed(0, 2, 1)
        x = MLX.concatenated([x, s.transposed(1, 2, 0)], axis: 1)
        x = MLX.where(m.expandedDimensions(axes: [-1]).transposed(0, 2, 1), MLXArray.zeros(like: x), x)
      } else if let lstm = block as? LSTM {
        // Not `x.transposed(0, 2, 1)[0]`: MLX 0.31.1 (bundled with
        // mlx-swift) returns wrong values on the CPU for an integer index
        // into a size-1 axis of a transposed array (MLX 0.32 and the GPU are
        // right). That gave background (CPU) speech wrong durations — fast,
        // with words dropped (2026-10-10). Slicing then reshaping is exact.
        let transposed = x.transposed(0, 2, 1)
        x = transposed[0 ..< 1].reshaped([transposed.dim(1), transposed.dim(2)])
        let (lstmOutput, _) = lstm(x)
        x = lstmOutput.transposed(0, 2, 1)
        let xPad = MLXArray.zeros([x.shape[0], x.shape[1], m.shape[m.shape.count - 1]])
        xPad[0 ..< x.shape[0], 0 ..< x.shape[1], 0 ..< x.shape[2]] = x
        x = xPad
      }
    }

    return x.transposed(0, 2, 1)
  }
}
