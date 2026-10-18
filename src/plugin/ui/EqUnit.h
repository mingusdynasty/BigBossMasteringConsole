// The Class-A EQ as a rack unit: five band columns, the filter row, and a phosphor response scope.
#pragma once

#include "RackControls.h"
#include "eq/EqDesign.h"

#include <memory>

namespace bbui::rack
{
// green-phosphor scope that draws the EQ's response from eq::responseDb, the same shapes the audio runs
class EqScope final : public juce::Component
{
public:
    void show (const bb::eq::Settings&);   // repaints only when the curve changes
    void paint (juce::Graphics&) override;

private:
    bb::eq::Settings shown;
    bool valid = false;
};

class EqUnit final : public Unit
{
public:
    EqUnit (Apvts&, std::function<bb::eq::Settings()> settings);
    void tick() override;

private:
    void layout() override;

    std::function<bb::eq::Settings()> settings;
    struct BandControls { std::unique_ptr<Knob> freq, gain; std::unique_ptr<Toggle> cut, shelf; };
    BandControls bands[bb::eq::kBands];
    std::unique_ptr<Knob> hpf, lpf, mono, out;
    std::unique_ptr<Toggle> hpf24, warm;
    EqScope scope;
};
} // namespace bbui::rack
