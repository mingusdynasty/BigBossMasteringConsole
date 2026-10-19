// Two-stage mastering compressor in the manner of the Shadow Hills Mastering Compressor: an optical
// stage for slow, program-dependent levelling, then a discrete VCA stage for control, then a choice
// of output transformer.
//
// Optical stage: a soft-knee feed-forward cell whose release depends on its history, like an
// electroluminescent panel and photocell: short bursts let go in about 60 ms, sustained compression
// slows the release toward 1.5 s. Discrete stage: threshold, ratio 1.2:1 to 6:1 or Flood, six
// attack and six recovery times, the sixth recovery being a dual (fast then slow) release.
// Transformers: Nickel (clean), Iron (low-frequency second harmonic), Steel (low-frequency third
// harmonic and weight). The two stages see the signal through an optional 90 Hz sidechain
// high-pass, and both channels are always linked so the stereo image holds still.
//
// Voiced from the unit's published manual and descriptions, not measured from one.
#pragma once

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
    double optoMs = 0, vcaMs = 0, optoDetC = 0, vcaDetC = 0;

    // optical cell: reduction (dB) and its memory of how long it has been working
    double optoGr = 0, optoMemory = 0, optoAtkC = 0, memChargeC = 0, memDrainC = 0;
    // discrete cell: reduction (dB); the slow half of the dual release
    double vcaGr = 0, vcaSlow = 0;

    // gliding makeup and the transformer blend
    double optoGainLin = 1, vcaGainLin = 1, glide = 0;
    double xfLow[2] {}, xfDcX[2] {}, xfDcY[2] {}, xfLowC = 0, xfDcR = 0;
    double xfWeight[3] { 1, 0, 0 };

    std::atomic<float> grOpto { 0 }, grVca { 0 };
};
} // namespace bb
