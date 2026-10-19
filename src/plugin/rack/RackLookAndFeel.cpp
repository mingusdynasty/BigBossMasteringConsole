// Ported from the owner's Vocal Chain plugin (mingusdynasty/rare-luxuriousVocalChain, rebuild/engine at 7502ba1),
// so the two plugins share one rack look. Namespace renamed to kit.
#include "RackLookAndFeel.h"
#include "KnobRenderer.h"
#include "BigBossFonts.h"

namespace kit
{
    void setStyle (juce::Component& c, KnobStyle s)   { c.getProperties().set (styleId, (int) s); }
    void setStyle (juce::Component& c, ButtonStyle s) { c.getProperties().set (styleId, (int) s); }

    namespace
    {
        int styleOf (const juce::Component& c, int fallback)
        {
            const auto v = c.getProperties()[styleId];
            return v.isVoid() ? fallback : (int) v;
        }

        void shadow (juce::Graphics& g, juce::Rectangle<float> r, float spread, float alpha)
        {
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (juce::Colours::black.withAlpha (alpha / (float) i));
                g.fillEllipse (r.translated (spread * 0.4f, spread * 0.7f).expanded (spread * (float) i / 3.0f));
            }
        }

        void pointer (juce::Graphics& g, juce::Point<float> c, float angle, float r0, float r1, float width, juce::Colour col)
        {
            juce::Path p;
            p.addRoundedRectangle (-width * 0.5f, -r1, width, r1 - r0, width * 0.5f);
            g.setColour (col);
            g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c));
        }

    }

    LookAndFeel::LookAndFeel()
    {
        setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xee151515));
        setColour (juce::BubbleComponent::outlineColourId, juce::Colour (0xff404040));
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xee151515));
        setColour (juce::TooltipWindow::textColourId, juce::Colours::white);
    }

    // Panel legends are screen printed in a grotesque close to the units' own
    // (Barlow, SIL Open Font License, bundled in resources/fonts).
    juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
    {
        static const auto semi = juce::Typeface::createSystemTypefaceFor (BigBossFonts::BarlowSemiBold_ttf, BigBossFonts::BarlowSemiBold_ttfSize);
        static const auto bold = juce::Typeface::createSystemTypefaceFor (BigBossFonts::BarlowBold_ttf, BigBossFonts::BarlowBold_ttfSize);
        static const auto condensed = juce::Typeface::createSystemTypefaceFor (BigBossFonts::BarlowCondensedBold_ttf, BigBossFonts::BarlowCondensedBold_ttfSize);
        if (f.getTypefaceName() == condensedFontName)
            return condensed;
        if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            return f.isBold() ? bold : semi;
        return LookAndFeel_V4::getTypefaceForFont (f);
    }

    juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setColour (juce::Label::textColourId, juce::Colours::white);
        return l;
    }

    void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                        float a0, float a1, juce::Slider& s)
    {
        const auto style = (KnobStyle) styleOf (s, (int) KnobStyle::neveGrey);
        // The component is a little larger than the knob, leaving room for its shadow.
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                                .reduced ((float) juce::jmin (width, height) * 0.06f);
        const auto r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const auto angle = a0 + pos * (a1 - a0);
        auto disc = [&] (float k) { return juce::Rectangle<float> (c.x - r * k, c.y - r * k, r * k * 2, r * k * 2); };

        // Bodies are lit height maps (KnobRenderer), cached per style and size;
        // the pointer turns over them.
        KnobBody k;
        float pFrom = 0.1f, pTo = 0.94f, pWidth = 0.07f;
        auto pColour = juce::Colour (0xfff4efe4);
        switch (style)
        {
            case KnobStyle::neveRed:
                k.skirtRadius = 0.98f; k.bodyRadius = 0.74f; k.skirtKnurls = 48;
                k.skirt = juce::Colour (0xff3b3d40); k.body = juce::Colour (0xffc93a30);
                k.gloss = 0.45f; k.shininess = 36.0f;
                break;
            case KnobStyle::neveGrey:
            case KnobStyle::neveBlue:
            {
                const auto body = style == KnobStyle::neveGrey ? juce::Colour (0xff9ea6ab) : juce::Colour (0xff557f9e);
                k.skirtRadius = 0.95f; k.bodyRadius = 0.72f; k.skirtKnurls = 36;
                k.skirt = body.darker (0.55f); k.body = body;
                k.gloss = 0.35f; k.shininess = 28.0f;
                pFrom = 0.15f; pTo = 0.92f; pWidth = 0.08f; pColour = juce::Colour (0xfff7f4ea);
                break;
            }
            case KnobStyle::dbxBlack:
                k.skirtRadius = 0.95f; k.bodyRadius = 0.95f; k.skirtHeight = 0.0f; k.bodyHeight = 0.42f;
                k.insertRadius = 0.58f; k.bodyFlutes = 24; k.knurlDepth = 0.05f;
                k.skirt = k.body = juce::Colour (0xff1c1c1c); k.insert = juce::Colour (0xffb9bcbf);
                k.gloss = 0.3f; k.shininess = 22.0f;
                pFrom = 0.6f; pWidth = 0.07f; pColour = juce::Colour (0xfff2f2f2);
                break;
            case KnobStyle::ureiLarge:
            case KnobStyle::ureiSmall:
                k.skirtRadius = 0.96f; k.bodyRadius = 0.82f; k.insertRadius = 0.80f; k.skirtKnurls = 60;
                k.bodyHeight = 0.22f; k.dome = 0.0f;
                k.skirt = juce::Colour (0xff181818); k.body = juce::Colour (0xff202020); k.insert = juce::Colour (0xffc4c6c8);
                k.gloss = 0.25f; k.shininess = 20.0f;
                pFrom = 0.05f; pTo = 0.78f; pColour = juce::Colour (0xff111111);
                break;
        }

        shadow (g, disc (k.skirtRadius), r * 0.12f, style == KnobStyle::ureiLarge || style == KnobStyle::ureiSmall ? 0.5f : 0.38f);
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto pixels = juce::roundToInt (2.0f * r * scale);
        g.setOpacity (1.0f);  // drawImage takes the current colour's alpha
        g.drawImage (KnobRenderer::body ((int) style, k, pixels), disc (1.0f));
        pointer (g, c, angle, r * pFrom, r * pTo, juce::jmax (2.0f, r * pWidth), pColour);
    }

    void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
    {
        const auto style = (ButtonStyle) styleOf (b, (int) ButtonStyle::neveSquare);
        const auto on = b.getToggleState();
        auto r = b.getLocalBounds().toFloat().reduced (1.5f);

        switch (style)
        {
            case ButtonStyle::neveSquare:
            {
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.fillRoundedRectangle (r.translated (0.0f, 1.5f), 2.0f);
                const auto face = on ? juce::Colour (0xfff3e2a6) : juce::Colour (0xff3c4248);
                juce::ColourGradient grad (face.brighter (0.2f), r.getX(), r.getY(), face.darker (0.3f), r.getX(), r.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (r, 2.0f);
                if (on)
                {
                    g.setColour (juce::Colour (0x40ffe08a));
                    g.drawRoundedRectangle (r.expanded (1.5f), 3.0f, 3.0f);
                }
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.drawRoundedRectangle (r, 2.0f, 1.0f);
                g.setColour (on ? juce::Colour (0xff3a2a10) : juce::Colour (0xffd8d8d8));
                g.setFont (juce::FontOptions (juce::jmin (11.0f, r.getHeight() * 0.42f), juce::Font::bold));
                g.drawText (b.getButtonText(), r, juce::Justification::centred);
                break;
            }
            case ButtonStyle::ureiWhite:
            {
                const auto inset = on || down ? 1.5f : 0.0f;
                g.setColour (juce::Colours::black);
                g.fillRect (r.expanded (1.0f));
                auto face = r.reduced (inset);
                const auto col = on ? juce::Colour (0xffd9d5c8) : juce::Colour (0xfff4f1e8);
                juce::ColourGradient grad (col, face.getX(), face.getY(), col.darker (on ? 0.25f : 0.12f), face.getX(), face.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRect (face);
                if (on)
                {
                    g.setColour (juce::Colour (0x30000000));
                    g.fillRect (face.removeFromTop (2.0f));
                }
                g.setColour (juce::Colour (0xff222222));
                g.setFont (juce::FontOptions (juce::jmin (11.0f, r.getHeight() * 0.55f), juce::Font::bold));
                g.drawText (b.getButtonText(), r, juce::Justification::centred);
                break;
            }
            case ButtonStyle::consoleKey:
            {
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.fillRoundedRectangle (r.translated (0, 1.0f), 2.5f);
                const auto face = r.reduced (on || down ? 0.5f : 0.0f);
                const auto col = on ? juce::Colour (0xffe0902a) : juce::Colour (0xff2b2b2e);
                g.setGradientFill (juce::ColourGradient (col.brighter (0.15f), face.getX(), face.getY(),
                                                         col.darker (0.3f), face.getX(), face.getBottom(), false));
                g.fillRoundedRectangle (face, 2.5f);
                if (on)
                {
                    g.setColour (juce::Colour (0x40ffb040));
                    g.drawRoundedRectangle (face.expanded (1.0f), 3.0f, 1.5f);
                }
                g.setColour (on ? juce::Colour (0xff1a1206) : juce::Colour (0xffd8d8d8));
                g.setFont (juce::FontOptions (juce::jmin (10.0f, r.getHeight() * 0.55f), juce::Font::bold));
                g.drawText (b.getButtonText(), r, juce::Justification::centred);
                break;
            }
            case ButtonStyle::dbxPush:
            {
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.fillRoundedRectangle (r.translated (0, 1.0f), 2.0f);
                g.setColour (on ? juce::Colour (0xff2a2a2a) : juce::Colour (0xff111111));
                g.fillRoundedRectangle (r, 2.0f);
                g.setColour (on ? juce::Colour (0xffffb43c) : juce::Colour (0xff8a8a8a));
                g.setFont (juce::FontOptions (juce::jmin (10.0f, r.getHeight() * 0.5f), juce::Font::bold));
                g.drawText (b.getButtonText(), r, juce::Justification::centred);
                break;
            }
            case ButtonStyle::power:
            {
                const auto d = juce::jmin (r.getWidth(), r.getHeight());
                auto lamp = r.withSizeKeepingCentre (d, d);
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.fillEllipse (lamp.translated (0.0f, 1.5f));
                g.setColour (juce::Colour (0xff1a1a1a));
                g.fillEllipse (lamp);
                const auto inner = lamp.reduced (d * 0.18f);
                if (on)
                {
                    g.setColour (juce::Colour (0x55ff2a1a));
                    g.fillEllipse (lamp.expanded (d * 0.12f));
                    juce::ColourGradient grad (juce::Colour (0xffff8a70), inner.getCentreX(), inner.getCentreY() - d * 0.1f,
                                               juce::Colour (0xffb0140a), inner.getRight(), inner.getBottom(), true);
                    g.setGradientFill (grad);
                }
                else
                {
                    g.setColour (juce::Colour (0xff4a1612));
                }
                g.fillEllipse (inner);
                break;
            }
        }

        if (highlighted && ! on && style != ButtonStyle::power)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (r);
        }
    }

    void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                        float, float, juce::Slider::SliderStyle style, juce::Slider& s)
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, pos, 0, 0, style, s);
            return;
        }

        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const auto capW = juce::jmin (34.0f, bounds.getHeight() * 0.9f);

        // Slot.
        auto slot = bounds.withSizeKeepingCentre (bounds.getWidth(), 6.0f);
        g.setColour (juce::Colour (0xff050505));
        g.fillRoundedRectangle (slot, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (slot.translated (0, 1.0f), 3.0f, 1.0f);

        // Fader cap: grey moulded cap with a white index line.
        auto cap = juce::Rectangle<float> (capW, bounds.getHeight()).withCentre ({ pos, bounds.getCentreY() });
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (cap.translated (2.0f, 3.0f), 3.0f);
        juce::ColourGradient grad (juce::Colour (0xff8e9296), cap.getX(), cap.getY(), juce::Colour (0xff3d4044), cap.getRight(), cap.getY(), false);
        grad.addColour (0.45, juce::Colour (0xffc6c9cc));
        grad.addColour (0.55, juce::Colour (0xff9da1a5));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (cap, 3.0f, 1.0f);
        for (float k : { 0.25f, 0.75f })
        {
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.drawVerticalLine (juce::roundToInt (cap.getX() + cap.getWidth() * k), cap.getY() + 3.0f, cap.getBottom() - 3.0f);
        }
        g.setColour (juce::Colours::white);
        g.fillRect (juce::Rectangle<float> (2.0f, cap.getHeight() - 6.0f).withCentre (cap.getCentre()));
    }

    void drawScale (juce::Graphics& g, juce::Point<float> c, float radius,
                    const std::vector<std::pair<float, juce::String>>& marks,
                    juce::Colour colour, float fontHeight, bool ticks)
    {
        g.setColour (colour);
        g.setFont (juce::FontOptions (fontHeight, juce::Font::bold));
        for (const auto& [prop, label] : marks)
        {
            const auto a = startAngle + prop * (endAngle - startAngle);
            const auto sx = std::sin (a), sy = -std::cos (a);
            if (ticks)
                g.drawLine (c.x + sx * radius * 0.98f, c.y + sy * radius * 0.98f,
                            c.x + sx * radius * 1.12f, c.y + sy * radius * 1.12f, 1.2f);
            if (label.isNotEmpty())
            {
                const auto lr = radius * 1.12f + fontHeight * 0.95f;
                const auto w = (float) label.length() * fontHeight * 0.62f + 4.0f;
                g.drawText (label, juce::Rectangle<float> (w, fontHeight + 2.0f).withCentre ({ c.x + sx * lr, c.y + sy * lr }),
                            juce::Justification::centred, false);
            }
        }
    }

    void drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       juce::Colour colour, float height, bool bold, juce::Justification just)
    {
        g.setFont (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
        g.setColour (colour);
        g.drawText (text, area, just, false);
    }

    void drawScrew (juce::Graphics& g, juce::Point<float> c, float radius)
    {
        const auto r = juce::Rectangle<float> (radius * 2, radius * 2).withCentre (c);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (r.translated (0.6f, 1.0f).expanded (0.8f));
        juce::ColourGradient grad (juce::Colour (0xffe2e4e6), r.getX(), r.getY(), juce::Colour (0xff6a6d70), r.getRight(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillEllipse (r);
        g.setColour (juce::Colour (0xff3a3c3e));
        juce::Path slot;
        slot.addRectangle (-radius * 0.7f, -radius * 0.12f, radius * 1.4f, radius * 0.24f);
        g.fillPath (slot, juce::AffineTransform::rotation (0.6f).translated (c));
        slot.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi));
        g.fillPath (slot, juce::AffineTransform::rotation (0.6f).translated (c));
    }
}
