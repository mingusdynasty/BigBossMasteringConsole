#include "MeterPanel.h"

namespace bbui
{
namespace
{
juce::String fmt (float v, int decimals = 1, bool sign = false)
{
    if (v <= -199.0f) return "--.-";
    return (sign && v > 0 ? "+" : "") + juce::String (v, decimals);
}
}

void MeterPanel::setSource (bb::MeterSuite* m)
{
    src = m;
    hist.clear();
    histPos = m != nullptr ? (m->history.head() > kHistory ? m->history.head() - kHistory : 0) : 0;
    tpBar[0] = tpBar[1] = -200;
}

void MeterPanel::tick()
{
    if (src == nullptr) return;
    auto& rd = src->readings;
    M = rd.momentary.load(); S = rd.shortTerm.load(); I = rd.integrated.load(); lra = rd.range.load();
    maxM = rd.maxMomentary.load(); maxS = rd.maxShortTerm.load(); plr = rd.plr.load();
    for (int c = 0; c < 2; ++c)
    {
        tpHeld[c] = rd.truePeakHeld[c].load();
        spHeld[c] = rd.samplePeakHeld[c].load();
        tp[c] = rd.truePeakNow[c].exchange (-200.0f);
        // bars: instant attack, 20 dB/s fall (about 0.67 dB per 30 Hz frame)
        tpBar[c] = juce::jmax (tp[c], tpBar[c] - 0.67f);
    }
    corr = rd.correlation.load(); bal = rd.balance.load(); width = rd.widthDb.load();
    os = rd.oversampling.load();

    bb::LoudnessPoint pts[256];
    for (int got; (got = src->history.read (histPos, pts, 256)) > 0;)
        hist.insert (hist.end(), pts, pts + got);
    if (hist.size() > kHistory) hist.erase (hist.begin(), hist.begin() + (long) (hist.size() - kHistory));
    repaint();
}

void MeterPanel::paintReadout (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label, const juce::String& value,
                               const juce::String& unit, juce::Colour c, float valueHeight)
{
    g.setColour (col::panelHi);
    g.fillRoundedRectangle (r.toFloat(), 4.0f);
    auto in = r.reduced (8, 5);
    g.setColour (col::dim);
    g.setFont (font (10.5f, true));
    g.drawText (label, in.removeFromTop (14), juce::Justification::left);
    g.setColour (c);
    g.setFont (mono (valueHeight));
    auto valueArea = in;
    drawTabular (g, value, valueArea.removeFromLeft (in.getWidth() - 34).toFloat(), juce::Justification::bottomLeft);
    g.setColour (col::dim);
    g.setFont (font (11.0f));
    g.drawText (unit, valueArea, juce::Justification::bottomRight);
}

void MeterPanel::paintBar (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label, float value, float hold,
                           float lo, float hi, bool loudness)
{
    auto labelArea = r.removeFromBottom (16);
    g.setColour (col::dim);
    g.setFont (font (10.5f, true));
    g.drawText (label, labelArea, juce::Justification::centred);

    g.setColour (col::bg);
    g.fillRect (r);
    const auto rf = r.toFloat().reduced (1.0f);
    auto yOf = [&] (float v) { return rf.getBottom() - norm (v, lo, hi) * rf.getHeight(); };

    // ticks every 6 dB, with numbers on the loudness pair and the peak pair's left bar
    g.setFont (font (9.0f));
    for (float v = std::ceil (hi / 6.0f) * 6.0f - 6.0f; v > lo; v -= 6.0f)
    {
        g.setColour (col::faint.withAlpha (0.6f));
        g.drawHorizontalLine ((int) yOf (v), rf.getX(), rf.getRight());
    }
    if (value > lo)
    {
        const float top = yOf (value);
        // loudness bars change colour around the target, peak bars near full scale
        const float warn = loudness ? targetLufs - 2.0f : -6.0f, over = loudness ? targetLufs + 1.0f : -1.0f;
        juce::ColourGradient grad (col::green, 0, yOf (lo), col::red, 0, yOf (hi), false);
        grad.addColour (norm (warn, lo, hi), col::green);
        grad.addColour (juce::jlimit (0.0, 1.0, (double) norm (over, lo, hi)), col::yellow);
        g.setGradientFill (grad);
        g.fillRect (juce::Rectangle<float> (rf.getX(), top, rf.getWidth(), rf.getBottom() - top));
    }
    if (hold > lo)
    {
        g.setColour (col::legend);
        g.fillRect (rf.getX(), yOf (hold) - 1.0f, rf.getWidth(), 2.0f);
    }
    if (loudness)
    {
        g.setColour (col::amber.withAlpha (0.8f));
        const float y = yOf (targetLufs);
        g.drawLine (rf.getX() - 3, y, rf.getRight() + 3, y, 1.0f);
    }
}

void MeterPanel::paintHistory (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (col::bg);
    g.fillRect (r);
    const float lo = -40.0f, hi = 0.0f;
    const auto rf = r.toFloat().reduced (1.0f, 4.0f);
    auto yOf = [&] (float v) { return rf.getBottom() - norm (v, lo, hi) * rf.getHeight(); };

    g.setFont (font (10.0f));
    for (float v = -36.0f; v <= -6.0f; v += 6.0f)
    {
        g.setColour (col::faint.withAlpha (0.5f));
        g.drawHorizontalLine ((int) yOf (v), rf.getX(), rf.getRight());
        g.setColour (col::faint);
        g.drawText (juce::String ((int) v), (int) rf.getX() + 3, (int) yOf (v) - 11, 30, 11, juce::Justification::left);
    }
    g.setColour (col::amber.withAlpha (0.5f));
    g.drawHorizontalLine ((int) yOf (targetLufs), rf.getX(), rf.getRight());

    if (hist.size() > 1)
    {
        const float dx = rf.getWidth() / (float) (kHistory - 1);
        const float x0 = rf.getRight() - dx * (float) (hist.size() - 1);
        juce::Path pm, ps;
        bool startedM = false, startedS = false;
        for (size_t i = 0; i < hist.size(); ++i)
        {
            const float x = x0 + dx * (float) i;
            const float m = juce::jmax (lo, hist[i].momentary), s = juce::jmax (lo, hist[i].shortTerm);
            if (! startedM) { pm.startNewSubPath (x, yOf (m)); startedM = true; } else pm.lineTo (x, yOf (m));
            if (! startedS) { ps.startNewSubPath (x, yOf (s)); startedS = true; } else ps.lineTo (x, yOf (s));
        }
        g.setColour (col::blue.withAlpha (0.45f));
        g.strokePath (pm, juce::PathStrokeType (1.0f));
        g.setColour (col::amber);
        g.strokePath (ps, juce::PathStrokeType (1.8f));
    }
    if (I > lo)
    {
        g.setColour (col::legend.withAlpha (0.8f));
        const float y = yOf (I);
        const float dash[] { 4.0f, 3.0f };
        g.drawDashedLine ({ rf.getX(), y, rf.getRight(), y }, dash, 2, 1.0f);
    }
    g.setColour (col::dim);
    g.drawText ("short-term", r.getRight() - 140, r.getY() + 3, 64, 12, juce::Justification::right);
    g.setColour (col::blue);
    g.drawText ("momentary", r.getRight() - 72, r.getY() + 3, 66, 12, juce::Justification::right);
}

void MeterPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (col::panel);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    r.reduce (10, 10);

    // readouts: integrated is the hero
    auto top = r.removeFromTop (78);
    const int gap = 6;
    const int w = (top.getWidth() - 2 * gap) / 3;
    const float tpMax = juce::jmax (tpHeld[0], tpHeld[1]);
    paintReadout (g, top.removeFromLeft (w), "INTEGRATED", fmt (I), "LUFS", col::amber, 34.0f);
    top.removeFromLeft (gap);
    auto col2 = top.removeFromLeft (w);
    paintReadout (g, col2.removeFromTop (36), "SHORT-TERM", fmt (S), "LUFS", col::legend, 17.0f);
    col2.removeFromTop (gap);
    paintReadout (g, col2, "MOMENTARY", fmt (M), "LUFS", col::legend, 17.0f);
    top.removeFromLeft (gap);
    auto col3 = top;
    paintReadout (g, col3.removeFromTop (36), "TRUE PEAK " + juce::String (os) + "x", fmt (tpMax, 1, true), "dBTP",
                  tpMax > -1.0f ? col::red : col::legend, 17.0f);
    col3.removeFromTop (gap);
    paintReadout (g, col3, "LRA", fmt (I > -199 ? lra : -200.0f), "LU", col::legend, 17.0f);

    r.removeFromTop (gap);
    auto small = r.removeFromTop (36);
    const int sw = (small.getWidth() - 3 * gap) / 4;
    paintReadout (g, small.removeFromLeft (sw), "PLR", fmt (I > -199 ? plr : -200.0f), "LU", col::legend, 14.0f);
    small.removeFromLeft (gap);
    paintReadout (g, small.removeFromLeft (sw), "MAX S", fmt (maxS), "LUFS", col::legend, 14.0f);
    small.removeFromLeft (gap);
    paintReadout (g, small.removeFromLeft (sw), "MAX M", fmt (maxM), "LUFS", col::legend, 14.0f);
    small.removeFromLeft (gap);
    paintReadout (g, small, "SAMPLE PK", fmt (juce::jmax (spHeld[0], spHeld[1]), 1, true), "dBFS", col::legend, 14.0f);

    r.removeFromTop (10);

    // correlation and balance
    auto bottom = r.removeFromBottom (44);
    auto corrArea = bottom.removeFromTop (20);
    {
        g.setColour (col::dim);
        g.setFont (font (10.5f, true));
        g.drawText ("CORRELATION", corrArea.removeFromLeft (84), juce::Justification::left);
        auto valueArea = corrArea.removeFromRight (44);
        g.setColour (corr < 0 ? col::red : col::legend);
        g.setFont (mono (12.0f));
        drawTabular (g, juce::String (corr, 2), valueArea.toFloat(), juce::Justification::centredRight);
        g.setColour (col::bg);
        g.fillRect (corrArea);
        const float cx = (float) corrArea.getCentreX();
        const float x = (float) corrArea.getX() + (corr + 1.0f) * 0.5f * (float) corrArea.getWidth();
        g.setColour (corr < 0 ? col::red : col::green);
        g.fillRect (juce::Rectangle<float> (juce::jmin (cx, x), (float) corrArea.getY() + 3, std::abs (x - cx), (float) corrArea.getHeight() - 6));
        g.setColour (col::faint);
        g.drawVerticalLine ((int) cx, (float) corrArea.getY(), (float) corrArea.getBottom());
    }
    bottom.removeFromTop (6);
    {
        auto balArea = bottom;
        g.setColour (col::dim);
        g.setFont (font (10.5f, true));
        g.drawText ("BALANCE", balArea.removeFromLeft (84), juce::Justification::left);
        auto valueArea = balArea.removeFromRight (120);
        g.setFont (mono (12.0f));
        g.setColour (col::legend);
        const juce::String widthText = width < -60 ? "mono" : ("S/M " + juce::String (width, 1) + " dB");
        drawTabular (g, widthText, valueArea.toFloat(), juce::Justification::centredRight);
        g.setColour (col::bg);
        g.fillRect (balArea);
        const float cx = (float) balArea.getCentreX();
        const float x = cx + bal * 0.5f * (float) balArea.getWidth();
        g.setColour (col::blue);
        g.fillRect (juce::Rectangle<float> (x - 2.0f, (float) balArea.getY() + 3, 4.0f, (float) balArea.getHeight() - 6));
        g.setColour (col::faint);
        g.drawVerticalLine ((int) cx, (float) balArea.getY(), (float) balArea.getBottom());
    }
    r.removeFromBottom (10);

    // bars on the left, history on the right
    auto bars = r.removeFromLeft (172);
    {
        // dB scale for the loudness pair (left) and the peak pair (right)
        auto scaleL = bars.removeFromLeft (20).withTrimmedBottom (16).toFloat().reduced (0.0f, 1.0f);
        g.setFont (font (9.0f));
        g.setColour (col::dim);
        for (float v = -6.0f; v > -40.0f; v -= 6.0f)
            g.drawText (juce::String ((int) v), juce::Rectangle<float> (scaleL.getX(), scaleL.getBottom() - norm (v, -40.0f, 0.0f) * scaleL.getHeight() - 6.0f, 18.0f, 12.0f),
                        juce::Justification::centredRight);
    }
    const int bw = (bars.getWidth() - 3 * 6) / 4;
    paintBar (g, bars.removeFromLeft (bw), "M", M, maxM, -40.0f, 0.0f, true);
    bars.removeFromLeft (6);
    paintBar (g, bars.removeFromLeft (bw), "S", S, maxS, -40.0f, 0.0f, true);
    bars.removeFromLeft (6);
    paintBar (g, bars.removeFromLeft (bw), "TP L", tpBar[0], tpHeld[0], -40.0f, 3.0f, false);
    bars.removeFromLeft (6);
    paintBar (g, bars, "TP R", tpBar[1], tpHeld[1], -40.0f, 3.0f, false);
    r.removeFromLeft (10);
    paintHistory (g, r);
}
} // namespace bbui
