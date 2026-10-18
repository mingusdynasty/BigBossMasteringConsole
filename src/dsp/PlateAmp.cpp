#include "PlateAmp.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace cf
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// Koren's 12AX7: plate current (A) for grid-cathode and plate-cathode voltages
double triodeIp (double vgk, double vpk)
{
    constexpr double mu = 100, ex = 1.4, kg1 = 1060, kp = 600, kvb = 300;
    if (vpk <= 0) return 0;
    const double arg = kp * (1 / mu + vgk / std::sqrt (kvb + vpk * vpk));
    const double e1 = vpk / kp * (arg > 30 ? arg : std::log1p (std::exp (arg)));
    return e1 > 0 ? 2 * std::pow (e1, ex) / kg1 : 0;
}

constexpr double kBplus = 250, kRa = 100e3, kRk = 1.5e3;   // supply, plate load, cathode resistor (bypassed)
constexpr double kRs = 33e3, kGridK = 2.5e-4;               // source + grid stopper; grid diode (A / V^1.5)

// plate voltage on the load line for a given grid-cathode voltage (bisection; f is monotonic in vp)
double loadLine (double vgk)
{
    double lo = 0, hi = kBplus;
    for (int i = 0; i < 64; ++i)
    {
        const double vp = 0.5 * (lo + hi);
        if (vp - kBplus + kRa * triodeIp (vgk, vp) > 0) hi = vp; else lo = vp;
    }
    return 0.5 * (lo + hi);
}

// grid voltage actually reached through the source resistance once the grid starts conducting
double gridVoltage (double vs)
{
    if (vs <= 0) return vs;
    double lo = 0, hi = vs;
    for (int i = 0; i < 60; ++i)
    {
        const double vg = 0.5 * (lo + hi);
        if (vg + kRs * kGridK * std::pow (vg, 1.5) > vs) hi = vg; else lo = vg;
    }
    return 0.5 * (lo + hi);
}
} // namespace

// ---------------------------------------------------------------- triode

void TriodeCurve::build()
{
    // cathode bias: Vgk = -Ip * Rk at the operating point
    double vb = -1.0;
    for (int i = 0; i < 60; ++i) vb = 0.5 * vb + 0.5 * (-triodeIp (vb, loadLine (vb)) * kRk);
    vbias = vb;

    tab.assign (kN, 0.0);
    ig.assign (kN, 0.0);
    for (int i = 0; i < kN; ++i)
    {
        const double vs = kLo + i / scale;
        const double vg = gridVoltage (vs);
        tab[(std::size_t) i] = loadLine (vg);
        ig[(std::size_t) i]  = vg > 0 ? kGridK * std::pow (vg, 1.5) : 0.0;
    }
    // swing about the rest point, and the small-signal gain there
    const double rest = loadLine (vbias);
    for (auto& v : tab) v -= rest;
    const double h = 0.01;
    g0 = std::abs (loadLine (vbias + h) - loadLine (vbias - h)) / (2 * h);
}

const TriodeCurve& TriodeCurve::shared()
{
    static const TriodeCurve c = [] { TriodeCurve t; t.build(); return t; }();   // thread-safe initialisation
    return c;
}

void TriodeStage::prepare (const TriodeCurve& c, double rate)
{
    curve = &c;
    // coupling capacitor 47 nF into a 1 M grid leak: charges on grid current, leaks with tau = 47 ms
    constexpr double C = 47e-9, Rg = 1e6;
    chargeC = 0.15 / (C * rate);   // (a fraction of the full charge: the cathode bypass and source take the rest)
    leakC   = 1.0 / (Rg * C * rate);
    igC     = 1 - std::exp (-1 / (0.001 * rate));
    reset();
}

void TriodeStage::reset() { shift = 0; igLp = 0; rest = curve != nullptr ? curve->plate (curve->bias()) : 0.0; }

// ---------------------------------------------------------------- iron

void IronStage::prepare (double rate, double cornerHz)
{
    ia = std::exp (-2 * kPi * cornerHz / rate);
    ib = 1 - ia;
    core.setTape (PlateAmp::kIronCoercivity, PlateAmp::kIronReversible);
    core.prepare (rate);
    reset();
}

void IronStage::reset() { core.reset(); flux = 0; mPrev = 0; }

// ---------------------------------------------------------------- the amplifier

void PlateAmp::prepare (double esr)
{
    const auto& curve = TriodeCurve::shared();
    driver.prepare (curve, 2 * esr);
    for (auto& p : pickup) p.prepare (curve, 2 * esr);
    ironIn.prepare (esr, 40.0);
    for (auto& i : ironOut) i.prepare (esr, 40.0);
    halfBandDesign (hb1, 7, 2 * (0.25 - std::min (11000.0, 0.238 * 2 * esr) / (2 * esr)));
    dcC = 1 - std::exp (-2 * kPi * 8.0 / esr);
    // weighted from ~200 Hz: the plate's own low cut throws away the rumble a pushed tube makes
    levelIn.prepare (esr, 0.3, 4.0, 200.0);
    levelOut.prepare (esr, 0.3, 4.0, 200.0);
    smc = 1 - std::exp (-1 / (0.15 * esr));   // drive moves like a gain pot being turned, not a switch

    // iron calibration: small-signal gain of the core for each field level (largest over a few levels,
    // as for the tape), so each transformer is unity when quiet
    // (once per rate: the result depends on nothing else)
    static std::mutex cacheLock;
    static std::vector<std::pair<double, std::array<double, 33>>> cache;
    std::unique_lock<std::mutex> guard (cacheLock);
    bool cached = false;
    for (const auto& c : cache)
        if (c.first == esr) { ironCal = c.second; cached = true; }
    TapeHysteresis h;
    h.setTape (kIronCoercivity, kIronReversible);
    h.prepare (esr);
    const int per = (int) std::lround (esr / 100.0);
    for (std::size_t i = 0; i < ironCal.size() && ! cached; ++i)
    {
        const double f = ironCalLo * std::pow (ironCalHi / ironCalLo, i / (double) (ironCal.size() - 1));
        double best = 0;
        for (double amp : { 0.001, 0.01, 0.05 })
        {
            h.reset();
            double re = 0, im = 0;
            for (int n = 0; n < 6 * per; ++n)
            {
                const double ph = 2 * kPi * n / per;
                const double m = h.process (f * amp * std::sin (ph));
                if (n >= 2 * per) { re += m * std::sin (ph); im += m * std::cos (ph); }
            }
            best = std::max (best, 2 * std::sqrt (re * re + im * im) / (4 * per) / amp);
        }
        ironCal[i] = 1.0 / best;
    }
    if (! cached) cache.emplace_back (esr, ironCal);
    guard.unlock();
    driveS = driveT;
    apply (driveS);
    reset();
}

void PlateAmp::reset()
{
    driver.reset();
    for (auto& p : pickup) p.reset();
    ironIn.reset();
    for (auto& i : ironOut) i.reset();
    inUp.clear(); inDn.clear(); outUp.clear(); outDn.clear();
    dcOut[0] = dcOut[1] = dcMid[0] = dcMid[1] = 0;
    dcPre = dcIn = 0;
    levelIn.reset(); levelOut.reset();
}

double PlateAmp::ironMakeup (double field) const
{
    const double p = std::log (std::clamp (field, ironCalLo, ironCalHi) / ironCalLo) / std::log (ironCalHi / ironCalLo) * (ironCal.size() - 1);
    const int i = std::min ((int) p, (int) ironCal.size() - 2);
    return ironCal[(std::size_t) i] + (p - i) * (ironCal[(std::size_t) i + 1] - ironCal[(std::size_t) i]);
}

void PlateAmp::setDrive (double d, bool snap)
{
    driveT = std::clamp (d, 0.0, 1.0);
    if (snap && driveS != driveT) { driveS = driveT; apply (driveS); }
}

// Every level follows from the one drive value, so the field and its makeup always move together
// (gliding them separately would let the gain overshoot on the way).
void PlateAmp::apply (double d)
{
    // tubes: grid volts per unit of signal; at 0 VU (0.18 peak) the grid swings 11 mV at 0 (clean),
    // 33 mV by default (a soft 2nd-harmonic glow) and 0.45 V at full; louder peaks reach grid conduction
    // (the bias sits at -1.2 V)
    vIn  = 0.06 * std::pow (2.5 / 0.06, d);
    vOut = 0.6 * vIn;
    // iron: field per unit of flux; the cores only reach the knee of their curve on loud lows
    fIn  = 0.1 * std::pow (60.0, d);
    fOut = 0.7 * fIn;
    mIn  = ironMakeup (fIn);
    mOut = ironMakeup (fOut);
}

void PlateAmp::glide()
{
    if (driveS == driveT) return;
    driveS += smc * (driveT - driveS);
    if (std::abs (driveT - driveS) < 1e-6) driveS = driveT;
    apply (driveS);
}

double PlateAmp::input (double x)
{
    glide();
    const double dry = x;
    dcPre += dcC * (x - dcPre);
    x = ironIn.process (x - dcPre, fIn, mIn);
    double a0, a1;
    inUp.up (hb1, 7, x, a0, a1);
    a0 = driver.process (a0, vIn);
    a1 = driver.process (a1, vIn);
    double y = inDn.down (hb1, 7, a0, a1);
    dcIn += dcC * (y - dcIn);
    y -= dcIn;
    return y * levelIn.update (&dry, &y, 1);
}

void PlateAmp::output (double& l, double& r)
{
    const double in[2] { l, r };
    double a[2][2], y[2];
    outUp.up (hb1, 7, in, a[0], a[1]);
    for (auto& v : a)
        for (int c = 0; c < 2; ++c) v[c] = pickup[c].process (v[c], vOut);
    outDn.down (hb1, 7, a[0], a[1], y);
    for (int c = 0; c < 2; ++c)
    {
        dcMid[c] += dcC * (y[c] - dcMid[c]);
        const double v = ironOut[c].process (y[c] - dcMid[c], fOut, mOut);
        dcOut[c] += dcC * (v - dcOut[c]);
        y[c] = v - dcOut[c];
    }
    const double g = levelOut.update (in, y, 2);
    l = y[0] * g; r = y[1] * g;
}
} // namespace cf
