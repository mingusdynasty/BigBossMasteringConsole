// De-esser tests (no JUCE). Run via ctest.
#include "deess/DeEsser.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kPi = 3.14159265358979323846;

// a voice-like signal: a 180 Hz vowel (harmonics to 3 kHz), with bursts of band-limited noise ("s")
// centred on sHz from 0.5 to 0.7 s, at sDb relative to the vowel's level
struct Voice { std::vector<double> l, r, vowel; };
static Voice voice (double sr, double sHz, double sDb, double gain = 1.0)
{
    Voice v;
    const size_t n = (size_t) (sr * 1.0);
    v.l.assign (n, 0.0); v.vowel.assign (n, 0.0);
    std::mt19937 rng (9);
    std::normal_distribution<double> g (0, 1);
    // band-pass for the noise
    const double w = 2 * kPi * sHz / sr, al = std::sin (w) / (2 * 3.0), a0 = 1 + al;
    const double b0 = al / a0, b2 = -al / a0, a1 = -2 * std::cos (w) / a0, a2 = (1 - al) / a0;
    double z1 = 0, z2 = 0;
    const double sAmp = 0.1 * std::pow (10, sDb / 20) * 4.0;   // the band-pass keeps about a quarter of the noise
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        double vow = 0;
        for (int h = 1; h * 180 < 3000; ++h) vow += 0.1 / h * std::sin (2 * kPi * 180 * h * t);
        const double x = g (rng);
        const double y = b0 * x + z1; z1 = -a1 * y + z2; z2 = b2 * x - a2 * y;
        const double env = (t > 0.5 && t < 0.7) ? std::sin (kPi * (t - 0.5) / 0.2) : 0.0;
        vow *= std::min (1.0, t / 0.005);   // a voice starts over a few milliseconds, not with a click
        v.vowel[i] = gain * vow;
        v.l[i] = gain * (vow + env * sAmp * y);
    }
    v.r = v.l;
    return v;
}

// runs the de-esser in 256-sample blocks, returns the most reduction per band during the burst
static std::vector<double> run (bb::DeEsser& d, Voice& v, double sr)
{
    std::vector<double> most (bb::DeEsser::kBands, 0.0);
    for (size_t pos = 0; pos < v.l.size(); pos += 256)
    {
        const int n = (int) std::min<size_t> (256, v.l.size() - pos);
        d.process (v.l.data() + pos, v.r.data() + pos, n);
        if (pos > sr * 0.5 && pos < sr * 0.7)
            for (int k = 0; k < bb::DeEsser::kBands; ++k) most[(size_t) k] = std::max (most[(size_t) k], (double) d.reduction (k));
    }
    return most;
}

static double harmonicAmp (const std::vector<double>& x, double sr, double hz, size_t from, size_t to)
{
    double re = 0, im = 0;
    for (size_t i = from; i < to; ++i) { const double ph = 2 * kPi * hz * (double) i / sr; re += x[i] * std::cos (ph); im += x[i] * std::sin (ph); }
    return 2 * std::hypot (re, im) / (double) (to - from);
}

static double bandEnergy (const std::vector<double>& x, double sr, double hz, size_t from, size_t to)
{
    const double w = 2 * kPi * hz / sr, al = std::sin (w) / (2 * 2.0), a0 = 1 + al;
    const double b0 = al / a0, b2 = -al / a0, a1 = -2 * std::cos (w) / a0, a2 = (1 - al) / a0;
    double z1 = 0, z2 = 0, e = 0;
    for (size_t i = 0; i < to; ++i)
    {
        const double y = b0 * x[i] + z1; z1 = -a1 * y + z2; z2 = b2 * x[i] - a2 * y;
        if (i >= from) e += y * y;
    }
    return e;
}

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        for (int mode : { 0, 1 })
        {   // with nothing to catch it is a delay line, bit for bit
            bb::DeEsser d;
            bb::DeEsser::Settings s;
            s.mode = mode;
            d.setSettings (s);
            d.prepare (sr, 256);
            auto v = voice (sr, 6400, -60);
            const auto in = v.l;
            run (d, v, sr);
            double err = 0;
            for (size_t i = (size_t) d.latency(); i < in.size(); ++i) err = std::max (err, std::abs (v.l[i] - in[i - (size_t) d.latency()]));
            CHECK (err < 1e-15, "deess %.0f mode %d: changes a vowel with no sibilance (error %.3g)", sr, mode, err);
        }

        {   // an "s" at 6.4 kHz: band 2 catches it, the top band hardly moves, the vowel is untouched
            bb::DeEsser d;
            bb::DeEsser::Settings s;
            d.setSettings (s);
            d.prepare (sr, 256);
            auto v = voice (sr, 6364, 6);
            const auto in = v.l;
            const auto most = run (d, v, sr);
            CHECK (most[1] >= 4.0, "deess %.0f: 6.4 kHz band cut only %.1f dB on a hot s", sr, most[1]);
            CHECK (most[3] < most[1] - 3.0, "deess %.0f: 12.7 kHz band %.1f dB vs 6.4 kHz band %.1f dB", sr, most[3], most[1]);
            const size_t a = (size_t) (0.55 * sr), b = (size_t) (0.65 * sr);
            const double before = bandEnergy (in, sr, 6364, a, b), after = bandEnergy (v.l, sr, 6364, a + (size_t) d.latency(), b + (size_t) d.latency());
            CHECK (10 * std::log10 (after / before) < -3.0, "deess %.0f: s energy only down %.1f dB", sr, 10 * std::log10 (after / before));
            const double vin = harmonicAmp (in, sr, 180, a, b), vout = harmonicAmp (v.l, sr, 180, a + (size_t) d.latency(), b + (size_t) d.latency());
            CHECK (std::abs (20 * std::log10 (vout / vin)) < 0.1, "deess %.0f: the vowel moved %.2f dB", sr, 20 * std::log10 (vout / vin));

            // the same performance 20 dB quieter is caught the same way
            bb::DeEsser q;
            q.setSettings (s);
            q.prepare (sr, 256);
            auto quiet = voice (sr, 6364, 6, 0.1);
            const auto mq = run (q, quiet, sr);
            CHECK (std::abs (mq[1] - most[1]) < 1.5, "deess %.0f: level dependent (%.1f dB loud, %.1f dB quiet)", sr, most[1], mq[1]);
        }

        {   // a mild s, below threshold, is left alone
            bb::DeEsser d;
            bb::DeEsser::Settings s;
            d.setSettings (s);
            d.prepare (sr, 256);
            auto v = voice (sr, 6364, -20);
            const auto most = run (d, v, sr);
            CHECK (most[1] < 0.5, "deess %.0f: a soft s was cut %.1f dB", sr, most[1]);
        }

        {   // latency, measured by cross-correlation with noise
            bb::DeEsser d;
            bb::DeEsser::Settings s;
            d.setSettings (s);
            d.prepare (sr, 256);
            std::mt19937 rng (4);
            std::normal_distribution<double> g (0, 0.01);
            std::vector<double> in ((size_t) (sr / 2)), l, r;
            for (auto& x : in) x = g (rng);
            l = in; r = in;
            for (size_t pos = 0; pos < l.size(); pos += 256) d.process (l.data() + pos, r.data() + pos, (int) std::min<size_t> (256, l.size() - pos));
            int best = 0; double bv = -1e30;
            for (int k = 0; k < 400; ++k)
            {
                double acc = 0;
                for (size_t i = 400; i < in.size(); ++i) acc += in[i - (size_t) k] * l[i];
                if (acc > bv) { bv = acc; best = k; }
            }
            CHECK (best == d.latency(), "deess %.0f: latency reports %d, measured %d", sr, d.latency(), best);
        }
    }

    {   // listen plays only what is removed: silence when idle
        const double sr = 48000;
        bb::DeEsser d;
        bb::DeEsser::Settings s;
        s.listen = true;
        d.setSettings (s);
        d.prepare (sr, 256);
        auto v = voice (sr, 6364, -60);
        run (d, v, sr);
        double m = 0;
        for (size_t i = 0; i < (size_t) (0.45 * sr); ++i) m = std::max (m, std::abs (v.l[i]));
        CHECK (m < 1e-12, "deess listen: %.3g left over with nothing removed", m);
    }

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
