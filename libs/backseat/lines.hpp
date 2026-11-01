// The roast library: three tiers per key, picked by frustration.
//   0 = smug and passive-aggressive, 1 = openly exasperated, 2 = completely unhinged.
// The lines themselves are generated from engine/src/lines.ts into lines_data.cpp.
#pragma once

#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace backseat
{
using Vars = std::map<std::string, std::string>;

/// How the trip is being made. Decides which lines fit: nobody on foot has a brake pedal.
enum class TravelMode
{
  Drive,
  Walk,
  Bike,
  /// A car, off the pavement: no lanes, no limits, no signs. The driver says so ("off-road mode"),
  /// or the Navigator notices dirt tracks, or no road at all, on a drive.
  OffRoad,
  /// A walk on trails and dirt paths. Found by the Navigator on a Walk; the host never sets it.
  Hike
};

/// A car is involved: a drive, on or off the pavement.
inline bool InCar(TravelMode mode) { return mode == TravelMode::Drive || mode == TravelMode::OffRoad; }
/// What the host's route type would be for `mode`: an off-road trip is a drive, a hike a walk.
inline TravelMode BaseMode(TravelMode mode)
{
  return mode == TravelMode::OffRoad ? TravelMode::Drive : mode == TravelMode::Hike ? TravelMode::Walk : mode;
}
char const * TravelModeName(TravelMode mode);

/// The mode LinesFor() answers for. Driving until told otherwise.
void SetLineTravelMode(TravelMode mode);
/// Whether `text` makes sense for `mode`: no cars, gas or brake pedals for someone on foot.
bool LineFitsMode(std::string const & text, TravelMode mode);

/// Where the driver lives, from the getting-to-know-you questions. Decides which lines fit: someone
/// living on the streets doesn't need reminding that they don't have a house (Brandon: "I'm not that
/// cruel").
enum class Housing
{
  Unknown,
  House,
  Apartment,
  Streets
};
/// "house", "apartment", "streets", or "" for Unknown; and back (anything else is Unknown).
char const * HousingName(Housing housing);
Housing HousingFrom(std::string const & name);

/// The housing LinesFor() answers for. Unknown (every line fits) until told otherwise.
void SetLineHousing(Housing housing);
/// Whether `text` is fine to say to someone living in `housing`: no house, home, garage or rent
/// jokes for the streets; no lawn, driveway or mortgage jokes for an apartment.
bool LineFitsHousing(std::string const & text, Housing housing);

/// Whether LinesFor() answers for a metric driver: the few lines with a distance written into them
/// ("we moved four feet", "for a quarter mile") come back in meters and kilometers. Off until told.
void SetLineMetric(bool metric);
/// `text` with its written-in imperial distances said in metric (MetricWording("Ten feet of
/// glory") is "Three meters of glory"). Lines without any come back as they are.
std::string MetricWording(std::string const & text);

/// Templates for `key` at `tier` (0..2), for the current travel mode: what fits it, plus `key`.walk,
/// `key`.bike, `key`.hike (or else `key`.walk) or `key`.offroad if there are any, or mode.generic when
/// nothing fits. Lines that don't fit the driver's housing are left out too, unless that would leave
/// none. Throws std::out_of_range for an unknown key.
std::vector<std::string> const & LinesFor(std::string const & key, int tier);
/// False when nothing in `key` fits the current travel mode, so LinesFor() would only have mode.generic
/// ("Noted.") for it. A bit nobody asked for is better left unsaid then; a reply to the driver
/// (cmd.*) still gets its "Noted.".
bool LinesFitTravelMode(std::string const & key, int tier);

/// Fills {placeholders} from vars. {NAME} in capitals inserts the value in capitals.
/// Unknown placeholders are left as they are.
std::string Fill(std::string const & tmpl, Vars const & vars);

/// Text as it should be spoken. The fed-up lines are written in capitals for shouting, but a
/// voice reads a capitalized word as letters ("W-H-O"), so shouted words go back to normal case
/// (the exclamation marks carry the shouting). Real abbreviations (GPS, ETA, NASCAR) stay.
std::string SpeakableText(std::string const & text);

/// The running gags that wear out fastest ("There is valuable cargo aboard this car."): the first time
/// in `said` they stay and get remembered, after that they come out of `text`.
std::string DropRepeatedTags(std::string text, std::set<std::string> & said);

/// What the GPS said lately, per key, kept across trips so it keeps the driver guessing: a line
/// doesn't come back until the others for its key have had a turn.
class SaidLog
{
public:
  /// A line from `options` not said recently for `key`, chosen by `r` in [0, 1), and remembered.
  std::string const & Pick(std::string const & key, std::vector<std::string> const & options, double r);
  /// Whether anything for `key` was said before (as far as the log remembers).
  bool Said(std::string const & key) const
  {
    auto const it = m_recent.find(key);
    return it != m_recent.end() && !it->second.empty();
  }

  /// A value kept with the log, like how many trips have started ("" or 0 when never set).
  std::string Value(std::string const & key) const;
  void SetValue(std::string const & key, std::string value);
  void Erase(std::string const & key);
  int Counter(std::string const & key) const { return std::atoi(Value(key).c_str()); }
  void SetCounter(std::string const & key, int value) { SetValue(key, std::to_string(value)); }

  /// A note kept with the log, like the driver's answer to a question ("rest"), until forgotten.
  void Remember(std::string const & key, std::string const & value)
  {
    m_recent[key] = {value};
    m_dirty = true;
  }
  /// The note for `key`; empty when there's none.
  std::string Recalled(std::string const & key) const
  {
    auto const it = m_recent.find(key);
    return it == m_recent.end() || it->second.empty() ? std::string() : it->second.back();
  }
  void Forget(std::string const & key)
  {
    if (m_recent.erase(key))
      m_dirty = true;
  }

  /// Whether anything was picked since the last ClearDirty(), i.e. the log needs saving.
  bool Dirty() const { return m_dirty; }
  void ClearDirty() { m_dirty = false; }

  std::string Serialize() const;
  static SaidLog Parse(std::string const & text);

private:
  std::map<std::string, std::vector<std::string>> m_recent;
  bool m_dirty = false;
};
}  // namespace backseat
