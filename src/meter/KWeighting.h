// K-weighting (ITU-R BS.1770-4): a high-frequency shelf (the head) followed by an RLB high-pass.
//
// The standard gives the coefficients at 48 kHz only. They are re-derived here for any rate from the
// analogue prototypes behind them (shelf at 1681.97 Hz, +4.0 dB, Q 0.7072; high-pass at 38.135 Hz,
// Q 0.5003), which reproduces the 48 kHz table to about 1e-8.
#pragma once

#include <cmath>

namespace bb
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    inline double run (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void clear() { z1 = z2 = 0; }
};

class KWeighting
{
public:
    void prepare (double sampleRate)
    {
        constexpr double kPi = 3.14159265358979323846;
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / sampleRate);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPi * f0 / sampleRate);
            const double a0 = 1.0 + K / Q + K * K;
            hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
            hp.a1 = 2.0 * (K * K - 1.0) / a0;
            hp.a2 = (1.0 - K / Q + K * K) / a0;
        }
        reset();
    }
    void reset() { shelf.clear(); hp.clear(); }
    inline double run (double x) { return hp.run (shelf.run (x)); }

    Biquad shelf, hp;
};
} // namespace bb
