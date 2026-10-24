// EQ slot: the Class-A mastering equaliser (eq/ClassAEq.h).
#pragma once

#include "Module.h"
#include "eq/ClassAEq.h"

namespace bb
{
class EqModule final : public Module
{
public:
    const char* id() const override { return "eq"; }
    const char* name() const override { return "Parametric EQ"; }

    void prepare (double sampleRate, int maxBlock) override
    {
        eq.setSettings (settings);
        eq.prepare (sampleRate, maxBlock);
        dryOs.prepare (sampleRate, maxBlock);
    }
    void reset() override { eq.reset(); dryOs.reset(); }
    int latency() const override { return eq.latency(); }
    void process (double* l, double* r, int n) override { eq.setSettings (settings); eq.process (l, r, n); }
    // bypassed, the EQ is its oversampler's round trip: flat, and in phase with the EQ at rest, so the
    // bypass fade never dips the top end (a plain delay is up to 110 degrees out at 15 kHz)
    bool dry (const double* l, const double* r, double* outL, double* outR, int n) override
    {
        dryOs.up (l, r, n);
        dryOs.down (outL, outR, n);
        return true;
    }

    eq::Settings settings;   // set on the audio thread before each block
    ClassAEq eq;
    Oversampler dryOs;
};
} // namespace bb
