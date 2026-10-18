// Everything a mastering engineer watches, in one place: loudness (M, S, I, LRA, maxima), true peak and
// sample peak, PLR, stereo correlation, balance and width, plus raw sample and loudness-history feeds for
// the spectrum, goniometer and history graph.
//
// The audio thread calls process(); readings are published as atomics, and the feeds go through
// single-producer rings, so the editor reads without locks and nothing waits on anything.
#pragma once

#include "LoudnessMeter.h"
#include "StereoMeter.h"
#include "TruePeak.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bb
{
// Single-producer, single-consumer ring. The reader keeps its own position; if it falls more than a
// ring behind it jumps forward and loses the oldest data, which is what a display wants.
template <typename T, int N>
class ScopeRing
{
    static_assert ((N & (N - 1)) == 0, "N must be a power of two");
public:
    void push (const T& v)
    {
        const auto w = write.load (std::memory_order_relaxed);
        buf[w & kMask] = v;
        write.store (w + 1, std::memory_order_release);
    }
    // copies up to max new items into dst; returns how many
    int read (std::uint64_t& readPos, T* dst, int max) const
    {
        const auto w = write.load (std::memory_order_acquire);
        if (w - readPos > (std::uint64_t) N) readPos = w - (std::uint64_t) N;
        int k = 0;
        while (readPos < w && k < max) dst[k++] = buf[(readPos++) & kMask];
        return k;
    }
    std::uint64_t head() const { return write.load (std::memory_order_acquire); }

private:
    static constexpr std::uint64_t kMask = (std::uint64_t) N - 1;
    T buf[(std::size_t) N] {};
    std::atomic<std::uint64_t> write { 0 };
};

struct StereoSample { float l = 0, r = 0; };
struct LoudnessPoint { float momentary = -200, shortTerm = -200; };

class MeterSuite
{
public:
    void prepare (double sampleRate, int numChannels);   // allocates; call off the audio thread
    void process (const double* l, const double* r, int n);   // r == l for mono
    void requestReset() { resetPending.store (true); }

    struct Readings
    {
        std::atomic<float> momentary { -200 }, shortTerm { -200 }, integrated { -200 }, range { 0 };
        std::atomic<float> maxMomentary { -200 }, maxShortTerm { -200 };
        std::atomic<float> truePeakHeld[2] { { -200 }, { -200 } };     // dBTP since reset
        std::atomic<float> truePeakNow[2] { { -200 }, { -200 } };      // dBTP of the last block, for bars
        std::atomic<float> samplePeakHeld[2] { { -200 }, { -200 } };   // dBFS since reset
        std::atomic<float> rms[2] { { -200 }, { -200 } };              // dBFS, 300 ms
        std::atomic<float> correlation { 1 }, balance { 0 }, widthDb { -100 };
        std::atomic<float> plr { 0 };                                   // max true peak minus integrated, LU
        std::atomic<int> oversampling { 4 };
    } readings;

    ScopeRing<StereoSample, 32768> samples;
    ScopeRing<LoudnessPoint, 8192> history;   // one point per 100 ms: 13+ minutes

    // pure helpers, also used by the tests
    static float toDb (double lin) { return lin > 1e-10 ? (float) (20.0 * std::log10 (lin)) : -200.0f; }

private:
    LoudnessMeter loudness;
    TruePeak truePeak;
    StereoMeter stereo;
    double rmsC = 0, ms[2] {};
    int nch = 2;
    std::atomic<bool> resetPending { false };
};
} // namespace bb
