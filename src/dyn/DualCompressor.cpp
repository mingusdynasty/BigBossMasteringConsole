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

// the three cores (see dyn/Transformer.h). Measured at 48 kHz, a 50 Hz tone at -6 dBFS: nickel 0.12%
// THD, iron 1.3% (mostly 2nd, from its standing field), steel 2.4% (3rd); at 1 kHz and above all three
// stay under 0.1%
constexpr TransformerCore::Voicing kCores[3] = {
    { 0.6, 1.0, 0.99, 0.0 },    // Nickel: high permeability, barely driven
    { 1.2, 1.0, 0.99, 0.2 },    // Iron: driven harder, with a standing field
    { 2.6, 1.2, 0.98, 0.0 },    // Steel: driven hardest, a wider loop
};
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
    memChargeC = coef (2.0, sr);
    memDrainC = coef (4.0, sr);
    glide = coef (0.02, sr);
    for (int t = 0; t < 3; ++t) cores[t].prepare (sr, kCores[t]);
    reset();
}

void DualCompressor::reset()
{
    for (int c = 0; c < 2; ++c) hz1[c] = hz2[c] = 0;
    for (auto& core : cores) core.reset();
    for (int c = 0; c < 2; ++c) optoMs[c] = vcaMs[c] = optoFast[c] = optoSlow[c] = optoMemory[c] = vcaGr[c] = vcaSlow[c] = 0;
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
        const double sqMax = std::max (sc[0] * sc[0], sc[1] * sc[1]);

        double g[2];
        for (int c = 0; c < 2; ++c)
        {
            const double sq = s.link ? sqMax : sc[c] * sc[c];

            // optical stage
            optoMs[c] += (sq - optoMs[c]) * optoDetC;
            const double optoT = s.opto ? optoCurve (levelDb (optoMs[c]), s.optoThreshold) : 0.0;
            const double mem = optoMemory[c];
            double& fast = optoFast[c];
            double& slow = optoSlow[c];
            const double share = 0.35 + 0.5 * mem;   // how much of the reduction lets go slowly
            const double now = (1 - share) * fast + share * slow;
            // the panel brightens faster the harder it is driven; the cell lets go quickly after a
            // burst, slowly after it has been held down. Each part rises at the attack rate and
            // falls at its own release rate, so the sum never steps
            const double atk = 1.0 / (std::max (0.003, 0.010 / (1.0 + std::max (0.0, optoT - now) / 12.0)) * sr);
            const double relFast = 1.0 / ((s.opto ? 0.06 : 0.03) * sr), relSlow = 1.0 / ((s.opto ? 0.4 + 1.1 * mem : 0.03) * sr);
            fast += (optoT - fast) * (optoT > fast ? atk : relFast);
            slow += (optoT - slow) * (optoT > slow ? atk : relSlow);
            const double optoGr = (1 - share) * fast + share * slow;
            if (optoGr > 1.0) optoMemory[c] += (std::min (1.0, optoGr / 6.0) - mem) * memChargeC;
            else optoMemory[c] -= mem * memDrainC;
            const double gOpto = dbToLin (-optoGr) * optoGainLin;

            // discrete stage, fed from the optical stage's output
            vcaMs[c] += (sq * gOpto * gOpto - vcaMs[c]) * vcaDetC;
            const double vcaT = s.vca ? vcaCurve (levelDb (vcaMs[c]), s.vcaThreshold, ratio) : 0.0;
            double& vg = vcaGr[c];
            if (! s.vca) vg += (vcaT - vg) * offC;
            else if (vcaT > vg) vg += (vcaT - vg) * atkC;
            else vg += (vcaT - vg) * relC;
            double gr = vg;
            if (dual && s.vca)
            {
                // dual recovery: a slow follower holds sustained reduction while the fast one recovers
                vcaSlow[c] += (vg - vcaSlow[c]) * (vg > vcaSlow[c] ? slowAtkC : slowRelC);
                gr = std::max (vg, 0.7 * vcaSlow[c]);
            }
            else vcaSlow[c] += (0.0 - vcaSlow[c]) * offC;
            g[c] = gOpto * dbToLin (-gr) * vcaGainLin;
            peakOpto = std::max (peakOpto, optoGr);
            peakVca = std::max (peakVca, gr);
        }

        // output transformer: the chosen core, crossfaded from the last when the switch moves. A core
        // with no weight doesn't run, and starts from rest when it comes in
        for (int t = 0; t < 3; ++t)
        {
            const double w0 = xfWeight[t];
            double w = w0 + (wT[t] - w0) * glide;
            if (w < 1e-5) w = 0; else if (w > 1 - 1e-5) w = 1;
            if (w0 == 0 && w > 0) cores[t].reset();
            xfWeight[t] = w;
        }
        double out[2];
        for (int c = 0; c < 2; ++c)
        {
            const double x = in[c] * g[c];
            double y = 0;
            for (int t = 0; t < 3; ++t)
                if (xfWeight[t] > 0) y += xfWeight[t] * cores[t].process (c, x);
            out[c] = y;
        }
        l[i] = out[0];
        r[i] = out[1];
    }
    grOpto.store ((float) peakOpto, std::memory_order_relaxed);
    grVca.store ((float) peakVca, std::memory_order_relaxed);
}
} // namespace bb
