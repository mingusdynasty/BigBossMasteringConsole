// Loudness and level metering: readouts, bars, correlation and the loudness history.
#pragma once

#include "Look.h"
#include "meter/MeterSuite.h"

#include <vector>

namespace bbui
{
class MeterPanel final : public juce::Component
{
public:
    void setSource (bb::MeterSuite* m);   // switches input/output metering; clears the history view
    void tick();                           // from the editor's timer
    void paint (juce::Graphics&) override;

    float targetLufs = -14.0f;             // reference line on the bars and the history
    float truePeakBar (int ch) const { return tpBar[ch]; }   // this panel takes the block peaks, so it shares its bars

private:
    void paintReadout (juce::Graphics&, juce::Rectangle<int> r, const juce::String& label, const juce::String& value,
                       const juce::String& unit, juce::Colour c, float valueHeight);
    void paintBar (juce::Graphics&, juce::Rectangle<int> r, const juce::String& label, float value, float hold,
                   float lo, float hi, bool loudness);
    void paintHistory (juce::Graphics&, juce::Rectangle<int> r);

    bb::MeterSuite* src = nullptr;
    std::uint64_t histPos = 0;
    std::vector<bb::LoudnessPoint> hist;   // newest at the back, at most kHistory
    static constexpr size_t kHistory = 600;   // 60 s

    float M = -200, S = -200, I = -200, lra = 0, maxM = -200, maxS = -200, plr = 0;
    float tp[2] { -200, -200 }, tpHeld[2] { -200, -200 }, spHeld[2] { -200, -200 }, tpBar[2] { -200, -200 };
    float corr = 1, bal = 0, width = -100;
    int os = 4;
};
} // namespace bbui
