// Engine tests (no JUCE). Run via ctest.
//
// The loudness and true-peak cases are the synthetic signals of EBU Tech 3341 (cases 1-5, 15-19) and
// EBU Tech 3342 (cases 1-4), with the tolerances those documents give.
#include "chain/Chain.h"
#include "chain/TapeModule.h"
#include "meter/KWeighting.h"
#include "meter/LoudnessMeter.h"
#include "meter/MeterSuite.h"
#include "meter/StereoMeter.h"
#include "meter/TruePeak.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kPi = 3.14159265358979323846;

struct Stereo { std::vector<double> l, r; };

// stereo 1 kHz sine segments: {dBFS, seconds}
static Stereo toneSegments (double sr, std::initializer_list<std::pair<double, double>> segs, double hz = 1000)
{
    Stereo s;
    double ph = 0;
    for (auto [db, secs] : segs)
    {
        const double a = std::pow (10.0, db / 20.0);
        const auto n = (size_t) std::lround (secs * sr);
        for (size_t i = 0; i < n; ++i) { const double v = a * std::sin (ph); s.l.push_back (v); s.r.push_back (v); ph += 2 * kPi * hz / sr; }
    }
    return s;
}

static bb::LoudnessMeter measure (double sr, const Stereo& s, int block = 512)
{
    bb::LoudnessMeter m;
    m.prepare (sr, 2);
    for (size_t pos = 0; pos < s.l.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, s.l.size() - pos);
        const double* ch[2] { s.l.data() + pos, s.r.data() + pos };
        m.process (ch, n);
    }
    return m;
}

static void testKWeighting()
{
    bb::KWeighting k;
    k.prepare (48000);
    // BS.1770-4 Tables 1 and 2
    CHECK (std::abs (k.shelf.b0 - 1.53512485958697) < 1e-7 && std::abs (k.shelf.b1 + 2.69169618940638) < 1e-7
           && std::abs (k.shelf.b2 - 1.19839281085285) < 1e-7 && std::abs (k.shelf.a1 + 1.69065929318241) < 1e-7
           && std::abs (k.shelf.a2 - 0.73248077421585) < 1e-7, "K-weighting shelf does not match BS.1770 at 48 kHz");
    CHECK (std::abs (k.hp.a1 + 1.99004745483398) < 1e-7 && std::abs (k.hp.a2 - 0.99007225036621) < 1e-7,
           "K-weighting high-pass does not match BS.1770 at 48 kHz");
}

static void testLoudness()
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        {   // 3341 case 1 and 2: constant tones read their level in M, S and I
            for (double db : { -23.0, -33.0 })
            {
                auto m = measure (sr, toneSegments (sr, { { db, 20.0 } }));
                CHECK (std::abs (m.momentary() - db) < 0.1 && std::abs (m.shortTerm() - db) < 0.1 && std::abs (m.integrated() - db) < 0.1,
                       "3341 tone %.0f dBFS at %.0f Hz: M %.2f S %.2f I %.2f", db, sr, m.momentary(), m.shortTerm(), m.integrated());
            }
        }
        {   // 3341 case 3: relative gate drops the quiet parts
            auto m = measure (sr, toneSegments (sr, { { -36, 10 }, { -23, 60 }, { -36, 10 } }));
            CHECK (std::abs (m.integrated() + 23) < 0.1, "3341 case 3 at %.0f: I %.2f", sr, m.integrated());
        }
        {   // 3341 case 4: absolute gate too
            auto m = measure (sr, toneSegments (sr, { { -72, 10 }, { -36, 10 }, { -23, 60 }, { -36, 10 }, { -72, 10 } }));
            CHECK (std::abs (m.integrated() + 23) < 0.1, "3341 case 4 at %.0f: I %.2f", sr, m.integrated());
        }
        {   // 3341 case 5
            auto m = measure (sr, toneSegments (sr, { { -26, 20 }, { -20, 20.1 }, { -26, 20 } }));
            CHECK (std::abs (m.integrated() + 23) < 0.1, "3341 case 5 at %.0f: I %.2f", sr, m.integrated());
        }
        // 3342 cases 1-4: loudness range
        struct Lra { std::initializer_list<std::pair<double, double>> segs; double expect; };
        const Lra cases[] = {
            { { { -20, 20 }, { -30, 20 } }, 10 },
            { { { -20, 20 }, { -15, 20 } }, 5 },
            { { { -40, 20 }, { -20, 20 } }, 20 },
            { { { -50, 20 }, { -35, 20 }, { -20, 20 }, { -35, 20 }, { -50, 20 } }, 15 },
        };
        int idx = 1;
        for (const auto& c : cases)
        {
            auto m = measure (sr, toneSegments (sr, c.segs));
            CHECK (std::abs (m.range() - c.expect) < 1.0, "3342 case %d at %.0f: LRA %.2f, want %.0f", idx, sr, m.range(), c.expect);
            ++idx;
        }
    }

    {   // reset clears everything; silence reads silence
        auto m = measure (48000, toneSegments (48000, { { -10, 5 } }));
        m.reset();
        CHECK (m.integrated() <= bb::LoudnessMeter::kSilence && m.range() == 0, "reset did not clear the meter");
        auto z = measure (48000, toneSegments (48000, { { -300, 5 } }));
        CHECK (z.integrated() <= bb::LoudnessMeter::kSilence, "silence gave an integrated value %.2f", z.integrated());
    }
    {   // block size makes no difference
        const auto s = toneSegments (44100, { { -30, 3 }, { -18, 4 }, { -40, 3 } });
        auto a = measure (44100, s, 1), b = measure (44100, s, 4096), c = measure (44100, s, 333);
        CHECK (a.integrated() == b.integrated() && b.integrated() == c.integrated() && a.range() == c.range(),
               "results depend on block size: %.6f %.6f %.6f", a.integrated(), b.integrated(), c.integrated());
    }
}

static double truePeakDb (double sr, double amp, double hz, double phaseDeg, double secs = 1.0)
{
    bb::TruePeak tp;
    tp.prepare (sr, 1);
    std::vector<double> x ((size_t) (sr * secs));
    // a 20 ms fade-in: an abrupt start really does overshoot between the samples, which is not what
    // these cases measure
    const double fade = 0.02 * sr;
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = std::min (1.0, (double) i / fade) * amp * std::sin (2 * kPi * hz * (double) i / sr + phaseDeg * kPi / 180);
    const double* ch[1] { x.data() };
    tp.process (ch, (int) x.size());
    return 20 * std::log10 (tp.heldPeak (0));
}

static void testTruePeak()
{
    // 3341 cases 15-19 (48 kHz): tolerance +0.2 / -0.4 dB
    struct Tp { double amp, hz, phase, expect; };
    const Tp cases[] = { { 0.5, 12000, 0, -6.02 }, { 0.5, 12000, 45, -6.02 }, { 0.5, 8000, 60, -6.02 },
                         { 0.5, 6000, 67.5, -6.02 }, { 1.41, 12000, 45, 2.98 } };
    int idx = 15;
    for (const auto& c : cases)
    {
        const double v = truePeakDb (48000, c.amp, c.hz, c.phase);
        CHECK (v - c.expect < 0.2 && c.expect - v < 0.4, "3341 case %d: %.3f dBTP, want %.2f", idx, v, c.expect);
        ++idx;
    }
    // tighter than the standard: within 0.1 dB to 20 kHz at 44.1 kHz, at any phase
    double worst = 0;
    for (double hz : { 997.0, 5000.0, 11025.0, 15000.0, 18000.0, 20000.0 })
        for (double ph = 0; ph < 90; ph += 7.5)
            worst = std::max (worst, std::abs (truePeakDb (44100, 0.5, hz, ph, 0.25) + 6.0206));
    CHECK (worst < 0.1, "true peak error up to %.3f dB at 44.1 kHz (want < 0.1)", worst);
    // and it never reads below the sample peak
    CHECK (truePeakDb (48000, 1.0, 100, 90) >= -0.001, "true peak below the sample peak");
}

static void testStereo()
{
    const double sr = 48000;
    std::mt19937 rng (1);
    std::normal_distribution<double> g (0, 0.1);
    std::vector<double> a (48000), b (48000), inv (48000);
    for (size_t i = 0; i < a.size(); ++i) { a[i] = g (rng); b[i] = g (rng); inv[i] = -a[i]; }
    bb::StereoMeter s;
    s.prepare (sr); s.process (a.data(), a.data(), (int) a.size());
    CHECK (std::abs (s.correlation() - 1) < 1e-6 && s.widthDb() < -90, "mono should read +1, no width");
    s.reset(); s.process (a.data(), inv.data(), (int) a.size());
    CHECK (std::abs (s.correlation() + 1) < 1e-6, "inverted should read -1");
    s.reset(); s.process (a.data(), b.data(), (int) a.size());
    CHECK (std::abs (s.correlation()) < 0.2 && std::abs (s.widthDb()) < 1.5, "unrelated noise: corr %.3f width %.2f dB", s.correlation(), s.widthDb());
}

static void testSuite()
{
    // PLR and the published readings: -23 dBFS sine, peak -23 dBTP, I -23 LUFS -> PLR 0
    bb::MeterSuite m;
    m.prepare (48000, 2);
    const auto s = toneSegments (48000, { { -23, 10 } });
    for (size_t pos = 0; pos < s.l.size(); pos += 480) m.process (s.l.data() + pos, s.r.data() + pos, 480);
    CHECK (std::abs (m.readings.integrated.load() + 23) < 0.1 && std::abs (m.readings.plr.load()) < 0.1
           && std::abs (m.readings.truePeakHeld[0].load() + 23) < 0.05, "suite: I %.2f PLR %.2f TP %.2f",
           m.readings.integrated.load(), m.readings.plr.load(), m.readings.truePeakHeld[0].load());
    std::uint64_t rp = 0; bb::LoudnessPoint pts[256];
    const int got = m.history.read (rp, pts, 256);
    CHECK (got == 100, "history should hold one point per 100 ms (got %d)", got);
    m.requestReset();
    m.process (s.l.data(), s.r.data(), 480);
    CHECK (m.readings.integrated.load() <= -199, "reset did not reach the audio thread");
}

// a pure delay of d samples, to test the chain's latency accounting
class DelayModule final : public bb::Module
{
public:
    DelayModule (const char* i, int d) : mid (i), delay (d) {}
    const char* id() const override { return mid; }
    const char* name() const override { return mid; }
    void prepare (double, int) override { buf[0].assign ((size_t) delay + 1, 0.0); buf[1].assign ((size_t) delay + 1, 0.0); pos = 0; }
    void reset() override { std::fill (buf[0].begin(), buf[0].end(), 0.0); std::fill (buf[1].begin(), buf[1].end(), 0.0); }
    int latency() const override { return delay; }
    void process (double* l, double* r, int n) override
    {
        double* io[2] { l, r };
        const int len = delay + 1;
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c) { buf[c][(size_t) pos] = io[c][i]; io[c][i] = buf[c][(size_t) ((pos + 1) % len)] * gainBy; }
            pos = (pos + 1) % len;
        }
    }
    double gainBy = 1.0;

private:
    const char* mid; int delay; int pos = 0;
    std::vector<double> buf[2];
};

static int firstNonZero (const std::vector<double>& v)
{
    for (size_t i = 0; i < v.size(); ++i) if (std::abs (v[i]) > 1e-9) return (int) i;
    return -1;
}

static void runChain (bb::Chain& c, std::vector<double>& l, std::vector<double>& r, int block)
{
    for (size_t pos = 0; pos < l.size(); pos += (size_t) block)
        c.process (l.data() + pos, r.data() + pos, (int) std::min<size_t> ((size_t) block, l.size() - pos));
}

static void testChain()
{
    bb::Chain c;
    c.add (std::make_unique<DelayModule> ("a", 3));
    c.add (std::make_unique<DelayModule> ("b", 0));
    c.add (std::make_unique<DelayModule> ("c", 17));
    c.add (std::make_unique<bb::EmptySlot> ("d", "D"));
    c.prepare (48000, 256);
    CHECK (c.latency() == 20, "latency should be the sum (20), got %d", c.latency());

    auto impulseAt = [&] (int block)
    {
        std::vector<double> l (4000), r (4000);
        l[1000] = r[1000] = 1.0;
        runChain (c, l, r, block);
        return firstNonZero (l) - 1000;
    };
    CHECK (impulseAt (64) == 20, "impulse should arrive after 20 samples");

    // bypass keeps the delay: let the 10 ms crossfade finish, then check
    c.setBypassed (2, true);
    { std::vector<double> l (2000), r (2000); runChain (c, l, r, 100); }
    CHECK (impulseAt (37) == 20, "bypassed: impulse should still arrive after 20 samples");
    c.setBypassed (0, true); c.setBypassed (1, true);
    { std::vector<double> l (2000), r (2000); runChain (c, l, r, 100); }
    CHECK (impulseAt (1) == 20, "all bypassed: impulse should still arrive after 20 samples");
    for (int s = 0; s < 3; ++s) c.setBypassed (s, false);
    { std::vector<double> l (2000), r (2000); runChain (c, l, r, 100); }

    // bypass is a crossfade, not a step
    {
        auto& m = static_cast<DelayModule&> (c.module (1));
        m.gainBy = 0.0;                       // module 1 now mutes; bypassing it restores the signal
        std::vector<double> l (2000, 1.0), r (2000, 1.0);
        runChain (c, l, r, 500);              // settle on a steady (muted) signal first
        std::fill (l.begin(), l.end(), 1.0); std::fill (r.begin(), r.end(), 1.0);
        c.setBypassed (1, true);
        runChain (c, l, r, 2000);
        double maxJump = 0;
        for (size_t i = 1; i < l.size(); ++i) maxJump = std::max (maxJump, std::abs (l[i] - l[i - 1]));
        CHECK (maxJump < 0.01 && std::abs (l.back() - 1.0) < 1e-12, "bypass jumped by %.4f (end %.4f)", maxJump, l.back());
        m.gainBy = 1.0;
        c.setBypassed (1, false);
        std::vector<double> z (2000), z2 (2000); runChain (c, z, z2, 2000);
    }

    // order: invalid orders are refused; a new order mutes briefly and comes back intact
    bb::Chain::Order bad {}; bad[0] = 0; bad[1] = 0; bad[2] = 1; bad[3] = 2;
    CHECK (! c.setOrder (bad), "duplicate slots should be refused");
    bb::Chain::Order o {}; o[0] = 3; o[1] = 2; o[2] = 1; o[3] = 0;
    {
        std::vector<double> l (4800, 0.5), r (4800, 0.5);
        runChain (c, l, r, 128);              // steady signal through the old order
        std::fill (l.begin(), l.end(), 0.5); std::fill (r.begin(), r.end(), 0.5);
        CHECK (c.setOrder (o), "a permutation should be accepted");
        runChain (c, l, r, 128);
        double minV = 1, maxJump = 0;
        for (size_t i = 0; i < l.size(); ++i) { minV = std::min (minV, l[i]); if (i) maxJump = std::max (maxJump, std::abs (l[i] - l[i - 1])); }
        CHECK (minV == 0.0 && std::abs (l.back() - 0.5) < 1e-12 && maxJump < 0.01, "reorder: min %.3f end %.3f jump %.4f", minV, l.back(), maxJump);
        CHECK (c.order() == o, "order() should report the new order");
    }
    { std::vector<double> z (2000), z2 (2000); runChain (c, z, z2, 2000); }   // flush the steady signal
    { const int d = impulseAt (512); CHECK (d == 20, "after reorder the latency is unchanged (impulse after %d)", d); }
}

static void testTapeLatency()
{
    // the tape slot reports the latency its record path really has: find it by cross-correlation
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        bb::Chain c;
        auto* t = new bb::TapeModule();
        t->amount.store (0.0);
        c.add (std::unique_ptr<bb::Module> (t));
        c.prepare (sr, 512);
        std::mt19937 rng (7);
        std::normal_distribution<double> g (0, 0.02);
        std::vector<double> in ((size_t) sr), l, r;
        for (auto& v : in) v = g (rng);
        l = in; r = in;
        runChain (c, l, r, 512);
        int best = -1; double bestV = -1;
        for (int d = 0; d < 64; ++d)
        {
            double acc = 0;
            for (size_t i = (size_t) sr / 2; i < in.size(); ++i) acc += in[i - (size_t) d] * l[i];
            if (acc > bestV) { bestV = acc; best = d; }
        }
        CHECK (std::abs (best - c.latency()) <= 1, "tape at %.0f: reports %d samples, measured %d", sr, c.latency(), best);
    }
}

int main()
{
    testKWeighting();
    testLoudness();
    testTruePeak();
    testStereo();
    testSuite();
    testChain();
    testTapeLatency();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
