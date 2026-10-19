// Limiter slot: the true-peak mastering limiter (dyn/Limiter.h).
#pragma once

#include "Module.h"
#include "dyn/Limiter.h"

namespace bb
{
class LimitModule final : public Module
{
public:
    const char* id() const override { return "limit"; }
    const char* name() const override { return "Limiter"; }

    void prepare (double sampleRate, int maxBlock) override { limiter.setSettings (settings); limiter.prepare (sampleRate, maxBlock); }
    void reset() override { limiter.reset(); }
    int latency() const override { return limiter.latency(); }
    void process (double* l, double* r, int n) override { limiter.setSettings (settings); limiter.process (l, r, n); }

    Limiter::Settings settings;   // set on the audio thread before each block
    Limiter limiter;
};
} // namespace bb
