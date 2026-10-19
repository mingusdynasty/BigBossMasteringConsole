#include "Limiter.h"

#include <algorithm>
#include <cmath>

namespace bb
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

double besselI0 (double x)
{
    double sum = 1, term = 1;
    for (int k = 1; k < 40; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; }
    return sum;
}

double smoothing (double seconds, double sr) { return 1.0 - std::exp (-1.0 / (std::max (1e-6, seconds) * sr)); }
double dbToLin (double db) { return std::pow (10.0, db / 20.0); }
}

Limiter::Times Limiter::times (int style, const Settings& set) const
{
    // hold (s), the fast follower's release, and the slow follower's attack and release (s)
    static constexpr double t[kStyles][4] = { { 0.020, 0.060, 0.050, 1.200 },    // Transparent
                                              { 0.008, 0.030, 0.100, 0.700 },    // Punch
                                              { 0.003, 0.010, 0.040, 0.300 } };  // Dense
    const int s = std::clamp (style, 0, kStyles - 1);
    Times r;
    r.hold = std::max (1, (int) std::lround (t[s][0] * sr));
    r.fastC = smoothing (set.autoRelease ? t[s][1] : std::clamp (set.releaseMs, 1.0, 2000.0) * 0.001, sr);
    r.slowAtkC = set.autoRelease ? smoothing (t[s][2], sr) : 0.0;
    r.slowRelC = smoothing (t[s][3], sr);
    // the gain's descent: two halves of the lookahead (a smooth S-curve), half of it, or a quarter
    // (the gain arrives late and steep, letting more of the attack's shape through before it clamps)
    r.boxA = s == Dense ? lookahead / 4 : lookahead / 2;
    r.boxB = s == Transparent ? lookahead / 2 : 1;
    return r;
}

void Limiter::prepare (double sampleRate, int)
{
    sr = sampleRate;
    L = sr < 88200.0 ? 8 : (sr < 176400.0 ? 4 : 2);
    lookahead = 2 * std::max (2, (int) std::lround (0.0015 * sr));   // 3 ms, even

    // polyphase Kaiser-windowed sinc; phase p, tap t multiplies x[n - t]
    const int N = L * kTaps;
    coef.assign ((size_t) N, 0.0);
    const double centre = 0.5 * (N - 1), beta = 8.0;
    std::vector<double> proto ((size_t) N);
    for (int k = 0; k < N; ++k)
    {
        const double t = (k - centre) / L;
        const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
        const double r = (k - centre) / centre;
        proto[(size_t) k] = sinc * besselI0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / besselI0 (beta);
    }
    for (int p = 0; p < L; ++p)
    {
        double s = 0;
        for (int t = 0; t < kTaps; ++t) s += proto[(size_t) (p + L * t)];
        for (int t = 0; t < kTaps; ++t) coef[(size_t) (p * kTaps + t)] = proto[(size_t) (p + L * t)] / s;
    }

    int len = 1;
    while (len < lookahead + kFirDelay + 2) len <<= 1;
    delayMask = len - 1;
    for (auto& c : ch)
    {
        c.hist.assign (2 * kTaps, 0.0);
        c.delay.assign ((size_t) len, 0.0);
    }
    ceilDelay.assign ((size_t) len, 1.0);
    glide = smoothing (0.02, sr);
    reset();
}

void Limiter::reset()
{
    for (auto& c : ch)
    {
        std::fill (c.hist.begin(), c.hist.end(), 0.0);
        std::fill (c.delay.begin(), c.delay.end(), 0.0);
        c.hpos = 0;
        c.ph0 = c.ph1 = c.seg1 = c.seg2 = 0;
        c.window.init (lookahead);
        for (int s = 0; s < kStyles; ++s)
        {
            const auto t = times (s, target);
            auto& e = c.engine[(size_t) s];
            e.hold.init (t.hold);
            e.fast = e.slow = 1;
            e.a.init (t.boxA);
            e.b.init (t.boxB);
        }
    }
    dPos = 0;
    ceil = dbToLin (std::clamp (target.ceilingDb, -24.0, 0.0));
    pre = dbToLin (std::clamp (target.driveDb, -12.0, 36.0)) / ceil;
    std::fill (ceilDelay.begin(), ceilDelay.end(), ceil);
    for (int s = 0; s < kStyles; ++s) styleW[s] = s == target.style ? 1 : 0;
    linkNow = target.link;
    gr.store (0);
}

double Limiter::segmentPeak (Channel& c, double x, bool truePeak)
{
    // newest first: hist[hpos .. hpos + kTaps)
    c.hpos = (c.hpos - 1 + kTaps) % kTaps;
    c.hist[(size_t) c.hpos] = c.hist[(size_t) (c.hpos + kTaps)] = x;
    const double* w = c.hist.data() + c.hpos;
    double pk = std::max (std::abs (w[kFirDelay]), std::abs (w[kFirDelay - 1]));
    if (! truePeak) return std::abs (w[kFirDelay]);
    // the phases fall between x[n - kFirDelay] and x[n - kFirDelay + 1], evenly spaced across segments
    for (int p = 0; p < L; ++p)
    {
        const double* cf = coef.data() + p * kTaps;
        double y = 0;
        for (int t = 0; t < kTaps; ++t) y += cf[t] * w[t];
        const double a2 = std::abs (y);
        pk = std::max (pk, a2);
        // a crest lies between the reconstructed points: a parabola through the three around it finds it
        const double curve = 2 * c.ph1 - c.ph0 - a2;
        if (c.ph1 >= c.ph0 && c.ph1 >= a2 && curve > 1e-12)
            pk = std::max (pk, c.ph1 + (a2 - c.ph0) * (a2 - c.ph0) / (8 * curve));
        c.ph0 = c.ph1;
        c.ph1 = a2;
    }
    return pk;
}

void Limiter::process (double* l, double* r, int n)
{
    const Settings s = target;
    const double ceilT = dbToLin (std::clamp (s.ceilingDb, -24.0, 0.0));
    const double preT = dbToLin (std::clamp (s.driveDb, -12.0, 36.0)) / ceilT;
    const double margin = s.truePeak ? kMargin : 1.0;
    const int style = std::clamp (s.style, 0, kStyles - 1);
    Times tm[kStyles];
    for (int st = 0; st < kStyles; ++st) tm[st] = times (st, s);
    const int delaySamples = latency();
    double minG = 1, pkIn = 0, pkOut = 0;

    for (int i = 0; i < n; ++i)
    {
        pre += (preT - pre) * glide;
        ceil += (ceilT - ceil) * glide;
        linkNow += (std::clamp (s.link, 0.0, 1.0) - linkNow) * glide;
        for (int st = 0; st < kStyles; ++st) styleW[st] += ((st == style ? 1.0 : 0.0) - styleW[st]) * glide;

        // required gain for the sample kFirDelay + 1 ago, from the peaks on either side of it
        const double x[2] = { l[i] * pre, r[i] * pre };
        pkIn = std::max ({ pkIn, std::abs (x[0]) * ceil, std::abs (x[1]) * ceil });
        double req[2];
        for (int c = 0; c < 2; ++c)
        {
            auto& cc = ch[(size_t) c];
            const double seg = segmentPeak (cc, x[c], s.truePeak);
            const double q = s.truePeak ? std::max ({ cc.seg2, cc.seg1, seg }) : cc.seg1;
            cc.seg2 = cc.seg1;
            cc.seg1 = seg;
            req[c] = q > margin ? margin / q : 1.0;
        }
        const double both = std::min (req[0], req[1]);
        for (int c = 0; c < 2; ++c) req[c] += (both - req[c]) * linkNow;

        double out[2];
        const double ceilNow = ceilDelay[(size_t) ((dPos - delaySamples) & delayMask)];
        for (int c = 0; c < 2; ++c)
        {
            auto& cc = ch[(size_t) c];
            const double h = cc.window.push (req[c]);
            double g = 0;
            for (int st = 0; st < kStyles; ++st)
            {
                auto& e = cc.engine[(size_t) st];
                const double held = e.hold.push (h);
                if (held < e.fast) e.fast = held; else e.fast += (held - e.fast) * tm[st].fastC;
                if (tm[st].slowAtkC > 0) e.slow += (held - e.slow) * (held < e.slow ? tm[st].slowAtkC : tm[st].slowRelC);
                else e.slow = 1;
                g += styleW[st] * e.b.push (e.a.push (std::min (e.fast, e.slow)));
            }

            cc.delay[(size_t) dPos] = x[c];
            const double xd = cc.delay[(size_t) ((dPos - delaySamples) & delayMask)];
            minG = std::min (minG, g);
            // the gain path already holds the ceiling; this only guards against rounding
            out[c] = std::clamp (xd * g, -1.0, 1.0) * ceilNow;
        }
        ceilDelay[(size_t) dPos] = ceil;
        dPos = (dPos + 1) & delayMask;
        l[i] = out[0];
        r[i] = out[1];
        pkOut = std::max ({ pkOut, std::abs (out[0]), std::abs (out[1]) });
    }
    const auto grDb = (float) (-20.0 * std::log10 (std::max (minG, 1e-6)));
    gr.store (grDb, std::memory_order_relaxed);
    auto raise = [] (std::atomic<float>& a, float v)
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    };
    raise (grHeld, grDb);
    raise (inPeak, (float) pkIn);
    raise (outPeak, (float) pkOut);
}
} // namespace bb
