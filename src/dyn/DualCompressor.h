// Two-stage mastering compressor in the manner of the Shadow Hills Mastering Compressor: an optical
// stage for slow, program-dependent levelling, then a discrete VCA stage for control, then a choice
// of output transformer (dyn/Transformer.h: flux-driven Jiles-Atherton cores).
//
// Optical stage: a soft-knee feed-forward cell whose release depends on its history, like an
// electroluminescent panel and photocell. It pulls down faster the further it has to go (10 ms for
// a small move, down to 3 ms for a big one), and lets go in two parts, as a photocell does: a fast
// part (60 ms) and a slow part whose share and time grow with how long the cell has been held down,
// so short bursts let go almost at once and sustained compression lets go over a second or more. Discrete stage: threshold, ratio 1.2:1 to 6:1 or Flood, six
// attack and six recovery times, the sixth recovery being a dual (fast then slow) release.
// Transformers: Nickel (clean), Iron (low-frequency second harmonic), Steel (low-frequency third
// harmonic; loud lows round off). Their distortion rises as the frequency falls, as on real iron. The two stages see the signal through an optional 90 Hz sidechain
// high-pass. Linked (the default), both channels share one gain so the stereo image holds still;
// unlinked, each channel has its own detectors and cells (dual mono).
//
// Voiced from the unit's published manual and descriptions, not measured from one.
#pragma once

#include "Transformer.h"

#include <atomic>
#include <cmath>

namespace bb
{
class DualCompressor
{
public:
    enum Transformer { Nickel = 0, Iron = 1, Steel = 2 };
    static constexpr int kRatios = 6, kAttacks = 6, kRecoveries = 6;
    static constexpr double kRatio[kRatios] = { 1.2, 2.0, 3.0, 4.0, 6.0, 20.0 };     // last: Flood
    static constexpr double kAttackMs[kAttacks] = { 0.1, 0.3, 1.0, 3.0, 10.0, 30.0 };
    static constexpr double kRecoveryMs[kRecoveries] = { 100, 250, 400, 800, 1200, 0 };   // last: Dual

    struct Settings
    {
        bool opto = true;
        double optoThreshold = -16;   // dBFS
        double optoGain = 0;          // dB
        bool vca = true;
        double vcaThreshold = -12;    // dBFS
        int ratio = 1, attack = 3, recovery = 5;
        double vcaGain = 0;           // dB
        int transformer = Nickel;
        bool sidechainHpf = true;     // 90 Hz, so bass does not drive the compression
        bool link = true;             // false: dual mono
    };

    void prepare (double sampleRate, int maxBlock);   // allocates
    void reset();
    void setSettings (const Settings& s) { target = s; }
    void process (double* l, double* r, int n);
    int latency() const { return 0; }

    // gain reduction of each stage, dB (positive), for the meters
    float optoReduction() const { return grOpto.load (std::memory_order_relaxed); }
    float vcaReduction() const { return grVca.load (std::memory_order_relaxed); }

    // the static curves, also used by the tests: reduction in dB for a level in dBFS
    static double optoCurve (double levelDb, double thresholdDb);
    static double vcaCurve (double levelDb, double thresholdDb, double ratio);

private:
    double sr = 48000;
    Settings target;

    // sidechain: 90 Hz 2nd-order high-pass (faded in and out), then mean-square detectors
    double hb0 = 1, hb1 = 0, hb2 = 0, ha1 = 0, ha2 = 0, hz1[2] {}, hz2[2] {}, hpfMix = 1;
    // per channel from here; linked, both channels are fed the same detector input, so their states
    // are equal, and a link switch just lets them drift together or apart
    double optoMs[2] {}, vcaMs[2] {}, optoDetC = 0, vcaDetC = 0;

    // optical cell: the fast and slow parts of its reduction (dB), and its memory of how long it has
    // been working
    double optoFast[2] {}, optoSlow[2] {}, optoMemory[2] {}, memChargeC = 0, memDrainC = 0;
    // discrete cell: reduction (dB); the slow half of the dual release
    double vcaGr[2] {}, vcaSlow[2] {};

    // gliding makeup and the transformer blend
    double optoGainLin = 1, vcaGainLin = 1, glide = 0;
    TransformerCore cores[3];   // Nickel, Iron, Steel
    double xfWeight[3] { 1, 0, 0 };

    std::atomic<float> grOpto { 0 }, grVca { 0 };
};
} // namespace bb
