// The console's look: dark anodised panel, cream legends, amber for live values.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bbui
{
namespace col
{
    const juce::Colour bg       { 0xff121315 };
    const juce::Colour panel    { 0xff1b1d20 };
    const juce::Colour panelHi  { 0xff25282c };
    const juce::Colour edge     { 0xff32363b };
    const juce::Colour legend   { 0xffe9e3d5 };
    const juce::Colour dim      { 0xff8a8e94 };
    const juce::Colour faint    { 0xff4a4e54 };
    const juce::Colour amber    { 0xfff2a43a };
    const juce::Colour green    { 0xff6cc88f };
    const juce::Colour yellow   { 0xffe8d25a };
    const juce::Colour red      { 0xffe5534b };
    const juce::Colour blue     { 0xff6aa7e8 };
}

juce::Font font (float height, bool bold = false);
juce::Font mono (float height);   // the readout face: the bundled Barlow, so it measures the same on every system

// draws a number with every digit in a cell of the same width, so readouts don't jitter as they
// change (Barlow's figures are proportional); justified like Graphics::drawText
void drawTabular (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, juce::Justification);

// map a value in dB onto 0..1 of a meter scale
inline float norm (float v, float lo, float hi) { return juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo)); }

class Look final : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (12.0f, true); }
};
} // namespace bbui
