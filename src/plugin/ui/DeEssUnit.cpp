#include "DeEssUnit.h"

#include "plugin/Params.h"

namespace bbui::rack
{
namespace
{
// deep blue anodised plate, silver legends, a cyan jewel
const Finish kFinish { juce::Colour (0xff1f2a3a), juce::Colour (0xff131a25), juce::Colour (0xffd3dbe6), juce::Colour (0xff4fd1e0), true };
using Scale = std::vector<std::pair<float, const char*>>;

constexpr float kSteps[] = { 0.5f, 1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 18 };
constexpr int kLeds = (int) (sizeof (kSteps) / sizeof (kSteps[0]));

juce::String khz (double hz) { return juce::String (hz / 1000.0, 1) + "k"; }
}

// ---------------------------------------------------------------- ladders

void BandLadders::push (int band, float reductionDb, float fps)
{
    auto& lv = level[(size_t) band];
    // LEDs jump up and fall back at 30 dB a second; the top lit one holds for a second
    lv = reductionDb > lv ? reductionDb : juce::jmax (reductionDb, lv - 30.0f / fps);
    auto& pk = peak[(size_t) band];
    auto& h = held[(size_t) band];
    if (reductionDb >= pk) { pk = reductionDb; h = 1.0f; }
    else if ((h -= 1.0f / fps) <= 0.0f) pk = lv;
    repaint();
}

void BandLadders::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (b, 6.0f);
    auto glass = b.reduced (6.0f);
    g.setColour (juce::Colour (0xff0b0d10));
    g.fillRoundedRectangle (glass, 4.0f);

    auto area = glass.reduced (8.0f, 10.0f);
    auto labels = area.removeFromBottom (18.0f);
    auto scale = area.removeFromLeft (26.0f);
    const float colW = area.getWidth() / (float) bb::DeEsser::kBands;
    const float ledH = area.getHeight() / (float) kLeds;

    g.setFont (engraving (8.5f, false));
    for (int i = 0; i < kLeds; ++i)
    {
        if (i % 2 == 0 && i != kLeds - 1 && i != 0) continue;
        const float y = area.getBottom() - ledH * ((float) i + 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawText (juce::String (kSteps[i], kSteps[i] < 1 ? 1 : 0), juce::Rectangle<float> (24, 10).withCentre ({ scale.getCentreX(), y }),
                    juce::Justification::centred);
    }

    for (int k = 0; k < bb::DeEsser::kBands; ++k)
    {
        const auto col = juce::Rectangle<float> (area.getX() + colW * (float) k, area.getY(), colW, area.getHeight()).reduced (colW * 0.18f, 0);
        for (int i = 0; i < kLeds; ++i)
        {
            const auto led = juce::Rectangle<float> (col.getX(), area.getBottom() - ledH * (float) (i + 1), col.getWidth(), ledH).reduced (0, ledH * 0.16f);
            const bool on = level[(size_t) k] >= kSteps[i] || (peak[(size_t) k] >= kSteps[i] && (i == kLeds - 1 || peak[(size_t) k] < kSteps[i + 1]));
            const auto hue = i < 6 ? juce::Colour (0xff4fd1e0) : (i < 9 ? juce::Colour (0xfff2a43a) : juce::Colour (0xffe5352b));
            g.setColour (on ? hue : hue.withAlpha (0.10f));
            g.fillRoundedRectangle (led, 1.5f);
            if (on)
            {
                g.setColour (hue.withAlpha (0.25f));
                g.fillRoundedRectangle (led.expanded (1.5f), 2.5f);
            }
        }
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (engraving (9.5f, true));
        g.drawText (names[(size_t) k], juce::Rectangle<float> (area.getX() + colW * (float) k, labels.getY() + 2, colW, 14),
                    juce::Justification::centred);
    }
}

// ---------------------------------------------------------------- unit

DeEssUnit::DeEssUnit (Apvts& state, const bb::DeEsser& d, std::function<bb::DeEsser::Settings()> s)
    : Unit ("MULTIBAND DE-ESSER", juce::String::fromUTF8 ("BB-S4  ·  FOUR BANDS  ·  BODY-REFERENCED DETECTION"), kFinish),
      deesser (d), settings (std::move (s))
{
    threshold = std::make_unique<Knob> (state, pid::deessThr, "big", Scale { { -24, "-24" }, { -12, "-12" }, { -6, "-6" }, { 0, "0" }, { 12, "+12" } });
    range     = std::make_unique<Knob> (state, pid::deessRange, "big", Scale { { 0, "0" }, { 4, "4" }, { 8, "8" }, { 12, "12" }, { 18, "18" } });
    freq      = std::make_unique<Knob> (state, pid::deessFreq, "small", Scale { { 3000, "3k" }, { 4500, "4.5k" }, { 6000, "6k" }, { 8000, "8k" } });
    release   = std::make_unique<Knob> (state, pid::deessRelease, "small", Scale { { 10, "10" }, { 50, "50" }, { 200, "200" } });
    mode      = std::make_unique<Knob> (state, pid::deessMode, "chicken", Scale { { 0, "STEREO" }, { 1, "MID" }, { 2, "SIDE" } }, true);
    mode->setRotaryParameters (juce::MathConstants<float>::pi * 1.65f, juce::MathConstants<float>::pi * 2.35f, true);
    listen    = std::make_unique<Toggle> (state, pid::deessListen, "LISTEN", "OUT");
    for (juce::Component* k : std::initializer_list<juce::Component*> { &ladders, threshold.get(), range.get(), freq.get(),
                                                                         release.get(), mode.get(), listen.get() })
        addAndMakeVisible (*k);
}

void DeEssUnit::tick()
{
    Unit::tick();
    for (int k = 0; k < bb::DeEsser::kBands; ++k) ladders.push (k, deesser.reduction (k), 30.0f);
    if (settings)
    {
        const auto f = settings().freq;
        for (int k = 0; k < bb::DeEsser::kBands; ++k) ladders.names[(size_t) k] = khz (bb::DeEsser::bandFreq (f, k));
    }
}

void DeEssUnit::layout()
{
    auto c = content();
    c.removeFromTop (6);
    const int w = c.getWidth();
    auto left = c.removeFromLeft (w * 36 / 100);
    auto mid = c.removeFromLeft (w * 34 / 100);
    auto right = c;
    rules.push_back ({ (float) mid.getX(), (float) mid.getY(), (float) mid.getX(), (float) mid.getBottom() });
    rules.push_back ({ (float) right.getX(), (float) right.getY(), (float) right.getX(), (float) right.getBottom() });

    auto place = [] (juce::Component& k, juce::Rectangle<int> a, int maxW)
    { k.setBounds (a.withSizeKeepingCentre (juce::jmin (a.getWidth(), maxW), a.getHeight())); };
    auto toggle = [] (juce::Component& t, juce::Rectangle<int> a) { t.setBounds (a.withSizeKeepingCentre (56, juce::jmin (70, a.getHeight()))); };

    // left: the four bands' reduction
    legend ("REDUCTION  dB", left.removeFromTop (18), 12.0f);
    ladders.setBounds (left.withTrimmedBottom (8).reduced (12, 0));

    // centre: how much is caught and how far it is pulled down
    legend ("DETECTION", mid.removeFromTop (18), 12.0f);
    const int mh = mid.getHeight() / 2;
    auto t = mid.removeFromTop (mh), r = mid;
    legend ("THRESHOLD  dB", t.removeFromTop (16), 10.0f);
    legend ("OVER THE VOICE BODY", t.removeFromBottom (14), 8.0f, false);
    place (*threshold, t, 170);
    legend ("RANGE  dB", r.removeFromTop (16), 10.0f);
    legend ("MOST ANY BAND CUTS", r.removeFromBottom (14), 8.0f, false);
    place (*range, r, 170);

    // right: where and how
    legend ("VOICING", right.removeFromTop (18), 12.0f);
    const int rh = right.getHeight() / 2, cw = right.getWidth() / 2;
    auto row1 = right.removeFromTop (rh), row2 = right;
    auto a = row1.removeFromLeft (cw), b = row1;
    legend ("FREQUENCY", a.removeFromTop (16), 10.0f);
    legend ("LOWEST BAND", a.removeFromBottom (14), 8.0f, false);
    place (*freq, a, 140);
    legend ("RELEASE  ms", b.removeFromTop (16), 10.0f);
    b.removeFromBottom (14);
    place (*release, b, 140);
    auto m = row2.removeFromLeft (cw), l = row2;
    legend ("MODE", m.removeFromTop (16), 10.0f);
    place (*mode, m, 200);
    legend ("MONITOR", l.removeFromTop (16), 10.0f);
    legend ("HEAR WHAT IS TAKEN", l.removeFromBottom (14), 8.0f, false);
    toggle (*listen, l);
}
} // namespace bbui::rack
