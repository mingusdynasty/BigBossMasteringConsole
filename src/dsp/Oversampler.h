// Stereo block oversampler on the shared polyphase IIR half-bands: 4x below 88.2 kHz, 2x below
// 176.4 kHz, none above, so whatever runs inside always sees 176.4 kHz or more. Flat to 20 kHz.
#pragma once

#include "HalfBand.h"

#include <vector>

namespace bb
{
class Oversampler
{
public:
    void prepare (double sampleRate, int maxBlock);   // allocates
    void reset();

    int factor() const { return F; }
    double rate() const { return hiRate; }
    double latency() const { return lat; }            // low-frequency delay, in base-rate samples

    // n base-rate samples in; leaves n * factor() samples in hiL()/hiR()
    void up (const double* l, const double* r, int n);
    double* hiL() { return bufL.data(); }
    double* hiR() { return bufR.data(); }
    // the n * factor() samples in hiL()/hiR() back down to n samples
    void down (double* l, double* r, int n);

private:
    static constexpr int kStage1 = 7, kStage2 = 3;
    double hb1[kStage1] {}, hb2[kStage2] {};
    cf::StereoHalfBand u1, u2, d2, d1;
    std::vector<double> bufL, bufR;
    int F = 4;
    double hiRate = 192000, lat = 0;
};
} // namespace bb
