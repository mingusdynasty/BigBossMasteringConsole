#include "LimitUnit.h"

#include "plugin/Params.h"

namespace bbui::rack
{
namespace
{
// satin aluminium, black legends, a red jewel
const Finish kFinish { juce::Colour (0xffc3c6ca), juce::Colour (0xff979a9f), juce::Colour (0xff1b1c1f), juce::Colour (0xffe5352b), true };
using Scale = std::vector<std::pair<float, const char*>>;

float toDb (float lin) { return lin > 1e-6f ? 20.0f * std::log10 (lin) : -120.0f; }
}

// ---------------------------------------------------------------- history

void LimitHistory::push (float inDb, float outDb, float reductionDb)
{
    if (! primed)
    {
        in.fill (-120.0f); out.fill (-120.0f); red.fill (0.0f);
        primed = true;
    }
    in[(size_t) pos] = inDb; out[(size_t) pos] = outDb; red[(size_t) pos] = reductionDb;
    pos = (pos + 1) % kFrames;
    repaint();
}

void LimitHistory::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (b, 6.0f);
    const auto glass = b.reduced (6.0f);
    g.setColour (juce::Colour (0xff0c0d0f));
    g.fillRoundedRectangle (glass, 4.0f);

    const auto plot = glass.reduced (10.0f, 12.0f).withTrimmedLeft (22.0f);
    auto yLevel = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 30.0f) / 30.0f); };
    auto yRed = [&] (float db) { return plot.getY() + plot.getHeight() * 0.5f * juce::jlimit (0.0f, 1.0f, db / 12.0f); };

    g.setFont (engraving (8.5f, false));
    for (float db : { -24.f, -18.f, -12.f, -6.f, 0.f })
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine ((int) yLevel (db), plot.getX(), plot.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (24, 10).withCentre ({ plot.getX() - 14, yLevel (db) }), juce::Justification::centred);
    }

    const float dx = plot.getWidth() / (float) (kFrames - 1);
    auto xOf = [&] (int k) { return plot.getX() + dx * (float) k; };   // k = 0 oldest
    auto at = [&] (const std::array<float, kFrames>& a, int k) { return a[(size_t) ((pos + k) % kFrames)]; };

    juce::Path inFill, outLine, redFill;
    inFill.startNewSubPath (xOf (0), plot.getBottom());
    redFill.startNewSubPath (xOf (0), plot.getY());
    for (int k = 0; k < kFrames; ++k)
    {
        inFill.lineTo (xOf (k), yLevel (at (in, k)));
        const auto p = juce::Point<float> (xOf (k), yLevel (at (out, k)));
        if (k == 0) outLine.startNewSubPath (p); else outLine.lineTo (p);
        redFill.lineTo (xOf (k), yRed (at (red, k)));
    }
    inFill.lineTo (xOf (kFrames - 1), plot.getBottom());
    inFill.closeSubPath();
    redFill.lineTo (xOf (kFrames - 1), plot.getY());
    redFill.closeSubPath();

    g.setColour (juce::Colour (0xff5b6068).withAlpha (0.55f));
    g.fillPath (inFill);
    g.setColour (juce::Colour (0xffe5352b).withAlpha (0.65f));
    g.fillPath (redFill);
    g.setColour (juce::Colour (0xfff2a43a));
    g.strokePath (outLine, juce::PathStrokeType (1.4f));

    // the ceiling
    const float yc = yLevel (ceilingDb);
    g.setColour (juce::Colour (0xfff2a43a).withAlpha (0.6f));
    const float dash[] = { 4.0f, 3.0f };
    g.drawDashedLine ({ plot.getX(), yc, plot.getRight(), yc }, dash, 2, 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.setFont (engraving (8.5f, true));
    auto key = glass.reduced (12.0f, 8.0f).toNearestInt();
    g.drawText (juce::String::fromUTF8 ("IN  ·  OUT  ·  REDUCTION   6 s"), key, juce::Justification::topRight);
}

// ---------------------------------------------------------------- unit

LimitUnit::LimitUnit (Apvts& state, bb::Limiter& lim, std::function<bb::Limiter::Settings()> s)
    : Unit ("TRUE-PEAK MASTERING LIMITER", juce::String::fromUTF8 ("BB-L1  ·  8x RECONSTRUCTION  ·  3 ms LOOKAHEAD"), kFinish),
      limiter (lim), settings (std::move (s))
{
    drive    = std::make_unique<Knob> (state, pid::limDrive, "big", Scale { { 0, "0" }, { 6, "6" }, { 12, "12" }, { 18, "18" }, { 24, "24" } });
    ceiling  = std::make_unique<Knob> (state, pid::limCeiling, "big", Scale { { -12, "-12" }, { -6, "-6" }, { -3, "-3" }, { -1, "-1" }, { 0, "0" } });
    style    = std::make_unique<Knob> (state, pid::limStyle, "chicken", Scale { { 0, "TRANSP" }, { 1, "PUNCH" }, { 2, "DENSE" } }, true);
    style->setRotaryParameters (juce::MathConstants<float>::pi * 1.65f, juce::MathConstants<float>::pi * 2.35f, true);
    release  = std::make_unique<Knob> (state, pid::limRelease, "small", Scale { { 1, "1" }, { 10, "10" }, { 100, "100" }, { 1000, "1k" } });
    link     = std::make_unique<Knob> (state, pid::limLink, "small", Scale { { 0, "0" }, { 50, "50" }, { 100, "100" } });
    autoRel  = std::make_unique<Toggle> (state, pid::limAuto, "AUTO", "MANUAL");
    truePeak = std::make_unique<Toggle> (state, pid::limTp, "TRUE PK", "SAMPLE");
    for (juce::Component* k : std::initializer_list<juce::Component*> { &history, &meter, drive.get(), ceiling.get(), style.get(),
                                                                         release.get(), link.get(), autoRel.get(), truePeak.get() })
        addAndMakeVisible (*k);
}

void LimitUnit::tick()
{
    Unit::tick();
    float in, out, red;
    limiter.takeMeters (in, out, red);
    history.push (toDb (in), toDb (out), red);
    meter.push (red, 30.0f);
    if (settings)
    {
        const auto s = settings();
        history.ceilingDb = (float) s.ceilingDb;
        release->setEnabled (! s.autoRelease);
        release->setAlpha (s.autoRelease ? 0.45f : 1.0f);
    }
}

void LimitUnit::layout()
{
    auto c = content();
    c.removeFromTop (6);
    auto left = c.removeFromLeft (c.getWidth() * 58 / 100);
    c.removeFromLeft (16);
    rules.push_back ({ (float) c.getX() - 8, (float) c.getY(), (float) c.getX() - 8, (float) c.getBottom() });

    auto place = [] (juce::Component& k, juce::Rectangle<int> a, int maxW)
    { k.setBounds (a.withSizeKeepingCentre (juce::jmin (a.getWidth(), maxW), a.getHeight())); };
    auto toggle = [] (juce::Component& t, juce::Rectangle<int> a) { t.setBounds (a.withSizeKeepingCentre (56, juce::jmin (70, a.getHeight()))); };

    // left: the history display over style, detection and link
    history.setBounds (left.removeFromTop (left.getHeight() * 62 / 100).withTrimmedRight (8));
    left.removeFromTop (10);
    auto names = left.removeFromTop (16);
    const int w3 = left.getWidth() / 3;
    legend ("DETECTION", names.removeFromLeft (w3), 10.0f);
    legend ("STYLE", names.removeFromLeft (w3), 10.0f);
    legend ("STEREO LINK  %", names, 10.0f);
    toggle (*truePeak, left.removeFromLeft (w3));
    place (*style, left.removeFromLeft (w3), 220);
    place (*link, left, 110);

    // right: meter, drive and ceiling, release
    meter.setBounds (c.removeFromTop (156).withSizeKeepingCentre (juce::jmin (c.getWidth() - 20, 300), 152));
    c.removeFromTop (8);
    auto row1 = c.removeFromTop (c.getHeight() * 55 / 100), row2 = c;
    auto n1 = row1.removeFromTop (16);
    legend ("DRIVE  dB", n1.removeFromLeft (n1.getWidth() / 2), 10.0f);
    legend ("CEILING  dBTP", n1, 10.0f);
    place (*drive, row1.removeFromLeft (row1.getWidth() / 2), 150);
    place (*ceiling, row1, 150);
    auto n2 = row2.removeFromTop (16);
    legend ("RELEASE", n2.removeFromLeft (n2.getWidth() / 2), 10.0f);
    legend ("TIME  ms", n2, 10.0f);
    toggle (*autoRel, row2.removeFromLeft (row2.getWidth() / 2));
    place (*release, row2, 110);
}
} // namespace bbui::rack
