import Foundation
#if canImport(FoundationModels)
import FoundationModels
#endif

/// Plugs Apple Intelligence's on-device model into Backseat Driver, so roasts come out in
/// fresh words each time and anything the driver asks for maps to an app action. iPhones without
/// Apple Intelligence keep the canned lines and the keyword commands.
@objc final class BackseatLLM: NSObject {
  private static var availabilityTimer: Timer?

  @objc static func install() {
    MWMBackseatRewriter.shareVoices()
    // Load Kokoro now, not when the first route starts: its first lines would get the system voice.
    KokoroVoice.shared.start()
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
    installTools(model: model)
    // The model can finish downloading, or Apple Intelligence can be switched on, while the app runs.
    MWMBackseatRewriter.setAvailable(model.isAvailable)
    MWMBackseatRewriter.setToolsAvailable(model.isAvailable)
    availabilityTimer = Timer.scheduledTimer(withTimeInterval: 60, repeats: true) { _ in
      MWMBackseatRewriter.setAvailable(model.isAvailable)
      MWMBackseatRewriter.setToolsAvailable(model.isAvailable)
    }
    #endif
  }

  #if canImport(FoundationModels)
  @available(iOS 26.0, *)
  private static func installTools(model: SystemLanguageModel) {
    let arguments = DynamicGenerationSchema(name: "AppAction", properties: [
      .init(name: "action", description: "Which app action the driver asked for",
            schema: DynamicGenerationSchema(name: "ActionName", anyOf: MWMBackseatRewriter.toolNames)),
      .init(name: "argument", description: "The place or voice name the action needs, else empty",
            schema: DynamicGenerationSchema(type: String.self), isOptional: true),
    ])
    guard let parameters = try? GenerationSchema(root: arguments, dependencies: []) else {
      LOG(.error, "Backseat: couldn't describe the app actions to the model")
      return
    }
    let instructions = """
      You run the controls of a navigation app. When the driver asks the app to do something, call \
      do_app_action with the matching action, once per thing asked. When they're just talking, \
      calling out a license plate, or answering a question, don't call it. Reply in a few words.
      Actions:
      \(MWMBackseatRewriter.toolGuide)
      """
    MWMBackseatRewriter.installToolHandler { spoken, done in
      Task {
        let used = UsedFlag()
        do {
          let tool = AppActionTool(parameters: parameters, spoken: spoken, used: used)
          let session = LanguageModelSession(model: model, tools: [tool], instructions: instructions)
          _ = try await session.respond(to: spoken)
        } catch {
          // Guardrails or a busy model: the keyword parser takes the words if no action ran.
        }
        done(used.value)
      }
    }
  }
  #endif
}

/// Whether the model carried out an action, set from the tool call.
private final class UsedFlag: @unchecked Sendable {
  private let lock = NSLock()
  private var used = false

  var value: Bool {
    lock.lock(); defer { lock.unlock() }
    return used
  }

  func set() {
    lock.lock(); used = true; lock.unlock()
  }
}

#if canImport(FoundationModels)
/// The one tool the model gets: run an app action by name. The navigator answers with the snark.
@available(iOS 26.0, *)
private struct AppActionTool: Tool {
  let name = "do_app_action"
  let description = "Runs an app action the driver asked for, like cancel_route or change_voice."
  let parameters: GenerationSchema
  let spoken: String
  let used: UsedFlag

  func call(arguments: GeneratedContent) async throws -> String {
    let action = try arguments.value(String.self, forProperty: "action")
    let argument = (try? arguments.value(String?.self, forProperty: "argument")) ?? nil
    used.set()
    await MainActor.run {
      MWMBackseatRewriter.runTool(action, argument: argument ?? "", spoken: spoken)
    }
    return "Done."
  }
}
#endif
