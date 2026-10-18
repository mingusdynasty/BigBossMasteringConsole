#pragma once

#include "PluginProcessor.h"
#include "ui/ChainStrip.h"
#include "ui/Look.h"
#include "ui/MeterPanel.h"
#include "ui/MeterStrip.h"
#include "ui/Rack.h"
#include "ui/Spectrum.h"

// The console: header, then the ANALYSIS button and the chain strip, which double as the tabs.
// ANALYSIS is the metering dashboard; clicking a module shows it as a rack unit, with a slim
// loudness and peak strip under it.
class BigBossEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BigBossEditor (BigBossProcessor&);
    ~BigBossEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }   // one meter frame, without waiting for the timer (snapshot tool)
    void showTab (int tab);                  // 0 = analysis, 1 + slot index = that module

    static constexpr int kNumTabs = 1 + BigBossProcessor::kNumSlots;

private:
    void timerCallback() override;
    void useSource (bool output);

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob (Knob&, const juce::String& paramId, const juce::String& text);

    BigBossProcessor& proc;
    bbui::Look look;
    bbui::ChainStrip chain;
    juce::TextButton analysisButton { "ANALYSIS" };
    int tab = 0;

    // analysis
    bbui::MeterPanel meters;
    bbui::Spectrum spectrum;
    bbui::Goniometer gonio;
    Knob inTrim, outTrim;
    juce::Rectangle<int> knobArea;

    // modules, by slot index
    std::array<std::unique_ptr<bbui::rack::Unit>, BigBossProcessor::kNumSlots> units;
    bbui::MeterStrip strip;

    juce::TextButton inButton { "IN" }, outButton { "OUT" }, resetButton { "RESET" };
    juce::ComboBox target;
    bool showingOutput = true;
    double shownRate = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BigBossEditor)
};
