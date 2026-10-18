#include "TruePeak.h"

#include <algorithm>
#include <cmath>

namespace bb
{
namespace
{
double besselI0 (double x)
{
    double sum = 1, term = 1;
    for (int k = 1; k < 40; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; }
    return sum;
}
}

void TruePeak::prepare (double sampleRate, int numChannels)
{
    constexpr double kPi = 3.14159265358979323846;
    nch = std::clamp (numChannels, 1, 2);
    L = sampleRate < 88200.0 ? 4 : (sampleRate < 176400.0 ? 2 : 1);
    const int N = L * kTaps;
    coef.assign ((size_t) N, 0.0);
    const double centre = 0.5 * (N - 1), beta = 8.0;
    std::vector<double> proto ((size_t) N);
    for (int k = 0; k < N; ++k)
    {
        const double t = (k - centre) / L;                       // in input samples
        const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
        const double r = (k - centre) / centre;
        proto[(size_t) k] = sinc * besselI0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / besselI0 (beta);
    }
    // phase p, tap t multiplies x[n - t]; each phase is normalised to unity gain at DC
    for (int p = 0; p < L; ++p)
    {
        double s = 0;
        for (int t = 0; t < kTaps; ++t) s += proto[(size_t) (p + L * t)];
        for (int t = 0; t < kTaps; ++t) coef[(size_t) (p * kTaps + t)] = proto[(size_t) (p + L * t)] / s;
    }
    reset();
}

void TruePeak::reset()
{
    for (auto& h : hist) h.fill (0.0);
    for (auto& v : prev) v = { 0.0, 0.0 };
    pos = 0;
    blockPeak.fill (0.0);
    held.fill (0.0);
    heldSample.fill (0.0);
}

void TruePeak::process (const double* const* channels, int n)
{
    for (int c = 0; c < nch; ++c)
    {
        auto& h = hist[(size_t) c];
        int p = pos;
        double pk = blockPeak[(size_t) c], sp = heldSample[(size_t) c];
        double a0 = prev[(size_t) c][0], a1 = prev[(size_t) c][1];   // the last two |oversampled| values
        const double* x = channels[c];
        for (int i = 0; i < n; ++i)
        {
            // newest sample at h[p], older ones follow, mirrored kTaps further on
            p = (p - 1 + kTaps) % kTaps;
            h[(size_t) p] = h[(size_t) (p + kTaps)] = x[i];
            sp = std::max (sp, std::abs (x[i]));
            pk = std::max (pk, std::abs (x[i]));   // the phases sit between the samples, never on them
            const double* w = h.data() + p;
            for (int ph = 0; ph < L; ++ph)
            {
                const double* cf = coef.data() + ph * kTaps;
                double y = 0;
                for (int t = 0; t < kTaps; ++t) y += cf[t] * w[t];
                const double a2 = std::abs (y);
                pk = std::max (pk, a2);
                // a local maximum: the true crest lies between the oversampled points, so fit a parabola
                // through the three around it (brings 4x within 0.05 dB up to 20 kHz at 44.1 kHz)
                if (a1 >= a0 && a1 >= a2)
                {
                    const double d = a0 - 2 * a1 + a2;
                    if (d < 0) pk = std::max (pk, a1 - (a0 - a2) * (a0 - a2) / (8 * d));
                }
                a0 = a1; a1 = a2;
            }
        }
        prev[(size_t) c] = { a0, a1 };
        blockPeak[(size_t) c] = pk;
        held[(size_t) c] = std::max (held[(size_t) c], pk);
        heldSample[(size_t) c] = sp;
        if (c == nch - 1) pos = p;
    }
}
} // namespace bb
