import Foundation
#if canImport(FoundationModels)
import FoundationModels
#endif

/// Plugs Apple Intelligence's on-device model into Backseat Driver, so roasts come out in
/// fresh words each time. iPhones without Apple Intelligence keep the canned lines.
@objc final class BackseatLLM: NSObject {
  private static var availabilityTimer: Timer?

  @objc static func install() {
    #if canImport(FoundationModels)
    guard #available(iOS 26.0, *) else { return }
    let model = SystemLanguageModel.default
    MWMBackseatRewriter.install { instructions, prompt, completion in
      Task {
        do {
          let session = LanguageModelSession(model: model, instructions: instructions)
          let response = try await session.respond(to: prompt, options: GenerationOptions(temperature: 0.9))
          completion(response.content)
        } catch {
          // Guardrails, busy model, or not ready yet: the canned line is spoken instead.
        }
      }
    }
    // The model can finish downloading, or Apple Intelligence can be switched on, while the app runs.
    MWMBackseatRewriter.setAvailable(model.isAvailable)
    availabilityTimer = Timer.scheduledTimer(withTimeInterval: 60, repeats: true) { _ in
      MWMBackseatRewriter.setAvailable(model.isAvailable)
    }
    #endif
  }
}
