// Ported from the owner's Vocal Chain plugin (mingusdynasty/rare-luxuriousVocalChain, rebuild/engine at 7502ba1),
// so the two plugins share one rack look. Namespace renamed to kit.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "RackLookAndFeel.h"

namespace kit
{
    // Moving-coil VU meter. `source` returns the reading in VU dB (0 = reference,
    // negative values for gain reduction); the needle follows it with standard
    // VU ballistics: 300 ms rise with about 1.5% overshoot.
    class VuMeter : public juce::Component
    {
    public:
        explicit VuMeter (std::function<float()> source, juce::String legend = "VU");
        void paint (juce::Graphics&) override;
        void tick (float seconds);   // once per editor frame

    private:
        static float deflectionFor (float vuDb);

        std::function<float()> source;
        juce::String legend;
        float position = 0.0f, velocity = 0.0f;
    };

    // Round indicator lamp.
    class Led : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Led (juce::Colour colour, std::function<bool()> lit);
        void paint (juce::Graphics&) override;
        void tick();

    private:
        juce::Colour colour;
        std::function<bool()> lit;
        bool state = false;
    };

    // A row or column of push buttons that together select one value of a
    // choice parameter. With `allIndex` set, shift-clicking any button selects
    // that index and lights every button, like pushing all of the 1176's ratio
    // buttons in.
    class ChoiceButtons : public juce::Component, private juce::AudioProcessorValueTreeState::Listener,
                          private juce::AsyncUpdater
    {
    public:
        ChoiceButtons (juce::AudioProcessorValueTreeState&, const juce::String& paramId,
                       const juce::StringArray& labels, ButtonStyle, bool vertical, int allIndex = -1,
                       bool reversed = false);
        ~ChoiceButtons() override;

        void resized() override;
        void setTooltipForButtons (const juce::String& tip) { for (auto* b : buttons) b->setTooltip (tip); }

    private:
        void parameterChanged (const juce::String&, float) override;
        void handleAsyncUpdate() override;
        void select (int index);
        void refresh();

        juce::AudioProcessorValueTreeState& state;
        juce::String paramId;
        juce::RangedAudioParameter* param;
        juce::OwnedArray<juce::ToggleButton> buttons;
        bool vertical, reversed;
        int allIndex;
    };

    // Two-channel peak meter (LED-bar style) for the fader panel.
    class PeakMeter : public juce::Component
    {
    public:
        explicit PeakMeter (std::function<float (int)> takePeakDb);
        void paint (juce::Graphics&) override;

        // Maps dB to 0..1 across the meter, matching the fader's scale.
        static float proportionFor (float db);

        void tick();   // once per editor frame, at 30 Hz

    private:
        std::function<float (int)> takePeakDb;
        std::array<float, 2> level { -100.0f, -100.0f }, hold { -100.0f, -100.0f };
        std::array<int, 2> holdFrames { 0, 0 };
    };
}
