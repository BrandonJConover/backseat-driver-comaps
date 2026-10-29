#include "backseat/running_bits.hpp"

#include "backseat/report.hpp"

#include <algorithm>
#include <iterator>

namespace backseat
{
namespace
{
/// A plain "nice turn" goes out for one in this many on-time turns that have nothing better.
int constexpr kPlainVerdictEvery = 3;
/// The clean-driving beats: minutes without a mistake for each one.
double constexpr kCleanBeatMin[] = {5, 10, 16};
/// The arrival callback for a clean trip goes out this often (a story or a blown turn always does).
double constexpr kCleanArrivalChance = 0.5;
/// A story needs this many beats told before the arrival line calls back to it.
int constexpr kStoryArrivalBeats = 2;
/// Story beats are at least this far apart, however often the engine wants to nag.
double constexpr kStoryBeatGapMs = 120000;

bool BadTurn(IncidentKind k)
{
  return k == IncidentKind::MissedTurn || k == IncidentKind::WrongTurn || k == IncidentKind::LateTurn;
}

/// Mistakes the clean-driving bit counts. Traffic and long stops aren't the driver's fault.
bool Mistake(IncidentKind k) { return k != IncidentKind::Traffic && k != IncidentKind::LongStop; }

std::optional<std::string> LastBadStreet(std::vector<Incident> const & incidents)
{
  for (auto it = incidents.rbegin(); it != incidents.rend(); ++it)
    if (BadTurn(it->kind))
      return it->detail ? StreetOf(*it->detail) : std::string();
  return std::nullopt;
}
}  // namespace

std::vector<std::string> const & RunningBits::StoryIds()
{
  static std::vector<std::string> const ids = {"ticket", "mother", "lawyer", "nature", "insurance", "diary"};
  return ids;
}

std::vector<int> const & RunningBits::StreakMilestones()
{
  static std::vector<int> const streaks = {3, 6, 10, 15, 20, 30, 40, 50, 75, 100, 150, 200, 300, 500, 1000};
  return streaks;
}

void RunningBits::NewTrip(bool cleanBitOn, int streak)
{
  *this = RunningBits();
  m_cleanOn = cleanBitOn;
  m_streak = std::max(0, streak);
  auto const & milestones = StreakMilestones();
  while (m_milestonesSaid < milestones.size() && milestones[m_milestonesSaid] <= m_streak)
    ++m_milestonesSaid;
}

void RunningBits::SyncTurns(TripStats const & stats, std::vector<Incident> const & incidents)
{
  int const bad = stats.missedTurns + stats.wrongTurns + stats.lateTurns;
  int const onTime = stats.turnsCompleted - stats.lateTurns;
  if (bad > m_badSeen)
  {
    m_badSeen = bad;
    m_streak = 0;
    m_milestonesSaid = 0;
    auto const street = LastBadStreet(incidents);
    m_redeem = street.value_or("");
  }
  m_streak += std::max(0, onTime - m_onTimeSeen);
  m_onTimeSeen = onTime;
}

std::optional<BitLine> RunningBits::OnTimeTurn(TripStats const & stats, std::vector<Incident> const & incidents,
                                               double r)
{
  SyncTurns(stats, incidents);
  bool const first = m_firstVerdict;
  m_firstVerdict = false;
  // Made up for the one blown earlier: always worth saying.
  if (m_redeem)
  {
    std::string const street = *m_redeem;
    m_redeem.reset();
    return BitLine{"turnCompleted.redemption", {{"street", street.empty() ? "the one you missed" : street}}, std::nullopt};
  }
  auto const & milestones = StreakMilestones();
  if (m_milestonesSaid < milestones.size() && m_streak >= milestones[m_milestonesSaid])
  {
    // A streak skipped past (verdicts held back) still gets its line, but only the latest one.
    while (m_milestonesSaid + 1 < milestones.size() && m_streak >= milestones[m_milestonesSaid + 1])
      ++m_milestonesSaid;
    int const n = milestones[m_milestonesSaid++];
    return BitLine{n <= 10 ? "turnCompleted.streak" + std::to_string(n) : "turnCompleted.streakBig",
                   {{"n", std::to_string(n)}}, std::nullopt};
  }
  // Otherwise mostly quiet: a plain verdict for one turn in three (the trip's first gets the same
  // odds), alternating between disbelief and the GPS taking the credit.
  if (first ? r >= 1.0 / kPlainVerdictEvery : ++m_plainSkipped < kPlainVerdictEvery)
    return std::nullopt;
  m_plainSkipped = 0;
  bool const credit = (m_plainSaid++ + (r < 0.5 / kPlainVerdictEvery ? 0 : 1)) % 2 == 1;
  return BitLine{credit ? "turnCompleted.credit" : "turnCompleted.onTime", {}, std::nullopt};
}

std::optional<BitLine> RunningBits::StillSpeeding(double t)
{
  if (m_story.empty() || t - m_lastBeatT < kStoryBeatGapMs)
    return std::nullopt;
  m_lastBeatT = t;
  std::string const key = "speeding.story." + m_story;
  if (static_cast<size_t>(m_storyBeat) >= LinesFor(key, 0).size())
    return std::nullopt;
  return BitLine{key, {}, static_cast<size_t>(m_storyBeat++)};
}

std::optional<BitLine> RunningBits::Arrival(std::string const & gradeLetter, std::vector<Incident> const & incidents,
                                            double r) const
{
  if (!m_story.empty() && m_storyBeat >= kStoryArrivalBeats)
    return BitLine{"speeding.end." + m_story, {}, std::nullopt};
  if (auto const street = LastBadStreet(incidents); street && !street->empty())
  {
    std::string const better = BetterGrade(gradeLetter);
    if (better != gradeLetter)
      return BitLine{"arrived.blame", {{"street", *street}, {"better", SpokenGrade(better)}}, std::nullopt};
    return std::nullopt;
  }
  bool const clean = std::none_of(incidents.begin(), incidents.end(), [](Incident const & i) { return Mistake(i.kind); });
  if (clean && r < kCleanArrivalChance)
    return BitLine{"arrived.clean", {}, std::nullopt};
  return std::nullopt;
}

std::optional<BitLine> RunningBits::CleanBeatDue(double t, double tripStartT, std::vector<Incident> const & incidents)
{
  if (!m_cleanOn || m_cleanBeat >= static_cast<int>(std::size(kCleanBeatMin)))
    return std::nullopt;
  // Counted from the last mistake, so one undoes the build-up; the bit picks up at its next beat.
  double from = tripStartT;
  for (auto const & i : incidents)
    if (Mistake(i.kind))
      from = std::max(from, i.t);
  if (t - from < kCleanBeatMin[m_cleanBeat] * 60000)
    return std::nullopt;
  return BitLine{"clean." + std::to_string(++m_cleanBeat), {}, std::nullopt};
}

std::string StreetOf(std::string const & instruction)
{
  auto const onto = instruction.find(" onto ");
  if (onto == std::string::npos)
    return {};
  std::string street = instruction.substr(onto + 6);
  for (char const * end : {", then", ".", ","})
    if (auto const p = street.find(end); p != std::string::npos)
      street.resize(p);
  return street;
}

std::string BetterGrade(std::string const & letter)
{
  static char const * const kUp[] = {"F", "D", "C-", "C", "C+", "B-", "B", "B+", "A-", "A"};
  auto const it = std::find(std::begin(kUp), std::end(kUp), letter);
  if (it == std::end(kUp) || it + 1 == std::end(kUp))
    return letter;
  return *(it + 1);
}
}  // namespace backseat
