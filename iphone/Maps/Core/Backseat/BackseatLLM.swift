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
    BackseatWhereTo.install()
    BackseatFileQuestions.install()
    BackseatDriveWatcher.startIfEnabled()
    // December or January, once: the year in miles.
    DispatchQueue.main.asyncAfter(deadline: .now() + 15) {
      let yearEnd = MWMBackseatRewriter.mileageYearEnd
      guard !yearEnd.isEmpty else { return }
      KokoroVoice.shared.sayReply(yearEnd, voiceID: nil) { MWMTextToSpeech.tts().play(yearEnd) }
    }
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
    let conversation = Conversation(model: model, parameters: parameters)
    MWMBackseatRewriter.installToolHandler { spoken, done in
      Task { await conversation.answer(spoken, done: done) }
    }
  }
  #endif
}

#if canImport(FoundationModels)
/// One conversation per app run, so the GPS remembers what was said a minute ago. It can run app
/// actions and save facts about the driver; otherwise it just answers, slightly jaded.
@available(iOS 26.0, *)
private actor Conversation {
  private let model: SystemLanguageModel
  private let parameters: GenerationSchema
  private var session: LanguageModelSession?
  private var sessionVoice = ""
  /// The tool calls go to whatever is being answered right now.
  private let current = Turn()

  init(model: SystemLanguageModel, parameters: GenerationSchema) {
    self.model = model
    self.parameters = parameters
  }

  func answer(_ spoken: String, done: @escaping (Bool, String) -> Void) async {
    let voice = KokoroVoice.selectedVoiceName
    current.begin(spoken)
    var chat = ""
    for attempt in 0..<2 {
      let session = await session(for: voice, fresh: attempt > 0)
      guard !session.isResponding else { break }
      do {
        chat = try await session.respond(to: spoken).content
        break
      } catch LanguageModelSession.GenerationError.exceededContextWindowSize {
        // The conversation got too long: start over with what's in the driver's file.
        continue
      } catch {
        // Guardrails or a busy model: the keyword parser takes the words.
        break
      }
    }
    done(current.used, chat.trimmingCharacters(in: .whitespacesAndNewlines))
  }

  /// The session for `voice`, new when the voice changed (its name is in the instructions).
  private func session(for voice: String, fresh: Bool) async -> LanguageModelSession {
    if let session, !fresh, sessionVoice == voice { return session }
    let spokenName = voice.split(separator: " ").first.map(String.init) ?? voice
    let instructions = await MainActor.run {
      MWMBackseatRewriter.chatInstructions(spokenName) + "\nApp actions for do_app_action:\n" +
        MWMBackseatRewriter.toolGuide
    }
    let tools: [any Tool] = [AppActionTool(parameters: parameters, turn: current), RememberFactTool()]
    let made = LanguageModelSession(model: model, tools: tools, instructions: instructions)
    session = made
    sessionVoice = voice
    return made
  }
}
#endif

/// What the driver said this turn, and whether an app action ran for it.
private final class Turn: @unchecked Sendable {
  private let lock = NSLock()
  private var spokenText = ""
  private var usedAction = false

  var spoken: String {
    lock.lock(); defer { lock.unlock() }
    return spokenText
  }

  var used: Bool {
    lock.lock(); defer { lock.unlock() }
    return usedAction
  }

  func begin(_ spoken: String) {
    lock.lock(); spokenText = spoken; usedAction = false; lock.unlock()
  }

  func markUsed() {
    lock.lock(); usedAction = true; lock.unlock()
  }
}

#if canImport(FoundationModels)
/// Runs an app action by name. The navigator answers with the snark.
@available(iOS 26.0, *)
private struct AppActionTool: Tool {
  let name = "do_app_action"
  let description = "Runs an app action the driver asked for, like cancel_route or change_voice."
  let parameters: GenerationSchema
  let turn: Turn

  func call(arguments: GeneratedContent) async throws -> String {
    let action = try arguments.value(String.self, forProperty: "action")
    let argument = (try? arguments.value(String?.self, forProperty: "argument")) ?? nil
    turn.markUsed()
    let spoken = turn.spoken
    await MainActor.run {
      MWMBackseatRewriter.runTool(action, argument: argument ?? "", spoken: spoken)
    }
    return "Done. The app already told the driver; don't add anything."
  }
}

/// Saves something the driver said about themselves to their permanent file.
@available(iOS 26.0, *)
private struct RememberFactTool: Tool {
  let name = "remember_fact"
  let description = "Saves something worth keeping about the driver to their permanent file."

  @Generable
  struct Arguments {
    @Guide(description: "A short topic like pet, job, music, family, or note for anything else")
    var topic: String
    @Guide(description: "The fact, in a few words, e.g. has a dog named Biscuit")
    var fact: String
  }

  func call(arguments: Arguments) async throws -> String {
    await MainActor.run {
      _ = MWMBackseatRewriter.remember(arguments.fact, topic: arguments.topic)
    }
    return "Saved."
  }
}
#endif
