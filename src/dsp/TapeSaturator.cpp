#include "TapeSaturator.h"

#include <algorithm>
#include <cmath>

namespace cf
{
namespace { constexpr double kPi = 3.14159265358979323846; }

void TapeSaturator::prepare (double sampleRate)
{
    sr = sampleRate;
    const int factor = sr < 70000 ? 4 : sr < 140000 ? 2 : 1;   // the tape always runs at 176 - 192 kHz
    setup.prepare (sr, 20000.0, factor, kDriveMin, kDriveMax);
    heads.prepare (setup);
    smc    = 1 - std::exp (-1 / (0.02 * sr));
    level.prepare (sr);
    dcC = 1 - std::exp (-2 * kPi * 5.0 / sr);
    eqAmt = -1;
    setAmount (amtT);
    amt = amtT; drive = driveT; make = makeT;
    reset();
}

void TapeSaturator::reset()
{
    heads.reset();
    subsonic.clear(); bump.clear(); gap.clear();
    level.reset();
    dcIn[0] = dcIn[1] = 0;
    updateColour (amt);
    eqCountdown = 0;
}

void TapeSaturator::setAmount (double a, bool snap)
{
    amtT   = std::clamp (a, 0.0, 1.0);
    if (snap) { amt = amtT; drive = setup.driveFor (amt); make = setup.makeupFor (amt); updateColour (amt); }
    driveT = setup.driveFor (amtT);
    makeT  = setup.makeupFor (amtT);
}

// Playback head colour, scaled by the knob: the head bump (a broad lift around 55 Hz, up to about
// 2 dB at full), the playback roll-off below it, and gap loss easing off the top octave.
void TapeSaturator::updateColour (double a)
{
    if (std::abs (a - eqAmt) < 1e-4) return;
    eqAmt = a;
    {   // 2nd-order high-pass, 8 Hz at 0 rising to 16 Hz at full (always on: also blocks DC)
        const double f = 8.0 + 8.0 * a, w = 2 * kPi * f / sr, cs = std::cos (w), al = std::sin (w) / (2 * 0.707);
        subsonic.set ((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + al, -2 * cs, 1 - al);
    }
    {   // head bump: RBJ peaking
        const double f = 55.0, q = 0.9, db = 2.0 * a, A = std::pow (10.0, db / 40), w = 2 * kPi * f / sr;
        const double cs = std::cos (w), al = std::sin (w) / (2 * q);
        bump.set (1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
    }
    {   // gap loss: RBJ high shelf at 15 kHz, down to -1.5 dB at full
        const double f = std::min (15000.0, 0.4 * sr), db = -1.5 * a, A = std::pow (10.0, db / 40), w = 2 * kPi * f / sr;
        const double cs = std::cos (w), al = std::sin (w) / 2 * std::sqrt (2.0), sA = 2 * std::sqrt (A) * al;
        gap.set (A * ((A + 1) + (A - 1) * cs + sA), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sA),
                 (A + 1) - (A - 1) * cs + sA, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sA);
    }
}

template <typename T>
void TapeSaturator::process (T* left, T* right, int n)
{
    for (int i = 0; i < n; ++i)
    {
        amt   += smc * (amtT - amt);
        if (amt != amtT)   // drive and makeup both follow the knob, so the gain never overshoots mid-move
        {
            if (std::abs (amtT - amt) < 1e-6) amt = amtT;
            drive = setup.driveFor (amt);
            make  = setup.makeupFor (amt);
        }
        if (--eqCountdown <= 0) { updateColour (amt); eqCountdown = 32; }

        double in[2] { (double) left[i], (double) right[i] };
        for (int c = 0; c < 2; ++c) { dcIn[c] += dcC * (in[c] - dcIn[c]); in[c] -= dcIn[c]; }
        double rec[2], y[2];
        heads.record (in, drive, make, rec);
        for (int c = 0; c < 2; ++c) y[c] = heads.playback (c, rec[c]);

        const double gain = level.update (in, y, 2);   // what came back, at the loudness that went in

        double out[2];
        for (int c = 0; c < 2; ++c) out[c] = gap.run (c, bump.run (c, subsonic.run (c, y[c] * gain)));
        meterAcc += 0.5 * (out[0] * out[0] + out[1] * out[1]); ++meterN;
        left[i] = (T) out[0];
        if (right != left) right[i] = (T) out[1];
    }
}

template void TapeSaturator::process<float> (float*, float*, int);
template void TapeSaturator::process<double> (double*, double*, int);
} // namespace cf
