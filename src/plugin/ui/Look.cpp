#include "Look.h"

namespace bbui
{
juce::Font font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font mono (float height)
{
    // not the system monospace face: Courier New on Windows sits far taller in its box than the
    // Linux one, which lifted the values into their labels
    return font (height);
}

void drawTabular (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification just)
{
    const auto f = g.getCurrentFont();
    const float cell = juce::GlyphArrangement::getStringWidth (f, "0");
    auto widthOf = [&] (juce::juce_wchar c)
    { return juce::CharacterFunctions::isDigit (c) ? cell : juce::GlyphArrangement::getStringWidth (f, juce::String::charToString (c)); };

    float total = 0;
    for (auto c : text) total += widthOf (c);

    float x = area.getX();
    if (just.testFlags (juce::Justification::right)) x = area.getRight() - total;
    else if (just.testFlags (juce::Justification::horizontallyCentred)) x = area.getCentreX() - total * 0.5f;

    float baseline = area.getCentreY() + (f.getAscent() - f.getDescent()) * 0.5f;
    if (just.testFlags (juce::Justification::bottom)) baseline = area.getBottom() - f.getDescent();
    else if (just.testFlags (juce::Justification::top)) baseline = area.getY() + f.getAscent();

    for (auto c : text)
    {
        const auto ch = juce::String::charToString (c);
        const float w = widthOf (c);
        const float natural = juce::GlyphArrangement::getStringWidth (f, ch);
        g.drawSingleLineText (ch, juce::roundToInt (x + (w - natural) * 0.5f), juce::roundToInt (baseline));
        x += w;
    }
}

Look::Look()
{
    setColour (juce::Slider::textBoxTextColourId, col::amber);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId, col::legend);
    setColour (juce::TextButton::textColourOnId, col::bg);
    setColour (juce::TextButton::buttonColourId, col::panelHi);
    setColour (juce::TextButton::buttonOnColourId, col::amber);
    setColour (juce::Label::textColourId, col::legend);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto b = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = start + pos * (end - start);

    // scale ticks
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (float) i / 10.0f * (end - start);
        const auto p1 = c.getPointOnCircumference (r, a), p2 = c.getPointOnCircumference (r - 4.0f, a);
        g.setColour (i == 5 ? col::dim : col::faint);
        g.drawLine ({ p1, p2 }, 1.2f);
    }
    // value arc, from the centre for bipolar controls
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float from = bipolar ? start + 0.5f * (end - start) : start;
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, r - 7.0f, r - 7.0f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (col::amber.withAlpha (s.isEnabled() ? 1.0f : 0.3f));
    g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob body: machined cap
    const float kr = r - 11.0f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3e44), c.x, c.y - kr, juce::Colour (0xff1c1e21), c.x, c.y + kr, false));
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (col::edge);
    g.drawEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2, 1.0f);
    // pointer
    g.setColour (col::legend);
    g.drawLine ({ c.getPointOnCircumference (kr * 0.25f, angle), c.getPointOnCircumference (kr - 2.0f, angle) }, 2.0f);
}

juce::Label* Look::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (mono (12.0f));
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineWhenEditingColourId, col::amber);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textColourId, col::amber);
    return l;
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = b.getToggleState() ? col::amber : c;
    if (over) fill = fill.brighter (0.08f);
    if (down) fill = fill.darker (0.15f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (col::edge);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}
} // namespace bbui
