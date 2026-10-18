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

    void prepare (double sampleRate, int maxBlock) override { eq.setSettings (settings); eq.prepare (sampleRate, maxBlock); }
    void reset() override { eq.reset(); }
    int latency() const override { return eq.latency(); }
    void process (double* l, double* r, int n) override { eq.setSettings (settings); eq.process (l, r, n); }

    eq::Settings settings;   // set on the audio thread before each block
    ClassAEq eq;
};
} // namespace bb
