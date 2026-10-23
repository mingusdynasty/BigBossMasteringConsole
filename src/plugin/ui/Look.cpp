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
} // namespace bbui
