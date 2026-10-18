// Rack controls attached to parameters: knobs, selectors and toggles, styled through RackLook.
#pragma once

#include "Rack.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace bbui::rack
{
using Apvts = juce::AudioProcessorValueTreeState;

class Knob final : public juce::Slider
{
public:
    // style: "big", "small" or "chicken"; scale: (value, legend) pairs printed round the knob
    Knob (Apvts& state, const juce::String& paramId, const char* style,
          const std::vector<std::pair<float, const char*>>& scale = {}, bool detents = false)
        : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
    {
        auto* p = state.getParameter (paramId);
        jassert (p != nullptr);
        getProperties().set ("style", style);
        juce::StringArray items;
        for (const auto& s : scale) items.add (juce::String (p->convertTo0to1 (s.first), 4) + ":" + s.second);
        if (! items.isEmpty()) getProperties().set ("scale", items.joinIntoString ("|"));
        getProperties().set ("detents", detents);
        setPopupDisplayEnabled (true, true, nullptr);
        setTooltip (p->getName (64));
        attachment = std::make_unique<Apvts::SliderAttachment> (state, paramId, *this);
        setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        if (detents) setMouseDragSensitivity (120);   // a few clicks of travel per position
        param = p;
    }

    juce::String getTextFromValue (double v) override
    {
        // the parameter's own text in the drag popup: "4.5 kHz", "OFF", "+2.0 dB"
        if (param != nullptr) return param->getText (param->convertTo0to1 ((float) v), 32);
        return juce::Slider::getTextFromValue (v);
    }

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<Apvts::SliderAttachment> attachment;
};

// bat-handle toggle: "up" is the parameter's on (or second) state
class Toggle final : public juce::ToggleButton
{
public:
    Toggle (Apvts& state, const juce::String& paramId, const char* up, const char* down)
    {
        getProperties().set ("up", up);
        getProperties().set ("down", down);
        if (auto* p = state.getParameter (paramId)) setTooltip (p->getName (64));
        attachment = std::make_unique<Apvts::ButtonAttachment> (state, paramId, *this);
    }

private:
    std::unique_ptr<Apvts::ButtonAttachment> attachment;
};

} // namespace bbui::rack
