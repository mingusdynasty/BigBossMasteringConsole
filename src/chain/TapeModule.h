// Tape slot: the A80-style two-track machine (tape/TapeMachine.h).
#pragma once

#include "Module.h"
#include "tape/TapeMachine.h"

namespace bb
{
class TapeModule final : public Module
{
public:
    const char* id() const override { return "tape"; }
    const char* name() const override { return "Tape"; }

    void prepare (double sampleRate, int maxBlock) override { machine.setSettings (settings); machine.prepare (sampleRate, maxBlock); }
    void reset() override { machine.reset(); }
    int latency() const override { return machine.latency(); }
    void process (double* l, double* r, int n) override { machine.setSettings (settings); machine.process (l, r, n); }

    TapeMachine::Settings settings;   // set on the audio thread before each block
    TapeMachine machine;
};
} // namespace bb
