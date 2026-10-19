#include "Rack.h"

#include <random>

namespace bbui::rack
{
namespace
{
constexpr int kEar = 34;       // rack ear width
constexpr int kTitle = 34;     // title band height on the plate

juce::Colour steel (float shade) { return juce::Colour::fromFloatRGBA (shade, shade, shade * 1.02f, 1.0f); }
}

juce::Font engraving (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (0.08f);
}

void paintScrew (juce::Graphics& g, juce::Point<float> c, float r)
{
    g.setGradientFill (juce::ColourGradient (steel (0.82f), c.x - r, c.y - r, steel (0.38f), c.x + r, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 0.8f);
    // Phillips head
    g.setColour (steel (0.25f));
    g.drawLine (c.x - r * 0.55f, c.y - r * 0.15f, c.x + r * 0.55f, c.y + r * 0.15f, 1.6f);
    g.drawLine (c.x - r * 0.15f, c.y + r * 0.55f, c.x + r * 0.15f, c.y - r * 0.55f, 1.6f);
}

// ---------------------------------------------------------------- look

RackLook::RackLook()
{
    setColour (juce::BubbleComponent::backgroundColourId, col::bg.withAlpha (0.95f));
    setColour (juce::BubbleComponent::outlineColourId, col::amber);
    setColour (juce::TooltipWindow::textColourId, col::amber);
}

juce::Colour RackLook::legendFor (const juce::Component& c) const
{
    for (auto* p = c.getParentComponent(); p != nullptr; p = p->getParentComponent())
        if (auto* u = dynamic_cast<const Unit*> (p)) return u->finish().legend;
    return col::legend;
}

void RackLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto style = s.getProperties()["style"].toString();
    const auto legend = legendFor (s);
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    const bool hasScale = s.getProperties().contains ("scale");
    // with a scale, leave room for its legends: up to 20 px either side of the tick, 10 px above
    const float r = hasScale ? juce::jmin (area.getWidth() * 0.5f - 32.0f, area.getHeight() * 0.5f - 17.0f)
                             : juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 4.0f;
    const auto c = area.getCentre();
    const float angle = start + pos * (end - start);

    // printed scale: ticks round the knob and legends at the given positions
    g.setColour (legend.withAlpha (0.85f));
    if (hasScale)
    {
        g.setFont (engraving (8.5f, false));
        for (auto item : juce::StringArray::fromTokens (s.getProperties()["scale"].toString(), "|", ""))
        {
            const float p = item.upToFirstOccurrenceOf (":", false, false).getFloatValue();
            const auto text = item.fromFirstOccurrenceOf (":", false, false);
            const float a = start + p * (end - start);
            g.drawLine ({ c.getPointOnCircumference (r + 2.0f, a), c.getPointOnCircumference (r + 6.0f, a) }, 1.2f);
            const auto tp = c.getPointOnCircumference (r + 12.0f, a);
            g.drawText (text, juce::Rectangle<float> (40.0f, 10.0f).withCentre (tp), juce::Justification::centred);
        }
        if (! s.getProperties().getWithDefault ("detents", false))
            for (int i = 0; i <= 20; ++i)
            {
                const float a = start + (float) i / 20.0f * (end - start);
                g.drawLine ({ c.getPointOnCircumference (r + 2.0f, a), c.getPointOnCircumference (r + 3.5f, a) }, 0.8f);
            }
    }

    if (style == "chicken")
    {
        // black bakelite pointer knob: round skirt, long tapered pointer with a white line
        const float sr = r * 0.58f;
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (c.x - sr + 2, c.y - sr + 3, sr * 2, sr * 2);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3c), c.x, c.y - sr, juce::Colour (0xff0e0e0f), c.x, c.y + sr, false));
        g.fillEllipse (c.x - sr, c.y - sr, sr * 2, sr * 2);
        juce::Path ptr;
        ptr.startNewSubPath (0.0f, -r);
        ptr.lineTo (r * 0.33f, r * 0.15f);
        ptr.quadraticTo (0.0f, r * 0.42f, -r * 0.33f, r * 0.15f);
        ptr.closeSubPath();
        const auto t = juce::AffineTransform::rotation (angle).translated (c.x, c.y);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillPath (ptr, t.translated (1.5f, 2.5f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a4d), c.x, c.y - r, juce::Colour (0xff111112), c.x, c.y + r * 0.4f, false));
        g.fillPath (ptr, t);
        g.setColour (juce::Colour (0xffeeeae0));
        g.drawLine ({ c.getPointOnCircumference (r * 0.2f, angle), c.getPointOnCircumference (r * 0.9f, angle) }, 2.0f);
        return;
    }

    // machined aluminium: knurled skirt, brushed cap, engraved pointer line
    const float kr = style == "small" ? r * 0.82f : r;
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (c.x - kr + 2, c.y - kr + 3, kr * 2, kr * 2);
    g.setGradientFill (juce::ColourGradient (steel (0.78f), c.x - kr, c.y - kr, steel (0.30f), c.x + kr, c.y + kr, false));
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    for (int i = 0; i < 48; ++i)
    {
        const float a = (float) i / 48.0f * juce::MathConstants<float>::twoPi;
        g.drawLine ({ c.getPointOnCircumference (kr * 0.86f, a), c.getPointOnCircumference (kr - 0.5f, a) }, 0.7f);
    }
    const float cr = kr * 0.78f;
    juce::ColourGradient cap (steel (0.92f), c.x - cr * 0.6f, c.y - cr, steel (0.55f), c.x + cr * 0.6f, c.y + cr, true);
    cap.addColour (0.5, steel (0.74f));
    g.setGradientFill (cap);
    g.fillEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2, 0.8f);
    // the spun finish: faint concentric rings
    g.setColour (juce::Colours::black.withAlpha (0.05f));
    for (float rr = cr * 0.2f; rr < cr; rr += 2.0f) g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, 0.6f);
    g.setColour (style == "small" ? juce::Colour (0xff1b1b1b) : juce::Colour (0xffd8402e));
    g.drawLine ({ c.getPointOnCircumference (cr * 0.25f, angle), c.getPointOnCircumference (cr * 0.95f, angle) }, 2.4f);
}

void RackLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    const auto legend = legendFor (b);
    auto r = b.getLocalBounds().toFloat();
    const auto up = b.getProperties()["up"].toString(), down = b.getProperties()["down"].toString();
    g.setFont (engraving (8.5f, true));
    g.setColour (legend.withAlpha (b.getToggleState() ? 1.0f : 0.55f));
    g.drawText (up, r.removeFromTop (11.0f), juce::Justification::centred);
    g.setColour (legend.withAlpha (b.getToggleState() ? 0.55f : 1.0f));
    g.drawText (down, r.removeFromBottom (11.0f), juce::Justification::centred);

    // hex nut and bezel
    const auto c = r.getCentre();
    const float nr = juce::jmin (r.getWidth(), r.getHeight()) * 0.26f;
    juce::Path nut;
    nut.addPolygon (c, 6, nr * 1.25f, 0.0f);
    g.setGradientFill (juce::ColourGradient (steel (0.85f), c.x - nr, c.y - nr, steel (0.40f), c.x + nr, c.y + nr, false));
    g.fillPath (nut);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.strokePath (nut, juce::PathStrokeType (0.8f));
    g.setColour (steel (0.2f));
    g.fillEllipse (c.x - nr * 0.45f, c.y - nr * 0.45f, nr * 0.9f, nr * 0.9f);

    // the bat handle, thrown up or down
    const bool on = b.getToggleState();
    const float len = juce::jmin (r.getHeight() * 0.5f - nr * 0.5f, nr * 2.6f);
    const auto tip = c.translated (0.0f, on ? -len : len);
    juce::Path bat;
    bat.startNewSubPath (c.x - nr * 0.32f, c.y);
    bat.lineTo (tip.x - nr * 0.42f, tip.y);
    bat.lineTo (tip.x + nr * 0.42f, tip.y);
    bat.lineTo (c.x + nr * 0.32f, c.y);
    bat.closeSubPath();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillPath (bat, juce::AffineTransform::translation (2.0f, 2.0f));
    g.setGradientFill (juce::ColourGradient (steel (0.95f), tip.x - nr, tip.y, steel (0.45f), tip.x + nr, tip.y, false));
    g.fillPath (bat);
    g.setColour (steel (0.98f));
    g.fillEllipse (tip.x - nr * 0.48f, tip.y - nr * 0.48f, nr * 0.96f, nr * 0.96f);
}

// ---------------------------------------------------------------- unit

Unit::Unit (juce::String t, juce::String m, Finish f) : title (std::move (t)), model (std::move (m)), fin (f)
{
    setLookAndFeel (&look);
}

void Unit::resized()
{
    texture = {};
    legends.clear();
    rules.clear();
    layout();
}

juce::Rectangle<int> Unit::lampArea() const
{
    return plate().removeFromTop (kTitle).reduced (16, 0).removeFromRight (16).withSizeKeepingCentre (14, 14);
}

void Unit::tick()
{
    const bool now = isIn && isIn();
    if (now != lit)
    {
        lit = now;
        repaint (lampArea().expanded (6));
    }
}

void Unit::mouseDown (const juce::MouseEvent& e)
{
    if (toggleIn && lampArea().expanded (5).contains (e.getPosition()))
    {
        toggleIn();
        tick();
    }
}

juce::Rectangle<int> Unit::plate() const { return getLocalBounds().reduced (kEar, 0); }

juce::Rectangle<int> Unit::content() const { return plate().withTrimmedTop (kTitle).reduced (14, 6); }

void Unit::engrave (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, float size, juce::Justification j, bool bold) const
{
    g.setFont (engraving (size, bold));
    // engraved: a dark lower lip under the legend
    g.setColour ((fin.brushed ? juce::Colours::black : juce::Colours::white).withAlpha (0.25f));
    g.drawText (text, r.translated (0, 1), j);
    g.setColour (fin.legend);
    g.drawText (text, r, j);
}

void Unit::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    // ears: dark steel with slotted holes and screws
    for (auto ear : { b.withWidth ((float) kEar + 6), b.withTrimmedLeft (b.getWidth() - (float) kEar - 6) })
    {
        g.setGradientFill (juce::ColourGradient (steel (0.34f), ear.getX(), 0, steel (0.22f), ear.getRight(), 0, false));
        g.fillRoundedRectangle (ear, 3.0f);
        for (float yy : { 22.0f, b.getHeight() - 22.0f })
        {
            const auto sc = juce::Point<float> (ear.getCentreX(), yy);
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.fillRoundedRectangle (juce::Rectangle<float> (18.0f, 11.0f).withCentre (sc), 5.5f);
            paintScrew (g, sc, 6.5f);
        }
    }

    // faceplate, with a brushed texture cached per size
    const auto p = plate().toFloat();
    if (texture.isNull() || texture.getWidth() != plate().getWidth() || texture.getHeight() != plate().getHeight())
    {
        texture = juce::Image (juce::Image::ARGB, juce::jmax (1, plate().getWidth()), juce::jmax (1, plate().getHeight()), true);
        juce::Graphics tg (texture);
        tg.setGradientFill (juce::ColourGradient (fin.top, 0, 0, fin.bottom, 0, (float) texture.getHeight(), false));
        tg.fillAll();
        if (fin.brushed)
        {
            std::mt19937 rng (7);
            std::uniform_real_distribution<float> u (0.0f, 1.0f);
            for (int yy = 0; yy < texture.getHeight(); ++yy)
            {
                tg.setColour ((u (rng) < 0.5f ? juce::Colours::white : juce::Colours::black).withAlpha (0.015f + 0.03f * u (rng)));
                tg.drawHorizontalLine (yy, 0.0f, (float) texture.getWidth());
            }
        }
        // bevel
        tg.setColour (juce::Colours::white.withAlpha (0.18f));
        tg.drawHorizontalLine (0, 0.0f, (float) texture.getWidth());
        tg.setColour (juce::Colours::black.withAlpha (0.45f));
        tg.drawHorizontalLine (texture.getHeight() - 1, 0.0f, (float) texture.getWidth());
    }
    g.drawImageAt (texture, plate().getX(), plate().getY());
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRect (p, 1.0f);

    // title band: name left, model and the IN lamp right, a thin rule under both
    auto tb = plate().removeFromTop (kTitle).reduced (16, 0);
    engrave (g, title, tb, 15.0f, juce::Justification::centredLeft);
    if (isIn)
    {
        const auto l = lampArea().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillEllipse (l.expanded (2.0f));
        juce::ColourGradient jewel (lit ? fin.accent.brighter (0.6f) : fin.accent.darker (2.2f), l.getCentreX() - 2, l.getCentreY() - 3,
                                    lit ? fin.accent : fin.accent.darker (3.0f), l.getRight(), l.getBottom(), true);
        g.setGradientFill (jewel);
        g.fillEllipse (l);
        if (lit)
        {
            g.setColour (fin.accent.withAlpha (0.3f));
            g.fillEllipse (l.expanded (4.0f));
        }
        tb.removeFromRight (22);
        engrave (g, "IN", tb.removeFromRight (16), 9.0f, juce::Justification::centredRight);
        tb.removeFromRight (14);
    }
    engrave (g, model, tb, 10.0f, juce::Justification::centredRight, false);
    g.setColour (fin.legend.withAlpha (0.35f));
    g.drawHorizontalLine (plate().getY() + kTitle - 1, (float) plate().getX() + 14, (float) plate().getRight() - 14);

    for (const auto& r : rules)
    {
        g.setColour (fin.legend.withAlpha (0.3f));
        g.drawLine (r, 1.0f);
    }
    for (const auto& l : legends) engrave (g, l.text, l.area, l.size, l.just, l.bold);
    paintPlate (g);
}

// ---------------------------------------------------------------- VU

VuMeter::VuMeter (juce::String l) : label (std::move (l)) { setOpaque (true); }

void VuMeter::push (float meanSquareRelOp, float fps)
{
    // VU ballistics: a 300 ms rise to 99% is close to a 65 ms first-order lag on the rectified level
    const float c = 1.0f - std::exp (-1.0f / (0.065f * fps));
    ms += (meanSquareRelOp - ms) * c;
    const float db = ms > 1e-6f ? 10.0f * std::log10 (ms) : -40.0f;
    const float prev = needle;
    needle = juce::jlimit (-22.0f, 4.0f, db);
    if (std::abs (needle - prev) > 0.01f) repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff141414));
    g.fillRect (b);
    auto face = b.reduced (5.0f);
    // backlit cream face
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7e7b8), face.getCentreX(), face.getY() + face.getHeight() * 0.35f,
                                             juce::Colour (0xffd9b86a), face.getX(), face.getBottom(), true));
    g.fillRoundedRectangle (face, 3.0f);

    // the scale: VU positions follow the classic non-linear spacing (log of the voltage)
    // the arc spans +-0.82 rad and must fit the face with room for its legends
    const float radius = juce::jmin (face.getHeight() * 1.02f, (face.getWidth() * 0.5f - 20.0f) / std::sin (0.82f));
    const juce::Point<float> pivot (face.getCentreX(), face.getY() + face.getHeight() * 0.3f + radius);
    auto angleOf = [] (float db)
    {
        const float v = std::pow (10.0f, db / 20.0f);   // 0 VU at 1.0
        const float t = (v - std::pow (10.0f, -20.0f / 20.0f)) / (std::pow (10.0f, 3.0f / 20.0f) - std::pow (10.0f, -20.0f / 20.0f));
        return juce::jmap (t, -0.82f, 0.82f);
    };
    juce::Path arc, red;
    arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleOf (-20.0f), angleOf (0.0f), true);
    red.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleOf (0.0f), angleOf (3.0f), true);
    g.setColour (juce::Colour (0xff1d1a14));
    g.strokePath (arc, juce::PathStrokeType (1.2f));
    g.setColour (juce::Colour (0xffc0261c));
    g.strokePath (red, juce::PathStrokeType (4.0f));
    g.setFont (engraving (9.0f, false));
    for (float db : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
    {
        const float a = angleOf (db);
        g.setColour (db > 0.0f ? juce::Colour (0xffc0261c) : juce::Colour (0xff1d1a14));
        g.drawLine ({ pivot.getPointOnCircumference (radius, a), pivot.getPointOnCircumference (radius + 6.0f, a) }, 1.2f);
        const auto tp = pivot.getPointOnCircumference (radius + 14.0f, a);
        g.drawText (juce::String (std::abs ((int) db)), juce::Rectangle<float> (22.0f, 10.0f).withCentre (tp), juce::Justification::centred);
    }
    g.setColour (juce::Colour (0xff1d1a14));
    g.setFont (engraving (15.0f, true));
    g.drawText ("VU", face.withTrimmedTop (face.getHeight() * 0.48f).withHeight (18.0f), juce::Justification::centred);
    g.setFont (engraving (8.0f, false));
    g.drawText (label, face.withTrimmedTop (face.getHeight() * 0.48f + 18.0f).withHeight (12.0f), juce::Justification::centred);

    // needle and its shadow, clipped to the face
    g.saveState();
    g.reduceClipRegion (face.toNearestInt());
    const float a = angleOf (needle);
    const auto tip = pivot.getPointOnCircumference (radius + 4.0f, a);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawLine ({ pivot.translated (3.0f, 3.0f), tip.translated (3.0f, 3.0f) }, 1.6f);
    g.setColour (juce::Colour (0xff111111));
    g.drawLine ({ pivot, tip }, 1.4f);
    g.restoreState();

    // glass: a soft highlight across the top
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), face.getX(), face.getY(),
                                             juce::Colours::transparentWhite, face.getX(), face.getCentreY(), false));
    g.fillRoundedRectangle (face, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (face, 3.0f, 1.5f);
}

// ---------------------------------------------------------------- gain reduction

GrMeter::GrMeter (juce::String l, float m) : label (std::move (l)), maxDb (m) { setOpaque (true); }

void GrMeter::push (float reductionDb, float fps)
{
    // the movement's ballistics: quick to fall (about 10 ms), 300 ms to come back
    const float target = juce::jlimit (0.0f, maxDb * 1.05f, reductionDb);
    const float c = 1.0f - std::exp (-1.0f / ((target > needle ? 0.01f : 0.3f) * fps));
    const float prev = needle;
    needle += (target - needle) * c;
    if (std::abs (needle - prev) > 0.005f) repaint();
}

void GrMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff141414));
    g.fillRect (b);
    auto face = b.reduced (5.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7e7b8), face.getCentreX(), face.getY() + face.getHeight() * 0.35f,
                                             juce::Colour (0xffd9b86a), face.getX(), face.getBottom(), true));
    g.fillRoundedRectangle (face, 3.0f);

    const float radius = juce::jmin (face.getHeight() * 1.02f, (face.getWidth() * 0.5f - 20.0f) / std::sin (0.82f));
    const juce::Point<float> pivot (face.getCentreX(), face.getY() + face.getHeight() * 0.3f + radius);
    auto angleOf = [this] (float db) { return juce::jmap (std::sqrt (juce::jlimit (0.0f, 1.1f, db / maxDb)), 0.82f, -0.82f); };
    juce::Path arc;
    arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleOf (maxDb), angleOf (0.0f), true);
    const juce::Colour ink (0xff1d1a14);
    g.setColour (ink);
    g.strokePath (arc, juce::PathStrokeType (1.2f));
    g.setFont (engraving (9.0f, false));
    for (float db : { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 10.0f, 12.0f, 15.0f, 20.0f })
    {
        if (db > maxDb) continue;
        const float a = angleOf (db);
        g.drawLine ({ pivot.getPointOnCircumference (radius, a), pivot.getPointOnCircumference (radius + 6.0f, a) }, 1.2f);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (22.0f, 10.0f).withCentre (pivot.getPointOnCircumference (radius + 14.0f, a)),
                    juce::Justification::centred);
    }
    g.setFont (engraving (12.0f, true));
    g.drawText ("GAIN REDUCTION", face.withTrimmedTop (face.getHeight() * 0.48f).withHeight (16.0f), juce::Justification::centred);
    g.setFont (engraving (8.0f, false));
    g.drawText (label, face.withTrimmedTop (face.getHeight() * 0.48f + 16.0f).withHeight (12.0f), juce::Justification::centred);

    g.saveState();
    g.reduceClipRegion (face.toNearestInt());
    const float a = angleOf (needle);
    const auto tip = pivot.getPointOnCircumference (radius + 4.0f, a);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawLine ({ pivot.translated (3.0f, 3.0f), tip.translated (3.0f, 3.0f) }, 1.6f);
    g.setColour (juce::Colour (0xff111111));
    g.drawLine ({ pivot, tip }, 1.4f);
    g.restoreState();

    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), face.getX(), face.getY(),
                                             juce::Colours::transparentWhite, face.getX(), face.getCentreY(), false));
    g.fillRoundedRectangle (face, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (face, 3.0f, 1.5f);
}

// ---------------------------------------------------------------- blank

BlankUnit::BlankUnit (juce::String t, juce::String w)
    : Unit (std::move (t), "", { juce::Colour (0xff2a2c2f), juce::Colour (0xff1d1f21), juce::Colour (0xffb9b4a8), col::amber, true }),
      when (std::move (w)) {}

void BlankUnit::paintPlate (juce::Graphics& g)
{
    auto c = content();
    // ventilation slots
    const int rows = 3, slotsPerRow = 14;
    auto vents = c.withSizeKeepingCentre (juce::jmin (c.getWidth(), 560), 70).translated (0, -30);
    for (int rr = 0; rr < rows; ++rr)
        for (int s = 0; s < slotsPerRow; ++s)
        {
            const float w = (float) vents.getWidth() / slotsPerRow;
            const auto slot = juce::Rectangle<float> ((float) vents.getX() + s * w + 4.0f, (float) vents.getY() + rr * 24.0f, w - 8.0f, 9.0f);
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.fillRoundedRectangle (slot, 4.5f);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawRoundedRectangle (slot.translated (0.0f, 1.0f), 4.5f, 0.8f);
        }
    engrave (g, "COMING IN " + when.toUpperCase(), c.withTrimmedTop (c.getHeight() / 2 + 40).withHeight (20), 12.0f);
}
} // namespace bbui::rack
