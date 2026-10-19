// Ported from the owner's Vocal Chain plugin (mingusdynasty/rare-luxuriousVocalChain, rebuild/engine at 7502ba1),
// so the two plugins share one rack look. Namespace renamed to kit.
#include "Widgets.h"

namespace kit
{
    // ---------------------------------------------------------------- VU meter

    VuMeter::VuMeter (std::function<float()> src, juce::String legendText)
        : source (std::move (src)), legend (std::move (legendText))
    {
        // Start at rest on the current reading rather than swinging in from the stop.
        position = deflectionFor (source != nullptr ? source() : -40.0f);
    }

    float VuMeter::deflectionFor (float vuDb)
    {
        // A VU meter's scale is linear in voltage, with +3 VU at full scale.
        return juce::jlimit (-0.03f, 1.06f, std::pow (10.0f, (vuDb - 3.0f) / 20.0f));
    }

    void VuMeter::tick (float seconds)
    {
        const auto dt = juce::jlimit (0.001f, 0.05f, seconds);

        // Second-order needle: damping 0.8 (about 1.5% overshoot) and a natural
        // frequency that brings it to 99% of a step in roughly 300 ms.
        const auto target = deflectionFor (source != nullptr ? source() : -40.0f);
        constexpr float wn = 2.0f * juce::MathConstants<float>::pi * 2.3f, zeta = 0.8f;
        const int steps = 4;
        const auto h = (float) dt / (float) steps;
        for (int i = 0; i < steps; ++i)
        {
            const auto accel = wn * wn * (target - position) - 2.0f * zeta * wn * velocity;
            velocity += accel * h;
            position += velocity * h;
        }
        position = juce::jlimit (-0.04f, 1.08f, position);
        repaint();
    }

    void VuMeter::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        // Bezel.
        g.setColour (juce::Colour (0xff0b0b0b));
        g.fillRoundedRectangle (bounds, 4.0f);
        auto face = bounds.reduced (4.0f);

        juce::ColourGradient lamp (juce::Colour (0xfffff3c8), face.getCentreX(), face.getY() + face.getHeight() * 0.2f,
                                   juce::Colour (0xffe1c98a), face.getX(), face.getBottom(), true);
        g.setGradientFill (lamp);
        g.fillRoundedRectangle (face, 2.0f);

        const auto pivot = juce::Point<float> (face.getCentreX(), face.getBottom() + face.getHeight() * 0.42f);
        const auto radius = face.getHeight() * 1.12f;
        const auto halfSweep = juce::degreesToRadians (46.0f);
        auto angleFor = [&] (float deflection) { return -halfSweep + deflection * 2.0f * halfSweep; };

        g.saveState();
        g.reduceClipRegion (face.toNearestInt());

        // Red zone above 0 VU.
        {
            juce::Path red;
            red.addCentredArc (pivot.x, pivot.y, radius * 0.9f, radius * 0.9f, 0.0f,
                               angleFor (deflectionFor (0.0f)), angleFor (1.0f), true);
            g.setColour (juce::Colour (0xffc0281c));
            g.strokePath (red, juce::PathStrokeType (face.getHeight() * 0.06f));
        }

        // Scale arc and marks.
        {
            juce::Path arc;
            arc.addCentredArc (pivot.x, pivot.y, radius * 0.86f, radius * 0.86f, 0.0f, angleFor (0.0f), angleFor (1.0f), true);
            g.setColour (juce::Colour (0xff1b1b1b));
            g.strokePath (arc, juce::PathStrokeType (1.0f));
        }

        const auto fontH = juce::jmax (7.0f, face.getHeight() * 0.13f);
        g.setFont (juce::FontOptions (fontH, juce::Font::bold));
        for (auto db : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
        {
            const auto a = angleFor (deflectionFor (db));
            const auto sx = std::sin (a), sy = -std::cos (a);
            g.setColour (db > 0.0f ? juce::Colour (0xffc0281c) : juce::Colour (0xff1b1b1b));
            g.drawLine (pivot.x + sx * radius * 0.86f, pivot.y + sy * radius * 0.86f,
                        pivot.x + sx * radius * 0.94f, pivot.y + sy * radius * 0.94f, 1.2f);
            const auto label = db > 0.0f ? "+" + juce::String ((int) db) : juce::String ((int) std::abs (db));
            const auto lp = juce::Point<float> (pivot.x + sx * radius * 1.03f, pivot.y + sy * radius * 1.03f);
            g.drawText (label, juce::Rectangle<float> (fontH * 2.0f, fontH).withCentre (lp), juce::Justification::centred);
        }

        g.setColour (juce::Colour (0xff1b1b1b));
        g.setFont (juce::FontOptions (fontH * 1.25f, juce::Font::bold));
        g.drawText (legend, face.withTrimmedTop (face.getHeight() * 0.48f).withHeight (fontH * 1.4f), juce::Justification::centred);

        // Needle.
        {
            const auto a = angleFor (position);
            const auto tip = juce::Point<float> (pivot.x + std::sin (a) * radius * 0.97f, pivot.y - std::cos (a) * radius * 0.97f);
            g.setColour (juce::Colours::black.withAlpha (0.2f));
            g.drawLine (juce::Line<float> (pivot.translated (2.0f, 1.0f), tip.translated (2.0f, 1.0f)), 1.6f);
            g.setColour (juce::Colour (0xff121212));
            g.drawLine (juce::Line<float> (pivot, tip), 1.3f);
        }

        // Glass reflection.
        {
            juce::ColourGradient glass (juce::Colours::white.withAlpha (0.28f), face.getX(), face.getY(),
                                        juce::Colours::white.withAlpha (0.0f), face.getX(), face.getCentreY(), false);
            g.setGradientFill (glass);
            g.fillRect (face.withHeight (face.getHeight() * 0.45f));
        }
        g.restoreState();

        // Needle pivot cover.
        g.setColour (juce::Colour (0xff0b0b0b));
        g.fillRect (face.withTop (face.getBottom() - face.getHeight() * 0.14f));
    }

    // ---------------------------------------------------------------- LED

    Led::Led (juce::Colour c, std::function<bool()> l) : colour (c), lit (std::move (l)) {}

    void Led::tick()
    {
        const auto now = lit != nullptr && lit();
        if (now != state) { state = now; repaint(); }
    }

    void Led::paint (juce::Graphics& g)
    {
        const auto d = (float) juce::jmin (getWidth(), getHeight());
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (d, d).reduced (1.0f);
        g.setColour (juce::Colour (0xff101010));
        g.fillEllipse (r);
        r = r.reduced (d * 0.12f);
        if (state)
        {
            g.setColour (colour.withAlpha (0.35f));
            g.fillEllipse (r.expanded (d * 0.18f));
            juce::ColourGradient grad (colour.brighter (0.8f), r.getCentreX(), r.getCentreY() - d * 0.1f, colour, r.getRight(), r.getBottom(), true);
            g.setGradientFill (grad);
        }
        else
        {
            g.setColour (colour.darker (0.85f));
        }
        g.fillEllipse (r);
    }

    // ---------------------------------------------------------------- choice buttons

    ChoiceButtons::ChoiceButtons (juce::AudioProcessorValueTreeState& s, const juce::String& id,
                                  const juce::StringArray& labels, ButtonStyle style, bool isVertical, int all,
                                  bool isReversed)
        : state (s), paramId (id), param (s.getParameter (id)), vertical (isVertical), reversed (isReversed), allIndex (all)
    {
        for (int i = 0; i < labels.size(); ++i)
        {
            if (i == allIndex)
                continue;
            auto* b = buttons.add (new juce::ToggleButton (labels[i]));
            setStyle (*b, style);
            b->setClickingTogglesState (false);
            b->onClick = [this, i]
            {
                const auto mods = juce::ModifierKeys::getCurrentModifiers();
                select (allIndex >= 0 && mods.isShiftDown() ? allIndex : i);
            };
            addAndMakeVisible (b);
        }

        if (allIndex >= 0)
            for (auto* b : buttons)
                b->setTooltip ("Shift-click to push all the ratio buttons in");

        state.addParameterListener (paramId, this);
        refresh();
    }

    ChoiceButtons::~ChoiceButtons()
    {
        state.removeParameterListener (paramId, this);
    }

    void ChoiceButtons::select (int index)
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
        param->endChangeGesture();
    }

    void ChoiceButtons::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }
    void ChoiceButtons::handleAsyncUpdate() { refresh(); }

    void ChoiceButtons::refresh()
    {
        const auto index = juce::roundToInt (param->convertFrom0to1 (param->getValue()));
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (index == allIndex || index == i, juce::dontSendNotification);
    }

    void ChoiceButtons::resized()
    {
        auto r = getLocalBounds();
        const auto n = buttons.size();
        const auto gap = 3;
        for (int k = 0; k < n; ++k)
        {
            const auto i = reversed ? n - 1 - k : k;
            if (vertical)
            {
                const auto h = (getHeight() - gap * (n - 1)) / n;
                buttons[i]->setBounds (r.removeFromTop (h));
                r.removeFromTop (gap);
            }
            else
            {
                const auto w = (getWidth() - gap * (n - 1)) / n;
                buttons[i]->setBounds (r.removeFromLeft (w));
                r.removeFromLeft (gap);
            }
        }
    }

    // ---------------------------------------------------------------- peak meter

    PeakMeter::PeakMeter (std::function<float (int)> take) : takePeakDb (std::move (take)) {}

    float PeakMeter::proportionFor (float db)
    {
        // Same shape as the fader's scale: -60 dB at the left, +12 dB at the right.
        return juce::jlimit (0.0f, 1.0f, std::pow ((db + 60.0f) / 72.0f, 1.0f / 2.5f));
    }

    void PeakMeter::tick()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto p = takePeakDb != nullptr ? takePeakDb (ch) : -100.0f;
            // Instant attack, about 20 dB/s fall, 1 s peak hold.
            level[(size_t) ch] = juce::jmax (p, level[(size_t) ch] - 20.0f / 30.0f);
            if (p >= hold[(size_t) ch]) { hold[(size_t) ch] = p; holdFrames[(size_t) ch] = 30; }
            else if (--holdFrames[(size_t) ch] <= 0) hold[(size_t) ch] = juce::jmax (-100.0f, hold[(size_t) ch] - 1.0f);
        }
        repaint();
    }

    void PeakMeter::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff050505));
        g.fillRoundedRectangle (r, 2.0f);
        r = r.reduced (2.0f);

        const auto segments = 48;
        const auto segW = r.getWidth() / (float) segments;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto row = r.withHeight (r.getHeight() * 0.5f - 1.0f).withY (r.getY() + (float) ch * r.getHeight() * 0.5f + (float) ch);
            const auto lit = proportionFor (level[(size_t) ch]);
            const auto held = proportionFor (hold[(size_t) ch]);
            for (int i = 0; i < segments; ++i)
            {
                const auto p0 = (float) i / (float) segments;
                const auto colour = p0 >= proportionFor (0.0f) ? juce::Colour (0xffff3b2a)
                                    : p0 >= proportionFor (-6.0f) ? juce::Colour (0xffffc23a)
                                                                  : juce::Colour (0xff3ee06a);
                const auto on = p0 < lit || (p0 <= held && p0 + 1.0f / (float) segments > held && hold[(size_t) ch] > -60.0f);
                g.setColour (on ? colour : colour.withAlpha (0.12f));
                g.fillRect (row.getX() + (float) i * segW + 0.5f, row.getY(), segW - 1.0f, row.getHeight());
            }
        }
    }
}
