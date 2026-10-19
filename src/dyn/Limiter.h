// Mastering limiter of our own design: true-peak safe, low distortion, no pumping.
//
// The gain is computed from the true peak, not the sample peak: an 8x (or 4x, 2x) polyphase sinc
// reconstruction of the input, with each crest refined by a parabola, finds the overs between
// samples, so the ceiling holds after any D/A or codec. The gain path is built so it can never let
// an over through:
//   required gain per sample (ceiling / true peak either side of it)
//   -> minimum over the lookahead window (3 ms)
//   -> a hold (3 to 20 ms by style) and a two-speed release: a fast follower for transients and a
//      slow one that settles on the programme's level. The hold spans a bass note's half cycle, so a
//      sustained low note gets a steady gain instead of one rippling at its own frequency (the usual
//      source of a limiter's low-frequency distortion)
//   -> one or two moving averages no longer than the lookahead, so the gain eases down over the
//      lookahead instead of snapping, and is still at or below the required gain at every sample.
// The audio is delayed to meet its gain, so nothing passes before its gain is ready.
//
// Styles change the hold, the release speeds and the shape of the gain's descent; switching between
// them crossfades the gains, which is safe because every style's gain is at or below the required one.
#pragma once

#include <array>
#include <atomic>
#include <vector>

namespace bb
{
class Limiter
{
public:
    enum Style { Transparent = 0, Punch = 1, Dense = 2 };
    static constexpr int kStyles = 3;

    struct Settings
    {
        double driveDb = 0;      // 0 .. +24
        double ceilingDb = -1;   // -12 .. 0 dBTP
        bool autoRelease = true;
        double releaseMs = 100;  // when not auto
        int style = Transparent;
        bool truePeak = true;    // false: sample peaks only
        double link = 1;         // stereo link, 0 .. 1
    };

    void prepare (double sampleRate, int maxBlock);   // allocates
    void reset();
    void setSettings (const Settings& s) { target = s; }
    void process (double* l, double* r, int n);
    int latency() const { return lookahead + kFirDelay; }

    float reduction() const { return gr.load (std::memory_order_relaxed); }   // dB, positive, last block's most

    // peaks (linear, after drive and before the ceiling; the output's sample peak) and the most
    // gain reduction (dB) since the last call; for the editor's history display
    void takeMeters (float& in, float& out, float& reductionDb)
    {
        in = inPeak.exchange (0.0f, std::memory_order_relaxed);
        out = outPeak.exchange (0.0f, std::memory_order_relaxed);
        reductionDb = grHeld.exchange (0.0f, std::memory_order_relaxed);
    }

private:
    static constexpr int kTaps = 32, kFirDelay = kTaps / 2;
    static constexpr double kMargin = 0.9954;   // -0.04 dB for what the reconstruction and the gain's movement leave

    struct SlidingMin   // minimum of the last len values, O(1) per value
    {
        std::vector<double> val;
        std::vector<long> dq;
        int len = 1, head = 0, tail = 0;
        long idx = 0;
        void init (int n) { len = n; val.assign ((size_t) n, 1.0); dq.assign ((size_t) n + 1, 0); head = tail = 0; idx = 0; }
        double push (double v)
        {
            const int Q = len + 1;
            const long id = idx++;
            val[(size_t) (id % len)] = v;
            while (tail != head && val[(size_t) (dq[(size_t) ((tail - 1 + Q) % Q)] % len)] >= v) tail = (tail - 1 + Q) % Q;
            dq[(size_t) tail] = id;
            tail = (tail + 1) % Q;
            while (id - dq[(size_t) head] >= len) head = (head + 1) % Q;
            return val[(size_t) (dq[(size_t) head] % len)];
        }
    };
    struct Box   // moving average over len samples
    {
        std::vector<double> buf;
        int len = 1, pos = 0, sinceExact = 0;
        double sum = 0;
        void init (int n) { len = n; buf.assign ((size_t) n, 1.0); pos = 0; sum = n; sinceExact = 0; }
        double push (double v)
        {
            sum += v - buf[(size_t) pos];
            buf[(size_t) pos] = v;
            if (++pos == len) pos = 0;
            if (++sinceExact >= 8192) { sum = 0; for (double b : buf) sum += b; sinceExact = 0; }   // no drift
            return sum / len;
        }
    };
    struct Engine   // one style's gain path for one channel, after the lookahead minimum
    {
        SlidingMin hold;
        double fast = 1, slow = 1;
        Box a, b;
    };
    struct Times { double fastC, slowAtkC, slowRelC; int hold, boxA, boxB; };
    struct Channel
    {
        std::vector<double> hist;              // input history for the reconstruction, doubled ring
        int hpos = 0;
        double ph0 = 0, ph1 = 0;               // the last two reconstructed magnitudes, for crest refinement
        double seg1 = 0, seg2 = 0;             // the two previous segments' peaks
        SlidingMin window;                     // the lookahead minimum
        std::array<Engine, kStyles> engine;
        std::vector<double> delay;             // the audio, delayed to meet its gain
    };

    double segmentPeak (Channel& c, double x, bool truePeak);
    Times times (int style, const Settings& s) const;

    double sr = 48000;
    int L = 8, lookahead = 144, delayMask = 0, dPos = 0;
    std::vector<double> coef;                  // [phase][tap]
    std::array<Channel, 2> ch;
    std::vector<double> ceilDelay;             // ceiling per sample, delayed with the audio
    Settings target;
    double pre = 1, ceil = 1, glide = 0, styleW[kStyles] { 1, 0, 0 }, linkNow = 1;
    std::atomic<float> gr { 0 }, grHeld { 0 }, inPeak { 0 }, outPeak { 0 };
};
} // namespace bb
