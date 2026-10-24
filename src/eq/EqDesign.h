// The Class-A mastering EQ's band shapes, in one place, so the audio and the editor's response curve
// are always the same filters.
//
// Five bands in the manner of the elysia museq: each band's gain knob only goes one way, and a Cut
// switch per band turns a boost into a cut with a different, narrower curve: boosts are broad and
// gentle, cuts are tighter, as on the hardware. The outer two bands switch between shelf and bell.
// Warm mode broadens the boosts a little further and adds the class-A stage's low-order harmonics.
// The shapes are voiced from the unit's published description, not measured from one.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace bb::eq
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kBands = 5;

struct Band
{
    double freq = 1000;   // Hz
    double gain = 0;      // dB, 0 .. kMaxGain; the Cut switch gives it its sign
    bool cut = false;
    bool shelf = false;   // bands 0 and 4 only
};

struct Settings
{
    Band band[kBands];
    bool warm = false;
    double hpfHz = 0;       // 0 = off
    int hpfOrder = 2;       // 2 (12 dB/oct) or 4 (24 dB/oct)
    double lpfHz = 0;       // 0 = off; 12 dB/oct
    double monoBassHz = 0;  // 0 = off: the side signal is high-passed here (24 dB/oct)
    double outputDb = 0;
    int stereo = 0;         // where the five bands work: 0 both channels, 1 mid only, 2 side only
};

constexpr double kMaxGain = 12.0;

struct BandRange { double lo, hi, def; bool canShelf; const char* name; };
constexpr BandRange kRange[kBands] = {
    { 20.0,   300.0,   60.0,    true,  "LOW" },
    { 40.0,   800.0,   200.0,   false, "LOW MID" },
    { 200.0,  4000.0,  1000.0,  false, "MID" },
    { 800.0,  12000.0, 4000.0,  false, "HIGH MID" },
    { 2500.0, 28000.0, 12000.0, true,  "HIGH" },
};

struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

// The Q a bell or shelf gets: boosts broad, cuts narrower; Warm softens boosts further
inline double bandQ (const Band& b, bool warm)
{
    if (b.shelf) return b.cut ? 0.9 : (warm ? 0.55 : 0.65);
    if (b.cut) return 1.4;
    return warm ? 0.5 : 0.6;
}

// RBJ cookbook shapes with quality Q (bandQ() for the switch settings), frequency prewarped: they
// run at 176.4 kHz or more, so the bilinear transform's cramping is far above the audio band
inline Coef design (const Band& b, int index, double Q, double rate)
{
    Coef c;
    const double db = b.cut ? -b.gain : b.gain;
    if (std::abs (db) < 1e-6) return c;
    const double f = std::clamp (b.freq, 10.0, 0.45 * rate);
    const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * f / rate, cs = std::cos (w), al = std::sin (w) / (2 * Q);
    double b0, b1, b2, a0, a1, a2;
    if (b.shelf && (index == 0 || index == kBands - 1))
    {
        const double sA = 2 * std::sqrt (A) * al, s = index == 0 ? 1.0 : -1.0;   // low shelf, high shelf
        b0 = A * ((A + 1) - s * (A - 1) * cs + sA); b1 = s * 2 * A * ((A - 1) - s * (A + 1) * cs); b2 = A * ((A + 1) - s * (A - 1) * cs - sA);
        a0 = (A + 1) + s * (A - 1) * cs + sA;       a1 = -s * 2 * ((A - 1) + s * (A + 1) * cs);    a2 = (A + 1) + s * (A - 1) * cs - sA;
    }
    else
    {
        b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A;
        a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A;
    }
    c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0;
    return c;
}

// Butterworth high- or low-pass section with quality q
inline Coef pass (bool high, double f, double q, double rate)
{
    Coef c;
    const double w = 2 * kPi * std::clamp (f, 5.0, 0.45 * rate) / rate, cs = std::cos (w), al = std::sin (w) / (2 * q);
    const double a0 = 1 + al;
    if (high) { c.b0 = (1 + cs) / 2 / a0; c.b1 = -(1 + cs) / a0; c.b2 = c.b0; }
    else      { c.b0 = (1 - cs) / 2 / a0; c.b1 = (1 - cs) / a0;  c.b2 = c.b0; }
    c.a1 = -2 * cs / a0; c.a2 = (1 - al) / a0;
    return c;
}

inline double magnitude (const Coef& c, double f, double rate)
{
    const std::complex<double> z = std::polar (1.0, -2 * kPi * f / rate), z2 = z * z;
    return std::abs ((c.b0 + c.b1 * z + c.b2 * z2) / (1.0 + c.a1 * z + c.a2 * z2));
}

// The whole EQ's response on the mid signal, in dB (what the editor draws)
inline double responseDb (const Settings& s, double f, double rate)
{
    double g = 1;
    for (int i = 0; i < kBands; ++i) g *= magnitude (design (s.band[i], i, bandQ (s.band[i], s.warm), rate), f, rate);
    if (s.hpfHz > 0)
    {
        if (s.hpfOrder >= 4) g *= magnitude (pass (true, s.hpfHz, 0.5412, rate), f, rate) * magnitude (pass (true, s.hpfHz, 1.3066, rate), f, rate);
        else g *= magnitude (pass (true, s.hpfHz, 0.7071, rate), f, rate);
    }
    if (s.lpfHz > 0) g *= magnitude (pass (false, s.lpfHz, 0.7071, rate), f, rate);
    return 20 * std::log10 (std::max (g, 1e-9)) + s.outputDb;
}
} // namespace bb::eq
