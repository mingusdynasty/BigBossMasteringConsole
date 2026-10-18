#include "Oversampler.h"

#include <algorithm>

namespace bb
{
void Oversampler::prepare (double sampleRate, int maxBlock)
{
    F = sampleRate < 88200.0 ? 4 : (sampleRate < 176400.0 ? 2 : 1);
    hiRate = sampleRate * F;
    // stage 1 keeps 0 - 20 kHz; stage 2 only has to reject images above ~3/8 of its rate
    cf::halfBandDesign (hb1, kStage1, 2 * (0.25 - std::min (20000.0, 0.238 * sampleRate) / sampleRate));
    cf::halfBandDesign (hb2, kStage2, 0.25);
    bufL.assign ((size_t) (maxBlock * F), 0.0);
    bufR.assign ((size_t) (maxBlock * F), 0.0);

    // latency: centroid of the round trip's impulse response
    reset();
    std::vector<double> imp (2048, 0.0), zero (2048, 0.0);
    imp[0] = 1.0;
    double num = 0, den = 0;
    for (int pos = 0; pos < 2048; pos += maxBlock)
    {
        const int n = std::min (maxBlock, 2048 - pos);
        up (imp.data() + pos, zero.data() + pos, n);
        std::vector<double> l ((size_t) n), r ((size_t) n);
        down (l.data(), r.data(), n);
        for (int i = 0; i < n; ++i) { num += (pos + i) * l[(size_t) i]; den += l[(size_t) i]; }
    }
    lat = den != 0 ? num / den : 0.0;
    reset();
}

void Oversampler::reset()
{
    u1.clear(); u2.clear(); d2.clear(); d1.clear();
}

void Oversampler::up (const double* l, const double* r, int n)
{
    double* L = bufL.data();
    double* R = bufR.data();
    for (int i = 0; i < n; ++i)
    {
        const double in[2] { l[i], r[i] };
        if (F == 1) { L[i] = in[0]; R[i] = in[1]; continue; }
        double a[2], b[2];
        u1.up (hb1, kStage1, in, a, b);
        if (F == 2)
        {
            L[2 * i] = a[0]; R[2 * i] = a[1];
            L[2 * i + 1] = b[0]; R[2 * i + 1] = b[1];
            continue;
        }
        double c[4][2];
        u2.up (hb2, kStage2, a, c[0], c[1]);
        u2.up (hb2, kStage2, b, c[2], c[3]);
        for (int k = 0; k < 4; ++k) { L[4 * i + k] = c[k][0]; R[4 * i + k] = c[k][1]; }
    }
}

void Oversampler::down (double* l, double* r, int n)
{
    const double* L = bufL.data();
    const double* R = bufR.data();
    for (int i = 0; i < n; ++i)
    {
        double out[2];
        if (F == 1) { out[0] = L[i]; out[1] = R[i]; }
        else if (F == 2)
        {
            const double a[2] { L[2 * i], R[2 * i] }, b[2] { L[2 * i + 1], R[2 * i + 1] };
            d1.down (hb1, kStage1, a, b, out);
        }
        else
        {
            double c[4][2];
            for (int k = 0; k < 4; ++k) { c[k][0] = L[4 * i + k]; c[k][1] = R[4 * i + k]; }
            double a[2], b[2];
            d2.down (hb2, kStage2, c[0], c[1], a);
            d2.down (hb2, kStage2, c[2], c[3], b);
            d1.down (hb1, kStage1, a, b, out);
        }
        l[i] = out[0];
        r[i] = out[1];
    }
}
} // namespace bb
