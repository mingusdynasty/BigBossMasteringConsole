#include "DualCompressor.h"

#include <algorithm>

namespace bb
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

double coef (double seconds, double sr) { return 1.0 - std::exp (-1.0 / (std::max (1e-6, seconds) * sr)); }
double dbToLin (double db) { return std::exp (db * 0.11512925464970229); }

// level (dBFS, sine-peak referenced) from a mean square
double levelDb (double ms) { return 10.0 * std::log10 (std::max (2.0 * ms, 1e-12)); }

// soft-knee reduction: 0 below the knee, (1 - 1/ratio) of the overshoot above it
double kneeReduction (double over, double knee, double slope)
{
    if (over <= -knee * 0.5) return 0.0;
    if (over >= knee * 0.5) return slope * over;
    const double t = over + knee * 0.5;
    return slope * t * t / (2.0 * knee);
}

// the transformer nonlinearities, applied to the low band; each is unity-slope at zero
inline double iron (double v)  { constexpr double k = 0.06, b = 0.35, tb = 0.33637554433, s = 0.88685; return (std::tanh (k * v + b) - tb) / (k * s); }
inline double steel (double v) { constexpr double k = 0.62; return std::tanh (k * v) / k; }
inline double nickel (double v) { constexpr double k = 0.2; return std::tanh (k * v) / k; }
}

constexpr double DualCompressor::kRatio[];
constexpr double DualCompressor::kAttackMs[];
constexpr double DualCompressor::kRecoveryMs[];

double DualCompressor::optoCurve (double level, double threshold)
{
    // the photocell's gentle, wide-kneed curve: about 3:1 well above threshold
    return kneeReduction (level - threshold, 12.0, 1.0 - 1.0 / 3.0);
}

double DualCompressor::vcaCurve (double level, double threshold, double ratio)
{
    return kneeReduction (level - threshold, ratio >= 10 ? 12.0 : 6.0, 1.0 - 1.0 / ratio);
}

void DualCompressor::prepare (double sampleRate, int)
{
    sr = sampleRate;
    // 90 Hz Butterworth high-pass for the sidechain
    const double w = 2 * kPi * 90.0 / sr, cs = std::cos (w), al = std::sin (w) / (2 * 0.7071), a0 = 1 + al;
    hb0 = (1 + cs) / 2 / a0; hb1 = -(1 + cs) / a0; hb2 = hb0; ha1 = -2 * cs / a0; ha2 = (1 - al) / a0;

    optoDetC = coef (0.010, sr);
    vcaDetC = coef (0.0008, sr);
    optoAtkC = coef (0.010, sr);
    memChargeC = coef (2.0, sr);
    memDrainC = coef (4.0, sr);
    glide = coef (0.02, sr);
    xfLowC = coef (1.0 / (2 * kPi * 150.0), sr);   // the low band the core saturates: 150 Hz one-pole
    xfDcR = 1.0 - 2 * kPi * 5.0 / sr;
    reset();
}

void DualCompressor::reset()
{
    for (int c = 0; c < 2; ++c) hz1[c] = hz2[c] = xfLow[c] = xfDcX[c] = xfDcY[c] = 0;
    optoMs = vcaMs = optoGr = optoMemory = vcaGr = vcaSlow = 0;
    hpfMix = target.sidechainHpf ? 1 : 0;
    optoGainLin = target.opto ? dbToLin (target.optoGain) : 1;
    vcaGainLin = target.vca ? dbToLin (target.vcaGain) : 1;
    for (int t = 0; t < 3; ++t) xfWeight[t] = t == target.transformer ? 1 : 0;
    grOpto.store (0); grVca.store (0);
}

void DualCompressor::process (double* l, double* r, int n)
{
    const Settings s = target;
    const double ratio = kRatio[std::clamp (s.ratio, 0, kRatios - 1)];
    const double atkC = coef (kAttackMs[std::clamp (s.attack, 0, kAttacks - 1)] * 0.001, sr);
    const int rec = std::clamp (s.recovery, 0, kRecoveries - 1);
    const bool dual = kRecoveryMs[rec] <= 0;
    const double relC = coef ((dual ? 120.0 : kRecoveryMs[rec]) * 0.001, sr);
    const double slowAtkC = coef (0.4, sr), slowRelC = coef (1.6, sr), offC = coef (0.03, sr);
    const double optoGainT = s.opto ? dbToLin (s.optoGain) : 1, vcaGainT = s.vca ? dbToLin (s.vcaGain) : 1;
    const double hpfT = s.sidechainHpf ? 1 : 0;
    double wT[3] = { 0, 0, 0 };
    wT[std::clamp (s.transformer, 0, 2)] = 1;

    double peakOpto = 0, peakVca = 0;
    for (int i = 0; i < n; ++i)
    {
        hpfMix += (hpfT - hpfMix) * glide;
        optoGainLin += (optoGainT - optoGainLin) * glide;
        vcaGainLin += (vcaGainT - vcaGainLin) * glide;

        // sidechain
        double sc[2];
        const double in[2] = { l[i], r[i] };
        for (int c = 0; c < 2; ++c)
        {
            const double y = hb0 * in[c] + hz1[c];
            hz1[c] = hb1 * in[c] - ha1 * y + hz2[c];
            hz2[c] = hb2 * in[c] - ha2 * y;
            sc[c] = in[c] + (y - in[c]) * hpfMix;
        }
        const double sq = std::max (sc[0] * sc[0], sc[1] * sc[1]);

        // optical stage
        optoMs += (sq - optoMs) * optoDetC;
        const double optoT = s.opto ? optoCurve (levelDb (optoMs), s.optoThreshold) : 0.0;
        if (optoT > optoGr) optoGr += (optoT - optoGr) * optoAtkC;
        else
        {
            // the cell lets go quickly after a burst, slowly after it has been held down
            const double tau = s.opto ? 0.06 + 1.44 * optoMemory : 0.03;
            optoGr += (optoT - optoGr) * (1.0 / (tau * sr));
        }
        if (optoGr > 1.0) optoMemory += (std::min (1.0, optoGr / 6.0) - optoMemory) * memChargeC;
        else optoMemory -= optoMemory * memDrainC;
        const double gOpto = dbToLin (-optoGr) * optoGainLin;

        // discrete stage, fed from the optical stage's output
        vcaMs += (sq * gOpto * gOpto - vcaMs) * vcaDetC;
        const double vcaT = s.vca ? vcaCurve (levelDb (vcaMs), s.vcaThreshold, ratio) : 0.0;
        if (! s.vca) vcaGr += (vcaT - vcaGr) * offC;
        else if (vcaT > vcaGr) vcaGr += (vcaT - vcaGr) * atkC;
        else vcaGr += (vcaT - vcaGr) * relC;
        double gr = vcaGr;
        if (dual && s.vca)
        {
            // dual recovery: a slow follower holds sustained reduction while the fast one recovers
            vcaSlow += (vcaGr - vcaSlow) * (vcaGr > vcaSlow ? slowAtkC : slowRelC);
            gr = std::max (vcaGr, 0.7 * vcaSlow);
        }
        else vcaSlow += (0.0 - vcaSlow) * offC;
        const double g = gOpto * dbToLin (-gr) * vcaGainLin;
        peakOpto = std::max (peakOpto, optoGr);
        peakVca = std::max (peakVca, gr);

        // output transformer: the core saturates the low band
        double out[2];
        for (int t = 0; t < 3; ++t) xfWeight[t] += (wT[t] - xfWeight[t]) * glide;
        for (int c = 0; c < 2; ++c)
        {
            const double x = in[c] * g;
            xfLow[c] += (x - xfLow[c]) * xfLowC;
            const double v = xfLow[c];
            const double added = xfWeight[0] * (nickel (v) - v) + xfWeight[1] * (iron (v) - v) + xfWeight[2] * (steel (v) - v);
            // the core's asymmetry would leave a DC offset: block it below 5 Hz
            const double dc = added - xfDcX[c] + xfDcR * xfDcY[c];
            xfDcX[c] = added;
            xfDcY[c] = dc;
            out[c] = x + dc;
        }
        l[i] = out[0];
        r[i] = out[1];
    }
    grOpto.store ((float) peakOpto, std::memory_order_relaxed);
    grVca.store ((float) peakVca, std::memory_order_relaxed);
}
} // namespace bb
