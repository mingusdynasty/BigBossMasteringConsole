#include "CompUnit.h"

#include "plugin/Params.h"

namespace bbui::rack
{
namespace
{
// black anodised plate, gold legends
const Finish kFinish { juce::Colour (0xff2a2b2e), juce::Colour (0xff18191b), juce::Colour (0xffe2c27f), juce::Colour (0xffff9a2e), true };

using Scale = std::vector<std::pair<float, const char*>>;
const Scale kThr { { -40, "-40" }, { -30, "-30" }, { -20, "-20" }, { -10, "-10" }, { 0, "0" } };
const Scale kGain { { -6, "-6" }, { 0, "0" }, { 6, "+6" }, { 12, "+12" } };
}

CompUnit::CompUnit (Apvts& state, const bb::DualCompressor& c)
    : Unit ("STEREO MASTERING COMPRESSOR", juce::String::fromUTF8 ("BB-2  ·  OPTICAL / DISCRETE  ·  STEREO LINKED"), kFinish), comp (c)
{
    optoIn   = std::make_unique<Toggle> (state, pid::compOpto, "IN", "OUT");
    vcaIn    = std::make_unique<Toggle> (state, pid::compVca, "IN", "OUT");
    scHpf    = std::make_unique<Toggle> (state, pid::compScHpf, "90 Hz", "FULL");
    optoThr  = std::make_unique<Knob> (state, pid::compOptoThr, "big", kThr);
    optoGain = std::make_unique<Knob> (state, pid::compOptoGain, "small", kGain);
    vcaThr   = std::make_unique<Knob> (state, pid::compVcaThr, "big", kThr);
    vcaGain  = std::make_unique<Knob> (state, pid::compVcaGain, "small", kGain);
    ratio    = std::make_unique<Knob> (state, pid::compRatio, "chicken",
                                       Scale { { 0, "1.2" }, { 1, "2" }, { 2, "3" }, { 3, "4" }, { 4, "6" }, { 5, "FLD" } }, true);
    attack   = std::make_unique<Knob> (state, pid::compAttack, "chicken",
                                       Scale { { 0, ".1" }, { 1, ".3" }, { 2, "1" }, { 3, "3" }, { 4, "10" }, { 5, "30" } }, true);
    recovery = std::make_unique<Knob> (state, pid::compRecovery, "chicken",
                                       Scale { { 0, ".1" }, { 1, ".25" }, { 2, ".4" }, { 3, ".8" }, { 4, "1.2" }, { 5, "DUAL" } }, true);
    xfmr     = std::make_unique<Knob> (state, pid::compXfmr, "chicken", Scale { { 0, "NICKEL" }, { 1, "IRON" }, { 2, "STEEL" } }, true);
    xfmr->setRotaryParameters (juce::MathConstants<float>::pi * 1.65f, juce::MathConstants<float>::pi * 2.35f, true);
    for (juce::Component* k : std::initializer_list<juce::Component*> { &optoMeter, &vcaMeter, optoIn.get(), vcaIn.get(), scHpf.get(),
                                                                         optoThr.get(), optoGain.get(), vcaThr.get(), vcaGain.get(),
                                                                         ratio.get(), attack.get(), recovery.get(), xfmr.get() })
        addAndMakeVisible (*k);
}

void CompUnit::tick()
{
    Unit::tick();
    optoMeter.push (comp.optoReduction(), 30.0f);
    vcaMeter.push (comp.vcaReduction(), 30.0f);
}

void CompUnit::layout()
{
    auto c = content();
    c.removeFromTop (6);
    const int w = c.getWidth();
    auto left = c.removeFromLeft (w * 30 / 100);
    auto mid = c.removeFromLeft (w * 18 / 100);
    auto right = c;
    rules.push_back ({ (float) mid.getX(), (float) mid.getY(), (float) mid.getX(), (float) mid.getBottom() });
    rules.push_back ({ (float) right.getX(), (float) right.getY(), (float) right.getX(), (float) right.getBottom() });

    auto place = [] (juce::Component& k, juce::Rectangle<int> a, int maxW)
    { k.setBounds (a.withSizeKeepingCentre (juce::jmin (a.getWidth(), maxW), a.getHeight())); };
    auto toggle = [] (juce::Component& t, juce::Rectangle<int> a) { t.setBounds (a.withSizeKeepingCentre (48, juce::jmin (70, a.getHeight()))); };

    // optical section
    legend ("OPTICAL", left.removeFromTop (18), 12.0f);
    optoMeter.setBounds (left.removeFromTop (160).reduced (14, 2));
    left.removeFromTop (10);
    auto ln = left.removeFromTop (16), lk = left.removeFromTop (juce::jmin (140, left.getHeight() - 100));
    legend ("THRESHOLD", ln.removeFromLeft (ln.getWidth() * 3 / 5), 10.0f);
    legend ("GAIN", ln, 10.0f);
    place (*optoThr, lk.removeFromLeft (lk.getWidth() * 3 / 5), 150);
    place (*optoGain, lk, 110);
    legend ("STAGE", left.removeFromTop (16), 10.0f);
    toggle (*optoIn, left);

    // centre: transformer and sidechain
    legend ("OUTPUT", mid.removeFromTop (18), 12.0f);
    legend ("TRANSFORMER", mid.removeFromTop (14), 10.0f);
    place (*xfmr, mid.removeFromTop (150), 220);
    mid.removeFromTop (30);
    legend ("SIDECHAIN", mid.removeFromTop (16), 10.0f);
    legend ("HIGH-PASS", mid.removeFromTop (14), 9.0f, false);
    toggle (*scHpf, mid.removeFromTop (80));
    legend ("BOTH STAGES SEE", mid.removeFromTop (14), 8.0f, false);
    legend ("THE LINKED SUM", mid.removeFromTop (14), 8.0f, false);

    // discrete section
    legend ("DISCRETE", right.removeFromTop (18), 12.0f);
    vcaMeter.setBounds (right.removeFromTop (160).withSizeKeepingCentre (juce::jmin (right.getWidth() - 28, 300), 156));
    right.removeFromTop (10);
    const int rh = right.getHeight() / 2;
    auto row1 = right.removeFromTop (rh), row2 = right;
    const int cw = row1.getWidth() / 3;
    const char* n1[] = { "THRESHOLD", "RATIO", "ATTACK  ms" };
    juce::Component* k1[] = { vcaThr.get(), ratio.get(), attack.get() };
    const char* n2[] = { "STAGE", "RECOVERY  s", "GAIN" };
    juce::Component* k2[] = { vcaIn.get(), recovery.get(), vcaGain.get() };
    for (int i = 0; i < 3; ++i)
    {
        auto a = row1.removeFromLeft (cw);
        legend (n1[i], a.removeFromTop (16), 10.0f);
        place (*k1[i], a, i == 0 ? 150 : 180);
        auto b = row2.removeFromLeft (cw);
        legend (n2[i], b.removeFromTop (16), 10.0f);
        if (i == 0) toggle (*k2[i], b); else place (*k2[i], b, i == 1 ? 180 : 110);
    }
}
} // namespace bbui::rack
