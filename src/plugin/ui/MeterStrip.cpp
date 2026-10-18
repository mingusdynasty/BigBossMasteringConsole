#include "MeterStrip.h"

namespace bbui
{
namespace
{
juce::String lufsText (float v) { return v <= -70.0f ? juce::String ("--.-") : juce::String (v, 1); }
}

void MeterStrip::tick (float barL, float barR)
{
    if (src == nullptr) return;
    const auto& r = src->readings;
    M = r.momentary.load(); S = r.shortTerm.load(); I = r.integrated.load(); lra = r.range.load();
    corr = r.correlation.load();
    tp[0] = barL;
    tp[1] = barR;
    for (int c = 0; c < 2; ++c) tpHeld[c] = r.truePeakHeld[c].load();
    repaint();
}

void MeterStrip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    g.setColour (col::panel);
    g.fillRoundedRectangle (b.toFloat(), 6.0f);
    b.reduce (14, 6);

    g.setFont (font (11.0f, true));
    g.setColour (col::amber);
    g.drawText (sourceName, b.removeFromLeft (40), juce::Justification::centredLeft);

    auto readout = [&] (const juce::String& label, const juce::String& value, int width, juce::Colour c)
    {
        auto a = b.removeFromLeft (width);
        g.setColour (col::dim);
        g.setFont (font (10.0f, true));
        g.drawText (label, a.removeFromLeft (label.length() * 7 + 6), juce::Justification::centredLeft);
        g.setColour (c);
        g.setFont (mono (15.0f));
        g.drawText (value, a, juce::Justification::centredLeft);
    };
    readout ("M", lufsText (M), 74, col::legend);
    readout ("S", lufsText (S), 74, col::legend);
    readout ("I", lufsText (I), 74, col::amber);
    readout ("LRA", lra > 0 ? juce::String (lra, 1) : juce::String ("--"), 84, col::legend);
    g.setColour (col::dim);
    g.setFont (font (10.0f));
    g.drawText ("LUFS / LU", b.removeFromLeft (64), juce::Justification::centredLeft);

    // right: correlation, then true peak per channel with its held maximum
    auto corrA = b.removeFromRight (150);
    b.removeFromRight (16);
    auto peakA = b.removeFromRight (juce::jmin (330, b.getWidth() / 2));
    b.removeFromRight (16);

    // short-term loudness bar, -36 .. 0 LUFS, with the target line
    auto bar = [&] (juce::Rectangle<int> a, float v, float lo, float hi, juce::Colour c)
    {
        g.setColour (col::bg);
        g.fillRoundedRectangle (a.toFloat(), 2.0f);
        g.setColour (c);
        g.fillRoundedRectangle (a.toFloat().withWidth ((float) a.getWidth() * norm (v, lo, hi)), 2.0f);
    };
    {
        g.setColour (col::dim);
        g.setFont (font (10.0f, true));
        g.drawText ("SHORT-TERM", b.removeFromLeft (76), juce::Justification::centredLeft);
        auto a = b.withSizeKeepingCentre (b.getWidth(), 10);
        bar (a, S, -36.0f, 0.0f, S > targetLufs + 1.0f ? col::yellow : col::green);
        const float x = (float) a.getX() + (float) a.getWidth() * norm (targetLufs, -36.0f, 0.0f);
        g.setColour (col::amber);
        g.drawLine (x, (float) a.getY() - 4, x, (float) a.getBottom() + 4, 1.5f);
    }
    {
        g.setColour (col::dim);
        g.setFont (font (10.0f, true));
        g.drawText ("TP", peakA.removeFromLeft (24), juce::Justification::centredLeft);
        for (int c = 0; c < 2; ++c)
        {
            auto row = peakA.removeFromTop (peakA.getHeight() / (2 - c));
            g.setColour (col::dim);
            g.setFont (font (9.0f, true));
            g.drawText (c == 0 ? "L" : "R", row.removeFromLeft (12), juce::Justification::centredLeft);
            g.setColour (tpHeld[c] > -1.0f ? col::red : col::legend);
            g.setFont (mono (11.0f));
            g.drawText (tpHeld[c] <= -70.0f ? juce::String ("--.-") : juce::String (tpHeld[c], 1), row.removeFromRight (44),
                        juce::Justification::centredRight);
            bar (row.reduced (0, 4), tp[c], -30.0f, 3.0f, tp[c] > -1.0f ? col::red : col::green);
        }
    }
    {
        g.setColour (col::dim);
        g.setFont (font (10.0f, true));
        g.drawText ("CORR", corrA.removeFromLeft (40), juce::Justification::centredLeft);
        auto a = corrA.withSizeKeepingCentre (corrA.getWidth(), 10);
        g.setColour (col::bg);
        g.fillRoundedRectangle (a.toFloat(), 2.0f);
        const float mid = (float) a.getCentreX(), x = mid + (float) a.getWidth() * 0.5f * juce::jlimit (-1.0f, 1.0f, corr);
        g.setColour (corr < 0 ? col::red : col::green);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (mid, x), (float) a.getY(), juce::jmax (mid, x), (float) a.getBottom()));
        g.setColour (col::faint);
        g.drawVerticalLine ((int) mid, (float) a.getY() - 3, (float) a.getBottom() + 3);
    }
}
} // namespace bbui
