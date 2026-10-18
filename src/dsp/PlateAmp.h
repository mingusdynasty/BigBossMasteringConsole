// Chocolate Fudge — the plate's electronics, modelled stage by stage.
//
// A studio plate is as much its amplifiers as its steel: the signal goes through an input transformer
// and a tube driver amplifier into the moving-coil driver, and the two contact pickups each feed a tube
// preamplifier and an output transformer. Each stage here is a small physical model:
//
//   Iron (transformers): the core's flux is the integral of the voltage, so it saturates from the
//     bottom up. Modelled as integrate -> magnetic hysteresis (Jiles-Atherton, a soft, narrow loop
//     for transformer steel) -> differentiate, exactly inverse when quiet. Lows thicken and round
//     off on peaks; the mids and highs pass almost untouched.
//
//   Triode (12AX7 common-cathode stage): the transfer curve is solved from Koren's triode equations
//     on a 100 k load line at 250 V, with grid current through the source resistance when the grid
//     goes positive, and a coupling capacitor that charges on those peaks and shifts the bias for a
//     moment afterwards, so the tube "breathes" with the programme. Mostly 2nd harmonic, then
//     softly asymmetric compression. Run at 2x the plate rate: the 2nd and 3rd harmonics of the whole
//     11 kHz band land where the decimation filter removes them.
//
// Drive sweeps the level into all of it: clean and transparent at 0, a subtle glow by default, then
// plainly vintage. Each stage is normalised to unity gain for quiet signals, and level-matched
// (LevelMatch.h) so that pushing it changes the colour, not the loudness.
#pragma once

#include "HalfBand.h"
#include "TapeHysteresis.h"
#include "LevelMatch.h"

#include <array>
#include <vector>

namespace cf
{
// 12AX7 stage as a lookup table: grid source voltage -> plate voltage swing, solved on the load line
class TriodeCurve
{
public:
    void build();                                     // ~20 ms; use shared() instead
    static const TriodeCurve& shared();               // built once per process (it doesn't depend on the rate)
    inline double plate (double vs) const             // source volts -> output (volts, inverted, DC removed)
    {
        const double p = (std::clamp (vs, kLo, kHi) - kLo) * scale;
        const int i = std::min ((int) p, kN - 2);
        const double f = p - i;
        return tab[(std::size_t) i] + f * (tab[(std::size_t) i + 1] - tab[(std::size_t) i]);
    }
    inline double gridCurrent (double vs) const       // grid conduction (for the bias shift), arbitrary units
    {
        const double p = (std::clamp (vs, kLo, kHi) - kLo) * scale;
        const int i = std::min ((int) p, kN - 2);
        const double f = p - i;
        return ig[(std::size_t) i] + f * (ig[(std::size_t) i + 1] - ig[(std::size_t) i]);
    }
    double gain() const { return g0; }                // small-signal gain at the bias point (magnitude)
    double bias() const { return vbias; }             // grid-cathode bias at rest (V, negative)

private:
    static constexpr int kN = 4096;
    static constexpr double kLo = -14.0, kHi = 8.0;
    double scale = (kN - 1) / (kHi - kLo), g0 = 1, vbias = -1.4;
    std::vector<double> tab, ig;
};

class TriodeStage
{
public:
    void prepare (const TriodeCurve& c, double rate);
    void reset();
    // one sample at the oversampled rate; x in signal units, voltsPerUnit sets how hard the grid is driven
    inline double process (double x, double voltsPerUnit)
    {
        const double vs = x * voltsPerUnit + curve->bias() - shift;
        // the coupling capacitor charges while the grid conducts and leaks back through the grid resistor
        igLp += igC * (curve->gridCurrent (vs) - igLp);          // the cap charges through the source: ~1 ms
        shift += chargeC * igLp - leakC * shift;
        shift = shift < 1e-12 ? 0.0 : std::min (shift, 1.0);   // the bias can be pushed back by up to a volt
        return -(curve->plate (vs) - rest) / (curve->gain() * voltsPerUnit);
    }

private:
    const TriodeCurve* curve = nullptr;
    double shift = 0, igLp = 0, chargeC = 0, leakC = 0, igC = 0, rest = 0;   // rest: output at the bias point (exactly 0 out)
};

// integrate -> hysteresis -> differentiate (a transformer's core)
class IronStage
{
public:
    void prepare (double rate, double cornerHz);
    void reset();
    inline double process (double x, double field, double makeup)
    {
        flux = ia * flux + ib * x;                              // leaky integrator: flat below the corner
        const double m = core.process (field * flux) * makeup;  // magnetisation
        const double y = (m - ia * mPrev) / ib;                 // exact inverse of the integrator
        mPrev = m;
        return y;
    }

private:
    TapeHysteresis core;
    double ia = 0, ib = 1, flux = 0, mPrev = 0;
};

class PlateAmp
{
public:
    void prepare (double plateRate);
    void reset();
    void setDrive (double drive01, bool snap = false);   // the live values glide (~150 ms) unless snap

    // input transformer and driver amplifier (mono, plate rate). Call once per plate sample, before output().
    double input (double x);
    // pickup preamplifiers and output transformers (stereo, plate rate, in place)
    void output (double& l, double& r);

    // transformer steel: a much narrower, more reversible loop than tape
    static constexpr double kIronCoercivity = 0.05, kIronReversible = 0.985;

private:
    void glide();
    void apply (double drive01);
    double ironMakeup (double field) const;

    TriodeStage driver, pickup[2];
    IronStage ironIn, ironOut[2];
    // 2x oversampling around the tubes, keeping 0 - 11 kHz
    double hb1[7] {};
    HalfBand inUp, inDn;
    StereoHalfBand outUp, outDn;
    // coupling capacitors (~8 Hz): before each transformer, as no iron can take DC (it would sit the
    // core part-way up its curve), and after each tube
    double dcPre = 0, dcIn = 0, dcMid[2] {}, dcOut[2] {}, dcC = 0;
    LevelMatch levelIn, levelOut;                         // driven harder, the amps squash but don't just get quieter

    // drive (live and target), and what follows from it: grid volts per unit for each tube, field per
    // unit and makeup for the iron
    double driveS = 0.3, driveT = 0.3, smc = 0;
    double vIn = 0.2, vOut = 0.1, fIn = 0.3, fOut = 0.2, mIn = 1, mOut = 1;
    std::array<double, 33> ironCal {};                    // makeup vs field (log-spaced)
    double ironCalLo = 0.02, ironCalHi = 10.0;
};
} // namespace cf
