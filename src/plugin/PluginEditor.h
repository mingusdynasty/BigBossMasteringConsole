// The console as a rack, like the owner's Vocal Chain: the half-height master section (presets) on
// top, one 1U unit per module in chain order, then the output fader. The analyser is a 5U unit that
// takes the modules' place when its key is on; the assistant drops down over the rack from BB-1.
// Everything is laid out in a fixed design space and scaled as a whole, so the window can be
// dragged to any size (50% to 200%) and the rack keeps its proportions.
#pragma once

#include "PluginProcessor.h"
#include "rack/Panels.h"

class BigBossEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BigBossEditor (BigBossProcessor&);
    ~BigBossEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refreshNow() { tickAll (1.0f / 30.0f); }   // one meter frame, without waiting for the timer (snapshot tool)
    void showAnalyser (bool on);
    void showAssistant (bool on);
    kit::AssistantPanel& assistantSection() { return assistant; }

    static constexpr int kModules = BigBossProcessor::kNumSlots;
    static constexpr int designWidth = kit::rackWidth;
    static constexpr int designHeight = kit::masterHeight + kit::unitHeight * (kModules + 1);
    static constexpr int modulesTop = kit::masterHeight;   // below the master section

private:
    void timerCallback() override { tickAll (1.0f / 30.0f); }
    void tickAll (float seconds);
    juce::AffineTransform designToWindow() const;
    void layoutModules();
    void dragModule (int slot, int dy);
    void dropModule (int slot);

    BigBossProcessor& proc;
    kit::LookAndFeel lookAndFeel;
    juce::Component content;
    std::array<std::unique_ptr<kit::Panel>, kModules> modules;   // by slot index
    kit::MasterPanel master;
    kit::OutputPanel output;
    kit::AnalyserPanel analyser;
    kit::AssistantPanel assistant;
    juce::TooltipWindow tooltips { this, 600 };
    int draggingSlot = -1, dragRow = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BigBossEditor)
};
