// The assistant, on synthetic mixes with one clear problem each: it reaches every loudness goal within
// half a LU under its ceiling, does about the compression it means to, and makes the obvious move for
// the problem (air on a dull mix, a cut on a muddy one, the de-esser on a sibilant one, mono bass on a
// wide one) while leaving the rest alone.
#include "assist/Assistant.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kPi = 3.14159265358979323846;

struct Mix
{
    double dull = 0, mud = 0, sibilance = 0, wideBass = 0;   // how much of each problem, 0 .. 1
};

// a 120 bpm loop: kick, bass, a pad of harmonics, hats, a vocal-ish tone with "s" bursts
static void make (const Mix& m, double sr, double secs, std::vector<float>& l, std::vector<float>& r)
{
    const size_t n = (size_t) (secs * sr);
    l.assign (n, 0.0f);
    r.assign (n, 0.0f);
    std::mt19937 rng (7);
    std::normal_distribution<double> noise (0, 1);
    double lp = 0, hpState = 0, prev = 0, s1 = 0, s2 = 0;
    // a resonator at 7 kHz for the "s" sounds
    const double rr = 0.97, w = 2 * kPi * 7000 / sr, ra1 = -2 * rr * std::cos (w), ra2 = rr * rr;
    const double beat = 0.5;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr, inBeat = std::fmod (t, beat), inBar = std::fmod (t, 4 * beat);
        const double kick = 0.9 * std::exp (-inBeat * 18) * std::sin (2 * kPi * (50 + 80 * std::exp (-inBeat * 30)) * inBeat);
        const double bass = 0.25 * std::sin (2 * kPi * 55 * t) * (0.6 + 0.4 * std::exp (-inBeat * 4));
        double pad = 0;
        for (int h = 1; h <= 12; ++h) pad += std::sin (2 * kPi * 220 * h * t + h) / h;
        pad *= 0.06;
        const double mud = m.mud * 0.25 * (std::sin (2 * kPi * 280 * t) + std::sin (2 * kPi * 350 * t + 1));
        // hats: noise through a one-pole high-pass, on the off-beats; a dull mix loses them
        const double wn = noise (rng);
        hpState = 0.9 * (hpState + wn - prev);
        prev = wn;
        const double hat = (1.0 - m.dull) * 0.25 * hpState * std::exp (-std::fmod (t + beat / 2, beat) * 30);
        const double voice = 0.12 * std::sin (2 * kPi * 330 * t) * (0.5 + 0.5 * std::sin (2 * kPi * 0.5 * t));
        // "s" bursts: 80 ms of noise around 7 kHz once a bar, as loud as the vocal is in a harsh mix
        const double s0 = wn - ra1 * s1 - ra2 * s2;
        s2 = s1;
        s1 = s0;
        const double sib = inBar > 1.0 && inBar < 1.08 ? m.sibilance * 0.05 * s0 : 0.0;
        double mono = kick + bass + pad + mud + hat + voice + sib;
        // a dull mix: a gentle low-pass over everything
        lp += (m.dull > 0 ? 0.12 : 1.0) * (mono - lp);
        mono = lp;
        const double side = m.wideBass * 0.25 * std::sin (2 * kPi * 55 * t + 1.3) + 0.03 * std::sin (2 * kPi * 440 * t);
        l[i] = (float) (0.5 * (mono + side));
        r[i] = (float) (0.5 * (mono - side));
    }
}

static bb::assist::Plan run (const Mix& m, bb::assist::Goal g, double sr = 48000)
{
    std::vector<float> l, r;
    make (m, sr, 8.0, l, r);
    return bb::assist::plan (l, r, sr, g, { 0, 1, 2, 3, 4 });
}

static double eqMove (const bb::assist::Plan& p, int band) { const auto& b = p.eq.band[band]; return b.cut ? -b.gain : b.gain; }

int main()
{
    using bb::assist::Goal;
    for (auto g : { Goal::Streaming, Goal::Loud, Goal::Club, Goal::Dynamic })
    {
        const auto p = run ({}, g);
        const double t = bb::assist::targetLufs (g);
        CHECK (std::abs (p.outLufs - t) < 0.5, "%s: lands at %.2f LUFS for %.0f", bb::assist::goalName (g), p.outLufs, t);
        CHECK (p.outTruePeak <= p.limit.ceilingDb + 0.1, "%s: true peak %.2f over the %.2f ceiling", bb::assist::goalName (g), p.outTruePeak, p.limit.ceilingDb);
        std::printf ("%-9s in %.1f LUFS (PLR %.1f) -> out %.2f LUFS, %.2f dBTP, drive %.1f, comp %.1f dB, trim %.1f\n", bb::assist::goalName (g),
                     p.in.integrated, p.in.plr, p.outLufs, p.outTruePeak, p.limit.driveDb, p.compReductionDb, p.inTrimDb);
        if (! p.bypass[bb::assist::Comp])
            CHECK (p.compReductionDb > 0.5 && p.compReductionDb < 3.5, "%s: compression %.2f dB", bb::assist::goalName (g), p.compReductionDb);
    }

    const auto clean = run ({}, Goal::Streaming);
    std::printf ("clean: low %.1f lowMid %.1f mid %.1f highMid %.1f air %.1f sib %.1f width %.1f\n", clean.in.low, clean.in.lowMid, clean.in.mid,
                 clean.in.highMid, clean.in.air, clean.in.sibilance, clean.in.bassWidth);
    CHECK (clean.bypass[bb::assist::Deess], "clean: the de-esser should stay out (sibilance %.1f)", clean.in.sibilance);
    CHECK (clean.eq.monoBassHz == 0, "clean: no mono bass (width %.1f)", clean.in.bassWidth);

    const auto dull = run ({ 1.0, 0, 0, 0 }, Goal::Streaming);
    CHECK (eqMove (dull, 4) > eqMove (clean, 4) + 1.0, "dull: air %.1f dB vs %.1f on the clean mix", eqMove (dull, 4), eqMove (clean, 4));

    const auto muddy = run ({ 0, 1.0, 0, 0 }, Goal::Streaming);
    CHECK (eqMove (muddy, 1) < -0.7, "muddy: 300 Hz move %.1f dB (low mid %.1f)", eqMove (muddy, 1), muddy.in.lowMid);

    const auto hissy = run ({ 0, 0, 1.0, 0 }, Goal::Streaming);
    std::printf ("sibilance: clean %.1f, dull %.1f, muddy %.1f, sibilant %.1f dB\n", clean.in.sibilance, dull.in.sibilance, muddy.in.sibilance, hissy.in.sibilance);
    CHECK (! hissy.bypass[bb::assist::Deess], "sibilant: the de-esser should be in (sibilance %.1f)", hissy.in.sibilance);

    const auto wide = run ({ 0, 0, 0, 1.0 }, Goal::Streaming);
    CHECK (wide.eq.monoBassHz > 0, "wide bass: mono bass should be on (width %.1f)", wide.in.bassWidth);

    // at another rate, and a mix already louder than the goal is turned down, not limited
    {
        std::vector<float> l, r;
        make ({}, 44100, 8.0, l, r);
        for (auto* v : { &l, &r }) for (auto& x : *v) x *= 2.5f;
        const auto p = bb::assist::plan (l, r, 44100, Goal::Dynamic, { 0, 1, 2, 3, 4 });
        CHECK (p.inTrimDb < 0 && std::abs (p.outLufs + 16) < 0.5, "hot mix: trim %.1f, out %.2f LUFS", p.inTrimDb, p.outLufs);
        std::printf ("hot: in %.1f LUFS, trim %.1f, drive %.1f -> %.2f LUFS\n", p.in.integrated, p.inTrimDb, p.limit.driveDb, p.outLufs);
    }

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
