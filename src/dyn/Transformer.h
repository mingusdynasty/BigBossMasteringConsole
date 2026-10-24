// An output transformer's core, after the iron in chocolatefudgeverb's PlateAmp: a core's flux is the
// integral of the voltage across it, so it saturates from the bottom up. Loud lows round off and
// thicken while mids and highs pass almost untouched, and the distortion rises about 6 dB per octave
// as the frequency falls, as it does on real iron.
//
//     flux = ia * flux + ib * x               leaky integrator, flat below the corner (40 Hz)
//     m    = JA (field * flux + bias) * makeup Jiles-Atherton magnetisation (src/dsp/TapeHysteresis.h)
//     y    = (m - ia * m_prev) / ib            the exact inverse of the integrator
//
// When the core is linear the stage is exactly transparent. A bias (a standing field, as from a DC
// current through a single-ended winding) makes the loop asymmetric, which gives even harmonics.
// DC in the signal goes round the core, so it never parks the core part-way up its curve, and what
// the core adds is DC-blocked, so remanence never leaves an offset.
#pragma once

#include "dsp/TapeHysteresis.h"

#include <cmath>

namespace bb
{
class TransformerCore
{
public:
    struct Voicing
    {
        double field;   // flux to field: how hard the programme drives the core
        double k, c;    // coercivity and reversible fraction (loop width, and how much memory it has)
        double bias;    // standing field, for asymmetry (even harmonics); 0 for a symmetric core
    };

    void prepare (double rate, const Voicing& v, double cornerHz = 40.0)
    {
        voicing = v;
        sr = rate;
        ia = std::exp (-2.0 * 3.14159265358979323846 * cornerHz / rate);
        ib = 1.0 - ia;
        dcC = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * 5.0 / rate);
        dcR = 1.0 - 2.0 * 3.14159265358979323846 * 5.0 / rate;
        makeup = 1.0;
        // unity gain for a moderate 1 kHz tone (the core is linear there), measured through the stage
        Channel probe;
        prime (probe);
        const int per = (int) std::lround (rate / 1000.0);
        double re = 0, im = 0;
        for (int n = 0; n < 40 * per; ++n)
        {
            const double ph = 2.0 * 3.14159265358979323846 * n / per;
            const double y = run (probe, 0.1 * std::sin (ph));
            if (n >= 20 * per) { re += y * std::sin (ph); im += y * std::cos (ph); }
        }
        const double g = 2.0 * std::hypot (re, im) / (20.0 * per) / 0.1;
        makeup = g > 1e-9 ? 1.0 / g : 1.0;
        reset();
    }

    void reset() { for (auto& c : ch) prime (c); }

    inline double process (int channel, double x) { return run (ch[channel], x); }

private:
    struct Channel
    {
        cf::TapeHysteresis core;
        double flux = 0, mPrev = 0, m0 = 0, dc = 0, colourX = 0, colourY = 0;
    };

    // a quiet core sitting at its standing field, so the first sample doesn't kick it
    void prime (Channel& c) const
    {
        c.core.setTape (voicing.k, voicing.c);
        c.core.prepare (sr);
        c.flux = 0; c.dc = 0;
        double m = 0;
        const int n = 256;
        for (int i = 1; i <= n; ++i) m = c.core.process (voicing.bias * i / n);
        for (int i = 0; i < 64; ++i) m = c.core.process (voicing.bias);
        c.m0 = c.mPrev = m * makeup;
        c.colourX = c.colourY = 0;
    }

    inline double run (Channel& c, double x) const
    {
        c.dc += (x - c.dc) * dcC;   // DC goes round the core
        const double ac = x - c.dc;
        c.flux = ia * c.flux + ib * ac;
        const double m = c.core.process (voicing.field * c.flux + voicing.bias) * makeup;
        const double y = ((m - c.m0) - ia * (c.mPrev - c.m0)) / ib;
        c.mPrev = m;
        // only what the core adds is kept, below 5 Hz taken out: remanence after a loud passage
        // would otherwise leave a small DC offset
        const double colour = y - ac;
        const double blocked = colour - c.colourX + dcR * c.colourY;
        c.colourX = colour;
        c.colourY = blocked;
        return x + blocked;
    }

    Voicing voicing { 1.0, 0.05, 0.985, 0.0 };
    double sr = 48000, ia = 0.99, ib = 0.01, dcC = 0, dcR = 1, makeup = 1;
    Channel ch[2];
};
} // namespace bb
