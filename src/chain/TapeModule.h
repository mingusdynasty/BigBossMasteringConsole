// Tape slot. For now this is the chocolatefudgeverb tape model as it ships in Hot Sauce (Jiles-Atherton
// hysteresis between record and playback heads, level-matched). Step 2 retunes it into the Studer A80.
#pragma once

#include "Module.h"
#include "dsp/TapeSaturator.h"

#include <atomic>

namespace bb
{
class TapeModule final : public Module
{
public:
    const char* id() const override { return "tape"; }
    const char* name() const override { return "Tape"; }

    void prepare (double sampleRate, int) override
    {
        tape.prepare (sampleRate);
        tape.setAmount (amount.load(), true);
        tape.reset();
    }
    void reset() override { tape.reset(); }
    int latency() const override { return tape.latencySamples(); }
    void process (double* l, double* r, int n) override
    {
        tape.setAmount (amount.load (std::memory_order_relaxed));
        tape.process (l, r, n);
    }

    std::atomic<double> amount { 0.25 };   // 0..1

private:
    cf::TapeSaturator tape;
};
} // namespace bb
