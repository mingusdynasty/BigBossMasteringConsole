// Ported from the owner's Vocal Chain plugin (mingusdynasty/rare-luxuriousVocalChain, rebuild/engine at 7502ba1),
// so the two plugins share one rack look. Namespace renamed to kit.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Draws the hardware-style controls. Each control picks its look from a
// "style" component property, so one look-and-feel serves all four panels.
namespace kit
{
    enum class KnobStyle
    {
        neveRed,      // 1073 gain switch: large red knob
        neveGrey,     // 1073 EQ and trim knobs
        neveBlue,     // 1073 frequency switches
        dbxBlack,     // 160 knobs
        ureiLarge,    // 1176 input / output
        ureiSmall     // 1176 attack / release
    };

    enum class ButtonStyle
    {
        neveSquare,   // illuminated square push switch
        ureiWhite,    // 1176 ratio / meter buttons
        dbxPush,      // 160 meter buttons
        consoleKey,   // fader panel keys: dark, amber when lit
        power         // rack power switch with lamp
    };

    void setStyle (juce::Component& c, KnobStyle s);
    void setStyle (juce::Component& c, ButtonStyle s);

    inline const juce::Identifier styleId { "rackStyle" };

    // Rotary travel shared by every knob, and its scale markings.
    inline constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
    inline constexpr float endAngle = juce::MathConstants<float>::pi * 2.75f;

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

        juce::Label* createSliderTextBox (juce::Slider&) override;
        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    };

    // Ask for this typeface name to get the condensed legend face.
    inline const juce::String condensedFontName { "Rack Condensed" };

    // Engraved scale around a knob: tick marks and labels at given proportions.
    void drawScale (juce::Graphics&, juce::Point<float> centre, float radius,
                    const std::vector<std::pair<float, juce::String>>& marks,
                    juce::Colour colour, float fontHeight, bool ticks = true);

    // Panel text helper.
    void drawEngraved (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                       juce::Colour colour, float height, bool bold = false,
                       juce::Justification just = juce::Justification::centred);

    void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius);
}
