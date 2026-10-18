#pragma once

#include "PluginProcessor.h"
#include "ui/ChainStrip.h"
#include "ui/Look.h"
#include "ui/MeterPanel.h"
#include "ui/Spectrum.h"

class BigBossEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BigBossEditor (BigBossProcessor&);
    ~BigBossEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }   // one meter frame, without waiting for the timer (snapshot tool)

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
    bbui::MeterPanel meters;
    bbui::Spectrum spectrum;
    bbui::Goniometer gonio;
    Knob inTrim, tapeDrive, outTrim;
    juce::TextButton inButton { "IN" }, outButton { "OUT" }, resetButton { "RESET" };
    juce::ComboBox target;
    bool showingOutput = true;
    double shownRate = 0;
    juce::Rectangle<int> knobArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BigBossEditor)
};
