// Level match: keeps a saturating stage's output at the loudness of its input.
//
// It compares the power going in with the power coming out (weighted with a 2nd-order high-pass, ~150 Hz
// by default, as loudness is and so that rumble a stage creates does not count as level),
// smoothed over ~300 ms, and returns the gain that restores it: 1 when the stage is clean, up to the cap
// when it is squashing hard. It restores level, not dynamics, so the squash itself is still heard. When
// the input drops away both windows flush faster, so quiet material is not matched against the louder
// material's compression.
#pragma once

#include <algorithm>
#include <cmath>

namespace cf
{
class LevelMatch
{
public:
    void prepare (double rate, double slowSec = 0.3, double maxGain = 4.0, double weightHz = 150.0)
    {
        hpC   = 1 - std::exp (-2 * 3.14159265358979323846 * weightHz / rate);
        slowC = 1 - std::exp (-1 / (slowSec * rate));
        fastC = 1 - std::exp (-1 / (0.1 * slowSec * rate));
        qC    = 1 - std::exp (-1 / (0.005 * rate));
        cap = maxGain;
        reset();
    }
    void reset() { for (int k = 0; k < 2; ++k) { p[k] = q[k] = 0; for (int c = 0; c < 2; ++c) hp[k][c] = hp2[k][c] = 0; } }

    // n channels (1 or 2) in and out; returns the gain to apply to out
    inline double update (const double* in, const double* out, int n)
    {
        double e[2] {};
        for (int k = 0; k < 2; ++k)
        {
            const double* v = k == 0 ? in : out;
            for (int c = 0; c < n; ++c)
            {
                hp[k][c] += hpC * (v[c] - hp[k][c]);
                const double h1 = v[c] - hp[k][c];
                hp2[k][c] += hpC * (h1 - hp2[k][c]);
                const double h = h1 - hp2[k][c];
                e[k] += h * h;
            }
            q[k] += qC * (e[k] - q[k]);
        }
        const double cc = q[0] < 0.25 * p[0] ? fastC : slowC;
        for (int k = 0; k < 2; ++k) p[k] += cc * (e[k] - p[k]);
        return std::min (cap, std::sqrt ((p[0] + 1e-10) / (p[1] + 1e-10)));
    }

private:
    double hp[2][2] {}, hp2[2][2] {}, p[2] {}, q[2] {}, hpC = 0, slowC = 0, fastC = 0, qC = 0, cap = 4;
};
} // namespace cf
