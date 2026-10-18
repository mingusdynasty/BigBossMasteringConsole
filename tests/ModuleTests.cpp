// Module tests (no JUCE): the Class-A EQ and the tape machine. Run via ctest.
#include "eq/ClassAEq.h"
#include "tape/TapeMachine.h"

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

// run l (and r = l, or the given r) through p in 512-sample blocks
static void runBlocks (const Proc& p, std::vector<double>& l, std::vector<double>& r, int block = 512)
{
    for (size_t pos = 0; pos < l.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, l.size() - pos);
        p (l.data() + pos, r.data() + pos, n);
    }
}

// amplitude of harmonic h of hz over the last half of x
static double harmonic (const std::vector<double>& x, double sr, double hz, int h)
{
    const size_t start = x.size() / 2;
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

static double thd (const std::vector<double>& x, double sr, double hz)
{
    double h = 0;
    for (int k = 2; k <= 7 && k * hz < sr / 2; ++k) { const double a = harmonic (x, sr, hz, k); h += a * a; }
    return std::sqrt (h) / harmonic (x, sr, hz, 1);
}

static double gainDb (const Proc& p, double sr, double hz, double amp = 0.1, double secs = 0.5)
{
    auto l = sine (sr, hz, amp, secs), r = l;
    runBlocks (p, l, r);
    return 20 * std::log10 (harmonic (l, sr, hz, 1) / amp);
}

static int measuredLatency (const Proc& p, double sr)
{
    std::mt19937 rng (5);
    std::normal_distribution<double> g (0, 0.02);
    std::vector<double> in ((size_t) sr), l, r;
    for (auto& v : in) v = g (rng);
    l = in; r = in;
    runBlocks (p, l, r);
    int best = 0; double bestV = -1e30;
    for (int d = 0; d < 80; ++d)
    {
        double acc = 0;
        for (size_t i = (size_t) sr / 2; i < in.size(); ++i) acc += in[i - (size_t) d] * l[i];
        if (acc > bestV) { bestV = acc; best = d; }
    }
    return best;
}

// largest sample-to-sample step, relative to the largest a clean sine of this frequency has
static double roughness (const std::vector<double>& x, double sr, double hz, double amp, size_t from, size_t to)
{
    double m = 0;
    for (size_t i = std::max<size_t> (from, 1); i < to; ++i) m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m / (amp * 2 * kPi * hz / sr);
}

// ---------------------------------------------------------------- EQ

static void testEq()
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        bb::ClassAEq eq;
        bb::eq::Settings s;
        for (int i = 0; i < bb::eq::kBands; ++i) s.band[i].freq = bb::eq::kRange[i].def;
        auto make = [&] (const bb::eq::Settings& st) { eq.setSettings (st); eq.prepare (sr, 512); return Proc ([&] (double* l, double* r, int n) { eq.process (l, r, n); }); };

        // flat when nothing is engaged (the oversampling is transparent in the audio band)
        auto p = make (s);
        double worst = 0;
        for (double hz : { 30.0, 100.0, 1000.0, 10000.0, 18000.0 }) worst = std::max (worst, std::abs (gainDb (p, sr, hz)));
        CHECK (worst < 0.02, "EQ at %.0f: not flat when idle (%.3f dB)", sr, worst);

        // engaged bands match the response the editor draws, to 20 kHz
        s.band[0] = { 80, 4, false, true };
        s.band[1] = { 250, 3, true, false };
        s.band[2] = { 1000, 6, false, false };
        s.band[3] = { 5000, 2, true, false };
        s.band[4] = { 15000, 5, false, true };
        s.hpfHz = 25; s.hpfOrder = 4; s.lpfHz = 0;
        p = make (s);
        const double rate = sr < 88200 ? 4 * sr : (sr < 176400 ? 2 * sr : sr);
        worst = 0;
        for (double hz : { 40.0, 80.0, 250.0, 700.0, 1000.0, 3000.0, 5000.0, 10000.0, 15000.0, 19000.0 })
            worst = std::max (worst, std::abs (gainDb (p, sr, hz) - bb::eq::responseDb (s, hz, rate)));
        CHECK (worst < 0.1, "EQ at %.0f: audio and drawn curve differ by %.3f dB", sr, worst);
        CHECK (std::abs (gainDb (p, sr, 1000) - bb::eq::responseDb (s, 1000, rate)) < 0.05 && bb::eq::responseDb (s, 1000, rate) > 5.5,
               "EQ at %.0f: the 1 kHz boost should read about +6 dB", sr);
    }

    const double sr = 48000, rate = 192000;
    {   // cuts are narrower than boosts
        bb::eq::Settings b, c;
        b.band[2] = { 1000, 6, false, false };
        c.band[2] = { 1000, 6, true, false };
        const double boostAt2k = bb::eq::responseDb (b, 2000, rate), cutAt2k = -bb::eq::responseDb (c, 2000, rate);
        CHECK (boostAt2k > cutAt2k + 1.0, "boost should be broader than cut (%.2f vs %.2f dB an octave up)", boostAt2k, cutAt2k);
    }
    {   // high-pass corner, and its 24 dB slope
        bb::eq::Settings s; s.hpfHz = 40; s.hpfOrder = 4;
        CHECK (std::abs (bb::eq::responseDb (s, 40, rate) + 3.01) < 0.1 && std::abs (bb::eq::responseDb (s, 20, rate) + 24.1) < 1.0,
               "HPF: %.2f dB at 40 Hz, %.2f dB at 20 Hz", bb::eq::responseDb (s, 40, rate), bb::eq::responseDb (s, 20, rate));
    }
    {   // mono bass: side below the corner goes, mid stays
        bb::ClassAEq eq;
        bb::eq::Settings s; s.monoBassHz = 120;
        eq.setSettings (s); eq.prepare (sr, 512);
        auto l = sine (sr, 40, 0.1, 1.0), r = l;
        for (auto& v : r) v = -v;   // pure side
        runBlocks ([&] (double* a, double* b, int n) { eq.process (a, b, n); }, l, r);
        const double side = 20 * std::log10 (harmonic (l, sr, 40, 1) / 0.1);
        eq.reset();
        auto m = sine (sr, 40, 0.1, 1.0), m2 = m;
        runBlocks ([&] (double* a, double* b, int n) { eq.process (a, b, n); }, m, m2);
        const double mid = 20 * std::log10 (harmonic (m, sr, 40, 1) / 0.1);
        CHECK (side < -30 && std::abs (mid) < 0.05, "mono bass: side %.1f dB, mid %.2f dB at 40 Hz", side, mid);
    }
    {   // Warm: about 0.1% 2nd harmonic at 0 VU, nothing without it
        for (bool w : { false, true })
        {
            bb::ClassAEq eq;
            bb::eq::Settings s; s.warm = w;
            eq.setSettings (s); eq.prepare (sr, 512);
            const double amp = 0.12589 * std::sqrt (2.0);
            auto l = sine (sr, 1000, amp, 1.0), r = l;
            runBlocks ([&] (double* a, double* b, int n) { eq.process (a, b, n); }, l, r);
            const double h2 = harmonic (l, sr, 1000, 2) / harmonic (l, sr, 1000, 1);
            if (w) CHECK (h2 > 0.0005 && h2 < 0.002, "warm: 2nd harmonic %.4f%% at 0 VU", 100 * h2);
            else   CHECK (h2 < 1e-6, "not warm: 2nd harmonic %.6f%%", 100 * h2);
        }
    }
    {   // flipping Cut on a sounding band does not click; latency is what it reports
        bb::ClassAEq eq;
        bb::eq::Settings s; s.band[2] = { 1000, 9, false, false };
        eq.setSettings (s); eq.prepare (sr, 256);
        auto l = sine (sr, 1000, 0.3, 1.5), r = l;
        const size_t flip = (size_t) (0.5 * sr);
        for (size_t pos = 0; pos < l.size(); pos += 256)
        {
            if (pos >= flip) { s.band[2].cut = true; eq.setSettings (s); }
            if (pos >= flip + (size_t) (0.4 * sr)) { s.band[2].shelf = false; s.band[2].freq = 3000; eq.setSettings (s); }
            const int n = (int) std::min<size_t> (256, l.size() - pos);
            eq.process (l.data() + pos, r.data() + pos, n);
        }
        const double rough = roughness (l, sr, 1000, 0.3 * 2.82, (size_t) (0.2 * sr), l.size());
        CHECK (rough < 1.05, "EQ switch changes click: step %.2fx the largest clean step", rough);
        bb::eq::Settings f; f.band[2] = { 1000, 3, false, false };
        eq.setSettings (f); eq.prepare (sr, 512);
        const int d = measuredLatency ([&] (double* a, double* b, int n) { eq.process (a, b, n); }, sr);
        CHECK (std::abs (d - eq.latency()) <= 1, "EQ latency: reports %d, measured %d", eq.latency(), d);
    }
}

// ---------------------------------------------------------------- tape

static Proc tapeProc (bb::TapeMachine& t, const bb::TapeMachine::Settings& s, double sr)
{
    t.setSettings (s);
    t.prepare (sr, 512);
    return [&t] (double* l, double* r, int n) { t.process (l, r, n); };
}

static double tapeThd (const bb::TapeMachine::Settings& s, double sr, double dbOverOp, double hz = 1000)
{
    bb::TapeMachine t;
    auto p = tapeProc (t, s, sr);
    const double amp = std::sqrt (2.0) * 0.12589254 * std::pow (10.0, dbOverOp / 20);
    auto l = sine (sr, hz, amp, 1.0), r = l;
    runBlocks (p, l, r);
    return thd (l, sr, hz);
}

static void testTape()
{
    bb::TapeMachine::Settings s;
    s.wow = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        // MOL: 3% THD 8 dB over operating level on 456, 12 dB on GP9 (within a dB either way: the
        // record EQ and head response shift it a little from the bare-tape calibration)
        for (int tape : { 0, 1 })
        {
            s.tape = tape;
            const double mol = bb::TapeMachine::molDb (tape);
            const double below = tapeThd (s, sr, mol - 1), above = tapeThd (s, sr, mol + 1);
            CHECK (below < 0.03 && above > 0.03, "tape %d at %.0f: THD %.2f%% 1 dB under MOL, %.2f%% 1 dB over",
                   tape, sr, 100 * below, 100 * above);
        }
        s.tape = 0;
        // quiet signals pass at unity (playback head response aside, which is flat at 1 kHz)
        bb::TapeMachine t;
        const double g = gainDb (tapeProc (t, s, sr), sr, 1000, 0.01, 1.0);
        CHECK (std::abs (g) < 0.3, "tape at %.0f: small-signal gain %.2f dB at 1 kHz", sr, g);
        const int d = measuredLatency (tapeProc (t, s, sr), sr);
        CHECK (std::abs (d - t.latency()) <= 1, "tape at %.0f: latency reports %d, measured %d", sr, t.latency(), d);
    }

    const double sr = 48000;
    {   // formulation and bias move distortion the right way
        s.tape = 0; s.bias = 1;
        const double t456 = tapeThd (s, sr, 4);
        s.tape = 1;
        const double gp9 = tapeThd (s, sr, 4);
        CHECK (t456 > 1.5 * gp9, "456 should distort more than GP9 at +4 VU (%.3f%% vs %.3f%%)", 100 * t456, 100 * gp9);
        s.tape = 0;
        s.bias = 0; const double under = tapeThd (s, sr, 4);
        s.bias = 2; const double over = tapeThd (s, sr, 4);
        s.bias = 1;
        CHECK (under > t456 && t456 > over, "bias: under %.3f%%, nominal %.3f%%, over %.3f%%", 100 * under, 100 * t456, 100 * over);
    }
    {   // 30 ips keeps more of a hot 10 kHz tone than 15 ips; the head bump sits where it should
        bb::TapeMachine t;
        s.speed = 0; const double slow = gainDb (tapeProc (t, s, sr), sr, 10000, 0.5, 0.5);
        s.speed = 1; const double fast = gainDb (tapeProc (t, s, sr), sr, 10000, 0.5, 0.5);
        CHECK (fast > slow + 1.0, "hot 10 kHz: 30 ips %.2f dB, 15 ips %.2f dB", fast, slow);
        s.speed = 0;
        const double bump = gainDb (tapeProc (t, s, sr), sr, 50, 0.01, 2.0);
        s.speed = 1;
        const double bump30 = gainDb (tapeProc (t, s, sr), sr, 100, 0.01, 2.0);
        s.speed = 0;
        CHECK (bump > 0.6 && bump < 2.0 && bump30 > 0.5 && bump30 < 2.0, "head bump: %.2f dB at 50 Hz (15 ips), %.2f dB at 100 Hz (30 ips)", bump, bump30);
    }
    {   // wow and flutter: peak speed deviation in the A80's range at 100%, none at 0
        for (double w : { 0.0, 1.0 })
        {
            bb::TapeMachine t;
            s.wow = w;
            auto p = tapeProc (t, s, 96000);
            auto l = sine (96000, 1000, 0.05, 6.0), r = l;
            runBlocks (p, l, r);
            std::vector<double> zc;
            for (size_t i = 96000; i + 1 < l.size(); ++i)
                if (l[i] <= 0 && l[i + 1] > 0) zc.push_back ((double) i - l[i] / (l[i + 1] - l[i]));
            double dev = 0;
            for (size_t k = 10; k + 10 < zc.size(); ++k)
            {
                const double f = 10 * 96000 / (zc[k + 5] - zc[k - 5]);   // average over 10 cycles
                dev = std::max (dev, std::abs (f / 1000 - 1));
            }
            if (w > 0) CHECK (dev > 0.0002 && dev < 0.001, "wow at 100%%: peak deviation %.4f%%", 100 * dev);
            else CHECK (dev < 0.00005, "wow at 0: deviation %.5f%%", 100 * dev);
        }
        s.wow = 0;
    }
    {   // switching speed or tape mid-signal crossfades instead of clicking
        bb::TapeMachine t;
        t.setSettings (s); t.prepare (sr, 256);
        auto l = sine (sr, 300, 0.2, 1.5), r = l;
        for (size_t pos = 0; pos < l.size(); pos += 256)
        {
            auto st = s;
            if (pos >= (size_t) (0.5 * sr)) st.speed = 1;
            if (pos >= (size_t) (0.9 * sr)) st.tape = 1;
            t.setSettings (st);
            t.process (l.data() + pos, r.data() + pos, (int) std::min<size_t> (256, l.size() - pos));
        }
        const double rough = roughness (l, sr, 300, 0.2 * 1.2, (size_t) (0.3 * sr), l.size());
        CHECK (rough < 1.1, "tape switch changes click: step %.2fx the largest clean step", rough);
    }
    {   // input with link drives harder at the same output level
        bb::TapeMachine t;
        auto st = s; st.inputDb = 6;
        const double hot = tapeThd (st, sr, 0), cold = tapeThd (s, sr, 0);
        const double gHot = gainDb (tapeProc (t, st, sr), sr, 1000, 0.01, 1.0);
        CHECK (hot > 1.5 * cold && std::abs (gHot) < 0.3, "input +6 linked: THD %.3f%% vs %.3f%%, quiet gain %.2f dB", 100 * hot, 100 * cold, gHot);
    }
}

int main()
{
    testEq();
    testTape();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
