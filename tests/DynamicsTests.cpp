// Dynamics tests (no JUCE): the two-stage compressor and the limiter. Run via ctest.
// Prints the limiter's distortion figures so they can be compared against other limiters.
#include "dyn/DualCompressor.h"
#include "dyn/Limiter.h"
#include "meter/TruePeak.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kPi = 3.14159265358979323846;
using Proc = std::function<void (double*, double*, int)>;

static std::vector<double> sine (double sr, double hz, double amp, double secs)
{
    std::vector<double> v ((size_t) (sr * secs));
    for (size_t i = 0; i < v.size(); ++i) v[i] = amp * std::sin (2 * kPi * hz * (double) i / sr);
    return v;
}

static void runBlocks (const Proc& p, std::vector<double>& l, std::vector<double>& r, int block = 512)
{
    for (size_t pos = 0; pos < l.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, l.size() - pos);
        p (l.data() + pos, r.data() + pos, n);
    }
}

static double harmonic (const std::vector<double>& x, double sr, double hz, int h, size_t start)
{
    const auto per = (size_t) std::lround (sr / hz);
    const size_t n = (x.size() - start) / per * per;
    double re = 0, im = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double ph = 2 * kPi * hz * h * (double) (start + i) / sr;
        re += x[start + i] * std::cos (ph); im += x[start + i] * std::sin (ph);
    }
    return 2 * std::hypot (re, im) / (double) n;
}

static double thd (const std::vector<double>& x, double sr, double hz, size_t start)
{
    double h = 0;
    for (int k = 2; k <= 9 && k * hz < sr / 2; ++k) { const double a = harmonic (x, sr, hz, k, start); h += a * a; }
    return std::sqrt (h) / harmonic (x, sr, hz, 1, start);
}

static double rmsDb (const std::vector<double>& x, size_t from, size_t to)
{
    double s = 0;
    for (size_t i = from; i < to; ++i) s += x[i] * x[i];
    return 10 * std::log10 (std::max (s / (double) (to - from), 1e-30));
}

static double dbOf (double lin) { return 20 * std::log10 (std::max (lin, 1e-12)); }

static int measuredLatency (const Proc& p, double sr)
{
    std::mt19937 rng (5);
    std::normal_distribution<double> g (0, 0.02);
    std::vector<double> in ((size_t) sr), l, r;
    for (auto& v : in) v = g (rng);
    l = in; r = in;
    runBlocks (p, l, r);
    int best = 0; double bestV = -1e30;
    for (int d = 0; d < 400; ++d)
    {
        double acc = 0;
        for (size_t i = (size_t) sr / 2; i < in.size(); ++i) acc += in[i - (size_t) d] * l[i];
        if (acc > bestV) { bestV = acc; best = d; }
    }
    return best;
}

// a dense test programme: kick, bass, noise pads and bright transients, peaking well over 0 dBFS when driven
static void programme (double sr, double secs, std::vector<double>& l, std::vector<double>& r, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> g (0, 1);
    l.assign ((size_t) (sr * secs), 0.0);
    r.assign (l.size(), 0.0);
    double pl = 0, pr = 0;
    for (size_t i = 0; i < l.size(); ++i)
    {
        const double t = (double) i / sr, beat = std::fmod (t, 0.5);
        const double kick = 0.6 * std::exp (-beat * 14) * std::sin (2 * kPi * (48 + 150 * std::exp (-beat * 35)) * beat);
        const double bass = 0.25 * std::sin (2 * kPi * 55 * t);
        pl += (0.08 * g (rng) - pl) * 0.05; pr += (0.08 * g (rng) - pr) * 0.05;
        const double click = beat < 0.002 ? 0.5 * g (rng) : 0.0;   // bright transient: true peaks between samples
        l[i] = kick + bass + 3 * pl + click;
        r[i] = kick + bass + 3 * pr - click;
    }
}

// true peak from sample `from` on; the meter runs over the whole signal so it never sees an abrupt
// start (which has real overshoot of its own)
static double truePeakDb (const std::vector<double>& l, const std::vector<double>& r, double sr, size_t from)
{
    bb::TruePeak tp;
    tp.prepare (sr, 2);
    const double* head[2] = { l.data(), r.data() };
    tp.process (head, (int) from);
    tp.takeBlockPeak (0);
    tp.takeBlockPeak (1);
    const double* ch[2] = { l.data() + from, r.data() + from };
    tp.process (ch, (int) (l.size() - from));
    return dbOf (std::max (tp.takeBlockPeak (0), tp.takeBlockPeak (1)));
}

// ---------------------------------------------------------------- compressor

static void testCompressor()
{
    const double sr = 48000;
    auto run = [&] (bb::DualCompressor::Settings s, std::vector<double>& l, std::vector<double>& r)
    {
        bb::DualCompressor c;
        c.setSettings (s);
        c.prepare (sr, 512);
        runBlocks ([&] (double* a, double* b, int n) { c.process (a, b, n); }, l, r);
    };

    {   // quiet signal: untouched (Nickel is close to a wire)
        bb::DualCompressor::Settings s;
        auto l = sine (sr, 1000, 0.01, 1.0), r = l;
        run (s, l, r);
        const double g = dbOf (harmonic (l, sr, 1000, 1, l.size() / 2) / 0.01);
        CHECK (std::abs (g) < 0.02, "comp: quiet 1 kHz gain %.3f dB", g);
    }

    for (int ratio : { 1, 3 })
    {   // the discrete stage follows its static curve on a steady tone
        bb::DualCompressor::Settings s;
        s.opto = false; s.vcaThreshold = -20; s.ratio = ratio; s.recovery = 0;
        const double lvl = -6;
        auto l = sine (sr, 1000, std::pow (10, lvl / 20), 2.0), r = l;
        run (s, l, r);
        const double got = -dbOf (harmonic (l, sr, 1000, 1, l.size() / 2) / std::pow (10, lvl / 20));
        const double want = bb::DualCompressor::vcaCurve (lvl, -20, bb::DualCompressor::kRatio[ratio]);
        CHECK (std::abs (got - want) < 0.5, "comp: ratio %.0f:1 reduces %.2f dB, curve says %.2f", bb::DualCompressor::kRatio[ratio], got, want);
    }

    {   // attack settings: faster reaches its reduction sooner
        auto timeTo = [&] (int attack)
        {
            bb::DualCompressor::Settings s;
            s.opto = false; s.vcaThreshold = -30; s.ratio = 3; s.attack = attack;
            auto l = sine (sr, 1000, 0.0, 0.4), r = l;
            auto burst = sine (sr, 1000, 0.5, 0.3);
            std::copy (burst.begin(), burst.end(), l.begin() + (long) (sr * 0.1));
            r = l;
            bb::DualCompressor c; c.setSettings (s); c.prepare (sr, 64);
            int samples = -1;
            for (size_t pos = 0; pos < l.size(); pos += 64)
            {
                c.process (l.data() + pos, r.data() + pos, 64);
                if (pos > sr * 0.1 && samples < 0 && c.vcaReduction() > 0.9 * bb::DualCompressor::vcaCurve (dbOf (0.5), -30, 3))
                    samples = (int) (pos - sr * 0.1);
            }
            return samples;
        };
        const int fast = timeTo (1), slow = timeTo (5);
        CHECK (fast >= 0 && slow > fast * 3, "comp: attack 0.3 ms takes %d samples, 30 ms takes %d", fast, slow);
    }

    {   // optical release depends on history: a short burst lets go faster than a long one
        auto recovery = [&] (double burstSecs)
        {
            bb::DualCompressor::Settings s;
            s.vca = false; s.optoThreshold = -30;
            const double total = burstSecs + 10.0;
            auto l = sine (sr, 1000, 0.5, total), r = l;
            for (size_t i = (size_t) (burstSecs * sr); i < l.size(); ++i) l[i] *= 0.01;
            r = l;
            bb::DualCompressor c; c.setSettings (s); c.prepare (sr, 64);
            double t = -1;
            for (size_t pos = 0; pos + 64 <= l.size(); pos += 64)
            {
                c.process (l.data() + pos, r.data() + pos, 64);
                if (pos > burstSecs * sr && t < 0 && c.optoReduction() < 1.0) t = (double) pos / sr - burstSecs;
            }
            return t;
        };
        const double shortB = recovery (0.15), longB = recovery (5.0);
        CHECK (shortB > 0 && longB > 2 * shortB, "comp: opto lets go in %.2f s after 150 ms, %.2f s after 5 s", shortB, longB);
    }

    {   // transformers: Steel and Iron colour the lows, all leave the highs alone
        double t50[3], t5k[3];
        for (int x = 0; x < 3; ++x)
        {
            bb::DualCompressor::Settings s;
            s.opto = s.vca = false; s.transformer = x;
            auto l = sine (sr, 50, 0.5, 2.0), r = l;
            run (s, l, r);
            t50[x] = thd (l, sr, 50, l.size() / 2);
            auto h = sine (sr, 5000, 0.5, 1.0), hr = h;
            run (s, h, hr);
            t5k[x] = thd (h, sr, 5000, h.size() / 2);
        }
        CHECK (t50[2] > t50[1] && t50[1] > t50[0], "comp: 50 Hz THD steel %.3f%%, iron %.3f%%, nickel %.3f%%", 100 * t50[2], 100 * t50[1], 100 * t50[0]);
        CHECK (t50[0] < 0.002, "comp: nickel 50 Hz THD %.4f%%", 100 * t50[0]);
        CHECK (t5k[2] < 0.001 && t5k[1] < 0.001, "comp: 5 kHz THD steel %.4f%%, iron %.4f%%", 100 * t5k[2], 100 * t5k[1]);
    }

    {   // the cores saturate from the bottom up: distortion falls with frequency; Iron is mostly 2nd
        // harmonic, Steel 3rd; and switching transformers mid-note crossfades without a click
        auto thdAt = [&] (int x, double hz, double amp, double* h2 = nullptr, double* h3 = nullptr)
        {
            bb::DualCompressor::Settings s;
            s.opto = s.vca = false; s.transformer = x;
            auto l = sine (sr, hz, amp, 2.0), r = l;
            run (s, l, r);
            const size_t st = l.size() / 2;
            const double f = harmonic (l, sr, hz, 1, st);
            if (h2) *h2 = harmonic (l, sr, hz, 2, st) / f;
            if (h3) *h3 = harmonic (l, sr, hz, 3, st) / f;
            return thd (l, sr, hz, st);
        };
        for (int x = 1; x < 3; ++x)
        {
            const double a = thdAt (x, 40, 0.5), b = thdAt (x, 160, 0.5), c = thdAt (x, 640, 0.5);
            CHECK (a > 1.5 * b && b > 1.5 * c, "comp: %s distortion falls with frequency (40 Hz %.3f%%, 160 Hz %.3f%%, 640 Hz %.3f%%)",
                   x == 1 ? "iron" : "steel", 100 * a, 100 * b, 100 * c);
        }
        double i2, i3, s2, s3;
        thdAt (1, 50, 0.5, &i2, &i3);
        thdAt (2, 50, 0.5, &s2, &s3);
        CHECK (i2 > i3, "comp: iron is mostly 2nd harmonic (H2 %.3f%%, H3 %.3f%%)", 100 * i2, 100 * i3);
        CHECK (s3 > 10 * s2, "comp: steel is 3rd harmonic (H2 %.4f%%, H3 %.3f%%)", 100 * s2, 100 * s3);
        double quiet = 0;
        for (int x = 0; x < 3; ++x) quiet = std::max (quiet, thdAt (x, 50, 0.02));
        CHECK (quiet < 0.0005, "comp: every core is clean on a quiet low note (worst %.4f%%)", 100 * quiet);

        // switch Nickel -> Steel -> Iron on a held 80 Hz note
        bb::DualCompressor c;
        bb::DualCompressor::Settings s;
        s.opto = s.vca = false;
        c.setSettings (s);
        c.prepare (sr, 256);
        auto l = sine (sr, 80, 0.5, 1.5), r = l;
        const auto in = l;
        for (size_t pos = 0; pos < l.size(); pos += 256)
        {
            if (pos == (size_t) (0.5 * sr) / 256 * 256) { s.transformer = bb::DualCompressor::Steel; c.setSettings (s); }
            if (pos == (size_t) (1.0 * sr) / 256 * 256) { s.transformer = bb::DualCompressor::Iron; c.setSettings (s); }
            const int n = (int) std::min<size_t> (256, l.size() - pos);
            c.process (l.data() + pos, r.data() + pos, n);
        }
        double worst = 0;   // the largest sample-to-sample change of what the core adds
        for (size_t i = (size_t) (0.2 * sr); i < l.size(); ++i)
            worst = std::max (worst, std::abs ((l[i] - in[i]) - (l[i - 1] - in[i - 1])));
        CHECK (worst < 0.002, "comp: switching transformers is smooth (largest step in the added colour %.5f)", worst);
    }

    {   // sidechain high-pass: a loud 40 Hz note drives the compressor less
        double gr[2];
        for (int hpf = 0; hpf < 2; ++hpf)
        {
            bb::DualCompressor::Settings s;
            s.opto = false; s.vcaThreshold = -20; s.ratio = 3; s.sidechainHpf = hpf == 1;
            auto l = sine (sr, 40, 0.5, 1.5), r = l;
            bb::DualCompressor c; c.setSettings (s); c.prepare (sr, 512);
            runBlocks ([&] (double* a, double* b, int n) { c.process (a, b, n); }, l, r);
            gr[hpf] = c.vcaReduction();
        }
        CHECK (gr[1] < gr[0] - 6, "comp: 40 Hz reduction %.1f dB with the sidechain filter, %.1f dB without", gr[1], gr[0]);
    }
}

// ---------------------------------------------------------------- limiter

static void testLimiter()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        for (int tpMode = 0; tpMode < 2; ++tpMode)
            for (int style = 0; style < bb::Limiter::kStyles; ++style)
            {   // the ceiling holds on a hot programme
                bb::Limiter lim;
                bb::Limiter::Settings s;
                s.driveDb = 12; s.ceilingDb = -1; s.style = style; s.truePeak = tpMode == 1;
                lim.setSettings (s);
                lim.prepare (sr, 512);
                std::vector<double> l, r;
                programme (sr, 4.0, l, r, (unsigned) (style + 7));
                runBlocks ([&] (double* a, double* b, int n) { lim.process (a, b, n); }, l, r);
                double sp = 0;
                for (size_t i = 0; i < l.size(); ++i) sp = std::max ({ sp, std::abs (l[i]), std::abs (r[i]) });
                CHECK (dbOf (sp) <= -1.0 + 1e-9, "limiter %.0f style %d: sample peak %.4f dBFS over a -1 ceiling", sr, style, dbOf (sp));
                if (tpMode == 1)
                {
                    const double tp = truePeakDb (l, r, sr, 0);
                    CHECK (tp <= -1.0 + 0.05, "limiter %.0f style %d: true peak %.3f dBTP over a -1 ceiling", sr, style, tp);
                }
            }

        {   // below the ceiling it is a delay line and nothing else
            bb::Limiter lim;
            bb::Limiter::Settings s;
            lim.setSettings (s);
            lim.prepare (sr, 512);
            auto in = sine (sr, 997, 0.3, 0.5), l = in, r = in;
            runBlocks ([&] (double* a, double* b, int n) { lim.process (a, b, n); }, l, r);
            const int d = lim.latency();
            double err = 0;
            for (size_t i = (size_t) d; i < l.size(); ++i) err = std::max (err, std::abs (l[i] - in[i - (size_t) d]));
            CHECK (err < 1e-12, "limiter %.0f: not transparent below the ceiling (error %.3g)", sr, err);
            const int m = measuredLatency ([&] (double* a, double* b, int n) { lim.process (a, b, n); }, sr);
            CHECK (m == d, "limiter %.0f: latency reports %d, measured %d", sr, d, m);
        }

        {   // a 19 kHz tone at 44.1 kHz is the reconstruction's hardest case
            bb::Limiter lim;
            bb::Limiter::Settings s;
            s.driveDb = 6; s.ceilingDb = -1;
            lim.setSettings (s);
            lim.prepare (sr, 512);
            auto l = sine (sr, 19000, 0.9, 1.0), r = l;
            runBlocks ([&] (double* a, double* b, int n) { lim.process (a, b, n); }, l, r);
            const double tp = truePeakDb (l, r, sr, l.size() / 4);
            CHECK (tp <= -1.0 + 0.05, "limiter %.0f: 19 kHz true peak %.3f dBTP", sr, tp);
        }
    }

    {   // switching styles mid-limit never lets an over through
        const double sr = 48000;
        bb::Limiter lim;
        bb::Limiter::Settings s;
        s.driveDb = 14; s.ceilingDb = -0.3;
        lim.setSettings (s);
        lim.prepare (sr, 256);
        std::vector<double> l, r;
        programme (sr, 3.0, l, r, 3);
        int k = 0;
        for (size_t pos = 0; pos < l.size(); pos += 256, ++k)
        {
            if (k % 40 == 0) { s.style = (s.style + 1) % bb::Limiter::kStyles; lim.setSettings (s); }
            lim.process (l.data() + pos, r.data() + pos, (int) std::min<size_t> (256, l.size() - pos));
        }
        CHECK (truePeakDb (l, r, sr, 0) <= -0.3 + 0.05, "limiter: style switching let a true peak of %.3f dBTP through", truePeakDb (l, r, sr, 0));
    }

    {   // stereo link: an over in one channel turns both down together
        const double sr = 48000;
        bb::Limiter lim;
        bb::Limiter::Settings s;
        s.ceilingDb = -6;
        lim.setSettings (s);
        lim.prepare (sr, 512);
        auto l = sine (sr, 1000, 1.0, 1.0), r = sine (sr, 1000, 0.1, 1.0);
        runBlocks ([&] (double* a, double* b, int n) { lim.process (a, b, n); }, l, r);
        const double gl = harmonic (l, sr, 1000, 1, l.size() / 2), gr = harmonic (r, sr, 1000, 1, l.size() / 2) / 0.1;
        CHECK (std::abs (dbOf (gl) - dbOf (gr)) < 0.2, "limiter link: left %.2f dB, right %.2f dB", dbOf (gl), dbOf (gr));
    }

    // distortion at 6 dB of gain reduction, the figures to beat
    std::printf ("limiter THD+N at 6 dB reduction, 48 kHz, -1 dBTP ceiling (auto release):\n");
    const char* names[] = { "transparent", "punch", "dense" };
    double transparent50 = 1;
    for (int style = 0; style < bb::Limiter::kStyles; ++style)
    {
        std::printf ("  %-12s", names[style]);
        for (double hz : { 30.0, 50.0, 100.0, 1000.0 })
        {
            const double sr = 48000;
            bb::Limiter lim;
            bb::Limiter::Settings s;
            s.driveDb = 6; s.ceilingDb = -1; s.style = style;
            lim.setSettings (s);
            lim.prepare (sr, 512);
            auto l = sine (sr, hz, std::pow (10, -1.0 / 20), 6.0), r = l;
            runBlocks ([&] (double* a, double* b, int n) { lim.process (a, b, n); }, l, r);
            const double t = thd (l, sr, hz, l.size() / 2);
            if (style == 0 && hz == 50.0) transparent50 = t;
            std::printf ("  %4.0f Hz %.4f%%", hz, 100 * t);
        }
        std::printf ("\n");
    }
    CHECK (transparent50 < 0.01, "limiter: transparent 50 Hz THD %.3f%% at 6 dB reduction", 100 * transparent50);
}

int main()
{
    testCompressor();
    testLimiter();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
