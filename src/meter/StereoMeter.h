// Phase correlation, balance and width, integrated over ~300 ms like a hardware correlation meter.
#pragma once

#include <cmath>

namespace bb
{
class StereoMeter
{
public:
    void prepare (double sampleRate, double seconds = 0.3) { c = 1 - std::exp (-1 / (seconds * sampleRate)); reset(); }
    void reset() { ll = rr = lr = 0; }

    void process (const double* l, const double* r, int n)
    {
        double a = ll, b = rr, x = lr;
        for (int i = 0; i < n; ++i)
        {
            a += (l[i] * l[i] - a) * c;
            b += (r[i] * r[i] - b) * c;
            x += (l[i] * r[i] - x) * c;
        }
        ll = a < 1e-30 ? 0 : a; rr = b < 1e-30 ? 0 : b; lr = std::abs (x) < 1e-30 ? 0 : x;
    }

    // +1 mono, 0 unrelated, -1 out of phase; reads +1 on silence
    double correlation() const { const double d = std::sqrt (ll * rr); return d > 1e-20 ? lr / d : 1.0; }
    // -1 hard left .. +1 hard right, by power
    double balance() const { const double s = ll + rr; return s > 1e-20 ? (rr - ll) / s : 0.0; }
    // side power over mid power, in dB (-inf for mono); 0 dB is as wide as two unrelated channels
    double widthDb() const
    {
        const double m = 0.25 * (ll + rr + 2 * lr), s = 0.25 * (ll + rr - 2 * lr);
        return (m > 1e-20 && s > 1e-20) ? 10 * std::log10 (s / m) : -100.0;
    }

private:
    double c = 0, ll = 0, rr = 0, lr = 0;
};
} // namespace bb
