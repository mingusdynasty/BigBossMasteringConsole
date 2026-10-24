#include "DeEsser.h"

#include <algorithm>
#include <cmath>

namespace bb
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
double smoothing (double seconds, double sr) { return 1.0 - std::exp (-1.0 / (std::max (1e-6, seconds) * sr)); }

// RBJ band-pass with 0 dB peak gain
void bandpass (double f, double q, double sr, double& b0, double& b1, double& b2, double& a1, double& a2)
{
    const double w = 2 * kPi * std::clamp (f, 20.0, 0.45 * sr) / sr, al = std::sin (w) / (2 * q), a0 = 1 + al;
    b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * std::cos (w) / a0; a2 = (1 - al) / a0;
}
}

double DeEsser::bandFreq (double lowest, int band, double sampleRate)
{
    const double cap = std::min (16000.0, 0.42 * sampleRate);
    double below = 0;
    for (int k = 0; k <= band; ++k)
    {
        const double f = lowest * std::pow (std::sqrt (2.0), k);
        if (f <= cap) { below = f; continue; }
        if (below > 0 && cap / below < std::pow (2.0, 0.25)) return 0.0;
        if (k == band) return cap;
        below = cap;
    }
    return below;
}

void DeEsser::design (double freq)
{
    for (int k = 0; k < kBands; ++k)
    {
        const double fk = bandFreq (freq, k, sr);
        active[(size_t) k] = fk > 0;
        const double f = fk > 0 ? fk : 1000.0;   // a dropped band still gets valid (unused) coefficients
        auto& d = detect[(size_t) k];
        auto& a = apply[(size_t) k];
        bandpass (f, 2.0, sr, d.b0, d.b1, d.b2, d.a1, d.a2);   // narrow, to tell the bands apart
        bandpass (f, 1.4, sr, a.b0, a.b1, a.b2, a.a1, a.a2);   // about an octave wide, a natural-sounding cut
    }
    designedFreq = freq;
}

void DeEsser::prepare (double sampleRate, int)
{
    sr = sampleRate;
    lookahead = std::max (1, (int) std::lround (0.001 * sr));
    int len = 1;
    while (len < lookahead + 1) len <<= 1;
    mask = len - 1;
    for (auto& d : delay) d.assign ((size_t) len, 0.0);

    // the body: 300 Hz to 3 kHz, second-order each side
    {
        const double w = 2 * kPi * 300.0 / sr, cs = std::cos (w), al = std::sin (w) / (2 * 0.7071), a0 = 1 + al;
        bodyHp.b0 = (1 + cs) / 2 / a0; bodyHp.b1 = -(1 + cs) / a0; bodyHp.b2 = bodyHp.b0; bodyHp.a1 = -2 * cs / a0; bodyHp.a2 = (1 - al) / a0;
    }
    {
        const double w = 2 * kPi * 3000.0 / sr, cs = std::cos (w), al = std::sin (w) / (2 * 0.7071), a0 = 1 + al;
        bodyLp.b0 = (1 - cs) / 2 / a0; bodyLp.b1 = (1 - cs) / a0; bodyLp.b2 = bodyLp.b0; bodyLp.a1 = -2 * cs / a0; bodyLp.a2 = (1 - al) / a0;
    }
    detC = smoothing (0.005, sr);    // band energy: quick enough for a consonant, too slow for a click
    bodyC = smoothing (0.030, sr);   // body energy: slower to fall, the vowel around it
    bodyAtkC = smoothing (0.001, sr);
    atkC = smoothing (0.0005, sr);
    glide = smoothing (0.02, sr);
    fadeC = smoothing (0.002, sr);   // a mode change: the cuts let go in a few ms before the switch
    designedFreq = -1;
    design (std::clamp (target.freq, 3000.0, 8000.0));
    reset();
}

void DeEsser::reset()
{
    for (auto& b : detect) b.clear();
    for (auto& b : apply) b.clear();
    bodyHp.clear(); bodyLp.clear();
    bandMs.fill (0); gainDb.fill (0);
    bodyMs = 0;
    for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0);
    pos = 0;
    lastMode = std::clamp (target.mode, 0, 2);
    modeFading = false;
    listenW = target.listen ? 1 : 0;
    for (auto& g : gr) g.store (0);
}

void DeEsser::process (double* l, double* r, int n)
{
    const Settings s = target;
    const double f = std::clamp (s.freq, 3000.0, 8000.0);
    if (std::abs (f - designedFreq) > 1.0) design (f);   // coefficients only: the states carry on
    const double thr = std::clamp (s.thresholdDb, -30.0, 18.0), range = std::clamp (s.rangeDb, 0.0, 24.0);
    const double relC = smoothing (std::clamp (s.releaseMs, 5.0, 500.0) * 0.001, sr);
    const int wantMode = std::clamp (s.mode, 0, 2);
    // a new mode sends a new signal into the cut filters. Let the cuts fade out in the old mode first,
    // then start the filters clean with nothing cut, so the switch never steps
    if (wantMode != lastMode) modeFading = true;
    int mode = lastMode;
    const double listenT = s.listen ? 1.0 : 0.0;
    int nd = mode == Stereo ? 2 : 1;
    std::array<double, kBands> most {};

    for (int i = 0; i < n; ++i)
    {
        listenW += (listenT - listenW) * glide;

        // detection runs a millisecond ahead of the audio, on what the mode processes
        double det[2] = { l[i], r[i] };
        if (mode == Mid) det[0] = 0.5 * (l[i] + r[i]);
        if (mode == Side) det[0] = 0.5 * (l[i] - r[i]);
        double body = 0;
        std::array<double, kBands> e {};
        for (int c = 0; c < nd; ++c)
        {
            const double b = bodyLp.run (c, bodyHp.run (c, det[c]));
            body = std::max (body, b * b);
            for (int k = 0; k < kBands; ++k)
            {
                const double v = detect[(size_t) k].run (c, det[c]);
                e[(size_t) k] = std::max (e[(size_t) k], v * v);
            }
        }
        // the body rises as fast as the bands and falls slowly, so an onset (a vowel's start, a
        // plosive) lifts both together and only energy that stays high-heavy reads as sibilance
        bodyMs += (body - bodyMs) * (body > bodyMs ? bodyAtkC : bodyC);
        const double ref = bodyMs + 1e-6;   // a -60 dBFS floor: hiss and room tone never count as sibilance
        std::array<double, kBands> bandDb {}, depth {};
        double loudest = -300;
        for (int k = 0; k < kBands; ++k)
        {
            bandMs[(size_t) k] += (e[(size_t) k] - bandMs[(size_t) k]) * detC;
            bandDb[(size_t) k] = 10.0 * std::log10 (bandMs[(size_t) k] + 1e-12);
            loudest = std::max (loudest, bandDb[(size_t) k]);
        }
        for (int k = 0; k < kBands; ++k)
        {
            // focus: a band counts only as far as it stands out from the body AND keeps up with the
            // loudest band, so an "s" at 6 kHz is cut at 6 kHz and its spill into the air band is not
            const double over = bandDb[(size_t) k] - 10.0 * std::log10 (ref) - thr - (loudest - bandDb[(size_t) k]);
            // soft knee (3 dB); above it the band is pulled back to the threshold, never more than range
            const double want = over <= -1.5 ? 0.0 : (over >= 1.5 ? over : (over + 1.5) * (over + 1.5) / 6.0);
            const double t = modeFading || ! active[(size_t) k] ? 0.0 : std::min (want, range);
            auto& g = gainDb[(size_t) k];
            g += (t - g) * (t > g ? atkC : (modeFading ? fadeC : relC));
            if (g < 1e-9) g = 0;
            most[(size_t) k] = std::max (most[(size_t) k], g);
            depth[(size_t) k] = g > 1e-9 ? 1.0 - std::exp (-g * 0.11512925464970229) : 0.0;
        }

        delay[0][(size_t) pos] = l[i];
        delay[1][(size_t) pos] = r[i];
        const int rd = (pos - lookahead) & mask;
        const double dl = delay[0][(size_t) rd], dr = delay[1][(size_t) rd];
        pos = (pos + 1) & mask;

        // the four dynamic cuts in turn; the filters always run, so a cut starts from a settled state
        auto cuts = [&] (int c, double x)
        {
            for (int k = 0; k < kBands; ++k) x -= depth[(size_t) k] * apply[(size_t) k].run (c, x);
            return x;
        };
        double ol, orr;
        if (mode == Stereo) { ol = cuts (0, dl); orr = cuts (1, dr); }
        else
        {
            const double dm = 0.5 * (dl + dr), ds = 0.5 * (dl - dr);
            if (mode == Mid) { const double m = cuts (0, dm); ol = m + ds; orr = m - ds; }
            else { const double sd = cuts (0, ds); ol = dm + sd; orr = dm - sd; }
        }
        // LISTEN: what is being taken out
        l[i] = ol + listenW * ((dl - ol) - ol);
        r[i] = orr + listenW * ((dr - orr) - orr);

        if (modeFading && *std::max_element (gainDb.begin(), gainDb.end()) < 0.001)
        {
            for (auto& b : apply) b.clear();
            gainDb.fill (0);
            lastMode = mode = wantMode;
            nd = mode == Stereo ? 2 : 1;
            modeFading = false;   // the rest of the block runs in the new mode
        }
    }
    for (int k = 0; k < kBands; ++k) gr[(size_t) k].store ((float) most[(size_t) k], std::memory_order_relaxed);
}
} // namespace bb
