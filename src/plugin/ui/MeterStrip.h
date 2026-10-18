// One-line loudness and peak strip, shown under the module tabs so the levels stay in view.
#pragma once

#include "Look.h"
#include "meter/MeterSuite.h"

namespace bbui
{
class MeterStrip final : public juce::Component
{
public:
    void setSource (bb::MeterSuite* m, juce::String name) { src = m; sourceName = std::move (name); repaint(); }
    void tick (float truePeakBarL, float truePeakBarR);   // bars from the MeterPanel, which reads the block peaks
    void paint (juce::Graphics&) override;

    float targetLufs = -14.0f;

private:
    bb::MeterSuite* src = nullptr;
    juce::String sourceName;
    float M = -200, S = -200, I = -200, lra = 0, corr = 1;
    float tp[2] { -200, -200 }, tpHeld[2] { -200, -200 };
};
} // namespace bbui
