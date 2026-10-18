#include "ChainStrip.h"

namespace bbui
{
namespace
{
constexpr float kArrow = 18.0f;   // the gap between modules, with its signal-flow arrow
}

juce::Rectangle<float> ChainStrip::cell (int position) const
{
    const int n = juce::jmax (1, (int) slots.size());
    const float w = ((float) getWidth() - kArrow * (float) (n - 1)) / (float) n;
    return { (float) position * (w + kArrow), 0.0f, w, (float) getHeight() };
}

juce::Rectangle<float> ChainStrip::lamp (juce::Rectangle<float> c) const
{
    return { c.getRight() - 26.0f, c.getY() + 9.0f, 16.0f, 16.0f };
}

int ChainStrip::positionAt (float x) const
{
    for (int p = 0; p < (int) slots.size(); ++p)
        if (x < cell (p).getRight() + kArrow * 0.5f) return p;
    return (int) slots.size() - 1;
}

void ChainStrip::paint (juce::Graphics& g)
{
    if (! getOrder) return;
    auto order = getOrder();

    // while dragging, show the order the drop would produce
    if (dragging && dragFrom >= 0)
    {
        order.erase (order.begin() + dragFrom);
        order.insert (order.begin() + positionAt (dragX), dragSlot);
    }

    for (int p = 0; p < (int) order.size(); ++p)
    {
        const int s = order[(size_t) p];
        auto c = cell (p);
        if (p > 0)
        {
            const float ax = c.getX() - kArrow * 0.5f, ay = c.getCentreY();
            juce::Path arrow;
            arrow.addTriangle (ax - 4.0f, ay - 5.0f, ax - 4.0f, ay + 5.0f, ax + 4.0f, ay);
            g.setColour (col::faint);
            g.fillPath (arrow);
        }
        const bool lifted = dragging && s == dragSlot;
        if (lifted) c = c.withX (dragX - grabOffset);
        const bool bypassed = isBypassed && isBypassed (s);
        const auto& info = slots[(size_t) s];

        g.setColour (lifted ? col::panelHi.brighter (0.1f) : col::panelHi);
        g.fillRoundedRectangle (c, 5.0f);
        g.setColour (lifted || s == selected ? col::amber : col::edge);
        g.drawRoundedRectangle (c.reduced (0.5f), 5.0f, s == selected ? 1.5f : 1.0f);

        auto text = c.reduced (12.0f, 8.0f);
        g.setColour (info.built && ! bypassed ? col::legend : col::dim);
        g.setFont (font (14.0f, true));
        g.drawText (info.name, text.removeFromTop (20.0f), juce::Justification::left);
        g.setColour (col::dim);
        g.setFont (font (11.0f));
        g.drawText (info.built ? (bypassed ? "bypassed" : info.note) : "coming in " + info.note, text.removeFromTop (16.0f),
                    juce::Justification::left);

        // lamp: lit when the module is in
        const auto l = lamp (c);
        g.setColour (bypassed ? col::bg : (info.built ? col::amber : col::faint));
        g.fillEllipse (l.reduced (3.0f));
        if (! bypassed && info.built)
        {
            g.setColour (col::amber.withAlpha (0.25f));
            g.fillEllipse (l.expanded (2.0f));
        }
        g.setColour (col::edge);
        g.drawEllipse (l.reduced (3.0f), 1.0f);
    }
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! getOrder) return;
    const auto order = getOrder();
    const auto pos = e.position;
    for (int p = 0; p < (int) order.size(); ++p)
    {
        const auto c = cell (p);
        if (! c.contains (pos)) continue;
        if (lamp (c).expanded (4.0f).contains (pos))
        {
            if (toggleBypass) toggleBypass (order[(size_t) p]);
            repaint();
            return;
        }
        dragSlot = order[(size_t) p];
        dragFrom = p;
        grabOffset = pos.x - c.getX();
        dragX = pos.x;
        return;
    }
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragSlot < 0) return;
    dragging = dragging || e.getDistanceFromDragStart() > 4;
    dragX = e.position.x;
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent&)
{
    if (dragging && dragSlot >= 0 && getOrder && setOrder)
    {
        auto order = getOrder();
        order.erase (order.begin() + dragFrom);
        order.insert (order.begin() + positionAt (dragX), dragSlot);
        setOrder (order);
    }
    else if (dragSlot >= 0 && onSelect)
        onSelect (dragSlot);
    dragging = false;
    dragSlot = dragFrom = -1;
    repaint();
}
} // namespace bbui
