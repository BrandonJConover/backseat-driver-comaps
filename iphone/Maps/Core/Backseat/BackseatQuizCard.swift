import UIKit

/// The pop quiz on screen (Brandon, 2026-10-31): the question and its three choices while the GPS
/// asks it, then the right answer in green (and a wrong pick in orange) for a few seconds. On a
/// drive it's read-only: the driver has to say the answer out loud ("We can't wreck because you
/// tried to touch your phone screen"). On a walk the choices take taps too.
/// Reads +[MWMBackseatRewriter popQuiz] twice a second while navigating.
@objc final class BackseatQuiz: NSObject {
  private static var view: BackseatQuizCardView?
  private static var timer: Timer?

  static func install(in parent: UIView) {
    let card = BackseatQuizCardView()
    card.translatesAutoresizingMaskIntoConstraints = false
    card.alpha = 0
    card.isHidden = true
    parent.addSubview(card)
    NSLayoutConstraint.activate([
      card.leadingAnchor.constraint(equalTo: parent.safeAreaLayoutGuide.leadingAnchor, constant: 16),
      card.trailingAnchor.constraint(equalTo: parent.safeAreaLayoutGuide.trailingAnchor, constant: -16),
      // Under the turn card, clear of the GPS's face and speech bubble at the bottom.
      card.topAnchor.constraint(equalTo: parent.safeAreaLayoutGuide.topAnchor, constant: 132),
    ])
    card.onTap = { choice in
      MWMBackseatRewriter.answerQuiz(choice)
      refresh()
    }
    view = card

    NotificationCenter.default.addObserver(forName: BackseatTalkButton.navigationVisibilityChanged, object: nil,
                                           queue: .main) { note in
      timer?.invalidate()
      timer = nil
      if (note.object as? Bool) ?? false {
        timer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { _ in refresh() }
      } else {
        card.update(nil)
      }
    }
  }

  private static func refresh() {
    view?.update(Quiz(MWMBackseatRewriter.popQuiz))
  }

  struct Quiz: Equatable {
    let question: String
    let choices: [String]
    let picked: Int
    let correct: Int
    let tappable: Bool
    let brownies: Int

    init?(_ d: [String: Any]?) {
      guard let d, let question = d["question"] as? String, let choices = d["choices"] as? [String],
            choices.count == 3 else { return nil }
      self.question = question
      self.choices = choices
      picked = (d["picked"] as? NSNumber)?.intValue ?? -1
      correct = (d["correct"] as? NSNumber)?.intValue ?? -1
      tappable = (d["tappable"] as? NSNumber)?.boolValue ?? false
      brownies = (d["brownies"] as? NSNumber)?.intValue ?? 0
    }

    var answered: Bool { correct >= 0 }
  }
}

final class BackseatQuizCardView: UIView {
  var onTap: ((Int) -> Void)?

  private let title = UILabel()
  private let score = UILabel()
  private let question = UILabel()
  private let rows = (0..<3).map { _ in UIButton(type: .system) }
  private let footer = UILabel()
  private var shown: BackseatQuiz.Quiz?

  override init(frame: CGRect) {
    super.init(frame: frame)
    backgroundColor = BackseatTheme.card
    layer.cornerRadius = 22
    layer.cornerCurve = .continuous
    layer.borderWidth = 2
    layer.borderColor = BackseatTheme.accent.cgColor
    layer.shadowColor = UIColor.black.cgColor
    layer.shadowOpacity = 0.4
    layer.shadowRadius = 14
    layer.shadowOffset = CGSize(width: 0, height: 6)
    accessibilityViewIsModal = false

    title.text = "POP QUIZ"
    title.font = BackseatTheme.number(22)
    title.textColor = BackseatTheme.accent
    score.font = BackseatTheme.body(14, weight: .semibold)
    score.textColor = BackseatTheme.muted
    score.textAlignment = .right
    let header = UIStackView(arrangedSubviews: [title, score])
    header.axis = .horizontal

    question.font = BackseatTheme.body(19, weight: .semibold)
    question.textColor = BackseatTheme.text
    question.numberOfLines = 0

    for (i, row) in rows.enumerated() {
      var config = UIButton.Configuration.filled()
      config.cornerStyle = .large
      config.contentInsets = NSDirectionalEdgeInsets(top: 10, leading: 14, bottom: 10, trailing: 14)
      config.titleAlignment = .leading
      row.configuration = config
      row.contentHorizontalAlignment = .leading
      row.tag = i
      row.addTarget(self, action: #selector(tapped(_:)), for: .touchUpInside)
    }

    footer.font = BackseatTheme.body(14, weight: .semibold)
    footer.textColor = BackseatTheme.muted
    footer.numberOfLines = 0

    let stack = UIStackView(arrangedSubviews: [header, question] + rows + [footer])
    stack.axis = .vertical
    stack.spacing = 10
    stack.setCustomSpacing(6, after: header)
    stack.setCustomSpacing(14, after: question)
    stack.translatesAutoresizingMaskIntoConstraints = false
    addSubview(stack)
    NSLayoutConstraint.activate([
      stack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 18),
      stack.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -18),
      stack.topAnchor.constraint(equalTo: topAnchor, constant: 16),
      stack.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -16),
    ])
  }

  required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

  @objc private func tapped(_ sender: UIButton) {
    guard let quiz = shown, quiz.tappable, !quiz.answered else { return }
    onTap?(sender.tag)
  }

  func update(_ quiz: BackseatQuiz.Quiz?) {
    guard quiz != shown else { return }
    let wasShowing = shown != nil
    shown = quiz
    guard let quiz else {
      UIView.animate(withDuration: 0.25, animations: { self.alpha = 0 }) { _ in
        if self.shown == nil { self.isHidden = true }
      }
      return
    }
    question.text = quiz.question
    score.text = quiz.brownies == 1 ? "1 brownie point" : "\(quiz.brownies) brownie points"
    for (i, row) in rows.enumerated() {
      let letter = ["A", "B", "C"][i]
      var config = row.configuration ?? .filled()
      var text = AttributedString("\(letter)   ")
      text.font = BackseatTheme.number(18)
      var choice = AttributedString(quiz.choices[i])
      choice.font = BackseatTheme.body(17, weight: .medium)
      text.append(choice)
      config.attributedTitle = text
      let isRight = quiz.answered && i == quiz.correct
      let isWrongPick = quiz.answered && i == quiz.picked && i != quiz.correct
      config.baseBackgroundColor = isRight ? BackseatTheme.calm : isWrongPick ? BackseatTheme.hot : BackseatTheme.raised
      config.baseForegroundColor = isRight || isWrongPick ? BackseatTheme.ink : BackseatTheme.text
      row.configuration = config
      // On a drive the rows are a display, not buttons.
      row.isUserInteractionEnabled = quiz.tappable && !quiz.answered
      row.accessibilityTraits = row.isUserInteractionEnabled ? .button : .staticText
    }
    if !quiz.answered {
      footer.text = quiz.tappable ? "Say it out loud or tap it." : "Say your answer out loud. Hands on the wheel."
      footer.textColor = BackseatTheme.muted
    } else if quiz.picked == quiz.correct {
      footer.text = "Brownie point earned. Brown noser."
      footer.textColor = BackseatTheme.calm
    } else {
      footer.text = quiz.picked < 0 ? "No answer. Noted." : "Wrong. Noted."
      footer.textColor = BackseatTheme.hot
    }
    layer.borderColor = (quiz.answered ? (quiz.picked == quiz.correct ? BackseatTheme.calm : BackseatTheme.hot)
      : BackseatTheme.accent).cgColor
    if !wasShowing {
      isHidden = false
      superview?.bringSubviewToFront(self)
      transform = CGAffineTransform(translationX: 0, y: -20)
      UIView.animate(withDuration: 0.3, delay: 0, usingSpringWithDamping: 0.8, initialSpringVelocity: 0) {
        self.alpha = 1
        self.transform = .identity
      }
    }
  }
}
