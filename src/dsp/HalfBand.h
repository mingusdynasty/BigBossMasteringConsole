// Polyphase IIR half-band filters (after L. de Soras, HIIR), shared by the plate's rate conversion
// and the tape head's oversampling.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace cf
{
// n allpass coefficients for transition bandwidth tbw (fraction of the higher rate)
inline void halfBandDesign (double* dst, int n, double tbw)
{
    constexpr double kPi = 3.14159265358979323846;
    double k = std::tan ((1 - tbw * 2) * kPi / 4); k *= k;
    const double kk = std::pow (1 - k * k, 0.25);
    const double e  = 0.5 * (1 - kk) / (1 + kk);
    const double e4 = e * e * e * e;
    const double q  = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)));
    const int order = 2 * n + 1;
    for (int idx = 0; idx < n; ++idx)
    {
        const double c = idx + 1;
        double num = 0, den = 0, j = 1;
        for (int i = 0; i < 30; ++i) { num += std::pow (q, i * (i + 1)) * std::sin ((i * 2 + 1) * c * kPi / order) * j; j = -j; }
        j = -1;
        for (int i = 1; i <= 30; ++i) { den += std::pow (q, i * i) * std::cos (i * 2 * c * kPi / order) * j; j = -j; }
        const double ww = num * std::pow (q, 0.25) / (den + 0.5);
        const double x  = std::sqrt ((1 - ww * ww * k) * (1 - ww * ww / k)) / (1 + ww * ww);
        dst[idx] = (1 - x) / (1 + x);
    }
}

// Two-path polyphase half-band (allpass chains), up to 7 coefficients.
struct HalfBand
{
    std::array<double, 7> x {}, y {};
    inline void run (const double* c, int n, double in0, double in1, double& o0, double& o1)
    {
        double l0 = in0, l1 = in1;
        for (int i = 0; i < n; ++i)
        {
            double& inp = (i & 1) ? l1 : l0;
            const double t = (inp - y[(std::size_t) i]) * c[i] + x[(std::size_t) i];
            x[(std::size_t) i] = inp;
            y[(std::size_t) i] = t;
            inp = t;
        }
        o0 = l0; o1 = l1;
    }
    inline double down (const double* c, int n, double earlier, double later)
    {
        double o0, o1; run (c, n, later, earlier, o0, o1); return 0.5 * (o0 + o1);
    }
    inline void up (const double* c, int n, double v, double& earlier, double& later)
    {
        run (c, n, v, v, earlier, later);
    }
    void clear() { x.fill (0); y.fill (0); }
};
// Two-channel half-band with the channels in lockstep, so the two allpass chains run side by side.
struct StereoHalfBand
{
    double x[7][2] {}, y[7][2] {};
    inline void run (const double* c, int n, double* l0, double* l1)   // l0, l1: [2] each, in place
    {
        for (int i = 0; i < n; ++i)
        {
            double* inp = (i & 1) ? l1 : l0;
            for (int ch = 0; ch < 2; ++ch)
            {
                const double t = (inp[ch] - y[i][ch]) * c[i] + x[i][ch];
                x[i][ch] = inp[ch];
                y[i][ch] = t;
                inp[ch] = t;
            }
        }
    }
    // in[2] -> earlier[2], later[2]
    inline void up (const double* c, int n, const double* in, double* earlier, double* later)
    {
        earlier[0] = later[0] = in[0]; earlier[1] = later[1] = in[1];
        run (c, n, earlier, later);
    }
    // earlier[2], later[2] -> out[2]
    inline void down (const double* c, int n, const double* earlier, const double* later, double* out)
    {
        double l0[2] { later[0], later[1] }, l1[2] { earlier[0], earlier[1] };
        run (c, n, l0, l1);
        out[0] = 0.5 * (l0[0] + l1[0]); out[1] = 0.5 * (l0[1] + l1[1]);
    }
    void clear() { *this = StereoHalfBand(); }
};

} // namespace cf
