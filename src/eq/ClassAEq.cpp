#include "ClassAEq.h"

#include <algorithm>
#include <cmath>

namespace bb
{
namespace
{
constexpr int kStep = 32;           // control update interval, in oversampled samples
constexpr double kWarmH2 = 0.012;   // 2nd-harmonic coefficient: ~0.1% at 0 VU (-18 dBFS), rising with level
constexpr double kWarmH3 = 0.004;   // a little 3rd as the stage nears its rails
}

void ClassAEq::prepare (double sampleRate, int maxBlock)
{
    os.prepare (sampleRate, maxBlock);
    rate = os.rate();
    // per kStep samples: one-pole glide with a 20 ms time constant, 20 ms switch fades
    glide = 1 - std::exp (-kStep / (0.02 * rate));
    mixStep = kStep / (0.02 * rate);
    dcC = 1 - std::exp (-2 * eq::kPi * 4.0 / rate);
    for (int i = 0; i < eq::kBands; ++i)
    {
        auto& b = band[(size_t) i];
        b.freq = target.band[i].freq;
        b.gain = target.band[i].gain;
        b.cut = target.band[i].cut;
        b.shelf = target.band[i].shelf && eq::kRange[i].canShelf;
        b.q = eq::bandQ (target.band[i], target.warm);
    }
    hpf.want = target.hpfHz > 0; hpf.mix = hpf.want ? 1 : 0; hpf.freq = hpf.want ? target.hpfHz : 20; hpf.order = target.hpfOrder;
    lpf.want = target.lpfHz > 0; lpf.mix = lpf.want ? 1 : 0; lpf.freq = lpf.want ? target.lpfHz : 20000;
    mono.want = target.monoBassHz > 0; mono.mix = mono.want ? 1 : 0; mono.freq = mono.want ? target.monoBassHz : 60;
    stereoNow = target.stereo;
    warm = target.warm ? 1 : 0;
    outGain = std::pow (10.0, target.outputDb / 20);
    reset();
}

void ClassAEq::reset()
{
    os.reset();
    for (auto& b : band) b.f.clear();
    for (auto* f : { &hpf, &lpf, &mono }) { f->s1.clear(); f->s2.clear(); }
    dc[0] = dc[1] = 0;
    countdown = 0;
    // designs for the current values
    for (int i = 0; i < eq::kBands; ++i)
    {
        auto& b = band[(size_t) i];
        b.f.c = eq::design ({ b.freq, b.gain, b.cut, b.shelf }, i, b.q, rate);
    }
    update();
}

void ClassAEq::updateFilter (Filter& f, bool high, double wantHz, int wantOrder)
{
    const bool want = wantHz > 0;
    // a change of slope is a change of shape: fade out, switch, fade back in
    const bool reshape = want && f.mix > 0 && wantOrder != f.order;
    if (want && ! reshape)
    {
        if (f.mix == 0) { f.freq = wantHz; f.order = wantOrder; f.s1.clear(); f.s2.clear(); }   // come in where asked
        f.freq += (wantHz - f.freq) * glide;
        if (std::abs (f.freq - wantHz) < 1e-4 * wantHz) f.freq = wantHz;
        f.mix = std::min (1.0, f.mix + mixStep);
    }
    else
    {
        f.mix = std::max (0.0, f.mix - mixStep);
        if (f.mix == 0 && reshape) f.order = wantOrder;
    }
    if (f.mix == 0) return;
    if (&f == &mono || f.order >= 4)
    {
        f.s1.c = eq::pass (high, f.freq, 0.5412, rate);
        f.s2.c = eq::pass (high, f.freq, 1.3066, rate);
    }
    else
        f.s1.c = eq::pass (high, f.freq, 0.7071, rate);
}

void ClassAEq::update()
{
    // moving the bands between stereo, mid and side is a change of shape for all of them
    const bool restereo = target.stereo != stereoNow;
    bool allFlat = true;
    for (int i = 0; i < eq::kBands; ++i)
    {
        auto& b = band[(size_t) i];
        const auto& t = target.band[i];
        const bool shelf = t.shelf && eq::kRange[i].canShelf;
        const bool reshape = t.cut != b.cut || shelf != b.shelf || restereo;
        // a shape change happens at 0 dB, where every shape is flat
        const double gainT = reshape ? 0.0 : t.gain;
        const double f0 = b.freq, g0 = b.gain, q0 = b.q;
        const double freqT = std::clamp (t.freq, 10.0, 0.45 * rate);
        b.freq *= std::pow (freqT / b.freq, glide);
        b.gain += (gainT - b.gain) * glide;
        // land exactly, so a band at rest is not redesigned forever
        if (std::abs (b.freq - freqT) < 1e-4 * freqT) b.freq = freqT;
        if (std::abs (b.gain - gainT) < 1e-4) b.gain = gainT;
        if (reshape && b.gain < 0.001)   // 0.001 dB: dropping the filter there is inaudible
        {
            b.gain = 0; b.cut = t.cut; b.shelf = shelf;
        }
        const eq::Band shape { b.freq, b.gain, b.cut, b.shelf };
        const double qT = eq::bandQ (shape, target.warm);
        b.q += (qT - b.q) * glide;
        if (std::abs (b.q - qT) < 1e-5) b.q = qT;
        if (std::abs (b.freq - f0) > 1e-6 * f0 || std::abs (b.gain - g0) > 1e-7 || std::abs (b.q - q0) > 1e-7)
            b.f.c = eq::design (shape, i, b.q, rate);
        allFlat = allFlat && b.gain == 0.0;
    }
    if (restereo && allFlat)
    {
        stereoNow = target.stereo;
        for (auto& b : band) b.f.clear();
    }
    updateFilter (hpf, true, target.hpfHz, target.hpfOrder >= 4 ? 4 : 2);
    updateFilter (lpf, false, target.lpfHz, 2);
    updateFilter (mono, true, target.monoBassHz, 4);
    warm += ((target.warm ? 1.0 : 0.0) - warm) * glide;
    if (std::abs (warm) < 1e-6) warm = 0;
    const double outT = std::pow (10.0, target.outputDb / 20);
    outGain += (outT - outGain) * glide;
    if (std::abs (outGain - outT) < 1e-6) outGain = outT;
}

void ClassAEq::process (double* l, double* r, int n)
{
    os.up (l, r, n);
    double* L = os.hiL();
    double* R = os.hiR();
    const int N = n * os.factor();
    for (int i = 0; i < N; ++i)
    {
        if (--countdown <= 0) { update(); countdown = kStep; }
        double x[2] { L[i], R[i] };

        if (hpf.mix > 0)
            for (int c = 0; c < 2; ++c)
            {
                double y = hpf.s1.run (c, x[c]);
                if (hpf.order >= 4) y = hpf.s2.run (c, y);
                x[c] += hpf.mix * (y - x[c]);
            }
        if (stereoNow == 0)
        {
            for (auto& b : band)
                if (b.gain > 1e-6)
                    for (int c = 0; c < 2; ++c) x[c] = b.f.run (c, x[c]);
                else
                    b.f.clear();
        }
        else
        {
            // the bands on the mid or the side only: the other passes untouched
            double ms[2] { 0.5 * (x[0] + x[1]), 0.5 * (x[0] - x[1]) };
            const int c = stereoNow == 1 ? 0 : 1;
            for (auto& b : band)
                if (b.gain > 1e-6) ms[c] = b.f.run (c, ms[c]);
                else b.f.clear();
            x[0] = ms[0] + ms[1];
            x[1] = ms[0] - ms[1];
        }
        if (lpf.mix > 0)
            for (int c = 0; c < 2; ++c) x[c] += lpf.mix * (lpf.s1.run (c, x[c]) - x[c]);
        if (mono.mix > 0)
        {
            // side below the corner is removed: 24 dB/oct high-pass on S only
            const double m = 0.5 * (x[0] + x[1]), s = 0.5 * (x[0] - x[1]);
            const double sh = mono.s2.run (0, mono.s1.run (0, s));
            const double s2 = s + mono.mix * (sh - s);
            x[0] = m + s2; x[1] = m - s2;
        }
        if (warm > 0)
            for (int c = 0; c < 2; ++c)
            {
                // the class-A stage: even harmonics first, a little odd near the top; the DC the
                // 2nd harmonic brings is taken out by a 4 Hz coupling capacitor
                const double v = x[c];
                const double y = v + warm * (kWarmH2 * v * v - kWarmH3 * v * v * v);
                dc[c] += dcC * (y - v - dc[c]);
                x[c] = y - dc[c];
            }
        L[i] = x[0] * outGain;
        R[i] = x[1] * outGain;
    }
    os.down (l, r, n);
}
} // namespace bb
