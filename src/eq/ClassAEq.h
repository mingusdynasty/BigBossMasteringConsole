// Class-A mastering equaliser: five museq-style bands, high- and low-pass filters, mono bass, Warm.
//
// Runs oversampled (176.4 kHz or more) so bells and shelves keep their analogue shape right up to
// 20 kHz. Continuous controls glide (20 ms), and switches never click: a band whose Cut or Shelf
// switch flips fades its gain to 0 dB, changes shape, and fades back; filters fade in and out.
#pragma once

#include "EqDesign.h"
#include "dsp/Oversampler.h"

#include <array>

namespace bb
{
class ClassAEq
{
public:
    void prepare (double sampleRate, int maxBlock);   // allocates
    void reset();
    void setSettings (const eq::Settings& s) { target = s; }   // audio thread, once per block
    void process (double* l, double* r, int n);
    int latency() const { return (int) std::lround (os.latency()); }

private:
    struct Biquad
    {
        eq::Coef c;
        double z1[2] {}, z2[2] {};
        inline double run (int ch, double x)
        {
            const double y = c.b0 * x + z1[ch];
            z1[ch] = c.b1 * x - c.a1 * y + z2[ch];
            z2[ch] = c.b2 * x - c.a2 * y;
            return y;
        }
        void clear() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    };
    struct BandState
    {
        Biquad f;
        double freq = 1000, gain = 0, q = 0.6;   // current (smoothed) values
        bool cut = false, shelf = false;         // the shape running now
    };
    struct Filter
    {
        Biquad s1, s2;
        double freq = 20, mix = 0;   // mix: 0 bypassed .. 1 in
        int order = 2;
        bool want = false;
    };

    void update();   // glide every control one step toward its target, redesign what moved
    void updateFilter (Filter& f, bool high, double wantHz, int wantOrder);

    Oversampler os;
    double rate = 192000, glide = 0.1, mixStep = 0.01;
    eq::Settings target;
    std::array<BandState, eq::kBands> band;
    Filter hpf, lpf, mono;
    int stereoNow = 0;              // where the bands work now (Settings::stereo); changes at 0 dB
    double warm = 0, outGain = 1;   // warm: 0 .. 1 (harmonics fade with the switch)
    double dc[2] {}, dcC = 0;
    int countdown = 0;
};
} // namespace bb
