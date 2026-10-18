// True peak (ITU-R BS.1770-4 Annex 2): the peak of the signal reconstructed between the samples.
//
// Oversamples 4x below 88.2 kHz and 2x below 176.4 kHz, so the meter always looks at 176.4 kHz or
// more, with a polyphase Kaiser-windowed sinc (32 taps per phase; the standard's example has 12). Each
// local maximum is then refined with a parabola through its neighbours: plain 4x under-reads a 20 kHz
// tone at 44.1 kHz by up to ~0.2 dB, refined it stays within 0.05 dB.
#pragma once

#include <array>
#include <vector>

namespace bb
{
class TruePeak
{
public:
    static constexpr int kTaps = 32;

    void prepare (double sampleRate, int numChannels);   // allocates; call off the audio thread
    void reset();
    void process (const double* const* channels, int n);

    int factor() const { return L; }
    // linear peak per channel since the last take (for bars), and held since the last reset
    double takeBlockPeak (int ch) { const double v = blockPeak[(size_t) ch]; blockPeak[(size_t) ch] = 0; return v; }
    double heldPeak (int ch) const { return held[(size_t) ch]; }
    double heldSamplePeak (int ch) const { return heldSample[(size_t) ch]; }

private:
    int L = 4, nch = 2;
    std::vector<double> coef;                         // [phase][tap], phase-major
    std::array<std::array<double, 2 * kTaps>, 2> hist {};   // doubled ring: a contiguous window always
    int pos = 0;
    std::array<std::array<double, 2>, 2> prev {};
    std::array<double, 2> blockPeak {}, held {}, heldSample {};
};
} // namespace bb
