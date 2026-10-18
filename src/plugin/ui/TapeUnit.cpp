#include "TapeUnit.h"

#include "plugin/Params.h"

namespace bbui::rack
{
namespace
{
// the machine's warm grey enamel, legends printed in near-black
const Finish kFinish { juce::Colour (0xffd2cdc1), juce::Colour (0xffb8b2a4), juce::Colour (0xff24221f), juce::Colour (0xffe2432e), false };
}

TapeUnit::TapeUnit (Apvts& state, const bb::TapeMachine& m, std::function<bb::TapeMachine::Settings()> s)
    : Unit ("TWO-TRACK MASTERING RECORDER", juce::String::fromUTF8 ("BB-80  ·  1/4 INCH  ·  TRANSFORMER OUTPUT"), kFinish), machine (m), settings (std::move (s))
{
    speed    = std::make_unique<Toggle> (state, pid::tapeSpeed, "30", "15");
    standard = std::make_unique<Toggle> (state, pid::tapeEq, "CCIR", "NAB");
    link     = std::make_unique<Toggle> (state, pid::tapeLink, "LINK", "FREE");
    tape = std::make_unique<Knob> (state, pid::tapeType, "chicken", std::vector<std::pair<float, const char*>> { { 0, "456" }, { 1, "GP9" } }, true);
    bias = std::make_unique<Knob> (state, pid::tapeBias, "chicken",
                                   std::vector<std::pair<float, const char*>> { { 0, "UNDER" }, { 1, "NOM" }, { 2, "OVER" } }, true);
    const std::vector<std::pair<float, const char*>> level { { -12, "-12" }, { -6, "-6" }, { 0, "0" }, { 6, "+6" }, { 12, "+12" } };
    in  = std::make_unique<Knob> (state, pid::tapeIn, "big", level);
    out = std::make_unique<Knob> (state, pid::tapeOut, "big", level);
    wow = std::make_unique<Knob> (state, pid::tapeWow, "small",
                                  std::vector<std::pair<float, const char*>> { { 0, "0" }, { 25, "25" }, { 50, "50" }, { 75, "75" }, { 100, "100" } });
    // narrower travel for the selectors, like a detented switch
    for (auto* k : { tape.get(), bias.get() })
        k->setRotaryParameters (juce::MathConstants<float>::pi * 1.65f, juce::MathConstants<float>::pi * 2.35f, true);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &vuL, &vuR, speed.get(), standard.get(), link.get(), tape.get(),
                                                                         bias.get(), in.get(), out.get(), wow.get() })
        addAndMakeVisible (*c);
}

void TapeUnit::tick()
{
    Unit::tick();
    vuL.push (machine.recordLevel (0), 30.0f);
    vuR.push (machine.recordLevel (1), 30.0f);
    if (settings)
    {
        const auto s = settings();
        if (s.tape != shownTape || s.speed != shownSpeed)
        {
            shownTape = s.tape;
            shownSpeed = s.speed;
            repaint (molArea.getUnion (eqNoteArea));
        }
    }
}

void TapeUnit::layout()
{
    auto c = content();
    c.removeFromTop (8);

    // left: the meter bridge over the transport switches
    auto left = c.removeFromLeft (juce::jmin (500, c.getWidth() / 2));
    c.removeFromLeft (16);
    rules.push_back ({ (float) c.getX() - 8, (float) c.getY(), (float) c.getX() - 8, (float) c.getBottom() });

    auto bridge = left.removeFromTop (juce::jmin (200, left.getHeight() / 2));
    auto vl = bridge.removeFromLeft (bridge.getWidth() / 2);
    vuL.setBounds (vl.reduced (6, 4));
    vuR.setBounds (bridge.reduced (6, 4));
    legend (juce::String::fromUTF8 ("RECORD LEVEL  ·  0 VU = -18 dBFS"), left.removeFromTop (18), 9.0f, false);
    left.removeFromTop (10);
    rules.push_back ({ (float) left.getX(), (float) left.getY(), (float) left.getRight(), (float) left.getY() });
    left.removeFromTop (10);

    auto names = left.removeFromTop (16);
    auto sw = left.removeFromTop (juce::jmin (90, left.getHeight() - 30));
    const int w3 = sw.getWidth() / 3;
    legend ("SPEED  IPS", names.removeFromLeft (w3), 10.0f);
    legend ("EQUALIZATION", names.removeFromLeft (w3), 10.0f);
    legend ("WOW & FLUTTER", names, 10.0f);
    speed->setBounds (sw.removeFromLeft (w3).withSizeKeepingCentre (48, juce::jmin (70, sw.getHeight())));
    auto eqA = sw.removeFromLeft (w3);
    standard->setBounds (eqA.withSizeKeepingCentre (48, juce::jmin (70, sw.getHeight())));
    wow->setBounds (sw.withSizeKeepingCentre (juce::jmin (sw.getWidth(), sw.getHeight() + 30), sw.getHeight()));
    eqNoteArea = juce::Rectangle<int> (eqA.getX(), sw.getBottom() + 2, eqA.getWidth(), 14);

    // right: levels on top, tape and bias selectors below
    auto top = c.removeFromTop (c.getHeight() / 2);
    auto tn = top.removeFromTop (16);
    const int w = top.getWidth() / 3;
    legend ("INPUT  dB", tn.removeFromLeft (w), 10.0f);
    legend ("LEVEL", tn.removeFromLeft (w), 10.0f);
    legend ("OUTPUT  dB", tn, 10.0f);
    auto ia = top.removeFromLeft (w), la = top.removeFromLeft (w);
    in->setBounds (ia.withSizeKeepingCentre (juce::jmin (ia.getWidth(), ia.getHeight() + 10), ia.getHeight()));
    link->setBounds (la.withSizeKeepingCentre (48, juce::jmin (70, la.getHeight())));
    out->setBounds (top.withSizeKeepingCentre (juce::jmin (top.getWidth(), top.getHeight() + 10), top.getHeight()));
    legend ("LINK: OUTPUT FOLLOWS INPUT", la.withTrimmedTop (la.getHeight() / 2 + 40).withHeight (14), 8.0f, false);

    c.removeFromTop (8);
    rules.push_back ({ (float) c.getX(), (float) c.getY(), (float) c.getRight(), (float) c.getY() });
    c.removeFromTop (8);
    auto bn = c.removeFromTop (16);
    const int w2 = c.getWidth() / 2;
    legend ("TAPE", bn.removeFromLeft (w2), 10.0f);
    legend ("BIAS", bn, 10.0f);
    auto ta = c.removeFromLeft (w2);
    auto sel = ta.withTrimmedBottom (16);
    tape->setBounds (sel.withSizeKeepingCentre (juce::jmin (sel.getWidth(), 170), juce::jmin (sel.getHeight(), 150)));
    molArea = ta.removeFromBottom (16);
    sel = c.withTrimmedBottom (16);
    bias->setBounds (sel.withSizeKeepingCentre (juce::jmin (sel.getWidth(), 170), juce::jmin (sel.getHeight(), 150)));
    legend (juce::String::fromUTF8 ("UNDER: HOTTER, BRIGHTER  ·  OVER: CLEANER, DARKER"), c.removeFromBottom (16), 8.0f, false);
}

void TapeUnit::paintPlate (juce::Graphics& g)
{
    if (shownTape >= 0)
        engrave (g, "MOL  +" + juce::String ((int) bb::TapeMachine::molDb (shownTape)) + juce::String::fromUTF8 (" dB  ·  3% THD"), molArea, 8.0f, juce::Justification::centred, false);
    if (shownSpeed >= 0)
        engrave (g, shownSpeed == bb::TapeMachine::ips30 ? "AES AT 30 IPS" : "", eqNoteArea, 8.0f, juce::Justification::centred, false);
}
} // namespace bbui::rack
