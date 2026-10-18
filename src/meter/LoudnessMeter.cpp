#include "LoudnessMeter.h"

#include <algorithm>
#include <cmath>

namespace bb
{
int LoudnessMeter::Histogram::binOf (double lufs)
{
    return std::clamp ((int) std::floor ((lufs - kLo) / kStep), 0, kBins - 1);
}

void LoudnessMeter::Histogram::add (double e, double lufs)
{
    const int b = binOf (lufs);
    ++count[(size_t) b];
    energy[(size_t) b] += e;
    totalEnergy += e;
    ++totalCount;
}

void LoudnessMeter::prepare (double sampleRate, int numChannels)
{
    nch = std::clamp (numChannels, 1, 2);
    stepLen = std::max (1, (int) std::lround (0.1 * sampleRate));
    for (auto& k : kw) k.prepare (sampleRate);
    blocks.init();
    shortTerms.init();
    reset();
}

void LoudnessMeter::reset()
{
    for (auto& k : kw) k.reset();
    stepAcc = 0; stepPos = 0; stepsDone = 0; stepHead = 0;
    steps.fill (0.0);
    blocks.clear();
    shortTerms.clear();
    momentaryLufs = shortTermLufs = integratedLufs = kSilence;
    maxM = maxS = kSilence;
    lra = 0;
    stepped = false;
}

void LoudnessMeter::process (const double* const* channels, int n)
{
    int i = 0;
    while (i < n)
    {
        const int run = std::min (n - i, stepLen - stepPos);
        for (int c = 0; c < nch; ++c)
        {
            auto& k = kw[(size_t) c];
            const double* x = channels[c] + i;
            double acc = 0;
            for (int j = 0; j < run; ++j) { const double y = k.run (x[j]); acc += y * y; }
            stepAcc += acc;   // L and R both weigh 1.0 (BS.1770 Table 3)
        }
        i += run;
        stepPos += run;
        if (stepPos == stepLen) closeStep();
    }
}

void LoudnessMeter::closeStep()
{
    steps[(size_t) stepHead] = stepAcc / (double) stepLen;
    stepHead = (stepHead + 1) % (int) steps.size();
    stepAcc = 0;
    stepPos = 0;
    ++stepsDone;

    auto meanOfLast = [this] (int k)
    {
        double s = 0;
        for (int j = 1; j <= k; ++j) s += steps[(size_t) ((stepHead - j + (int) steps.size()) % (int) steps.size())];
        return s / k;
    };

    if (stepsDone >= 4)
    {
        const double e = meanOfLast (4);
        momentaryLufs = toLufs (e);
        maxM = std::max (maxM, momentaryLufs);
        if (momentaryLufs > Histogram::kLo) { blocks.add (e, momentaryLufs); updateIntegrated(); }
    }
    else
        momentaryLufs = toLufs (meanOfLast (stepsDone) * stepsDone / 4.0);   // window still filling with silence

    if (stepsDone >= 30)
    {
        const double e = meanOfLast (30);
        shortTermLufs = toLufs (e);
        maxS = std::max (maxS, shortTermLufs);
        if (shortTermLufs > Histogram::kLo) { shortTerms.add (e, shortTermLufs); updateRange(); }
    }
    else
        shortTermLufs = toLufs (meanOfLast (stepsDone) * stepsDone / 30.0);

    stepped = true;
}

void LoudnessMeter::updateIntegrated()
{
    if (blocks.totalCount == 0) { integratedLufs = kSilence; return; }
    // relative gate: 10 LU under the loudness of the absolutely gated blocks
    const double gate = toLufs (blocks.totalEnergy / (double) blocks.totalCount) - 10.0;
    double e = 0; unsigned long cnt = 0;
    for (int b = Histogram::binOf (gate); b < Histogram::kBins; ++b) { e += blocks.energy[(size_t) b]; cnt += blocks.count[(size_t) b]; }
    integratedLufs = cnt ? toLufs (e / (double) cnt) : kSilence;
}

void LoudnessMeter::updateRange()
{
    // EBU Tech 3342: short-term values, relative gate 20 LU, then the 10th to 95th percentile spread
    const double gate = toLufs (shortTerms.totalEnergy / (double) shortTerms.totalCount) - 20.0;
    const int first = Histogram::binOf (gate);
    unsigned long cnt = 0;
    for (int b = first; b < Histogram::kBins; ++b) cnt += shortTerms.count[(size_t) b];
    if (cnt < 2) { lra = 0; return; }

    auto percentile = [&] (double p)
    {
        const auto target = (unsigned long) std::floor (p * (double) (cnt - 1));
        unsigned long seen = 0;
        for (int b = first; b < Histogram::kBins; ++b)
        {
            seen += shortTerms.count[(size_t) b];
            if (seen > target) return Histogram::kLo + (b + 0.5) * Histogram::kStep;
        }
        return Histogram::kHi;
    };
    lra = percentile (0.95) - percentile (0.10);
}
} // namespace bb
