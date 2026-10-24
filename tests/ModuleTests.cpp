// Module tests (no JUCE): the Class-A EQ and the tape machine. Run via ctest.
#include "chain/Chain.h"
#include "chain/CompModule.h"
#include "chain/DeEssModule.h"
#include "chain/EqModule.h"
#include "chain/LimitModule.h"
#include "chain/TapeModule.h"
#include "eq/ClassAEq.h"
#include "tape/TapeMachine.h"
#include "dsp/Dither.h"

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
    {   // the opt-in extras: crosstalk, azimuth, hiss, bias trim
        auto rightFromLeft = [&] (bool xt)
        {
            bb::TapeMachine t;
            auto st = s; st.crosstalk = xt;
            auto p = tapeProc (t, st, sr);
            auto l = sine (sr, 1000, 0.3, 1.0);
            std::vector<double> r (l.size(), 0.0);
            runBlocks (p, l, r);
            return 20 * std::log10 (std::max (harmonic (r, sr, 1000, 1), 1e-12) / harmonic (l, sr, 1000, 1));
        };
        const double off = rightFromLeft (false), on = rightFromLeft (true);
        CHECK (off < -120 && on > -46 && on < -40, "tape crosstalk at 1 kHz: %.1f dB off, %.1f dB on", off, on);

        auto monoTop = [&] (double us)
        {
            bb::TapeMachine t;
            auto st = s; st.azimuthUs = us;
            auto p = tapeProc (t, st, sr);
            auto l = sine (sr, 15000, 0.05, 1.0), r = l;
            runBlocks (p, l, r);
            std::vector<double> m (l.size());
            for (size_t i = 0; i < l.size(); ++i) m[i] = 0.5 * (l[i] + r[i]);
            return 20 * std::log10 (harmonic (m, sr, 15000, 1) / harmonic (l, sr, 15000, 1));
        };
        const double az0 = monoTop (0), az20 = monoTop (20);
        CHECK (std::abs (az0) < 0.05 && az20 < -3.5 && az20 > -6, "tape azimuth: 15 kHz in mono %.2f dB at 0 us, %.2f dB at 20 us", az0, az20);

        auto hissDb = [&] (int tape, int speed)
        {
            bb::TapeMachine t;
            auto st = s; st.hiss = true; st.tape = tape; st.speed = speed;
            auto p = tapeProc (t, st, sr);
            std::vector<double> l ((size_t) sr * 2, 0.0), r = l;
            runBlocks (p, l, r);
            double e = 0;
            for (size_t i = l.size() / 2; i < l.size(); ++i) e += l[i] * l[i] + r[i] * r[i];
            return 10 * std::log10 (e / (double) l.size());
        };
        const double h456 = hissDb (0, 0), hGp9 = hissDb (1, 0), h30 = hissDb (0, 1);
        CHECK (h456 > -87 && h456 < -81 && hGp9 < h456 - 1 && h30 < h456 - 1, "tape hiss: 456 15 ips %.1f, GP9 %.1f, 30 ips %.1f dBFS RMS", h456, hGp9, h30);

        auto trimmed = [&] (int bias, double trim) { auto st = s; st.bias = bias; st.biasTrim = trim; return tapeThd (st, sr, 4); };
        const double nom = trimmed (1, 0), up = trimmed (1, 1), over = trimmed (2, 0), down = trimmed (1, -1), under = trimmed (0, 0);
        CHECK (over < up && up < nom && nom < down && down < under, "tape bias trim: under %.3f%%, -1 %.3f%%, nominal %.3f%%, +1 %.3f%%, over %.3f%%",
               100 * under, 100 * down, 100 * nom, 100 * up, 100 * over);

        // a trim sweep is smooth
        bb::TapeMachine t;
        t.setSettings (s); t.prepare (sr, 256);
        auto l = sine (sr, 300, 0.2, 1.5), r = l;
        for (size_t pos = 0; pos < l.size(); pos += 256)
        {
            auto st = s;
            st.biasTrim = pos < (size_t) (0.5 * sr) ? 0.0 : (pos < (size_t) (0.6 * sr) ? 1.0 : -1.0);
            t.setSettings (st);
            t.process (l.data() + pos, r.data() + pos, (int) std::min<size_t> (256, l.size() - pos));
        }
        const double rough = roughness (l, sr, 300, 0.2 * 1.2, (size_t) (0.3 * sr), l.size());
        CHECK (rough < 1.1, "tape bias trim jumps: step %.2fx the largest clean step", rough);
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

// reordering the full chain, every module working hard, makes no click: on two low tones the
// high-frequency residue (a 4th difference) around the switch stays within that of the steady
// sound after it
static void testReorder()
{
    for (double sr : { 44100.0, 96000.0 })
    {
        bb::Chain chain;
        auto* eq = new bb::EqModule(); auto* comp = new bb::CompModule(); auto* tape = new bb::TapeModule();
        auto* ds = new bb::DeEssModule(); auto* lim = new bb::LimitModule();
        eq->settings.band[0] = { 60, 4, false, true };
        eq->settings.band[2] = { 1000, 6, false, false };
        eq->settings.band[4] = { 10000, 4, false, true };
        eq->settings.hpfHz = 25;
        eq->settings.monoBassHz = 120;
        comp->settings.optoThreshold = -24;
        comp->settings.vcaThreshold = -20;
        tape->settings.inputDb = 6;
        ds->settings.thresholdDb = -12;
        lim->settings.driveDb = 8;
        for (bb::Module* m : std::initializer_list<bb::Module*> { eq, comp, tape, ds, lim }) chain.add (std::unique_ptr<bb::Module> (m));
        const int block = 256;
        chain.prepare (sr, block);

        const int n = (int) (sr * 3.0), sw = (int) (sr * 1.5) / block * block;
        std::vector<double> l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            l[(size_t) i] = 0.3 * std::sin (2 * kPi * 110 * t) + 0.2 * std::sin (2 * kPi * 330 * t + 1);
            r[(size_t) i] = 0.3 * std::sin (2 * kPi * 110 * t + 0.3) + 0.2 * std::sin (2 * kPi * 330 * t);
        }
        for (int off = 0; off < n; off += block)
        {
            if (off == sw) { bb::Chain::Order o {}; const int p[5] = { 4, 2, 0, 3, 1 }; for (int k = 0; k < 5; ++k) o[(size_t) k] = p[k]; chain.setOrder (o); }
            chain.process (l.data() + off, r.data() + off, std::min (block, n - off));
        }
        auto residue = [&] (int from, int to)
        {
            double worst = 0;
            const int w = (int) (sr * 0.001);
            for (int a = from; a + w + 4 < to; a += w)
            {
                double e = 0;
                for (int i = a; i < a + w; ++i)
                    for (auto* x : { &l, &r })
                    {
                        const auto* v = x->data() + i;
                        const double d = v[4] - 4 * v[3] + 6 * v[2] - 4 * v[1] + v[0];
                        e += d * d;
                    }
                worst = std::max (worst, std::sqrt (e / (2 * w)));
            }
            return worst;
        };
        const double around = residue (sw - (int) (sr * 0.01), sw + (int) (sr * 0.2));
        const double after = residue (sw + (int) (sr * 0.5), n - 8);
        CHECK (around < 2.0 * after, "reorder at %.0f: residue %.3g around the switch, %.3g steady after", sr, around, after);
    }
}

// the output's dither: on the word's grid, TPDF noise at the right level, shaped noise pushed up
static void testDither()
{
    const double sr = 44100;
    const int n = (int) sr * 2;
    auto band = [&] (const std::vector<double>& e, double lo, double hi)   // mean power in a band, via DFT bins
    {
        double p = 0; int k = 0;
        for (double f = lo; f < hi; f += 97.0)
        {
            double re = 0, im = 0;
            for (int i = 0; i < n; ++i) { const double ph = 2 * kPi * f * i / sr; re += e[(size_t) i] * std::cos (ph); im += e[(size_t) i] * std::sin (ph); }
            p += re * re + im * im; ++k;
        }
        return 10 * std::log10 (p / k);
    };
    std::vector<double> err[3];
    for (int mode : { bb::Dither::Bits24, bb::Dither::Bits16, bb::Dither::Bits16Shaped })
    {
        bb::Dither d;
        auto l = sine (sr, 997, 0.25, 2.0), r = sine (sr, 997, 0.25, 2.0);
        const auto in = l;
        for (int pos = 0; pos < n; pos += 512) d.process (l.data() + pos, r.data() + pos, std::min (512, n - pos), mode);
        const double q = mode == bb::Dither::Bits24 ? 8388608.0 : 32768.0;
        bool onGrid = true;
        double e2 = 0, lr = 0;
        std::vector<double> e ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            onGrid = onGrid && std::abs (l[(size_t) i] * q - std::round (l[(size_t) i] * q)) < 1e-6;
            e[(size_t) i] = (l[(size_t) i] - in[(size_t) i]) * q;   // in LSBs
            e2 += e[(size_t) i] * e[(size_t) i];
            lr += e[(size_t) i] * (r[(size_t) i] - in[(size_t) i]) * q;
        }
        const double rms = std::sqrt (e2 / n);
        CHECK (onGrid, "dither %d: every sample is on the word's grid", mode);
        if (mode != bb::Dither::Bits16Shaped)
        {
            // TPDF dither: the total error is 1/4 LSB^2 (three times undithered rounding's 1/12), 0.5 LSB rms
            CHECK (std::abs (rms - 0.5) < 0.02, "dither %d: TPDF noise %.3f LSB rms (0.5 expected)", mode, rms);
            CHECK (std::abs (lr / e2) < 0.02, "dither %d: the channels' noise is independent (correlation %.3f)", mode, lr / e2);
        }
        err[mode - 1] = e;
    }
    // shaped: less noise where the ear is most sensitive (1 - 5 kHz), more near the top of the band
    const double flatMid = band (err[1], 1000, 5000), shapedMid = band (err[2], 1000, 5000);
    const double flatTop = band (err[1], 17000, 21000), shapedTop = band (err[2], 17000, 21000);
    CHECK (shapedMid < flatMid - 6, "dither shaped: %.1f dB less noise at 1-5 kHz", flatMid - shapedMid);
    CHECK (shapedTop > flatTop + 6, "dither shaped: %.1f dB more noise at 17-21 kHz, where it is least audible", shapedTop - flatTop);
}

// bypassing the EQ fades to a dry signal in phase with it, so the top end doesn't dip during the fade
static void testEqBypass()
{
    for (double sr : { 44100.0, 48000.0 })
    {
        bb::Chain chain;
        chain.add (std::make_unique<bb::EqModule>());
        chain.prepare (sr, 256);
        const double hz = 15000;
        auto l = sine (sr, hz, 0.5, 0.6), r = l;
        const int n = (int) l.size();
        for (int pos = 0; pos < n; pos += 256)
        {
            if (pos == (int) (0.3 * sr) / 256 * 256) chain.setBypassed (0, true);
            chain.process (l.data() + pos, r.data() + pos, std::min (256, n - pos));
        }
        // the tone's level in 1 ms windows (its own DFT bin), through the fade
        double lo = 1, hi = 0;
        const int w = (int) (0.001 * sr);
        for (int i = (int) (0.1 * sr); i + w < n; i += w / 2)
        {
            double re = 0, im = 0;
            for (int k = i; k < i + w; ++k) { re += l[(size_t) k] * std::cos (2 * kPi * hz * k / sr); im += l[(size_t) k] * std::sin (2 * kPi * hz * k / sr); }
            const double a = 2 * std::hypot (re, im) / w;
            lo = std::min (lo, a); hi = std::max (hi, a);
        }
        CHECK (20 * std::log10 (hi / lo) < 0.2, "eq bypass at %.0f: 15 kHz holds its level through the fade (%.2f dB swing)", sr, 20 * std::log10 (hi / lo));
    }
}

// M/S on the BB-5: in MID the bands touch only the mid, in SIDE only the side
static void testEqStereoModes()
{
    const double sr = 48000;
    for (int mode : { 1, 2 })
    {
        // the level (dB) a 1 kHz tone comes out at, for a mid-only (L = R) or side-only (L = -R) input,
        // with +6 dB at 1 kHz in the mid band
        auto level = [&] (bool sideIn)
        {
            bb::ClassAEq eq;
            bb::eq::Settings st;
            st.band[2] = { 1000.0, 6.0, false, false };
            st.stereo = mode;
            eq.setSettings (st);
            eq.prepare (sr, 512);
            auto l = sine (sr, 1000, 0.1, 0.5), r = l;
            if (sideIn) for (auto& v : r) v = -v;
            runBlocks ([&] (double* a, double* b, int n) { eq.process (a, b, n); }, l, r);
            double peak = 0;
            for (size_t i = l.size() / 2; i < l.size(); ++i) peak = std::max (peak, std::abs (l[i]));
            return 20 * std::log10 (peak / 0.1);
        };
        const double mid = level (false), side = level (true);
        const double boosted = mode == 1 ? mid : side, untouched = mode == 1 ? side : mid;
        CHECK (std::abs (boosted - 6.0) < 0.3, "eq %s mode: the %s gets the boost (%+.2f dB)", mode == 1 ? "MID" : "SIDE", mode == 1 ? "mid" : "side", boosted);
        CHECK (std::abs (untouched) < 0.05, "eq %s mode: the %s is untouched (%+.3f dB)", mode == 1 ? "MID" : "SIDE", mode == 1 ? "side" : "mid", untouched);
    }

    // switching the mode under a boost fades the bands through 0 dB and back: no worse than turning
    // the band's gain down by hand (the EQ's ordinary 20 ms glide)
    auto across = [&] (bool switchMode)
    {
        bb::ClassAEq eq;
        bb::eq::Settings st;
        st.band[2] = { 1000.0, 9.0, false, false };
        eq.setSettings (st);
        eq.prepare (sr, 256);
        auto l = sine (sr, 1000, 0.1, 0.6), r = sine (sr, 1000, 0.05, 0.6);
        for (size_t pos = 0; pos < l.size(); pos += 256)
        {
            if (pos == 256 * 80)
            {
                if (switchMode) st.stereo = 2; else st.band[2].gain = 0.0;
                eq.setSettings (st);
            }
            eq.process (l.data() + pos, r.data() + pos, (int) std::min<size_t> (256, l.size() - pos));
        }
        double worst = 0;
        for (size_t i = 256 * 80; i < l.size(); ++i)
            worst = std::max (worst, std::abs (l[i] - 4 * l[i - 1] + 6 * l[i - 2] - 4 * l[i - 3] + l[i - 4]));
        return worst;
    };
    const double modeSwitch = across (true), byHand = across (false);
    CHECK (modeSwitch < byHand * 1.2, "eq stereo mode switches as smoothly as a gain move (4th difference %g, by hand %g)", modeSwitch, byHand);
}

int main()
{
    testEqStereoModes();
    testEqBypass();
    testDither();
    testEq();
    testTape();
    testReorder();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
