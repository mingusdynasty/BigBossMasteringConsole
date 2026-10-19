// De-esser slot: the multiband de-esser (deess/DeEsser.h).
#pragma once

#include "Module.h"
#include "deess/DeEsser.h"

namespace bb
{
class DeEssModule final : public Module
{
public:
    const char* id() const override { return "deess"; }
    const char* name() const override { return "De-esser"; }

    void prepare (double sampleRate, int maxBlock) override { deesser.setSettings (settings); deesser.prepare (sampleRate, maxBlock); }
    void reset() override { deesser.reset(); }
    int latency() const override { return deesser.latency(); }
    void process (double* l, double* r, int n) override { deesser.setSettings (settings); deesser.process (l, r, n); }

    DeEsser::Settings settings;   // set on the audio thread before each block
    DeEsser deesser;
};
} // namespace bb
