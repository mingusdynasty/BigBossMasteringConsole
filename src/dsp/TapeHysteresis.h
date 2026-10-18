#pragma once
// Magnetic tape: Jiles-Atherton hysteresis.
//
// The record head turns the signal into a magnetising field H; the tape's magnetisation M follows H
// around a hysteresis loop. Jiles-Atherton splits dM/dH into a reversible part (domain walls bending,
// which tracks the anhysteretic Langevin curve) and an irreversible part (walls tearing free of pinning
// sites, which lags behind and only moves when the field drives M toward the anhysteretic curve):
//
//     dM/dH = c * dMan/dH  +  max (0, delta * (Man - M)) / k,     Man = Ms * L (H / a)
//
// with delta the sign of dH/dt. AC bias leaves well-recorded tape mostly reversible (c close to 1);
// the irreversible remainder gives the loop its width and the tape its memory. The inter-domain
// coupling term of the full model is left out: for recording tape it is tiny (alpha * Ms / a of about
// 0.02 in published fits), and without it the Langevin curve depends on the field alone, so it is
// evaluated ahead of the solver instead of inside its feedback path.
//
// The ODE is solved with second-order Runge-Kutta at 4x the plate rate, with dH/dt from an
// alpha-transform differentiator (after J. Chowdhury, "Real-time physical modelling for analog tape
// machines", DAFx 2019). Against a 768 kHz reference this matches RK4 at 2x for the same cost, with
// 10-20 dB less aliasing. Units are normalised: Ms = 1, field in units of a.

#include <algorithm>
#include <cmath>

namespace cf
{
class TapeHysteresis
{
public:
    // k: coercivity (loop width, in units of a), c: reversible fraction
    void setTape (double k, double c)
    {
        invK = 1.0 / k; rev = c;
    }

    void prepare (double rate)
    {
        T = 1.0 / rate;
        dGain = (1.0 + dAlpha) / T;
        reset();
    }

    void reset() { M = 0; Hp = 0; Hdp = 0; Lp = langevin (0.0); }

    // One sample of field in, magnetisation out (|M| <= 1).
    inline double process (double H)
    {
        double Hd = dGain * (H - Hp) - dAlpha * Hdp;
        if (std::abs (Hd) < 1e-15) Hd = 0;            // under a steady field it decays geometrically: stop it at zero, not in denormals
        const double Hm = 0.5 * (H + Hp), Hdm = 0.5 * (Hd + Hdp);
        const Anh am = langevin (Hm), an = langevin (H);
        const double k1 = T * dMdt (M, Lp, Hdp);
        double m = M + T * dMdt (M + 0.5 * k1, am, Hdm);
        if (! std::isfinite (m)) m = 0;               // never let a numerical fault into the echo loop
        M = std::clamp (m, -1.0, 1.0);
        Hp = H; Hdp = Hd; Lp = an;
        return M;
    }

    double magnetisation() const { return M; }

private:
    struct Anh { double L, dL; };   // anhysteretic magnetisation and its slope

    // Langevin function L(q) = coth q - 1/q and its derivative
    static inline Anh langevin (double q)
    {
        const double aq = std::abs (q);
        if (aq < 0.4)
        {
            const double q2 = q * q;   // series: q/3 - q^3/45 + 2q^5/945 - q^7/4725 + 2q^9/93555
            return { q * (1.0 / 3.0 + q2 * (-1.0 / 45.0 + q2 * (2.0 / 945.0 + q2 * (-1.0 / 4725.0 + q2 * (2.0 / 93555.0))))),
                     1.0 / 3.0 + q2 * (-1.0 / 15.0 + q2 * (2.0 / 189.0 + q2 * (-1.0 / 675.0 + q2 * (2.0 / 10395.0)))) };
        }
        // one exp and one division: E = e^-2|q|, coth|q| = (1+E)/(1-E), 1/sinh^2 = 4E/(1-E)^2
        const double E = std::exp (-2.0 * aq);
        const double r = 1.0 / (aq * (1.0 - E));
        const double iq = (1.0 - E) * r, s = aq * r;                // 1/|q|, 1/(1-E)
        return { std::copysign ((1.0 + E) * s - iq, q), iq * iq - 4.0 * E * s * s };
    }

    inline double dMdt (double m, const Anh& an, double Hd) const
    {
        const double mDiff = an.L - m;                               // distance to the anhysteretic curve
        const double toward = Hd >= 0 ? mDiff : -mDiff;              // > 0: field drives M toward it
        return (rev * an.dL + std::max (toward, 0.0) * invK) * Hd;
    }

    static constexpr double dAlpha = 0.9;   // differentiator: between backward Euler (0) and bilinear (1)
    double invK = 2.5, rev = 0.93;
    double T = 1.0 / 96000, dGain = 1;
    double M = 0, Hp = 0, Hdp = 0;
    Anh Lp { 0.0, 1.0 / 3.0 };
};
} // namespace cf
