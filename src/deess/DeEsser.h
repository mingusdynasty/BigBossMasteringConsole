// Multiband de-esser of our own design: transparent when idle, level independent, and precise.
//
// Four bands, half an octave apart from the FREQ setting up (4.5, 6.4, 9 and 12.7 kHz by
// default), each a dynamic cut of the form  y = x - (1 - g) * bandpass(x).  With nothing to do
// (g = 1) every band is an exact wire, so the de-esser is bit-transparent until it acts, and each
// band only ever cuts its own region, so an "s" at 6 kHz leaves 13 kHz air alone.
//
// Detection is relative, not absolute: each band's energy is compared with the energy of the voice
// body (300 Hz to 3 kHz) over the same moment, so the same "s" is caught the same way on a quiet
// verse and a limited chorus, and a bright but steady mix is not mistaken for sibilance. Threshold
// is therefore in dB of band energy over body energy. A short lookahead (1 ms) lets the cut arrive
// with the consonant's onset instead of after it.
//
// Modes: STEREO (both channels, gains linked), MID (only the centre, where vocals live), SIDE.
// LISTEN plays what is being removed.
#pragma once

#include <array>
#include <atomic>
#include <vector>

namespace bb
{
class DeEsser
{
public:
    static constexpr int kBands = 4;
    enum Mode { Stereo = 0, Mid = 1, Side = 2 };

    struct Settings
    {
        double thresholdDb = -6;   // band over body, -24 .. +12
        double rangeDb = 8;        // most a band may cut, 0 .. 18
        double freq = 4500;        // lowest band's centre, 3 .. 8 kHz
        double releaseMs = 50;     // 10 .. 200
        int mode = Mid;
        bool listen = false;       // play the removed signal
    };

    // centres, half an octave apart, kept under 16 kHz and 0.42 of the rate: a band that would land
    // there is placed at that cap, or dropped (0) if that puts it within a quarter octave of the band
    // below it
    static double bandFreq (double lowest, int band, double sampleRate);

    void prepare (double sampleRate, int maxBlock);   // allocates
    void reset();
    void setSettings (const Settings& s) { target = s; }
    void process (double* l, double* r, int n);
    int latency() const { return lookahead; }

    float reduction (int band) const { return gr[(size_t) band].load (std::memory_order_relaxed); }   // dB, last block's most

private:
    struct Biquad
    {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
        inline double run (int c, double x)
        {
            const double y = b0 * x + z1[c];
            z1[c] = b1 * x - a1 * y + z2[c];
            z2[c] = b2 * x - a2 * y;
            return y;
        }
        void clear() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    };
    void design (double freq);

    double sr = 48000, designedFreq = -1;
    int lookahead = 48, mask = 0, pos = 0;
    Settings target;
    std::array<Biquad, kBands> detect, apply;
    Biquad bodyHp, bodyLp;
    std::array<double, kBands> bandMs {}, gainDb {};
    double bodyMs = 0, detC = 0, bodyC = 0, bodyAtkC = 0, atkC = 0, glide = 0, listenW = 0;
    std::array<bool, kBands> active {};   // false: the band doesn't fit under the cap, so it never cuts
    int lastMode = Mid;
    bool modeFading = false;   // a mode change waits until the cuts have faded out
    double fadeC = 0;
    std::array<std::vector<double>, 2> delay;
    std::array<std::atomic<float>, kBands> gr {};
};
} // namespace bb
