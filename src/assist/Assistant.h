// The assistant: listens to a stretch of the mix and sets the console up for it, for people who don't
// yet know where to start. No JUCE, so it can be tested.
//
// It measures the input (loudness, dynamics, tonal balance against a mastered-music reference,
// sibilance, low-end width), picks settings for each module from rules a mastering engineer would use
// as a starting point, and then renders the captured audio through a private copy of the chain to
// check them: the compressor's thresholds are moved until it does the intended amount of work, and
// the limiter's drive until the result lands on the loudness target. With a reference track's
// profile and the MATCH REF goal, the reference stands in for the built-in master curve and sets the
// loudness, density and limiter style.
//
// It also listens for resonances (narrow peaks that stay put while the music moves, between 300 Hz
// and 8 kHz) and for how wide each band is, and says what it hears.
#pragma once

#include "deess/DeEsser.h"
#include "dyn/DualCompressor.h"
#include "dyn/Limiter.h"
#include "eq/EqDesign.h"
#include "tape/TapeMachine.h"

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace bb::assist
{
enum class Goal { Streaming = 0, Loud = 1, Club = 2, Dynamic = 3, Reference = 4 };
double targetLufs (Goal);
const char* goalName (Goal);

// the console's modules, in the processor's slot order
enum Slot { Eq = 0, Comp = 1, Tape = 2, Deess = 3, Limit = 4, kSlots = 5 };
using Order = std::array<int, kSlots>;   // order[k] is the slot that runs k-th

// what the capture measured
struct Profile
{
    double seconds = 0;
    double integrated = -200, range = 0;   // LUFS, LU
    double peak = -200;                    // sample peak, dBFS
    double plr = 0;                        // peak to loudness ratio, dB
    // tonal balance against the reference, dB (positive: more than a typical master); tilted 4.5 dB/oct
    double low = 0, lowMid = 0, mid = 0, highMid = 0, air = 0;
    double sibilance = -100;               // the 5-10 kHz band's share of the energy at its peaks, dB (0 = all of it)
    double bassWidth = -100;               // side over mid energy below 120 Hz, dB
    // side over mid energy, dB, in four bands: 120-500 Hz, 500 Hz-2 kHz, 2-6 kHz, 6-16 kHz
    std::array<double, 4> width { -100, -100, -100, -100 };
    // narrow peaks that hold through the capture: frequency and how far they stand over their
    // surroundings (the median across frames), strongest first, at most three
    struct Resonance { double hz = 0, db = 0; };
    std::vector<Resonance> resonances;
};

struct Plan
{
    Profile in;
    eq::Settings eq;
    DualCompressor::Settings comp;
    TapeMachine::Settings tape;
    DeEsser::Settings deess;
    Limiter::Settings limit;
    std::array<bool, kSlots> bypass {};
    double inTrimDb = 0;
    double outLufs = -200, outTruePeak = -200, compReductionDb = 0;   // measured on the render
    std::vector<std::string> notes;                                   // what it did and why, for the display
};

Profile analyse (const std::vector<float>& l, const std::vector<float>& r, double sampleRate);

// analyses, decides and checks by rendering; can take a few seconds, so call it off the message
// thread. cancel is polled between renders. reference: the reference track's profile, used by
// Goal::Reference (without one, that goal plans for streaming and says so)
Plan plan (const std::vector<float>& l, const std::vector<float>& r, double sampleRate, Goal, const Order&,
           const std::atomic<bool>* cancel = nullptr, const Profile* reference = nullptr);
} // namespace bb::assist
