#include "EqUnit.h"

#include "plugin/Params.h"

namespace bbui::rack
{
namespace
{
const Finish kFinish { juce::Colour (0xff2e3136), juce::Colour (0xff1d1f23), juce::Colour (0xffdfe3e8), juce::Colour (0xff5aa8ff), true };

constexpr double kScopeRate = 192000.0;   // the EQ runs oversampled; draw it at a rate in that range

// scales for each band's frequency knob
const std::vector<std::pair<float, const char*>> kFreqScale[bb::eq::kBands] = {
    { { 20, "20" }, { 40, "40" }, { 80, "80" }, { 150, "150" }, { 300, "300" } },
    { { 40, "40" }, { 100, "100" }, { 200, "200" }, { 400, "400" }, { 800, "800" } },
    { { 200, "200" }, { 500, "500" }, { 1000, "1k" }, { 2000, "2k" }, { 4000, "4k" } },
    { { 800, "800" }, { 2000, "2k" }, { 4000, "4k" }, { 8000, "8k" }, { 12000, "12k" } },
    { { 2500, "2.5k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" }, { 28000, "28k" } },
};

bool near (double a, double b) { return std::abs (a - b) < 1e-6; }

bool same (const bb::eq::Settings& a, const bb::eq::Settings& b)
{
    for (int i = 0; i < bb::eq::kBands; ++i)
        if (! near (a.band[i].freq, b.band[i].freq) || ! near (a.band[i].gain, b.band[i].gain) || a.band[i].cut != b.band[i].cut
            || a.band[i].shelf != b.band[i].shelf)
            return false;
    return a.warm == b.warm && near (a.hpfHz, b.hpfHz) && a.hpfOrder == b.hpfOrder && near (a.lpfHz, b.lpfHz) && near (a.outputDb, b.outputDb);
}
}

// ---------------------------------------------------------------- scope

void EqScope::show (const bb::eq::Settings& s)
{
    if (valid && same (s, shown)) return;
    shown = s;
    valid = true;
    repaint();
}

void EqScope::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    // bezel and glass
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (b, 6.0f);
    const auto glass = b.reduced (6.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0b1d12), glass.getCentreX(), glass.getCentreY(),
                                             juce::Colour (0xff030805), glass.getX(), glass.getY(), true));
    g.fillRoundedRectangle (glass, 4.0f);

    const auto plot = glass.reduced (30.0f, 18.0f).withTrimmedLeft (-4.0f);
    const float lo = std::log (20.0f), hi = std::log (20000.0f);
    auto xOf = [&] (float f) { return plot.getX() + plot.getWidth() * (std::log (f) - lo) / (hi - lo); };
    auto yOf = [&] (float db) { return plot.getCentreY() - plot.getHeight() * 0.5f * juce::jlimit (-1.1f, 1.1f, db / 15.0f); };
    const juce::Colour phosphor (0xff5cff9a);

    g.setFont (engraving (9.0f, false));
    for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
    {
        const float x = xOf (f);
        g.setColour (phosphor.withAlpha (0.12f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (phosphor.withAlpha (0.45f));
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (30, 12).withCentre ({ x, plot.getBottom() + 9 }), juce::Justification::centred);
    }
    for (float db : { -12.f, -6.f, 0.f, 6.f, 12.f })
    {
        const float y = yOf (db);
        g.setColour (phosphor.withAlpha (std::abs (db) < 0.1f ? 0.3f : 0.12f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (phosphor.withAlpha (0.45f));
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (24, 12).withCentre ({ plot.getX() - 15, y }),
                    juce::Justification::centred);
    }

    if (valid)
    {
        juce::Path curve;
        for (int i = 0; i <= 300; ++i)
        {
            const float f = std::exp (lo + (hi - lo) * (float) i / 300.0f);
            const auto p = juce::Point<float> (xOf (f), yOf ((float) bb::eq::responseDb (shown, f, kScopeRate)));
            if (i == 0) curve.startNewSubPath (p); else curve.lineTo (p);
        }
        // the beam: a wide faint glow under a bright trace
        g.setColour (phosphor.withAlpha (0.10f));
        g.strokePath (curve, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (phosphor.withAlpha (0.25f));
        g.strokePath (curve, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (phosphor.brighter (0.4f));
        g.strokePath (curve, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // tube glass: a highlight across the top and a vignette
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), glass.getX(), glass.getY(),
                                             juce::Colours::transparentWhite, glass.getX(), glass.getCentreY(), false));
    g.fillRoundedRectangle (glass, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (glass, 4.0f, 2.0f);
    g.setColour (phosphor.withAlpha (0.5f));
    g.setFont (engraving (9.0f, true));
    g.drawText ("RESPONSE  dB", glass.reduced (10, 6).toNearestInt(), juce::Justification::topRight);
}

// ---------------------------------------------------------------- unit

EqUnit::EqUnit (Apvts& state, std::function<bb::eq::Settings()> s)
    : Unit ("CLASS-A MASTERING EQUALIZER", juce::String::fromUTF8 ("BB-5  ·  FIVE BAND  ·  CLASS A"), kFinish), settings (std::move (s))
{
    for (int b = 0; b < bb::eq::kBands; ++b)
    {
        auto& c = bands[b];
        c.freq = std::make_unique<Knob> (state, pid::eqFreq (b), "small", kFreqScale[b]);
        c.gain = std::make_unique<Knob> (state, pid::eqGain (b), "big",
                                         std::vector<std::pair<float, const char*>> { { 0, "0" }, { 3, "3" }, { 6, "6" }, { 9, "9" }, { 12, "12" } });
        c.cut = std::make_unique<Toggle> (state, pid::eqCut (b), "CUT", "BOOST");
        addAndMakeVisible (*c.freq);
        addAndMakeVisible (*c.gain);
        addAndMakeVisible (*c.cut);
        if (bb::eq::kRange[b].canShelf)
        {
            c.shelf = std::make_unique<Toggle> (state, pid::eqShelf (b), "SHELF", "BELL");
            addAndMakeVisible (*c.shelf);
        }
    }
    hpf  = std::make_unique<Knob> (state, pid::eqHpf, "small",
                                   std::vector<std::pair<float, const char*>> { { 0, "OFF" }, { 30, "30" }, { 100, "100" }, { 300, "300" } });
    hpf24 = std::make_unique<Toggle> (state, pid::eqHpf24, "24", "12");
    lpf  = std::make_unique<Knob> (state, pid::eqLpf, "small",
                                   std::vector<std::pair<float, const char*>> { { 4000, "4k" }, { 8000, "8k" }, { 16000, "16k" }, { 0, "OFF" } });
    mono = std::make_unique<Knob> (state, pid::eqMono, "small",
                                   std::vector<std::pair<float, const char*>> { { 0, "OFF" }, { 80, "80" }, { 150, "150" }, { 300, "300" } });
    warm = std::make_unique<Toggle> (state, pid::eqWarm, "WARM", "CLEAN");
    out  = std::make_unique<Knob> (state, pid::eqOut, "big",
                                   std::vector<std::pair<float, const char*>> { { -12, "-12" }, { -6, "-6" }, { 0, "0" }, { 6, "+6" }, { 12, "+12" } });
    for (juce::Component* c : std::initializer_list<juce::Component*> { hpf.get(), hpf24.get(), lpf.get(), mono.get(), warm.get(), out.get() })
        addAndMakeVisible (*c);
    addAndMakeVisible (scope);
}

void EqUnit::tick()
{
    Unit::tick();
    if (settings) scope.show (settings());
}

void EqUnit::layout()
{
    auto c = content();
    c.removeFromTop (6);
    auto scopeArea = c.removeFromRight (juce::jmax (300, c.getWidth() * 2 / 5));
    c.removeFromRight (20);
    rules.push_back ({ (float) c.getRight() + 10, (float) c.getY(), (float) c.getRight() + 10, (float) c.getBottom() });

    // five band columns
    auto bandRow = c.removeFromTop (juce::roundToInt (c.getHeight() * 0.64));
    const int bw = bandRow.getWidth() / bb::eq::kBands;
    for (int b = 0; b < bb::eq::kBands; ++b)
    {
        auto col = bandRow.removeFromLeft (bw);
        if (b > 0) rules.push_back ({ (float) col.getX(), (float) col.getY() + 4, (float) col.getX(), (float) col.getBottom() - 4 });
        col.reduce (4, 0);
        legend (bb::eq::kRange[b].name, col.removeFromTop (16), 11.0f);
        const int h = col.getHeight();
        auto f = col.removeFromTop (h * 30 / 100);
        bands[b].freq->setBounds (f.withSizeKeepingCentre (juce::jmin (f.getWidth(), f.getHeight() + 20), f.getHeight()));
        legend ("FREQ  Hz", col.removeFromTop (12), 8.0f, false);
        auto g = col.removeFromTop (h * 44 / 100);
        bands[b].gain->setBounds (g.withSizeKeepingCentre (juce::jmin (g.getWidth(), g.getHeight() + 10), g.getHeight()));
        legend ("GAIN  dB", col.removeFromTop (12), 8.0f, false);
        col.removeFromTop (4);
        auto sw = col;
        if (bands[b].shelf != nullptr)
        {
            bands[b].cut->setBounds (sw.removeFromLeft (sw.getWidth() / 2).withSizeKeepingCentre (44, juce::jmin (64, sw.getHeight())));
            bands[b].shelf->setBounds (sw.withSizeKeepingCentre (44, juce::jmin (64, sw.getHeight())));
        }
        else
            bands[b].cut->setBounds (sw.withSizeKeepingCentre (44, juce::jmin (64, sw.getHeight())));
    }

    // filters and output
    c.removeFromTop (8);
    rules.push_back ({ (float) c.getX(), (float) c.getY(), (float) c.getRight(), (float) c.getY() });
    c.removeFromTop (6);
    auto names = c.removeFromTop (16);
    const int fw = c.getWidth() / 6;
    const char* titles[] = { "HIGH-PASS", "SLOPE", "LOW-PASS", "MONO BASS", "MODE", "OUTPUT" };
    juce::Component* ctl[] = { hpf.get(), hpf24.get(), lpf.get(), mono.get(), warm.get(), out.get() };
    for (int i = 0; i < 6; ++i)
    {
        legend (titles[i], names.removeFromLeft (fw), 10.0f);
        auto a = c.removeFromLeft (fw);
        if (dynamic_cast<Toggle*> (ctl[i]) != nullptr)
            ctl[i]->setBounds (a.withSizeKeepingCentre (44, juce::jmin (64, a.getHeight())));
        else
            ctl[i]->setBounds (a.withSizeKeepingCentre (juce::jmin (a.getWidth(), a.getHeight() + 20), a.getHeight()));
    }

    scope.setBounds (scopeArea.reduced (0, 4));
}
} // namespace bbui::rack
