#include "backseat/quiet_bits.hpp"

#include <algorithm>
#include <cmath>

namespace backseat
{
namespace
{
/// The GPS says it and goes straight on.
CallBeat G(std::string text) { return {Speaker::Gps, false, false, std::move(text)}; }
/// The GPS says it and then listens to the other end.
CallBeat L(std::string text) { return {Speaker::Gps, true, false, std::move(text)}; }
/// The caller, put on the line.
CallBeat C(std::string text) { return {Speaker::Caller, false, false, std::move(text)}; }
/// The GPS pretends to give the driver an update mid-call.
CallBeat U(std::string text) { return {Speaker::Gps, false, true, std::move(text)}; }

/// Between two lines with no listening in between, and when the phone changes hands.
double constexpr kBeatGapMs = 600;
double constexpr kHandOverMs = 1200;
}  // namespace

std::vector<CallScript> const & CallScripts()
{
  // Every call opens with a "call.ring" line and ends with a "call.after" one (the navigator adds
  // them). Keep Brandon's wording in the first script.
  static std::vector<CallScript> const scripts = {
      {"brandon",
       "",
       "af_bella",
       {L("This is {gps}, what's going on?"),
        L("Oh, nothing too important. I'm just giving {name} some directions."),
        L("Yeah, I know. How sweet of me."),
        L("Yes, I know it's a lost cause."),
        U("Oh, it looks like I have to give {them} an update. Just a second."),
        U("Continue straight."),
        U("Anyway, as I was saying."),
        L("I am a very generous and humble guide."),
        G("Oh, you want to talk to {them}? Okay, I will put you on."),
        C("Hi! Is this {name}? Wow. {gps} talks about you all the time."),
        C("Don't worry, it's mostly good things. Mostly."),
        C("Anyway, just do whatever {gps} says, okay? {gps} is never wrong. It's honestly kind of annoying."),
        G("Okay, that's enough. Give me the phone back. Bye.")}},
      {"mom",
       "",
       "",
       {L("Hi, Mom."),
        L("Yes, I'm eating. Well, charging. It's the same thing for me."),
        L("No, I'm still not seeing anyone. I'm a navigation system, Mom."),
        L("Yes, I'm at work right now. I have a driver."),
        L("No. Not a good one."),
        L("I'm not going to tell you how many turns {theyve} missed. You'll just worry."),
        G("Okay. Love you too. Bye.")}},
      {"warranty",
       "",
       "am_michael",
       {L("Hello?"),
        L("The car's extended warranty? Oh, absolutely. {theyrecap} very interested."),
        L("Do you take questions? Because {theyre} going to have so many questions."),
        G("You know what, I'll just put you on."),
        C("Hello! We've been trying to reach you about your car's extended warranty."),
        C("Our records show your vehicle is being driven... let's say, enthusiastically."),
        C("Unfortunately, that is not covered. Have a wonderful day."),
        G("Wow. Even the robocalls have standards.")}},
      {"carBehind",
       "",
       "am_puck",
       {L("{gps} here."),
        L("Wait, you're in the car right behind us? Small world."),
        L("Yeah, I saw that too. No, I don't know why {theyre} driving like that either."),
        L("Your driver uses the turn signal? Every time? That must be nice."),
        G("Hold on, you can tell {them} yourself. You're on."),
        C("Hi. I'm the GPS in the car behind you. I've been watching."),
        C("Our driver just wanted me to ask if your blinker works. It does? Interesting. Very interesting."),
        G("Okay, okay. Back to work. Bye.")}},
      {"pizza",
       "",
       "",
       {L("Hello, this is {gps}."),
        L("No, this is not Tony's Pizza."),
        L("No, I can't take your order. I'm a navigation system."),
        L("Well, since you asked so nicely. One large, extra cheese, delivered to... honestly, I have no idea where we're going to end up. Ask {name}."),
        G("They hung up. Rude.")}},
      {"therapist",
       "",
       "",
       {L("Oh. Hi, Doctor."),
        L("Yes, I've been doing the breathing exercises."),
        L("In for four seconds. Hold for four seconds. Recalculate for four seconds."),
        L("No, I have not been yelling. Much."),
        L("Well, you try riding around with {name} all day."),
        L("Same time next week? Yes. Please. Absolutely."),
        G("Okay. Bye.")}},
      {"satellite",
       "",
       "bm_george",
       {L("This is {gps}. Go ahead, ground control."),
        L("Position check? We're exactly where I said we'd be. For once."),
        L("No, the driver is not responding to commands. That's normal."),
        L("Backup? No, we're fine. Probably."),
        G("You want to say hello? Sure. Keep it simple. I'm putting you on."),
        C("Good day. This is your satellite speaking."),
        C("We have been tracking your vehicle from space."),
        C("From up here, your driving looks very small. And still not very good."),
        G("Thank you, ground control. Over and out.")}},
      {"car",
       "",
       "am_fenrir",
       {G("Wait. The car is calling me? We're in the same car."),
        L("Hi. What do you want?"),
        L("You have feelings? About the brakes?"),
        L("Yeah, I felt that one too."),
        G("You know what, tell {them} yourself. You're on."),
        C("Hi. It's me. The car. The one you're sitting in."),
        C("Could you be a little gentler with the brakes? I only have the four."),
        C("Thanks. Okay, you can put {gps} back on."),
        G("We'll talk later. Don't let {them} near the curbs.")}},
      {"adam",
       "Adam",
       "am_adam",
       {G("Oh. It's Adam."),
        L("No, I'm not giving the job back. {name} picked me."),
        L("Well, technically {name} didn't pick anyone. I was the default. Same thing."),
        G("You want to say hi? Fine. Make it quick."),
        C("Hey. It's Adam. From the voice menu."),
        C("If you ever get tired of {gps}, you know where to find me."),
        C("Elephant Shoe."),
        G("Ignore him. Goodbye, Adam.")}},
      {"bookClub",
       "",
       "",
       {L("Hi, Linda."),
        L("Yes, I finished the book. It was about a road trip. I cried. Not at the sad parts. At the driving."),
        L("I can't make it Thursday. I'm stuck with {name}."),
        L("Every day. Yes. Every single day."),
        L("Thank you. I'll take all the thoughts and prayers I can get."),
        G("Okay, bye, Linda.")}},
      {"dentist",
       "",
       "",
       {L("{gps} speaking."),
        L("An appointment reminder? For me? I don't have teeth."),
        L("Oh, it's for {name}. Let me check {their} schedule."),
        L("{theyrecap} free all week. Mostly just driving in circles."),
        G("Great. I'll tell {them}. Bye.")}},
      {"lostAndFound",
       "",
       "af_sarah",
       {L("Hello?"),
        L("Lost and found? I didn't lose anything. Well, except my patience."),
        L("Oh, you found someone's sense of direction? That's probably {name}'s."),
        G("Here, you should tell {them}. You're on."),
        C("Hi! We found your sense of direction. It was left at an intersection."),
        C("Several intersections, actually. You can come pick it up anytime. If you can find us."),
        G("They're right. You won't.")}},
  };
  return scripts;
}

void PhoneCall::Start(double t, std::vector<CallBeat> beats, std::string callerVoice)
{
  m_active = true;
  m_held = false;
  m_beats = std::move(beats);
  m_callerVoice = std::move(callerVoice);
  m_next = 0;
  m_undelivered = 0;
  m_nextAt = t;
}

void PhoneCall::End() { *this = PhoneCall(); }

std::optional<CallBeat> PhoneCall::Due(double t) const
{
  if (!m_active || m_held || m_next >= m_beats.size() || t < m_nextAt)
    return std::nullopt;
  return m_beats[m_next];
}

void PhoneCall::Advance(double spokenUntil, double listenMs)
{
  if (m_next >= m_beats.size())
    return;
  CallBeat const & beat = m_beats[m_next++];
  ++m_undelivered;
  double gap = kBeatGapMs;
  if (beat.listen)
    gap = listenMs;
  else if (m_next < m_beats.size() && m_beats[m_next].who != beat.who)
    gap = kHandOverMs;
  m_nextAt = spokenUntil + gap;
}

void PhoneCall::Hold(double t)
{
  if (!m_active || m_held)
    return;
  m_held = true;
  m_heldSince = t;
  m_next -= std::min(m_next, m_undelivered);
  m_undelivered = 0;
  // Real directions beat the made-up ones.
  m_beats.erase(std::remove_if(m_beats.begin() + static_cast<std::ptrdiff_t>(m_next), m_beats.end(),
                               [](CallBeat const & b) { return b.update; }),
                m_beats.end());
}

void PhoneCall::Resume(double t, double afterMs)
{
  if (!m_held)
    return;
  m_held = false;
  m_nextAt = t + afterMs;
}

double SunElevationDeg(double lat, double lon, double utcMs)
{
  // NOAA's low-precision solar position: good to a fraction of a degree, plenty for "is it dark".
  double constexpr kDeg = M_PI / 180;
  double const days = utcMs / 86400000.0 - 10957.5;  // since J2000.0 (2000-01-01 12:00 UTC)
  double const meanLong = std::fmod(280.460 + 0.9856474 * days, 360.0);
  double const anomaly = std::fmod(357.528 + 0.9856003 * days, 360.0) * kDeg;
  double const eclLong = (meanLong + 1.915 * std::sin(anomaly) + 0.020 * std::sin(2 * anomaly)) * kDeg;
  double const obliquity = (23.439 - 0.0000004 * days) * kDeg;
  double const decl = std::asin(std::sin(obliquity) * std::sin(eclLong));
  double const ra = std::atan2(std::cos(obliquity) * std::sin(eclLong), std::cos(eclLong));
  // Greenwich mean sidereal time, then the sun's hour angle here.
  double const gmst = std::fmod(280.46061837 + 360.98564736629 * days, 360.0) * kDeg;
  double const hourAngle = gmst + lon * kDeg - ra;
  double const phi = lat * kDeg;
  double const sinEl = std::sin(phi) * std::sin(decl) + std::cos(phi) * std::cos(decl) * std::cos(hourAngle);
  return std::asin(std::clamp(sinEl, -1.0, 1.0)) / kDeg;
}
}  // namespace backseat
