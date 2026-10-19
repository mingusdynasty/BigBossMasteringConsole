// Compressor slot: the two-stage mastering compressor (dyn/DualCompressor.h).
#pragma once

#include "Module.h"
#include "dyn/DualCompressor.h"

namespace bb
{
class CompModule final : public Module
{
public:
    const char* id() const override { return "comp"; }
    const char* name() const override { return "Compressor"; }

    void prepare (double sampleRate, int maxBlock) override { comp.setSettings (settings); comp.prepare (sampleRate, maxBlock); }
    void reset() override { comp.reset(); }
    int latency() const override { return comp.latency(); }
    void process (double* l, double* r, int n) override { comp.setSettings (settings); comp.process (l, r, n); }

    DualCompressor::Settings settings;   // set on the audio thread before each block
    DualCompressor comp;
};
} // namespace bb
