// Backseat Driver ships its Kokoro voice inside the app: bundle_kokoro.sh fills the CoreMLKokoro
// folder (FluidAudio's Core ML models, G2P assets, lexicon and voice packs, or a Kokoro fine-tune's
// models), and this copies it into FluidAudio's cache before Kokoro loads, so FluidAudio finds
// everything there and downloads nothing.
//
// The models keep their names when the weights change (a fine-tune is the same graphs with new
// numbers), so an update that bundles different weights replaces the installed .mlmodelc folders;
// skipping them because they exist would keep speaking with the old voice.

import FluidAudio
import Foundation

enum KokoroBundle {
  /// Bundle folder -> FluidAudio's folder under `<cache>/Models`.
  static let layout = [("ANE", "kokoro-82m-coreml/ANE"), ("kokoro", "kokoro")]
  /// Which weights the bundled models carry ("stock" or a fine-tune id), from bundle_kokoro.sh.
  static let weightsIDFile = "WEIGHTS_ID"
  /// The same id, recorded next to the installed models.
  static let installedWeightsIDFile = ".backseat-weights-id"

  static var bundledFolder: URL? { Bundle.main.url(forResource: "CoreMLKokoro", withExtension: nil) }

  /// True when this build carries the Kokoro files, so nothing has to wait for Wi-Fi.
  static var isBundled: Bool { bundledFolder != nil }

  static func weightsID(at url: URL) -> String {
    let text = (try? String(contentsOf: url, encoding: .utf8))?
      .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
    return text.isEmpty ? "stock" : text
  }

  /// Copies the bundled files into FluidAudio's cache. False when the build bundles none.
  @discardableResult
  static func install() -> Bool {
    guard let folder = bundledFolder,
          let models = try? TtsCacheDirectory.ensure().appendingPathComponent("Models") else { return false }
    install(from: folder, toModels: models)
    return true
  }

  /// Copies each bundled item that isn't installed yet, and replaces the installed `.mlmodelc`
  /// folders when the bundled weights differ from the installed ones. Returns how many items it copied.
  @discardableResult
  static func install(from bundleRoot: URL, toModels models: URL, fileManager: FileManager = .default) -> Int {
    let bundledWeights = weightsID(at: bundleRoot.appendingPathComponent(weightsIDFile))
    let stamp = models.appendingPathComponent(installedWeightsIDFile)
    let replaceModels = weightsID(at: stamp) != bundledWeights
    var copied = 0
    for (source, destination) in layout {
      let from = bundleRoot.appendingPathComponent(source)
      let to = models.appendingPathComponent(destination)
      guard let items = try? fileManager.contentsOfDirectory(atPath: from.path) else { continue }
      try? fileManager.createDirectory(at: to, withIntermediateDirectories: true)
      for item in items {
        let target = to.appendingPathComponent(item)
        if fileManager.fileExists(atPath: target.path) {
          guard replaceModels, item.hasSuffix(".mlmodelc") else { continue }
          try? fileManager.removeItem(at: target)
        }
        if (try? fileManager.copyItem(at: from.appendingPathComponent(item), to: target)) != nil {
          copied += 1
        }
      }
    }
    if replaceModels {
      try? bundledWeights.write(to: stamp, atomically: true, encoding: .utf8)
    }
    var values = URLResourceValues()
    values.isExcludedFromBackup = true  // re-created from the app bundle
    var modelsURL = models
    try? modelsURL.setResourceValues(values)
    return copied
  }
}
