#include "TapeHead.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace cf
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// magnetisation settled under the head's DC field alone
double settle (TapeHysteresis& h, double field)
{
    for (int i = 0; i < 64; ++i) h.process (field * (i + 1) / 64.0);
    for (int i = 0; i < 64; ++i) h.process (field);
    return h.magnetisation();
}
} // namespace

// ---------------------------------------------------------------- setup

double TapeHeadSetup::driveFor (double sat) const
{
    return dMin * std::pow (dMax / dMin, std::clamp (sat, 0.0, 1.0));
}

double TapeHeadSetup::makeupFor (double sat) const
{
    const double p = std::clamp (sat, 0.0, 1.0) * (kCal - 1);
    const int i = std::min ((int) p, kCal - 2);
    return makeup[(std::size_t) i] + (p - i) * (makeup[(std::size_t) i + 1] - makeup[(std::size_t) i]);
}

void TapeHeadSetup::prepare (double esr, double passbandHz, int oversample, double driveMin, double driveMax)
{
    dMin = driveMin; dMax = driveMax;
    factor = oversample >= 4 ? 4 : oversample >= 2 ? 2 : 1;
    rateOS = factor * esr;
    // oversampling: stage 1 keeps the audio band (0 - 11 kHz inside the plate, 0 - 20 kHz for the
    // standalone tape); stage 2 only has to reject images above ~3/8 of its rate
    halfBandDesign (hb1, kStage1, 2 * (0.25 - std::min (passbandHz, 0.238 * 2 * esr) / (2 * esr)));
    halfBandDesign (hb2, kStage2, 0.25);

    // record EQ: a first-order shelf, flat in the bass, rising from 3.2 kHz (50 us) to +10 dB at the top
    {
        const double wz = 2 * kPi * 3183.0, wp = wz * std::sqrt (10.0);
        const double K = wz / std::tan (wz / (2 * esr));   // prewarped at the turnover
        const double a0 = 1 + K / wp;
        peB0 = (1 + K / wz) / a0; peB1 = (1 - K / wz) / a0; peA1 = (1 - K / wp) / a0;
    }

    // latency of the oversampling chain at low frequencies: centroid of its impulse response
    {
        HalfBand u1, u2, d2, d1;
        double num = 0, den = 0;
        for (int n = 0; n < 2048; ++n)
        {
            const double x = n == 0 ? 1.0 : 0.0;
            double y = x;
            if (factor == 2)
            {
                double a[2];
                u1.up (hb1, kStage1, x, a[0], a[1]);
                y = d1.down (hb1, kStage1, a[0], a[1]);
            }
            else if (factor == 4)
            {
                double a[2], b[4];
                u1.up (hb1, kStage1, x, a[0], a[1]);
                u2.up (hb2, kStage2, a[0], b[0], b[1]);
                u2.up (hb2, kStage2, a[1], b[2], b[3]);
                const double c0 = d2.down (hb2, kStage2, b[0], b[1]), c1 = d2.down (hb2, kStage2, b[2], b[3]);
                y = d1.down (hb1, kStage1, c0, c1);
            }
            num += n * y; den += y;
        }
        lat = num / den;
    }

    // drive calibration: the small-signal gain of the hysteresis at each saturation setting, measured
    // with a 250 Hz tone from -60 to -4 dBFS. The gain rises slightly with level before saturation sets
    // in (the irreversible part of the loop opens up), so the makeup is set from the largest: the
    // record/playback chain never has more than unity gain, which safe mode's stability depends on
    // The result only depends on the tape's rate and the drive range, and prepare() can run often
    // (every transport start in some hosts, in every instance): compute each combination once.
    struct Cal { double rate, lo, hi, restM; std::array<double, kCal> makeup; };
    static std::mutex cacheLock;
    static std::vector<Cal> cache;
    std::lock_guard<std::mutex> guard (cacheLock);
    for (const auto& c : cache)
        if (c.rate == rateOS && c.lo == dMin && c.hi == dMax) { restM = c.restM; makeup = c.makeup; return; }

    TapeHysteresis h;
    h.setTape (kCoercivity, kReversible);
    h.prepare (rateOS);
    restM = settle (h, kHeadField);
    const int per = (int) std::lround (rateOS / 250.0);
    for (int i = 0; i < kCal; ++i)
    {
        const double g = driveFor (i / (double) (kCal - 1));
        double best = 0;
        for (double amp : { 0.001, 0.003, 0.01, 0.03, 0.1, 0.3, 0.6 })
        {
            h.reset();
            settle (h, kHeadField);
            double re = 0, im = 0;
            for (int n = 0; n < 6 * per; ++n)
            {
                const double ph = 2 * kPi * n / per;
                const double m = h.process (g * amp * std::sin (ph) + kHeadField) - restM;
                if (n >= 2 * per) { re += m * std::sin (ph); im += m * std::cos (ph); }
            }
            best = std::max (best, 2 * std::sqrt (re * re + im * im) / (4 * per) / amp);
        }
        makeup[(std::size_t) i] = 1.0 / best;
    }
    cache.push_back ({ rateOS, dMin, dMax, restM, makeup });
}

// ---------------------------------------------------------------- the heads

void TapeHeads::prepare (const TapeHeadSetup& s)
{
    setup = &s;
    for (auto& h : hyst)
    {
        h.setTape (TapeHeadSetup::kCoercivity, TapeHeadSetup::kReversible);
        h.prepare (s.rateOS);
    }
    reset();
}

void TapeHeads::reset()
{
    up1.clear(); up2.clear(); dn2.clear(); dn1.clear();
    for (int ch = 0; ch < 2; ++ch) dtX1[ch] = dtE1[ch] = 0;
    for (int ch = 0; ch < 2; ++ch)
    {
        reX1[ch] = reE1[ch] = pbX1[ch] = pbE1[ch] = 0;
        hyst[ch].reset();
        settle (hyst[ch], TapeHeadSetup::kHeadField);
    }
}

void TapeHeads::record (const double* in, double drive, double makeup, double* out)
{
    const auto& s = *setup;
    // record EQ
    double e[2];
    for (int ch = 0; ch < 2; ++ch)
    {
        e[ch] = s.peB0 * in[ch] + s.peB1 * reX1[ch] - s.peA1 * reE1[ch];
        reX1[ch] = in[ch]; reE1[ch] = e[ch];
    }

    // up to the tape's rate, through the tape, back down
    auto tapeOf = [&] (double* v)
    {
        for (int ch = 0; ch < 2; ++ch)
            v[ch] = (hyst[ch].process (drive * v[ch] + TapeHeadSetup::kHeadField) - s.restM) * makeup;
    };
    if (s.factor == 4)
    {
        double a[2][2], b[4][2];
        up1.up (s.hb1, TapeHeadSetup::kStage1, e, a[0], a[1]);
        up2.up (s.hb2, TapeHeadSetup::kStage2, a[0], b[0], b[1]);
        up2.up (s.hb2, TapeHeadSetup::kStage2, a[1], b[2], b[3]);
        for (auto& v : b) tapeOf (v);
        double c[2][2];
        dn2.down (s.hb2, TapeHeadSetup::kStage2, b[0], b[1], c[0]);
        dn2.down (s.hb2, TapeHeadSetup::kStage2, b[2], b[3], c[1]);
        dn1.down (s.hb1, TapeHeadSetup::kStage1, c[0], c[1], out);
    }
    else if (s.factor == 2)
    {
        double a[2][2];
        up1.up (s.hb1, TapeHeadSetup::kStage1, e, a[0], a[1]);
        tapeOf (a[0]); tapeOf (a[1]);
        dn1.down (s.hb1, TapeHeadSetup::kStage1, a[0], a[1], out);
    }
    else
    {
        out[0] = e[0]; out[1] = e[1];
        tapeOf (out);
    }

    // the recording as playback will hear it (the highs the record EQ boosts are cut again)
    for (int ch = 0; ch < 2; ++ch)
    {
        const double h = (out[ch] - s.peB1 * dtX1[ch] + s.peA1 * dtE1[ch]) / s.peB0;
        dtX1[ch] = h; dtE1[ch] = out[ch];
    }
}
} // namespace cf
