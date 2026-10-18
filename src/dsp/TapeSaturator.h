// Hot Sauce — one-knob magnetic tape saturation, at the session's full bandwidth.
//
// The same record head, tape and playback head as Chocolate Fudge's echo (TapeHead.h): record EQ,
// Jiles-Atherton hysteresis at 4x oversampling (2x from 88.2 kHz, none from 176.4 kHz), matching
// playback EQ. Around it, the parts of a tape machine that colour the sound without being saturation:
// the playback head's bass bump and low-end roll-off, and a touch of gap loss at the very top, both
// scaled with the knob. The output is level-matched, so the knob changes character, not loudness.
#pragma once

#include "TapeHead.h"
#include "LevelMatch.h"

namespace cf
{
class TapeSaturator
{
public:
    void prepare (double sampleRate);   // allocates nothing; call off the audio thread
    void reset();
    void setAmount (double amount01, bool snap = false);   // the one knob, 0..1; glides ~20 ms unless snap

    // stereo in place; mono is fine with the same pointer twice
    template <typename T> void process (T* left, T* right, int n);

    int latencySamples() const { return (int) std::lround (setup.latency()); }
    // mean square of the output since the last call (for a meter)
    double takeMeterLevel() { const double v = meterN > 0 ? meterAcc / (double) meterN : 0.0; meterAcc = 0; meterN = 0; return v; }

    // 0 VU = -18 dBFS: at 0 the tape is close to clean (0.2% THD at 0 VU), at the 35% default it is
    // a well-driven reel (0.7%), at 100% it is pushed into heavy saturation (14%)
    static constexpr double kDriveMin = 1.0, kDriveMax = 30.0;

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
        inline double run (int c, double x)
        {
            const double y = b0 * x + z1[c];
            z1[c] = b1 * x - a1 * y + z2[c];
            z2[c] = b2 * x - a2 * y;
            return y;
        }
        void clear() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
        void set (double nb0, double nb1, double nb2, double a0, double na1, double na2)
        {
            b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
        }
    };
    void updateColour (double amount);

    double sr = 48000;
    TapeHeadSetup setup;
    TapeHeads heads;
    double amtT = 0.35, amt = 0.35, drive = 1, driveT = 1, make = 1, makeT = 1, smc = 0;
    double eqAmt = -1;                 // amount the colour filters were last designed for
    Biquad subsonic, bump, gap;        // playback head's low end, head bump, gap loss
    LevelMatch level;
    double meterAcc = 0; long meterN = 0;
    double dcIn[2] {}, dcC = 0;        // input coupling capacitor (~5 Hz): DC would bias the tape
    int eqCountdown = 0;
};
} // namespace cf
