// Plugin-level tests: the processor as a host drives it (JUCE). Run via ctest.
#include "plugin/PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kSr = 48000;
static constexpr int kBlock = 512;

static float input (int i) { return 0.3f * (float) std::sin (2 * kPi * 110.0 * i / kSr) + 0.1f * (float) std::sin (2 * kPi * 2500.0 * i / kSr); }

// runs blocks through the processor; bypassed(k) says whether block k goes through the host's bypass
template <typename Bypassed, typename BeforeBlock>
static std::vector<float> run (BigBossProcessor& proc, int blocks, Bypassed bypassed, BeforeBlock before)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buf (2, kBlock);
    juce::MidiBuffer midi;
    for (int k = 0; k < blocks; ++k)
    {
        before (k);
        for (int i = 0; i < kBlock; ++i) buf.setSample (0, i, input (k * kBlock + i)), buf.setSample (1, i, input (k * kBlock + i));
        if (bypassed (k)) proc.processBlockBypassed (buf, midi);
        else proc.processBlock (buf, midi);
        out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + kBlock);
    }
    return out;
}

// the largest difference from the input delayed by the latency, over [from, to)
static double offDry (const std::vector<float>& out, int latency, int from, int to)
{
    double worst = 0;
    for (int i = juce::jmax (from, latency); i < to; ++i) worst = juce::jmax (worst, (double) std::abs (out[(size_t) i] - input (i - latency)));
    return worst;
}

// the largest 4th difference: tiny for smooth signals, large at a step (a click)
static double maxD4 (const std::vector<float>& out, int from, int to)
{
    double worst = 0;
    for (int i = from + 4; i < to; ++i)
    {
        const auto x = [&] (int k) { return (double) out[(size_t) (i - k)]; };
        worst = juce::jmax (worst, std::abs (x (0) - 4 * x (1) + 6 * x (2) - 4 * x (3) + x (4)));
    }
    return worst;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto make = []
    {
        auto p = std::make_unique<BigBossProcessor>();
        p->setPlayConfigDetails (2, 2, kSr, kBlock);
        p->prepareToPlay (kSr, kBlock);
        if (auto* d = p->apvts.getParameter ("limit_drive")) d->setValueNotifyingHost (d->convertTo0to1 (8.0f));   // audibly different from dry
        return p;
    };
    const auto none = [] (int) { return false; };
    const auto nothing = [] (int) {};

    // BYPASS ALL from the start: the output is exactly the input, delayed by the reported latency
    {
        auto p = make();
        const int lat = p->getLatencySamples();
        CHECK (lat > 0, "the chain reports latency (%d)", lat);
        p->bypassAll = true;
        p->prepareToPlay (kSr, kBlock);
        const auto out = run (*p, 40, none, nothing);
        const double off = offDry (out, lat, 0, (int) out.size());
        CHECK (off < 1e-6, "bypass all passes the input untouched (off by %g)", off);
    }

    // switching BYPASS ALL in and out mid-stream: no step bigger than the signal itself makes, and
    // fully dry once the fade is done
    {
        auto p = make();
        const int lat = p->getLatencySamples();
        BigBossProcessor* pp = p.get();
        const auto out = run (*p, 120, none, [pp] (int k) { if (k == 40) pp->bypassAll = true; if (k == 80) pp->bypassAll = false; });
        const double natural = juce::jmax (maxD4 (out, 20 * kBlock, 40 * kBlock), maxD4 (out, 60 * kBlock, 80 * kBlock));
        const double across = juce::jmax (maxD4 (out, 39 * kBlock, 44 * kBlock), maxD4 (out, 79 * kBlock, 84 * kBlock));
        CHECK (across < natural * 2, "bypass all switches without a click (4th difference %g, the signal's own %g)", across, natural);
        const double off = offDry (out, lat, 42 * kBlock, 80 * kBlock);
        CHECK (off < 1e-6, "bypass all is dry after its fade (off by %g)", off);
        const double wet = offDry (out, lat, 100 * kBlock, 120 * kBlock);
        CHECK (wet > 1e-3, "the console comes back after bypass all (differs from dry by %g)", wet);
    }

    // the host's bypass after normal processing: dry and time-aligned from its first sample
    {
        auto p = make();
        const int lat = p->getLatencySamples();
        const auto out = run (*p, 60, [] (int k) { return k >= 40; }, nothing);
        const double off = offDry (out, lat, 40 * kBlock, 60 * kBlock);
        CHECK (off < 1e-6, "the host's bypass is the input delayed by the latency, with no stale stretch (off by %g)", off);
    }

    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
