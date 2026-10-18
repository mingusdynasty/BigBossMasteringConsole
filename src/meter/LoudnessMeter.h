// Loudness to ITU-R BS.1770-4 and EBU R128 (Tech 3341 / 3342).
//
// Momentary (400 ms), short-term (3 s), integrated (gated, from the last reset) and loudness range.
// The audio is K-weighted and its energy summed in 100 ms steps; every step closes a 400 ms block
// (75% overlap) and a 3 s window. Gated quantities come from fixed histograms (0.01 LU bins that also
// keep each bin's summed energy), so an hour of programme costs no more memory or time than a second
// and nothing is allocated after prepare().
#pragma once

#include "KWeighting.h"

#include <algorithm>
#include <array>
#include <vector>

namespace bb
{
class LoudnessMeter
{
public:
    static constexpr double kSilence = -200.0;   // what a quantity reads before it has a value

    void prepare (double sampleRate, int numChannels);   // allocates; call off the audio thread
    void reset();                                        // restarts integrated, LRA and the windows

    // channel pointers, n samples; channels beyond numChannels are ignored
    void process (const double* const* channels, int n);

    double momentary() const  { return momentaryLufs; }
    double shortTerm() const  { return shortTermLufs; }
    double integrated() const { return integratedLufs; }
    double range() const      { return lra; }          // LU, 0 until there is enough programme
    double maxMomentary() const { return maxM; }
    double maxShortTerm() const { return maxS; }
    // true once per completed 100 ms step since the last call (for a history display)
    bool takeStep() { const bool s = stepped; stepped = false; return s; }

private:
    struct Histogram
    {
        static constexpr double kLo = -70.0, kHi = 10.0, kStep = 0.01;
        static constexpr int kBins = (int) ((kHi - kLo) / kStep);
        std::vector<unsigned> count;
        std::vector<double> energy;
        double totalEnergy = 0; unsigned long totalCount = 0;
        void init() { count.assign (kBins, 0); energy.assign (kBins, 0.0); clear(); }
        void clear() { std::fill (count.begin(), count.end(), 0u); std::fill (energy.begin(), energy.end(), 0.0); totalEnergy = 0; totalCount = 0; }
        static int binOf (double lufs);
        void add (double e, double lufs);
    };

    void closeStep();
    void updateIntegrated();
    void updateRange();

    static double toLufs (double e) { return e > 0 ? -0.691 + 10.0 * std::log10 (e) : kSilence; }

    int nch = 2, stepLen = 4800, stepPos = 0, stepsDone = 0;
    std::array<KWeighting, 2> kw;
    double stepAcc = 0;
    std::array<double, 30> steps {};      // mean-square energy (channel-summed) of the last 30 steps
    int stepHead = 0;

    Histogram blocks, shortTerms;
    double momentaryLufs = kSilence, shortTermLufs = kSilence, integratedLufs = kSilence, lra = 0;
    double maxM = kSilence, maxS = kSilence;
    bool stepped = false;
};
} // namespace bb
