#include "MeterSuite.h"

#include <algorithm>
#include <cmath>

namespace bb
{
void MeterSuite::prepare (double sampleRate, int numChannels)
{
    nch = std::clamp (numChannels, 1, 2);
    loudness.prepare (sampleRate, nch);
    truePeak.prepare (sampleRate, nch);
    stereo.prepare (sampleRate);
    rmsC = 1 - std::exp (-1 / (0.3 * sampleRate));
    ms[0] = ms[1] = 0;
    readings.oversampling.store (truePeak.factor());
    resetPending.store (true);
}

void MeterSuite::process (const double* l, const double* r, int n)
{
    if (resetPending.exchange (false))
    {
        loudness.reset();
        truePeak.reset();
        stereo.reset();
    }

    const double* ch[2] { l, r };
    // in chunks, so loudness steps and the history line up and the sample feed is not bursty
    for (int off = 0; off < n;)
    {
        const int k = std::min (n - off, 256);
        const double* c2[2] { ch[0] + off, ch[1] + off };
        loudness.process (c2, k);
        truePeak.process (c2, k);
        stereo.process (c2[0], c2[1], k);
        for (int i = 0; i < k; ++i)
        {
            for (int c = 0; c < 2; ++c) ms[c] += (c2[c][i] * c2[c][i] - ms[c]) * rmsC;
            samples.push ({ (float) c2[0][i], (float) c2[1][i] });
        }
        if (loudness.takeStep())
            history.push ({ (float) loudness.momentary(), (float) loudness.shortTerm() });
        off += k;
    }

    auto& rd = readings;
    rd.momentary.store ((float) loudness.momentary());
    rd.shortTerm.store ((float) loudness.shortTerm());
    rd.integrated.store ((float) loudness.integrated());
    rd.range.store ((float) loudness.range());
    rd.maxMomentary.store ((float) loudness.maxMomentary());
    rd.maxShortTerm.store ((float) loudness.maxShortTerm());
    double tpMax = 0;
    for (int c = 0; c < 2; ++c)
    {
        const int src = std::min (c, nch - 1);
        const double held = truePeak.heldPeak (src);
        tpMax = std::max (tpMax, held);
        rd.truePeakHeld[c].store (toDb (held));
        rd.samplePeakHeld[c].store (toDb (truePeak.heldSamplePeak (src)));
        rd.rms[c].store (toDb (std::sqrt (ms[c])));
    }
    // the bars take the block peak; mono shows the same value on both
    const double now0 = truePeak.takeBlockPeak (0), now1 = nch > 1 ? truePeak.takeBlockPeak (1) : now0;
    auto raise = [] (std::atomic<float>& a, float v)
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    };
    raise (rd.truePeakNow[0], toDb (now0));   // the editor resets these to -200 after reading
    raise (rd.truePeakNow[1], toDb (now1));
    rd.correlation.store ((float) stereo.correlation());
    rd.balance.store ((float) stereo.balance());
    rd.widthDb.store ((float) stereo.widthDb());
    const double I = loudness.integrated();
    rd.plr.store (I > LoudnessMeter::kSilence ? (float) (toDb (tpMax) - I) : 0.0f);
}
} // namespace bb
