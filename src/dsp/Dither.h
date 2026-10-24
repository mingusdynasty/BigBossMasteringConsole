// Word-length reduction for the console's output: TPDF dither at 24 or 16 bits, optionally
// noise-shaped at 16 bits.
//
// TPDF (two uniform values summed, ±1 LSB peak) makes the quantisation error independent of the
// signal, so fades and reverb tails decay into a steady, quiet hiss instead of breaking up into
// distortion. Shaped mode feeds the error back through a 3-tap filter (Wannamaker's 44.1 kHz
// F-weighted design), moving the noise up toward the top of the band where the ear is least
// sensitive: at 16 bits that buys about 10 dB of perceived noise floor. It is designed for 44.1 and
// 48 kHz; at higher rates the noise lands further above the audio band, which is harmless.
//
// The output stays in floating point, on the target word length's grid, so a host that writes 16- or
// 24-bit files stores exactly these values.
#pragma once

#include <cmath>
#include <cstdint>

namespace bb
{
class Dither
{
public:
    enum Mode { Off = 0, Bits24 = 1, Bits16 = 2, Bits16Shaped = 3 };

    void reset() { for (auto& c : ch) c.e[0] = c.e[1] = c.e[2] = 0; }   // the noise generators run on

    // in place; mode may change between blocks (a change starts with fresh error history)
    void process (double* l, double* r, int n, int mode)
    {
        if (mode != lastMode) { reset(); lastMode = mode; }
        if (mode == Off) return;
        const double q = mode == Bits24 ? 8388608.0 : 32768.0;   // steps per unit: 2^(bits - 1)
        const bool shaped = mode == Bits16Shaped;
        double* io[2] { l, r };
        for (int c = 0; c < 2; ++c)
        {
            auto& s = ch[c];
            double* x = io[c];
            for (int i = 0; i < n; ++i)
            {
                const double want = x[i] * q;
                const double v = shaped ? want - (kH[0] * s.e[0] + kH[1] * s.e[1] + kH[2] * s.e[2]) : want;
                const double d = uniform (s) + uniform (s) - 1.0;   // TPDF, -1 .. +1 LSB
                double y = std::floor (v + d + 0.5);
                y = y > q - 1 ? q - 1 : (y < -q ? -q : y);           // the word's own limits
                // the error, bounded: a clipped sample would otherwise feed a huge error back
                if (shaped) { s.e[2] = s.e[1]; s.e[1] = s.e[0]; s.e[0] = std::fmax (-4.0, std::fmin (4.0, y - v)); }
                x[i] = y / q;
            }
        }
    }

private:
    static constexpr double kH[3] = { 1.623, -0.982, 0.109 };   // error feedback (Wannamaker, F-weighted)

    struct Channel
    {
        double e[3] {};
        std::uint32_t rng = 0x9e3779b9u;
    };
    static double uniform (Channel& s)   // 0 .. 1, xorshift32
    {
        s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5;
        return (double) s.rng * (1.0 / 4294967296.0);
    }

    Channel ch[2] { {}, { { 0, 0, 0 }, 0x7f4a7c15u } };   // the channels' noise is independent
    int lastMode = Off;
};
} // namespace bb
