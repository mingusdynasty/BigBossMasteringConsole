#include "Panels.h"

#include "plugin/Params.h"
#include "plugin/PluginProcessor.h"

#include "BigBossFonts.h"

#include <map>
#include <tuple>

namespace kit
{
namespace
{
juce::Point<float> centreOf (const juce::Component& c) { return c.getBounds().toFloat().getCentre(); }
float toDb (float lin) { return lin > 1e-6f ? 20.0f * std::log10 (lin) : -120.0f; }

// faces and inks
const auto eqFace = juce::Colour (0xff22262b), eqInk = juce::Colour (0xffdfe3e8);
const auto compFace = juce::Colour (0xff1b1c1e), compInk = juce::Colour (0xffe2c27f);
const auto tapeFace = juce::Colour (0xffa9adb0), tapeInk = juce::Colour (0xff17191b);
const auto deessFace = juce::Colour (0xff1f3550), deessInk = juce::Colour (0xffdbe4ef);
const auto limitFace = juce::Colour (0xffc3c6ca), limitInk = juce::Colour (0xff17181a);
const auto consoleFace = juce::Colour (0xff2b2e31), consoleInk = juce::Colour (0xffbdbdbd);
}

// ===================================================================== Panel

Panel::Knob& Panel::addKnob (const juce::String& paramId, KnobStyle style, int size, const juce::String& tooltip, bool detents)
{
    auto k = std::make_unique<Knob>();
    k->size = size;
    k->paramId = paramId;
    k->slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
    k->slider->setRotaryParameters (startAngle, endAngle, true);
    k->slider->setPopupDisplayEnabled (true, true, nullptr);
    if (detents) k->slider->setMouseDragSensitivity (120);   // a few clicks of travel per position
    k->slider->setVelocityModeParameters (1.0, 1, 0.0, false);   // modifier keys mean fine, not velocity
    k->fine = std::make_unique<FineDrag> (*k->slider, detents ? 120 : 250);
    k->slider->setTooltip (tooltip.trimCharactersAtEnd (".")
                           + (tooltip.contains ("Double-click") ? ". Hold Ctrl or Shift for fine moves." : ". Double-click to reset; hold Ctrl or Shift for fine moves."));
    setStyle (*k->slider, style);
    k->attachment = std::make_unique<Apvts::SliderAttachment> (state, paramId, *k->slider);
    if (auto* p = state.getParameter (paramId))
    {
        k->slider->setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        // the parameter's own text in the drag popup: "4.5 kHz", "OFF", "+2.0 dB"
        k->slider->textFromValueFunction = [p] (double v) { return p->getText (p->convertTo0to1 ((float) v), 32); };
    }
    addAndMakeVisible (*k->slider);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

Panel::Switch& Panel::addSwitch (const juce::String& paramId, ButtonStyle style, const juce::String& text,
                                 const juce::String& tooltip, bool invert)
{
    auto s = std::make_unique<Switch>();
    s->button = std::make_unique<juce::ToggleButton> (text);
    s->button->setTooltip (tooltip);
    setStyle (*s->button, style);
    if (invert)
    {
        auto* param = state.getParameter (paramId);
        auto* button = s->button.get();
        s->inverted = std::make_unique<juce::ParameterAttachment> (*param, [button] (float v)
        {
            button->setToggleState (v < 0.5f, juce::dontSendNotification);
        });
        button->onClick = [att = s->inverted.get(), button] { att->setValueAsCompleteGesture (button->getToggleState() ? 0.0f : 1.0f); };
        s->inverted->sendInitialUpdate();
    }
    else
        s->attachment = std::make_unique<Apvts::ButtonAttachment> (state, paramId, *s->button);
    addAndMakeVisible (*s->button);
    switches.push_back (std::move (s));
    return *switches.back();
}

Panel::Switch& Panel::addPower (const juce::String& bypassParamId)
{
    return addSwitch (bypassParamId, ButtonStyle::power, {}, "In: takes this module in or out of the chain (a 10 ms crossfade)", true);
}

ChoiceButtons& Panel::addChoice (const juce::String& paramId, const juce::StringArray& labels, ButtonStyle style, bool vertical,
                                 const juce::String& tooltip)
{
    choices.push_back (std::make_unique<ChoiceButtons> (state, paramId, labels, style, vertical));
    choices.back()->setTooltipForButtons (tooltip);
    addAndMakeVisible (*choices.back());
    return *choices.back();
}

void Panel::placeKnob (Knob& k, juce::Point<int> centre)
{
    const auto outer = juce::roundToInt ((float) k.size / 0.88f);
    k.slider->setBounds (juce::Rectangle<int> (outer, outer).withCentre (centre));
}

std::vector<std::pair<float, juce::String>> Panel::marksFor (const juce::String& paramId,
                                                             const std::vector<std::pair<float, juce::String>>& valuesAndLabels) const
{
    std::vector<std::pair<float, juce::String>> marks;
    if (auto* p = state.getParameter (paramId))
        for (const auto& [v, label] : valuesAndLabels)
            marks.emplace_back (p->convertTo0to1 (v), label);
    return marks;
}

void Panel::scale (juce::Graphics& g, const Knob& k, const std::vector<std::pair<float, juce::String>>& valuesAndLabels,
                   juce::Colour ink, float fontHeight) const
{
    drawScale (g, centreOf (*k.slider), (float) k.size * 0.5f, marksFor (k.paramId, valuesAndLabels), ink, fontHeight);
}

// Faceplate finishes, rendered once per finish, colour and pixel size at the screen's resolution
void Panel::paintFinish (juce::Graphics& g, juce::Colour base, Finish finish) const
{
    const auto px = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto w = juce::jmax (1, juce::roundToInt ((float) getWidth() * px));
    const auto h = juce::jmax (1, juce::roundToInt ((float) getHeight() * px));

    static std::map<std::tuple<int, juce::uint32, int, int>, juce::Image> cache;
    const auto key = std::make_tuple ((int) finish, base.getARGB(), w, h);
    auto it = cache.find (key);
    if (it == cache.end())
    {
        juce::Image img (juce::Image::RGB, w, h, false);
        juce::Image::BitmapData bits (img, juce::Image::BitmapData::writeOnly);
        juce::Random rng (99);
        std::vector<float> rowPhase ((size_t) h), rowAmp ((size_t) h);
        for (int y = 0; y < h; ++y) { rowPhase[(size_t) y] = rng.nextFloat() * 100.0f; rowAmp[(size_t) y] = rng.nextFloat(); }
        for (int y = 0; y < h; ++y)
        {
            const auto v = (float) y / (float) h;
            for (int x = 0; x < w; ++x)
            {
                const auto u = (float) x / (float) w;
                float shade = 1.0f;
                switch (finish)
                {
                    case Finish::brushed:
                    {
                        const auto ph = rowPhase[(size_t) y];
                        const auto streak = std::sin (u * 23.0f + ph) * std::sin (u * 7.3f + 1.7f * ph);
                        shade += 0.07f * rowAmp[(size_t) y] * streak + 0.015f * (rng.nextFloat() - 0.5f);
                        shade += 0.10f * std::exp (-std::pow ((v - 0.32f) / 0.22f, 2.0f)) - 0.08f * v;
                        break;
                    }
                    case Finish::enamel:
                    {
                        const auto peel = std::sin (u * 900.0f + std::sin (v * 37.0f) * 3.0f) * std::sin (v * 61.0f + std::sin (u * 410.0f) * 2.0f);
                        shade += 0.012f * peel + 0.01f * (rng.nextFloat() - 0.5f);
                        shade += 0.06f * (1.0f - v) - 0.03f;
                        break;
                    }
                    case Finish::powder:
                        shade += 0.06f * (rng.nextFloat() - 0.5f) + 0.04f * (1.0f - v) - 0.02f;
                        break;
                }
                bits.setPixelColour (x, y, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, base.getFloatRed() * shade),
                                                                        juce::jlimit (0.0f, 1.0f, base.getFloatGreen() * shade),
                                                                        juce::jlimit (0.0f, 1.0f, base.getFloatBlue() * shade), 1.0f));
            }
        }
        it = cache.emplace (key, img).first;
    }
    g.setOpacity (1.0f);
    g.drawImage (it->second, getLocalBounds().toFloat());

    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawHorizontalLine (0, 0.0f, r.getRight());
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, r.getRight());
}

void Panel::nameplate (juce::Graphics& g, const juce::String& name, const juce::String& line1, const juce::String& line2,
                       juce::Colour ink) const
{
    drawEngraved (g, name, { 44, 26, 110, 28 }, ink, 24.0f, true, juce::Justification::left);
    drawEngraved (g, line1, { 44, 58, 110, 11 }, ink.withAlpha (0.85f), 8.0f, true, juce::Justification::left);
    drawEngraved (g, line2, { 44, 70, 110, 11 }, ink.withAlpha (0.6f), 7.5f, true, juce::Justification::left);
    if (onDrag != nullptr)   // the grip: drag the name plate to move the module in the chain
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (ink.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (36.0f, 1.5f).withCentre ({ 88.0f, 96.0f + 4.0f * (float) i }));
        }
}

void Panel::legendOver (juce::Graphics& g, const juce::String& text, int centreX, juce::Colour ink, int y, int width) const
{
    drawEngraved (g, text, juce::Rectangle<float> ((float) width, 12.0f).withCentre ({ (float) centreX, (float) y + 6.0f }), ink, 8.5f, true);
}

void Panel::rule (juce::Graphics& g, int x, juce::Colour ink) const
{
    g.setColour (ink.withAlpha (0.22f));
    g.drawVerticalLine (x, 10.0f, (float) getHeight() - 10.0f);
}

void Panel::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (onDrag != nullptr && nameArea().contains (e.getPosition()) ? juce::MouseCursor::DraggingHandCursor
                                                                                 : juce::MouseCursor::NormalCursor);
}

void Panel::mouseDown (const juce::MouseEvent& e) { dragging = onDrag != nullptr && nameArea().contains (e.getPosition()); }

void Panel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging) onDrag (e.getDistanceFromDragStartY());
}

void Panel::mouseUp (const juce::MouseEvent&)
{
    if (dragging && onDrop != nullptr) onDrop();
    dragging = false;
}

// ===================================================================== EQ

namespace
{
constexpr int eqY = 58;
int eqFreqX (int b) { return 296 + 150 * b; }
int eqGainX (int b) { return 356 + 150 * b; }
const std::vector<std::pair<float, juce::String>> kEqFreqMarks[5] = {
    { { 20.0f, "20" }, { 80.0f, "80" }, { 300.0f, "300" } },
    { { 40.0f, "40" }, { 200.0f, "200" }, { 800.0f, "800" } },
    { { 200.0f, "200" }, { 1000.0f, "1k" }, { 4000.0f, "4k" } },
    { { 800.0f, ".8k" }, { 4000.0f, "4k" }, { 12000.0f, "12k" } },
    { { 2500.0f, "2.5k" }, { 10000.0f, "10k" }, { 28000.0f, "28k" } },
};
}

EqPanel::EqPanel (Apvts& s) : Panel (s)
{
    power = &addPower (pid::bypass ("eq"));
    hpf = &addKnob (pid::eqHpf, KnobStyle::neveBlue, 36, "High-pass: OFF, or 10 to 300 Hz");
    hpf24 = &addSwitch (pid::eqHpf24, ButtonStyle::consoleKey, "24 dB", "High-pass slope: 12 or 24 dB/octave");
    for (int b = 0; b < 5; ++b)
    {
        const juce::String name = juce::String (bb::eq::kRange[b].name).toLowerCase();
        freq[(size_t) b] = &addKnob (pid::eqFreq (b), KnobStyle::neveBlue, 36, "Frequency, " + name + " band");
        gain[(size_t) b] = &addKnob (pid::eqGain (b), KnobStyle::ureiSmall, 46,
                                     "Gain, " + name + " band, 0 to 12 dB. BOOST is broad and gentle, CUT narrower, as on the hardware.");
        cut[(size_t) b] = &addSwitch (pid::eqCut (b), ButtonStyle::consoleKey, "CUT", "Boost or cut, " + name + " band");
        if (bb::eq::kRange[b].canShelf)
            shelf[(size_t) b] = &addSwitch (pid::eqShelf (b), ButtonStyle::consoleKey, "SHELF", "Shelf or bell, " + name + " band");
    }
    lpf = &addKnob (pid::eqLpf, KnobStyle::neveBlue, 36, "Low-pass: 4 to 30 kHz, or OFF");
    mono = &addKnob (pid::eqMono, KnobStyle::neveBlue, 36, "Mono bass: below this frequency the side signal is removed");
    out = &addKnob (pid::eqOut, KnobStyle::ureiSmall, 40, "Output level, -12 to +12 dB");
    warm = &addSwitch (pid::eqWarm, ButtonStyle::consoleKey, "WARM", "Warm: the class-A stage's low-order harmonics");
    stereo = &addChoice (pid::eqStereo, { "ST", "MID", "SIDE" }, ButtonStyle::consoleKey, false,
                         "Where the five bands work: both channels (ST), the mid only, or the side only. "
                         "The filters and Warm stay stereo.");
}

void EqPanel::resized()
{
    placePower (*power);
    placeKnob (*hpf, { 196, eqY });
    hpf24->button->setBounds (juce::Rectangle<int> (40, 15).withCentre ({ 196, 103 }));
    for (int b = 0; b < 5; ++b)
    {
        placeKnob (*freq[(size_t) b], { eqFreqX (b), eqY });
        placeKnob (*gain[(size_t) b], { eqGainX (b), eqY });
        cut[(size_t) b]->button->setBounds (juce::Rectangle<int> (38, 15).withCentre ({ eqGainX (b), 103 }));
        if (shelf[(size_t) b] != nullptr)
            shelf[(size_t) b]->button->setBounds (juce::Rectangle<int> (40, 15).withCentre ({ eqFreqX (b), 103 }));
    }
    placeKnob (*lpf, { 1030, eqY });
    placeKnob (*mono, { 1088, eqY });
    placeKnob (*out, { 1148, eqY });
    warm->button->setBounds (juce::Rectangle<int> (42, 15).withCentre ({ 1148, 103 }));
    stereo->setBounds (1006, 96, 108, 15);
}

void EqPanel::paint (juce::Graphics& g)
{
    paintFinish (g, eqFace, Finish::powder);
    nameplate (g, "BB-5", "CLASS-A EQUALISER", juce::String::fromUTF8 ("FIVE BANDS · PASSIVE FILTERS"), eqInk);
    const auto dim = eqInk.withAlpha (0.75f);
    legendOver (g, "HIGH-PASS", 196, eqInk);
    scale (g, *hpf, { { 0.0f, "OFF" }, { 30.0f, "30" }, { 300.0f, "300" } }, dim, 6.5f);
    rule (g, 240, eqInk);
    for (int b = 0; b < 5; ++b)
    {
        legendOver (g, bb::eq::kRange[b].name, (eqFreqX (b) + eqGainX (b)) / 2, eqInk);
        scale (g, *freq[(size_t) b], kEqFreqMarks[b], dim, 6.5f);
        scale (g, *gain[(size_t) b], { { 0.0f, "0" }, { 3.0f, "" }, { 6.0f, "6" }, { 9.0f, "" }, { 12.0f, "12" } }, dim, 6.5f);
        rule (g, 390 + 150 * b, eqInk);
    }
    legendOver (g, "LOW-PASS", 1030, eqInk, 4, 60);
    scale (g, *lpf, { { 4000.0f, "4k" }, { 12000.0f, "12k" }, { 0.0f, "OFF" } }, dim, 6.5f);
    legendOver (g, "MONO", 1088, eqInk, 4, 50);
    scale (g, *mono, { { 0.0f, "OFF" }, { 100.0f, "100" }, { 300.0f, "300" } }, dim, 6.5f);
    legendOver (g, "OUTPUT", 1148, eqInk, 4, 56);
    scale (g, *out, { { -12.0f, "-12" }, { 0.0f, "0" }, { 12.0f, "+12" } }, dim, 6.5f);
}

// ===================================================================== compressor

CompPanel::CompPanel (Apvts& s, const bb::DualCompressor& comp)
    : Panel (s),
      optoMeter ([&comp] { return -comp.optoReduction(); }, "GR"),
      vcaMeter ([&comp] { return -comp.vcaReduction(); }, "GR")
{
    power = &addPower (pid::bypass ("comp"));
    optoIn = &addSwitch (pid::compOpto, ButtonStyle::dbxPush, "IN", "Optical stage in or out");
    optoThr = &addKnob (pid::compOptoThr, KnobStyle::dbxBlack, 52, "Optical threshold: about 3:1 with a wide knee; the release slows the longer it works");
    optoGain = &addKnob (pid::compOptoGain, KnobStyle::dbxBlack, 36, "Optical stage makeup gain");
    xfmr = &addChoice (pid::compXfmr, { "NICKEL", "IRON", "STEEL" }, ButtonStyle::dbxPush, true,
                       "Output transformer: Nickel clean, Iron low 2nd harmonic, Steel low 3rd harmonic and weight");
    scHpf = &addSwitch (pid::compScHpf, ButtonStyle::dbxPush, "SC 90", "Sidechain high-pass at 90 Hz, so bass does not drive the compression");
    link = &addSwitch (pid::compLink, ButtonStyle::dbxPush, "LINK", "Stereo link: both channels share one gain so the image holds still. Off: dual mono, each channel compressed on its own");
    vcaIn = &addSwitch (pid::compVca, ButtonStyle::dbxPush, "IN", "Discrete stage in or out");
    vcaThr = &addKnob (pid::compVcaThr, KnobStyle::dbxBlack, 52, "Discrete threshold");
    ratio = &addKnob (pid::compRatio, KnobStyle::ureiSmall, 38, "Ratio, or Flood", true);
    attack = &addKnob (pid::compAttack, KnobStyle::ureiSmall, 38, "Attack, ms", true);
    recovery = &addKnob (pid::compRecovery, KnobStyle::ureiSmall, 38, "Recovery, s, or Dual (fast then slow)", true);
    vcaGain = &addKnob (pid::compVcaGain, KnobStyle::dbxBlack, 36, "Discrete stage makeup gain");
    addAndMakeVisible (optoMeter);
    addAndMakeVisible (vcaMeter);
}

void CompPanel::tick (float seconds)
{
    optoMeter.tick (seconds);
    vcaMeter.tick (seconds);
}

void CompPanel::resized()
{
    placePower (*power);
    optoIn->button->setBounds (juce::Rectangle<int> (34, 18).withCentre ({ 184, 62 }));
    placeKnob (*optoThr, { 250, 60 });
    placeKnob (*optoGain, { 322, 62 });
    optoMeter.setBounds (360, 18, 128, 86);
    xfmr->setBounds (508, 14, 60, 54);
    scHpf->button->setBounds (508, 72, 60, 16);
    link->button->setBounds (508, 92, 60, 16);
    vcaIn->button->setBounds (juce::Rectangle<int> (34, 18).withCentre ({ 604, 62 }));
    placeKnob (*vcaThr, { 670, 60 });
    placeKnob (*ratio, { 748, 62 });
    placeKnob (*attack, { 820, 62 });
    placeKnob (*recovery, { 892, 62 });
    placeKnob (*vcaGain, { 960, 62 });
    vcaMeter.setBounds (1010, 18, 140, 86);
}

void CompPanel::paint (juce::Graphics& g)
{
    paintFinish (g, compFace, Finish::brushed);
    nameplate (g, "BB-2", "MASTERING COMPRESSOR", juce::String::fromUTF8 ("OPTICAL · DISCRETE"), compInk);
    const auto dim = compInk.withAlpha (0.8f);
    legendOver (g, "OPTICAL", 184, compInk, 4, 60);
    legendOver (g, "THRESHOLD", 250, compInk, 4, 70);
    scale (g, *optoThr, { { -40.0f, "-40" }, { -20.0f, "-20" }, { 0.0f, "0" } }, dim, 6.5f);
    legendOver (g, "GAIN", 322, compInk, 4, 50);
    scale (g, *optoGain, { { -6.0f, "-6" }, { 0.0f, "0" }, { 12.0f, "+12" } }, dim, 6.5f);
    rule (g, 498, compInk);
    legendOver (g, "XFMR", 538, compInk, 4, 60);
    rule (g, 578, compInk);
    legendOver (g, "DISCRETE", 604, compInk, 4, 60);
    legendOver (g, "THRESHOLD", 670, compInk, 4, 70);
    scale (g, *vcaThr, { { -40.0f, "-40" }, { -20.0f, "-20" }, { 0.0f, "0" } }, dim, 6.5f);
    legendOver (g, "RATIO", 748, compInk, 4, 60);
    scale (g, *ratio, { { 0.0f, "1.2" }, { 1.0f, "2" }, { 2.0f, "3" }, { 3.0f, "4" }, { 4.0f, "6" }, { 5.0f, "FL" } }, dim, 6.0f);
    legendOver (g, "ATTACK ms", 820, compInk, 4, 66);
    scale (g, *attack, { { 0.0f, ".1" }, { 1.0f, ".3" }, { 2.0f, "1" }, { 3.0f, "3" }, { 4.0f, "10" }, { 5.0f, "30" } }, dim, 6.0f);
    legendOver (g, "RECOVERY s", 892, compInk, 4, 70);
    scale (g, *recovery, { { 0.0f, ".1" }, { 1.0f, ".25" }, { 2.0f, ".4" }, { 3.0f, ".8" }, { 4.0f, "1.2" }, { 5.0f, "DU" } }, dim, 6.0f);
    legendOver (g, "GAIN", 960, compInk, 4, 50);
    scale (g, *vcaGain, { { -6.0f, "-6" }, { 0.0f, "0" }, { 12.0f, "+12" } }, dim, 6.5f);
}

// ===================================================================== tape

TapePanel::TapePanel (Apvts& s, const bb::TapeMachine& machine)
    : Panel (s),
      vuL ([&machine] { const auto ms = machine.recordLevel (0); return ms > 1e-8f ? 10.0f * std::log10 (ms) : -40.0f; }, "VU  L"),
      vuR ([&machine] { const auto ms = machine.recordLevel (1); return ms > 1e-8f ? 10.0f * std::log10 (ms) : -40.0f; }, "VU  R")
{
    power = &addPower (pid::bypass ("tape"));
    speed = &addChoice (pid::tapeSpeed, { "15", "30" }, ButtonStyle::ureiWhite, true, "Tape speed, inches per second");
    eq = &addChoice (pid::tapeEq, { "NAB", "CCIR" }, ButtonStyle::ureiWhite, true, "Equalisation standard");
    formulation = &addChoice (pid::tapeType, { "456", "GP9" }, ButtonStyle::ureiWhite, true, "Tape formulation: 456 saturates earlier, GP9 has more headroom");
    bias = &addChoice (pid::tapeBias, { "UNDER", "NOM", "OVER" }, ButtonStyle::ureiWhite, true, "Bias: under is brighter and dirtier, over darker and cleaner");
    input = &addKnob (pid::tapeIn, KnobStyle::neveGrey, 50, "Record level: how hard the tape is hit. 0 dB puts 0 VU at the operating level.");
    output = &addKnob (pid::tapeOut, KnobStyle::neveGrey, 50, "Playback level");
    link = &addSwitch (pid::tapeLink, ButtonStyle::ureiWhite, "LINK", "Level link: playback follows record inversely, so drive changes colour, not loudness");
    wow = &addKnob (pid::tapeWow, KnobStyle::neveGrey, 36, "Wow and flutter, as a share of the machine's measured figure");
    biasTrim = &addKnob (pid::tapeBiasTrim, KnobStyle::neveGrey, 34, "Bias trim: up to half a step toward under (brighter, dirtier) or over (darker, cleaner)");
    azimuth = &addKnob (pid::tapeAzimuth, KnobStyle::neveGrey, 34, "Azimuth error: the right track lags by up to 20 microseconds, so the top end thins a little in mono");
    xtalk = &addSwitch (pid::tapeXtalk, ButtonStyle::ureiWhite, "XTALK", "Crosstalk: each track picks up the other, 100 Hz to 2 kHz, 42 dB down");
    hiss = &addSwitch (pid::tapeHiss, ButtonStyle::ureiWhite, "HISS", "Tape hiss: about -84 dBFS on 456 at 15 ips, quieter on GP9 and at 30 ips. Follows the playback level");
    addAndMakeVisible (vuL);
    addAndMakeVisible (vuR);
}

void TapePanel::tick (float seconds)
{
    vuL.tick (seconds);
    vuR.tick (seconds);
}

void TapePanel::resized()
{
    placePower (*power);
    speed->setBounds (172, 26, 46, 46);
    eq->setBounds (230, 26, 46, 46);
    formulation->setBounds (288, 26, 46, 46);
    bias->setBounds (346, 22, 52, 72);
    placeKnob (*input, { 464, 60 });
    placeKnob (*output, { 556, 60 });
    link->button->setBounds (juce::Rectangle<int> (40, 18).withCentre ({ 510, 103 }));
    placeKnob (*wow, { 636, 62 });
    placeKnob (*biasTrim, { 706, 62 });
    placeKnob (*azimuth, { 764, 62 });
    xtalk->button->setBounds (800, 40, 46, 18);
    hiss->button->setBounds (800, 72, 46, 18);
    vuL.setBounds (866, 16, 152, 90);
    vuR.setBounds (1028, 16, 152, 90);
}

void TapePanel::paint (juce::Graphics& g)
{
    paintFinish (g, tapeFace, Finish::brushed);
    nameplate (g, "BB-80", "TWO-TRACK TAPE MACHINE", juce::String::fromUTF8 ("1/2 INCH · 15 / 30 IPS"), tapeInk);
    legendOver (g, "IPS", 195, tapeInk, 8, 46);
    legendOver (g, "EQ", 253, tapeInk, 8, 46);
    legendOver (g, "TAPE", 311, tapeInk, 8, 46);
    legendOver (g, "BIAS", 372, tapeInk, 4, 52);
    rule (g, 414, tapeInk);
    legendOver (g, "RECORD", 464, tapeInk, 4, 70);
    legendOver (g, "PLAY", 556, tapeInk, 4, 70);
    const std::vector<std::pair<float, juce::String>> level { { -12.0f, "-12" }, { -6.0f, "" }, { 0.0f, "0" }, { 6.0f, "" }, { 12.0f, "+12" } };
    scale (g, *input, level, tapeInk, 6.5f);
    scale (g, *output, level, tapeInk, 6.5f);
    legendOver (g, "WOW", 636, tapeInk, 4, 50);
    scale (g, *wow, { { 0.0f, "0" }, { 100.0f, "100" } }, tapeInk, 6.5f);
    legendOver (g, "TRIM", 706, tapeInk, 4, 46);
    scale (g, *biasTrim, { { -1.0f, "U" }, { 0.0f, "0" }, { 1.0f, "O" } }, tapeInk, 6.5f);
    legendOver (g, "AZIM", 764, tapeInk, 4, 46);
    scale (g, *azimuth, { { 0.0f, "0" }, { 20.0f, "20" } }, tapeInk, 6.5f);
    rule (g, 856, tapeInk);
}

// ===================================================================== de-esser

namespace
{
constexpr float kSteps[] = { 0.5f, 1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 18 };
constexpr int kLeds = (int) (sizeof (kSteps) / sizeof (kSteps[0]));
}

void BandLadders::push (int band, float reductionDb, float seconds)
{
    auto& lv = level[(size_t) band];
    // LEDs jump up and fall back at 30 dB a second; the top lit one holds for a second
    lv = reductionDb > lv ? reductionDb : juce::jmax (reductionDb, lv - 30.0f * seconds);
    auto& pk = peak[(size_t) band];
    auto& h = held[(size_t) band];
    if (reductionDb >= pk) { pk = reductionDb; h = 1.0f; }
    else if ((h -= seconds) <= 0.0f) pk = lv;
    repaint();
}

void BandLadders::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (b, 4.0f);
    b = b.reduced (4.0f);
    g.setColour (juce::Colour (0xff0b0d10));
    g.fillRoundedRectangle (b, 3.0f);

    auto area = b.reduced (6.0f, 4.0f);
    auto nameCol = area.removeFromLeft (40.0f);
    auto top = area.removeFromTop (12.0f);
    const float rowH = area.getHeight() / (float) bb::DeEsser::kBands;
    const float ledW = area.getWidth() / (float) kLeds;

    g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    for (int i = 0; i < kLeds; ++i)
        if (i == 0 || i % 2 == 1 || i == kLeds - 1)
            g.drawText (juce::String (kSteps[i], kSteps[i] < 1 ? 1 : 0),
                        juce::Rectangle<float> (ledW, 10.0f).withCentre ({ area.getX() + ledW * ((float) i + 0.5f), top.getCentreY() }),
                        juce::Justification::centred);

    for (int k = 0; k < bb::DeEsser::kBands; ++k)
    {
        const auto row = juce::Rectangle<float> (area.getX(), area.getY() + rowH * (float) k, area.getWidth(), rowH).reduced (0, rowH * 0.18f);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText (names[(size_t) k], nameCol.withY (row.getY()).withHeight (row.getHeight()), juce::Justification::centredLeft);
        for (int i = 0; i < kLeds; ++i)
        {
            const auto led = juce::Rectangle<float> (row.getX() + ledW * (float) i, row.getY(), ledW, row.getHeight()).reduced (ledW * 0.12f, 0);
            const bool on = level[(size_t) k] >= kSteps[i]
                            || (peak[(size_t) k] >= kSteps[i] && (i == kLeds - 1 || peak[(size_t) k] < kSteps[i + 1]));
            const auto hue = i < 6 ? juce::Colour (0xff4fd1e0) : (i < 9 ? juce::Colour (0xfff2a43a) : juce::Colour (0xffe5352b));
            g.setColour (on ? hue : hue.withAlpha (0.10f));
            g.fillRoundedRectangle (led, 1.5f);
            if (on)
            {
                g.setColour (hue.withAlpha (0.25f));
                g.fillRoundedRectangle (led.expanded (1.2f), 2.0f);
            }
        }
    }
}

DeEssPanel::DeEssPanel (Apvts& s, const bb::DeEsser& d, std::function<double()> lowest, std::function<double()> rate)
    : Panel (s), deesser (d), lowestBandHz (std::move (lowest)), sampleRate (std::move (rate))
{
    power = &addPower (pid::bypass ("deess"));
    threshold = &addKnob (pid::deessThr, KnobStyle::neveGrey, 52, "Threshold, in dB of band energy over the voice body (300 Hz to 3 kHz)");
    range = &addKnob (pid::deessRange, KnobStyle::neveGrey, 52, "Range: the most any band cuts");
    freq = &addKnob (pid::deessFreq, KnobStyle::neveBlue, 36, "Frequency of the lowest band; the others sit half an octave apart above it");
    release = &addKnob (pid::deessRelease, KnobStyle::neveBlue, 36, "Release");
    mode = &addChoice (pid::deessMode, { "STEREO", "MID", "SIDE" }, ButtonStyle::consoleKey, true,
                       "Mid works on the centre, where a vocal lives; Stereo on both channels, linked; Side on the sides");
    listen = &addSwitch (pid::deessListen, ButtonStyle::consoleKey, "LISTEN", "Play only what is being removed");
    addAndMakeVisible (ladders);
}

void DeEssPanel::tick (float seconds)
{
    for (int k = 0; k < bb::DeEsser::kBands; ++k) ladders.push (k, deesser.reduction (k), seconds);
    if (lowestBandHz != nullptr && sampleRate != nullptr)
    {
        const auto f = lowestBandHz();
        const auto sr = sampleRate();
        for (int k = 0; k < bb::DeEsser::kBands; ++k)
        {
            // a band with no room below the top of the audio band is dropped
            const auto hz = bb::DeEsser::bandFreq (f, k, sr);
            ladders.names[(size_t) k] = hz > 0 ? juce::String (hz / 1000.0, 1) + "k" : juce::String::fromUTF8 ("\xe2\x80\x94");
        }
    }
}

void DeEssPanel::resized()
{
    placePower (*power);
    placeKnob (*threshold, { 206, 60 });
    placeKnob (*range, { 296, 60 });
    placeKnob (*freq, { 372, 62 });
    placeKnob (*release, { 436, 62 });
    mode->setBounds (482, 18, 64, 66);
    listen->button->setBounds (482, 90, 64, 18);
    ladders.setBounds (570, 10, 600, 98);
}

void DeEssPanel::paint (juce::Graphics& g)
{
    paintFinish (g, deessFace, Finish::enamel);
    nameplate (g, "BB-S4", "MULTIBAND DE-ESSER", juce::String::fromUTF8 ("FOUR BANDS · NEW DESIGN"), deessInk);
    const auto dim = deessInk.withAlpha (0.8f);
    legendOver (g, "THRESHOLD", 206, deessInk, 4, 72);
    scale (g, *threshold, { { -24.0f, "-24" }, { -6.0f, "-6" }, { 12.0f, "+12" } }, dim, 6.5f);
    legendOver (g, "RANGE", 296, deessInk, 4, 60);
    scale (g, *range, { { 0.0f, "0" }, { 8.0f, "8" }, { 18.0f, "18" } }, dim, 6.5f);
    legendOver (g, "FREQ", 372, deessInk, 4, 50);
    scale (g, *freq, { { 3000.0f, "3k" }, { 8000.0f, "8k" } }, dim, 6.5f);
    legendOver (g, "RELEASE", 436, deessInk, 4, 56);
    scale (g, *release, { { 10.0f, "10" }, { 200.0f, "200" } }, dim, 6.5f);
    legendOver (g, "MODE", 514, deessInk, 4, 60);
    rule (g, 560, deessInk);
}

// ===================================================================== limiter

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
    g.fillRoundedRectangle (b, 4.0f);
    const auto glass = b.reduced (4.0f);
    g.setColour (juce::Colour (0xff0c0d0f));
    g.fillRoundedRectangle (glass, 3.0f);

    const auto plot = glass.reduced (6.0f, 6.0f).withTrimmedLeft (18.0f);
    auto yLevel = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 24.0f) / 24.0f); };
    auto yRed = [&] (float db) { return plot.getY() + plot.getHeight() * 0.5f * juce::jlimit (0.0f, 1.0f, db / 12.0f); };

    g.setFont (juce::FontOptions (7.0f, juce::Font::bold));
    for (float db : { -18.f, -12.f, -6.f, 0.f })
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine ((int) yLevel (db), plot.getX(), plot.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (20, 9).withCentre ({ plot.getX() - 11, yLevel (db) }), juce::Justification::centred);
    }

    const float dx = plot.getWidth() / (float) (kFrames - 1);
    auto xOf = [&] (int k) { return plot.getX() + dx * (float) k; };
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
    g.strokePath (outLine, juce::PathStrokeType (1.2f));
    const float yc = yLevel (ceilingDb);
    g.setColour (juce::Colour (0xfff2a43a).withAlpha (0.6f));
    const float dash[] = { 4.0f, 3.0f };
    g.drawDashedLine ({ plot.getX(), yc, plot.getRight(), yc }, dash, 2, 1.0f);
}

LimitPanel::LimitPanel (Apvts& s, bb::Limiter& lim, std::function<bb::Limiter::Settings()> st)
    : Panel (s), limiter (lim), settings (std::move (st)),
      meter ([this] { return -lastReduction; }, "GR")
{
    power = &addPower (pid::bypass ("limit"));
    drive = &addKnob (pid::limDrive, KnobStyle::dbxBlack, 56, "Drive into the ceiling, 0 to 24 dB");
    ceiling = &addKnob (pid::limCeiling, KnobStyle::dbxBlack, 56, "Ceiling, 0 to -12 dBTP");
    style = &addChoice (pid::limStyle, { "TRANSP", "PUNCH", "DENSE" }, ButtonStyle::dbxPush, true,
                        "Transparent: long hold and release. Punch: faster. Dense: short hold, fast release, louder.");
    autoRel = &addSwitch (pid::limAuto, ButtonStyle::dbxPush, "AUTO", "Release: automatic (two-speed, programme dependent) or the RELEASE knob");
    release = &addKnob (pid::limRelease, KnobStyle::dbxBlack, 36, "Release, 1 ms to 1 s, when AUTO is off");
    truePeak = &addSwitch (pid::limTp, ButtonStyle::dbxPush, "TRUE PK", "True-peak detection (8x reconstruction), or sample peaks only");
    link = &addKnob (pid::limLink, KnobStyle::dbxBlack, 36, "Stereo link, 0 to 100%");
    stayLast = &addSwitch (pid::limLast, ButtonStyle::dbxPush, "LAST",
                           "Stay last: keep the limiter at the end of the chain, whatever order the modules are dragged into");
    addAndMakeVisible (history);
    addAndMakeVisible (meter);
}

void LimitPanel::tick (float seconds)
{
    float in, out, red;
    limiter.takeMeters (in, out, red);
    lastReduction = red;
    history.push (toDb (in), toDb (out), red);
    meter.tick (seconds);
    if (settings != nullptr)
    {
        const auto s = settings();
        history.ceilingDb = (float) s.ceilingDb;
        release->slider->setAlpha (s.autoRelease ? 0.45f : 1.0f);
    }
}

void LimitPanel::resized()
{
    placePower (*power);
    placeKnob (*drive, { 210, 60 });
    placeKnob (*ceiling, { 306, 60 });
    style->setBounds (356, 18, 64, 72);
    stayLast->button->setBounds (356, 94, 64, 16);
    autoRel->button->setBounds (440, 18, 52, 18);
    placeKnob (*release, { 466, 72 });
    truePeak->button->setBounds (510, 18, 60, 18);
    placeKnob (*link, { 540, 72 });
    history.setBounds (600, 10, 400, 98);
    meter.setBounds (1020, 18, 140, 86);
}

void LimitPanel::paint (juce::Graphics& g)
{
    paintFinish (g, limitFace, Finish::brushed);
    nameplate (g, "BB-L1", "TRUE-PEAK LIMITER", juce::String::fromUTF8 ("8x TRUE PEAK · 3 ms AHEAD"), limitInk);
    const auto dim = limitInk.withAlpha (0.85f);
    legendOver (g, "DRIVE dB", 210, limitInk, 4, 70);
    scale (g, *drive, { { 0.0f, "0" }, { 6.0f, "6" }, { 12.0f, "12" }, { 18.0f, "18" }, { 24.0f, "24" } }, dim, 6.5f);
    legendOver (g, "CEILING dBTP", 306, limitInk, 4, 80);
    scale (g, *ceiling, { { -12.0f, "-12" }, { -6.0f, "-6" }, { -3.0f, "-3" }, { -1.0f, "-1" }, { 0.0f, "0" } }, dim, 6.5f);
    legendOver (g, "STYLE", 388, limitInk, 4, 64);
    legendOver (g, "RELEASE", 466, limitInk, 98, 60);
    scale (g, *release, { { 1.0f, "1" }, { 1000.0f, "1k" } }, dim, 6.0f);
    legendOver (g, "LINK %", 540, limitInk, 98, 60);
    scale (g, *link, { { 0.0f, "0" }, { 100.0f, "100" } }, dim, 6.0f);
    rule (g, 588, limitInk);
}

// ===================================================================== output

OutputPanel::OutputPanel (BigBossProcessor& p)
    : Panel (p.apvts), proc (p),
      faderAttachment (p.apvts, pid::volume, fader),
      inMeter ([&p] (int ch) { return p.takePeakDb (false, ch); }),
      outMeter ([&p] (int ch) { return p.takePeakDb (true, ch); })
{
    trim = &addKnob (pid::inTrim, KnobStyle::dbxBlack, 42, "Input trim, -24 to +24 dB, before the whole chain. Double-click for 0.");
    mono = &addSwitch (pid::outMono, ButtonStyle::consoleKey, "MONO",
                       "Sum the output to mono, after the fader, to hear how the master holds up on one speaker. "
                       "It changes what is rendered too, so switch it off before you bounce.");
    lock = &addSwitch (pid::outLock, ButtonStyle::consoleKey, "CEIL LOCK",
                       "Ceiling lock: a last true-peak guard at the BB-L1's ceiling, after the fader and MONO, so nothing "
                       "leaves the console above it. The lamp below lights amber while it catches, red if the output goes over.");

    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setTooltip ("Output volume, after the last module. Automate it for rides and fades. Double-click for 0 dB; hold Ctrl or Shift for fine moves.");
    fader.setMouseDragSensitivity (600);
    fader.setVelocityBasedMode (false);
    fader.setVelocityModeParameters (1.0, 1, 0.0, false);
    faderFine = std::make_unique<FineDrag> (fader, 600);
    fader.setPopupDisplayEnabled (true, true, nullptr);
    if (auto* param = p.apvts.getParameter (pid::volume))
        fader.textFromValueFunction = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 16); };
    addAndMakeVisible (fader);

    // the dither's choices, in the parameter's own order (ComboBoxAttachment counts from item 1)
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (pid::outDither)))
        dither.addItemList (param->choices, 1);
    ditherAttachment = std::make_unique<Apvts::ComboBoxAttachment> (p.apvts, pid::outDither, dither);
    dither.setTooltip ("Word length of what leaves the console: dither to 24 or 16 bits (TPDF), or 16 bits noise-shaped. "
                       "Use it when you bounce to that word length; leave it OFF for a float or 32-bit file.");
    dither.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0b0b0b));
    dither.setColour (juce::ComboBox::textColourId, juce::Colour (0xffff9a3c));
    dither.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a3a3a));
    dither.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xffbdbdbd));
    addAndMakeVisible (dither);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    readout.setJustificationType (juce::Justification::centred);
    readout.setColour (juce::Label::textColourId, juce::Colour (0xffff9a3c));
    readout.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0b0b0b));
    readout.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (readout);
    fader.onValueChange = [this] { readout.setText (fader.getTextFromValue (fader.getValue()), juce::dontSendNotification); };
    fader.onValueChange();

    setStyle (analyserKey, ButtonStyle::consoleKey);
    analyserKey.setClickingTogglesState (true);
    analyserKey.setTooltip ("Show the analyser (loudness, true peak, spectrum, stereo field) in place of the modules");
    addAndMakeVisible (analyserKey);
}

void OutputPanel::tick (float seconds)
{
    if (proc.takeOverDb() > 0.0f) overHold = 1.0f;
    if (proc.takeLockDb() > 0.05f) lockHold = 1.0f;
    overHold = juce::jmax (0.0f, overHold - seconds);
    lockHold = juce::jmax (0.0f, lockHold - seconds);
    const int lamp = overHold > 0 ? 2 : (lockHold > 0 ? 1 : 0);
    if (lamp != shownLamp)
    {
        shownLamp = lamp;
        repaint (768, 80, 124, 24);
    }
    inMeter.tick();
    outMeter.tick();
    const auto& rd = proc.outMeters.readings;
    shortTerm = rd.shortTerm.load();
    integrated = rd.integrated.load();
    truePeak = juce::jmax (rd.truePeakHeld[0].load(), rd.truePeakHeld[1].load());
    repaint (960, 70, 220, 40);
    if (proc.bypassAll.load() != shownBypass)
    {
        shownBypass = ! shownBypass;
        repaint (178, 80, 120, 14);
    }
}

void OutputPanel::resized()
{
    analyserKey.setBounds (178, 22, 92, 22);
    mono->button->setBounds (178, 52, 92, 22);
    placeKnob (*trim, { 330, 62 });
    fader.setBounds (392, 46, 360, 44);
    lock->button->setBounds (772, 18, 112, 22);
    dither.setBounds (772, 52, 112, 20);
    inMeter.setBounds (900, 18, 196, 16);
    outMeter.setBounds (900, 42, 196, 16);
    readout.setBounds (1104, 28, 64, 22);
}

void OutputPanel::paint (juce::Graphics& g)
{
    paintFinish (g, consoleFace, Finish::brushed);
    drawEngraved (g, "OUTPUT", { 24, 30, 140, 30 }, juce::Colour (0xffe8e8e8), 22.0f, true, juce::Justification::left);
    drawEngraved (g, "VOLUME FADER", { 24, 60, 140, 12 }, consoleInk, 8.5f, true, juce::Justification::left);
    drawEngraved (g, "LAST IN THE CHAIN", { 24, 72, 140, 12 }, consoleInk.withAlpha (0.6f), 7.5f, true, juce::Justification::left);
    if (shownBypass)   // the analyser's BYPASS ALL is on
        drawEngraved (g, "ALL BYPASSED", { 178, 82, 110, 11 }, juce::Colour (0xffe0533c), 8.0f, true, juce::Justification::left);
    legendOver (g, "INPUT TRIM", 330, consoleInk, 4, 80);
    scale (g, *trim, { { -24.0f, "-24" }, { 0.0f, "0" }, { 24.0f, "+24" } }, consoleInk, 6.5f);

    // dB scale above and below the fader, from the parameter's own range
    const auto range = fader.getNormalisableRange();
    const auto track = fader.getBounds().toFloat();
    const auto travel = track.reduced (17.0f, 0.0f);   // half the cap width
    g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
    for (auto db : { -60.0f, -40.0f, -30.0f, -20.0f, -10.0f, -5.0f, 0.0f, 5.0f, 10.0f, 12.0f })
    {
        const auto x = travel.getX() + (float) range.convertTo0to1 (db) * travel.getWidth();
        g.setColour (juce::Colour (0xffcfcfcf));
        g.drawVerticalLine (juce::roundToInt (x), track.getY() - 10.0f, track.getY() - 3.0f);
        g.drawVerticalLine (juce::roundToInt (x), track.getBottom() + 3.0f, track.getBottom() + 8.0f);
        const auto label = db <= pid::volumeMinDb ? juce::String::fromUTF8 ("-\xe2\x88\x9e") : (db > 0 ? "+" : "") + juce::String ((int) db);
        g.drawText (label, juce::Rectangle<float> (30, 10).withCentre ({ x, track.getY() - 17.0f }), juce::Justification::centred);
    }

    drawEngraved (g, "DITHER", { 772, 73, 112, 10 }, consoleInk.withAlpha (0.7f), 7.0f, true, juce::Justification::left);

    // the OVER lamp
    const auto lampAt = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ 780.0f, 93.0f });
    const auto lit = shownLamp == 2 ? juce::Colour (0xffe5352b) : juce::Colour (0xfff2a43a);
    g.setColour (shownLamp > 0 ? lit : juce::Colour (0xff1a1a1a));
    g.fillEllipse (lampAt);
    if (shownLamp > 0)
    {
        g.setColour (lit.withAlpha (0.3f));
        g.fillEllipse (lampAt.expanded (3.0f));
    }
    drawEngraved (g, shownLamp == 1 ? "LOCK CATCHING" : "OVER", { 790, 87, 96, 12 }, consoleInk, 7.5f, true, juce::Justification::left);
    rule (g, 760, consoleInk);

    drawEngraved (g, "IN", { 866, 18, 30, 16 }, consoleInk, 8.0f, true, juce::Justification::right);
    drawEngraved (g, "OUT", { 866, 42, 30, 16 }, consoleInk, 8.0f, true, juce::Justification::right);
    drawEngraved (g, "dB", { 1104, 52, 64, 12 }, consoleInk, 7.5f, true);

    // the output's loudness at a glance; the analyser has the rest
    auto lcd = [&] (const juce::String& label, float v, int x, const juce::String& unit)
    {
        const juce::Rectangle<float> box ((float) x, 76.0f, 82.0f, 30.0f);
        g.setColour (juce::Colour (0xff0b0b0b));
        g.fillRoundedRectangle (box, 2.0f);
        g.setColour (consoleInk.withAlpha (0.7f));
        g.setFont (juce::FontOptions (7.0f, juce::Font::bold));
        g.drawText (label, box.reduced (4.0f, 2.0f), juce::Justification::topLeft);
        g.drawText (unit, box.reduced (4.0f, 2.0f), juce::Justification::bottomRight);
        g.setColour (juce::Colour (0xffff9a3c));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (v > -100.0f ? juce::String (v, 1) : juce::String::fromUTF8 ("\xe2\x80\x94"), box.reduced (4.0f, 0).withTrimmedTop (9.0f),
                    juce::Justification::centredLeft);
    };
    lcd ("SHORT-TERM", shortTerm, 900, "LUFS");
    lcd ("INTEGRATED", integrated, 988, "LUFS");
    lcd ("TRUE PEAK", truePeak, 1076, "dBTP");
}

// ===================================================================== master section

namespace
{
const auto lcdAmber = juce::Colour (0xffff9a3c), lcdBack = juce::Colour (0xff0b0b0b);
constexpr std::array<bb::assist::Goal, 4> kGoals { bb::assist::Goal::Streaming, bb::assist::Goal::Loud, bb::assist::Goal::Club,
                                                   bb::assist::Goal::Dynamic };
constexpr std::array<const char*, 4> kGoalLabels { "STREAM -14", "LOUD -11", "CLUB -9", "DYNAMIC -16" };
}

MasterPanel::MasterPanel (BigBossProcessor& p) : Panel (p.apvts), proc (p)
{
    for (auto* b : { &prev, &next, &save, &browse })
    {
        setStyle (*b, ButtonStyle::consoleKey);
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    save.setTooltip ("Save these settings, and the module order, as a preset of your own");
    browse.setTooltip ("All presets: factory and your own");
    prev.onClick = [this] { stepPreset (-1); };
    next.onClick = [this] { stepPreset (1); };
    save.onClick = [this] { savePreset(); };
    browse.onClick = [this] { showBrowser(); };

    for (auto* k : { &assistantKey, &analyserKey })
    {
        setStyle (*k, ButtonStyle::consoleKey);
        k->setClickingTogglesState (true);
        addAndMakeVisible (*k);
    }
    assistantKey.setTooltip ("The assistant: it listens to your mix and suggests settings for a loudness goal");
    analyserKey.setTooltip ("Show the analyser in place of the modules");

    for (auto* k : { &undoKey, &redoKey, &copyKey })
    {
        setStyle (*k, ButtonStyle::consoleKey);
        k->setClickingTogglesState (false);
        addAndMakeVisible (*k);
    }
    undoKey.setTooltip ("Undo the last change (a control, a preset, a slot, the order)");
    redoKey.setTooltip ("Redo what UNDO took back");
    copyKey.setTooltip ("Copy these settings to another slot: press COPY, then the slot");
    undoKey.onClick = [this] { if (proc.undo() && onOrderChanged) onOrderChanged(); };
    redoKey.onClick = [this] { if (proc.redo() && onOrderChanged) onOrderChanged(); };
    copyKey.onClick = [this] { copyArmed = ! copyArmed; copyKey.setToggleState (copyArmed, juce::dontSendNotification); };
    for (int i = 0; i < BigBossProcessor::kCompareSlots; ++i)
    {
        auto& k = slotKeys[(size_t) i];
        k.setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + i)));
        setStyle (k, ButtonStyle::consoleKey);
        k.setClickingTogglesState (false);
        k.setTooltip ("Compare slot " + k.getButtonText() + ": four sets of settings to switch between. "
                      "An empty slot starts as a copy of where you are.");
        k.onClick = [this, i]
        {
            if (copyArmed) { proc.copyToSlot (i); copyArmed = false; copyKey.setToggleState (false, juce::dontSendNotification); }
            else proc.selectSlot (i);
            if (onOrderChanged) onOrderChanged();
        };
        addAndMakeVisible (k);
    }
}

void MasterPanel::resized()
{
    undoKey.setBounds (316, 18, 50, 22);
    redoKey.setBounds (370, 18, 50, 22);
    presetLcd = { 428, 14, 226, 30 };
    prev.setBounds (660, 14, 28, 30);
    next.setBounds (692, 14, 28, 30);
    save.setBounds (728, 18, 52, 22);
    browse.setBounds (784, 18, 66, 22);
    for (int i = 0; i < BigBossProcessor::kCompareSlots; ++i) slotKeys[(size_t) i].setBounds (860 + 26 * i, 18, 24, 22);
    copyKey.setBounds (966, 18, 46, 22);
    assistantKey.setBounds (1030, 18, 82, 22);
    analyserKey.setBounds (1116, 18, 70, 22);
}

void MasterPanel::stepPreset (int delta)
{
    // through the factory list, then the user's
    const auto& f = presets::factory();
    const auto user = presets::userPresets();
    const int total = (int) f.size() + user.size();
    int at = proc.getCurrentProgram();
    for (int i = 0; i < user.size(); ++i)
        if (user[i].getFileNameWithoutExtension() == proc.getPresetName()) at = (int) f.size() + i;
    const int to = ((at + delta) % total + total) % total;
    if (to < (int) f.size()) proc.loadFactoryPreset (to);
    else proc.loadUserPreset (user[to - (int) f.size()]);
    if (onOrderChanged) onOrderChanged();
}

void MasterPanel::showPresetMenu()
{
    juce::PopupMenu menu, factoryMenu, userMenu;
    const auto& f = presets::factory();
    for (int i = 0; i < (int) f.size(); ++i)
        menu.addItem (1 + i, f[(size_t) i].name, true, proc.getPresetName() == f[(size_t) i].name);
    const auto user = presets::userPresets();
    menu.addSeparator();
    if (user.isEmpty()) menu.addItem (-1, "(no presets of your own yet: press SAVE)", false);
    for (int i = 0; i < user.size(); ++i)
        menu.addItem (1000 + i, user[i].getFileNameWithoutExtension(), true, proc.getPresetName() == user[i].getFileNameWithoutExtension());
    menu.addSeparator();
    menu.addItem (2000, "Save as...");
    menu.addItem (2001, "Open the presets folder");
    menu.addItem (2002, "Export recall sheet...");
    juce::Component::SafePointer<MasterPanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (presetLcd)),
                        [safe, user] (int r)
                        {
                            if (safe == nullptr || r == 0) return;
                            auto& self = *safe;
                            if (r >= 1 && r < 1000) self.proc.loadFactoryPreset (r - 1);
                            else if (r >= 1000 && r < 2000) self.proc.loadUserPreset (user[r - 1000]);
                            else if (r == 2000) { self.savePreset(); return; }
                            else if (r == 2002) { if (self.onRecallSheet) self.onRecallSheet(); return; }
                            else if (r == 2001)
                            {
                                presets::userFolder().createDirectory();
                                presets::userFolder().startAsProcess();
                                return;
                            }
                            if (self.onOrderChanged) self.onOrderChanged();
                        });
}

void MasterPanel::showBrowser()
{
    auto* top = getTopLevelComponent();
    juce::Component::SafePointer<MasterPanel> safe (this);
    auto content = std::make_unique<PresetBrowser> (proc, [safe] { if (safe != nullptr && safe->onOrderChanged) safe->onOrderChanged(); });
    juce::CallOutBox::launchAsynchronously (std::move (content), top->getLocalArea (this, browse.getBounds()), top);
}

void MasterPanel::savePreset()
{
    saveDialog = std::make_unique<juce::AlertWindow> ("Save preset", "Name it. A preset with the same name is replaced.",
                                                      juce::MessageBoxIconType::NoIcon, this);
    saveDialog->addTextEditor ("name", proc.getPresetName() == "Init" ? juce::String() : proc.getPresetName());
    juce::String tags;
    for (const auto& e : presets::all())
        if (e.name == proc.getPresetName()) tags = e.tags.joinIntoString (", ");
    saveDialog->addTextEditor ("tags", tags, "Tags, separated by commas (for the browser's search)");
    saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MasterPanel> safe (this);
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int r)
    {
        if (safe == nullptr) return;
        const auto name = safe->saveDialog->getTextEditorContents ("name");
        auto tagList = juce::StringArray::fromTokens (safe->saveDialog->getTextEditorContents ("tags"), ",", "");
        tagList.trim();
        tagList.removeEmptyStrings();
        if (r == 1 && ! safe->proc.saveUserPreset (name, tagList))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save preset",
                                                    "Couldn't save to " + presets::userFolder().getFullPathName());
        safe->saveDialog.reset();
    }), false);
}

// ===================================================================== preset browser

PresetBrowser::PresetBrowser (BigBossProcessor& p, std::function<void()> loaded) : proc (p), onLoaded (std::move (loaded))
{
    entries = presets::all();
    juce::StringArray tags;
    for (const auto& e : entries) for (const auto& t : e.tags) tags.addIfNotAlreadyThere (t.toLowerCase());
    tags.sort (true);
    for (const auto& t : tags)
    {
        auto k = std::make_unique<juce::ToggleButton> (t.toUpperCase());
        setStyle (*k, ButtonStyle::consoleKey);
        k->setClickingTogglesState (false);
        k->onClick = [this, t]
        {
            tag = tag == t ? juce::String() : t;
            for (auto& other : tagKeys) other->setToggleState (other->getButtonText().equalsIgnoreCase (tag), juce::dontSendNotification);
            refilter();
        };
        addAndMakeVisible (*k);
        tagKeys.push_back (std::move (k));
    }
    search.setTextToShowWhenEmpty ("Search names and tags", juce::Colour (0xff8a8a8a));
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0b0b0b));
    search.setColour (juce::TextEditor::textColourId, juce::Colour (0xffff9a3c));
    search.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff3a3a3a));
    search.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    search.onTextChange = [this] { refilter(); };
    search.onReturnKey = [this] { load (0); };
    addAndMakeVisible (search);
    list.setModel (this);
    list.setRowHeight (34);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff121212));
    addAndMakeVisible (list);
    refilter();
    setSize (440, 420);
}

void PresetBrowser::refilter()
{
    shown.clear();
    for (const auto& e : entries)
        if (presets::matches (e, search.getText(), tag)) shown.push_back (e);
    list.updateContent();
    list.repaint();
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& e = shown[(size_t) row];
    const bool current = e.name == proc.getPresetName();
    if (current)
    {
        g.setColour (juce::Colour (0xff2a2016));
        g.fillRect (0, 0, width, height);
    }
    g.setColour (juce::Colour (0xffff9a3c));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (e.name, 10, 2, width - 90, 17, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff8a8a8a));
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (e.factoryIndex >= 0 ? "FACTORY" : "YOURS", width - 80, 2, 70, 17, juce::Justification::centredRight);
    g.drawText (e.tags.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 ")).toUpperCase(), 10, 18, width - 20, 13, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff222222));
    g.drawHorizontalLine (height - 1, 0.0f, (float) width);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent&) { load (row); }

void PresetBrowser::load (int row)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& e = shown[(size_t) row];
    if (e.factoryIndex >= 0) proc.loadFactoryPreset (e.factoryIndex);
    else proc.loadUserPreset (e.file);
    list.repaint();
    if (onLoaded) onLoaded();
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (10);
    search.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    // the tags, wrapped into rows
    int x = r.getX(), y = r.getY();
    for (auto& k : tagKeys)
    {
        const int w = juce::jmax (40, (int) juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)), k->getButtonText()) + 18);
        if (x + w > r.getRight()) { x = r.getX(); y += 24; }
        k->setBounds (x, y, w, 20);
        x += w + 4;
    }
    r.removeFromTop (y - r.getY() + 28);
    list.setBounds (r);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1c1e));
}

juce::String MasterPanel::getTooltip()
{
    if (! presetLcd.contains (getMouseXYRelative())) return {};
    for (const auto& f : presets::factory())
        if (shownPreset == f.name) return juce::String (f.blurb) + ". Click for every preset.";
    return "Click for every preset";
}

void MasterPanel::tick (float)
{
    undoKey.setAlpha (proc.canUndo() ? 1.0f : 0.4f);
    redoKey.setAlpha (proc.canRedo() ? 1.0f : 0.4f);
    for (int i = 0; i < BigBossProcessor::kCompareSlots; ++i)
    {
        slotKeys[(size_t) i].setToggleState (proc.activeSlot() == i, juce::dontSendNotification);
        slotKeys[(size_t) i].setAlpha (proc.slotUsed (i) || proc.activeSlot() == i ? 1.0f : 0.55f);
    }
    const bool lamp = assistantNews != nullptr && assistantNews();
    if (proc.getPresetName() != shownPreset || lamp != shownLamp)
    {
        shownPreset = proc.getPresetName();
        shownLamp = lamp;
        repaint();
    }
}

void MasterPanel::mouseUp (const juce::MouseEvent& e)
{
    Panel::mouseUp (e);
    if (presetLcd.contains (e.getPosition())) showPresetMenu();
}

void MasterPanel::paint (juce::Graphics& g)
{
    paintFinish (g, consoleFace, Finish::brushed);

    // the name, in a retro script (Yellowtail), lit like a sign: an amber glow, a shadow, a warm gradient
    {
        static const auto script = juce::Typeface::createSystemTypefaceFor (BigBossFonts::YellowtailRegular_ttf,
                                                                            BigBossFonts::YellowtailRegular_ttfSize);
        const juce::String name ("Big Boss Mastering Suite");
        const auto area = juce::Rectangle<float> (34.0f, 4.0f, 274.0f, 50.0f);
        auto font = juce::Font (juce::FontOptions (script).withHeight (38.0f));
        const float w = juce::GlyphArrangement::getStringWidth (font, name);
        if (w > area.getWidth()) font = font.withHeight (38.0f * area.getWidth() / w);
        juce::GlyphArrangement glyphs;
        glyphs.addFittedText (font, name, area.getX(), area.getY(), area.getWidth(), area.getHeight(), juce::Justification::centredLeft, 1, 1.0f);
        juce::Path text;
        glyphs.createPath (text);
        juce::DropShadow (lcdAmber.withAlpha (0.5f), 10, {}).drawForPath (g, text);
        juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 3, { 1, 2 }).drawForPath (g, text);
        const auto b = text.getBounds();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff3d6), 0.0f, b.getY(), juce::Colour (0xffffa53a), 0.0f, b.getBottom(), false));
        g.fillPath (text);
    }

    // the preset display (what the preset does is its tooltip)
    g.setColour (lcdBack);
    g.fillRoundedRectangle (presetLcd.toFloat(), 3.0f);
    g.setColour (lcdAmber);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText (shownPreset, presetLcd.reduced (10, 0), juce::Justification::centredLeft);
    g.setColour (consoleInk.withAlpha (0.5f));
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText (juce::String::fromUTF8 ("\xe2\x96\xbe"), presetLcd.reduced (10, 0), juce::Justification::centredRight);

    // a lamp by the ASSISTANT key while it is listening, working, or has a suggestion waiting
    const auto lampAt = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ (float) assistantKey.getX() - 7.0f, (float) assistantKey.getBounds().getCentreY() });
    g.setColour (shownLamp ? lcdAmber : juce::Colour (0xff1a1a1a));
    g.fillEllipse (lampAt);
    if (shownLamp)
    {
        g.setColour (lcdAmber.withAlpha (0.3f));
        g.fillEllipse (lampAt.expanded (3.0f));
    }
}

// ===================================================================== the assistant

AssistantPanel::AssistantPanel (BigBossProcessor& p) : Panel (p.apvts), proc (p), host (p.assistant())
{
    for (size_t i = 0; i < goalKeys.size(); ++i)
    {
        auto& k = goalKeys[i];
        k.setButtonText (kGoalLabels[i]);
        setStyle (k, ButtonStyle::consoleKey);
        k.setRadioGroupId (0x6b6f);
        k.setClickingTogglesState (true);
        k.setToggleState (kGoals[i] == host.getGoal(), juce::dontSendNotification);
        k.onClick = [this, i] { if (goalKeys[i].getToggleState()) host.setGoal (kGoals[i]); };
        addAndMakeVisible (k);
    }
    goalKeys[0].setTooltip ("Goal: streaming services, -14 LUFS and -1 dBTP");
    goalKeys[1].setTooltip ("Goal: loud, -11 LUFS");
    goalKeys[2].setTooltip ("Goal: club and DJ play, -9 LUFS");
    goalKeys[3].setTooltip ("Goal: dynamic, -16 LUFS (Apple Music, acoustic, classical)");

    for (auto* k : { &listenKey, &applyKey, &undoKey, &closeKey })
    {
        setStyle (*k, ButtonStyle::consoleKey);
        k->setClickingTogglesState (false);
        addAndMakeVisible (*k);
    }
    listenKey.setTooltip ("Play your mix (its loudest part is best) and press LISTEN: the assistant listens for 10 seconds, "
                          "then suggests settings for the goal");
    applyKey.setTooltip ("Use the assistant's settings (the fader goes to 0 dB, as the plan was measured)");
    undoKey.setTooltip ("Put back the settings from before APPLY");
    closeKey.setTooltip ("Fold the assistant away; it keeps listening or working if it was");
    listenKey.onClick = [this] { host.listen(); };
    applyKey.onClick = [this] { host.apply(); };
    undoKey.onClick = [this] { if (host.undo() && onOrderChanged) onOrderChanged(); };
    closeKey.onClick = [this] { if (onClose) onClose(); };
}

void AssistantPanel::resized()
{
    for (size_t i = 0; i < goalKeys.size(); ++i) goalKeys[i].setBounds (170 + 78 * (int) i, 14, 74, 22);
    listenKey.setBounds (500, 14, 76, 22);
    applyKey.setBounds (582, 14, 76, 22);
    undoKey.setBounds (664, 14, 76, 22);
    closeKey.setBounds (getWidth() - 90, 14, 76, 22);
    statusLcd = { 170, 44, getWidth() - 184, 62 };
}

void AssistantPanel::tick (float)
{
    const auto now = host.getState();
    applyKey.setEnabled (host.canApply());
    undoKey.setEnabled (host.canUndo());
    listenKey.setToggleState (now == State::Listening, juce::dontSendNotification);
    for (auto* k : { &applyKey, &undoKey }) k->setAlpha (k->isEnabled() ? 1.0f : 0.4f);
    // the faceplate only redraws when something on it moves, and only while it is open
    const bool changed = now != shownState;
    shownState = now;
    if (isVisible() && (changed || host.busy())) repaint();
}

void AssistantPanel::paint (juce::Graphics& g)
{
    // a 1U faceplate with a shadow under it, over whatever it covers
    const auto face = getLocalBounds().withHeight (unitHeight);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, (float) face.getBottom(),
                                             juce::Colours::transparentBlack, 0.0f, (float) getHeight(), false));
    g.fillRect (getLocalBounds().withTop (face.getBottom()));
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (face);
        paintFinish (g, consoleFace, Finish::brushed);
    }
    drawEngraved (g, "ASSISTANT", { 24, 30, 140, 28 }, juce::Colour (0xffe8e8e8), 20.0f, true, juce::Justification::left);
    drawEngraved (g, "LISTENS, THEN SETS UP", { 24, 60, 140, 11 }, consoleInk.withAlpha (0.85f), 7.5f, true, juce::Justification::left);
    drawEngraved (g, "THE CONSOLE FOR A GOAL", { 24, 72, 140, 11 }, consoleInk.withAlpha (0.6f), 7.5f, true, juce::Justification::left);

    // the assistant's display
    g.setColour (lcdBack);
    g.fillRoundedRectangle (statusLcd.toFloat(), 3.0f);
    auto text = statusLcd.reduced (10, 6);
    g.setColour (lcdAmber);
    auto line = [&] (const juce::String& s, float h = 10.5f, juce::Colour c = lcdAmber)
    {
        g.setColour (c);
        g.setFont (juce::FontOptions (h, juce::Font::bold));
        g.drawText (s, text.removeFromTop ((int) h + 3), juce::Justification::centredLeft, true);
    };
    const auto dim = lcdAmber.withAlpha (0.6f);
    const auto now = host.getState();
    const auto plan = host.plan();
    switch (now)
    {
        case State::Idle:
            line ("Not sure where to start? Pick a goal, play the loudest part of your mix and press LISTEN.");
            line ("The assistant listens for 10 seconds, then suggests settings for every module.", 10.5f, dim);
            line ("Nothing changes until you press APPLY, and UNDO puts your settings back.", 10.5f, dim);
            break;
        case State::Listening:
        {
            const float pr = proc.captureProgress();
            line ("Listening: keep the mix playing" + juce::String (pr < 0.02f ? " (waiting for sound)" : "") + "...");
            auto bar = text.removeFromTop (12).toFloat().withTrimmedRight (40.0f);
            g.setColour (lcdAmber.withAlpha (0.18f));
            g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (lcdAmber);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * pr), 2.0f);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (pr * (float) BigBossProcessor::kCaptureSeconds, 0) + " s", juce::Rectangle<float> (bar.getRight() + 4, bar.getY() - 1, 36, 14),
                        juce::Justification::centredLeft);
            text.removeFromTop (6);
            line ("Press LISTEN again to stop.", 10.5f, dim);
            break;
        }
        case State::Thinking:
            line ("Working it out" + juce::String::repeatedString (".", 1 + (int) (host.thinkingSeconds() * 3) % 3));
            line ("Measuring the mix, then trying the settings on what it heard.", 10.5f, dim);
            break;
        case State::Ready:
        case State::Applied:
        {
            if (plan == nullptr) break;
            const auto& p = *plan;
            line (juce::String (now == State::Applied ? "Applied: " : "Ready: ") + juce::String (p.outLufs, 1) + " LUFS, "
                  + juce::String (p.outTruePeak, 1) + " dBTP (the mix came in at " + juce::String (p.in.integrated, 1) + " LUFS). "
                  + (now == State::Applied ? "UNDO puts your settings back." : "Press APPLY to use it."));
            juce::StringArray notes;
            for (const auto& n : p.notes) notes.add (n);
            g.setColour (dim);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawFittedText (notes.joinIntoString (juce::String::fromUTF8 ("  \xc2\xb7  ")), text, juce::Justification::topLeft, 3, 1.0f);
            break;
        }
        case State::Failed:
            line (plan != nullptr && ! plan->notes.empty() ? juce::String (plan->notes.front()) : juce::String ("Something went wrong; try LISTEN again."));
            break;
    }
}

// ===================================================================== analyser

AnalyserPanel::AnalyserPanel (BigBossProcessor& p) : Panel (p.apvts), proc (p)
{
    for (auto* k : { &freezeKey, &resetKey, &bypassKey, &matchKey, &loadRefKey, &refKey, &inKey, &outKey })
    {
        setStyle (*k, ButtonStyle::consoleKey);
        k->setClickingTogglesState (false);
        addAndMakeVisible (*k);
    }
    freezeKey.setTooltip ("Hold every display where it is; the meters keep measuring underneath");
    resetKey.setTooltip ("Restart integrated loudness, LRA, the peak holds and the spectrogram");
    bypassKey.setTooltip ("Bypass the whole console, trim and fader included, to compare with the untouched mix. "
                          "The meters keep running. Not saved with the session.");
    matchKey.setTooltip ("Level-matched compare: BYPASS ALL plays the untouched mix at the console's loudness, "
                         "so you hear what the console does, not just that it is louder");
    loadRefKey.setTooltip ("Load a reference track (WAV, AIFF, FLAC or Ogg) to compare with: its spectrum and loudness are drawn "
                           "over the analyser's, and REF plays it. Right-click to remove it.");
    refKey.setTooltip ("Play the reference track instead of the console, at the console's loudness");
    matchKey.onClick = [this] { proc.matchCompare = ! proc.matchCompare.load(); };
    loadRefKey.onClick = [this]
    {
        if (juce::ModifierKeys::currentModifiers.isPopupMenu()) proc.clearReference();
        else chooseReference();
    };
    refKey.onClick = [this] { if (proc.reference() != nullptr) proc.refPlay = ! proc.refPlay.load(); };
    inKey.setTooltip ("Meter the input, before the trim");
    outKey.setTooltip ("Meter the output, after the fader");
    freezeKey.onClick = [this]
    {
        frozen = ! frozen;
        freezeKey.setToggleState (frozen, juce::dontSendNotification);
        if (! frozen) useSource (showingOutput);   // pick up from now, not from where it stopped
    };
    resetKey.onClick = [this] { reset(); };
    bypassKey.onClick = [this] { proc.bypassAll = ! proc.bypassAll.load(); };
    inKey.onClick = [this] { useSource (false); };
    outKey.onClick = [this] { useSource (true); };

    // a reference line for the loudness bars and history; it changes nothing in the audio
    const std::pair<const char*, float> targets[] = { { "-9 LUFS", -9.0f }, { "-11 LUFS", -11.0f }, { "-14 LUFS  streaming", -14.0f },
                                                      { "-16 LUFS", -16.0f }, { "-23 LUFS  EBU R128", -23.0f } };
    int id = 1;
    for (auto& t : targets) target.addItem (t.first, id++);
    target.setSelectedId (3, juce::dontSendNotification);
    target.onChange = [this, targets]
    {
        const int i = target.getSelectedId() - 1;
        if (i >= 0 && i < 5) meters.targetLufs = targets[i].second;
    };
    target.setTooltip ("Loudness target: the reference line on the bars and the history");
    target.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0b0b0b));
    target.setColour (juce::ComboBox::textColourId, juce::Colour (0xffff9a3c));
    target.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a3a3a));
    target.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xffbdbdbd));
    addAndMakeVisible (target);

    addAndMakeVisible (meters);
    addAndMakeVisible (spectrum);
    addAndMakeVisible (spectrogram);
    addAndMakeVisible (gonio);
    useSource (true);
}

void AnalyserPanel::reset()
{
    proc.inMeters.requestReset();
    proc.outMeters.requestReset();
    spectrum.clearPeak();
    spectrogram.clear();
    codec.reset();
}

void AnalyserPanel::chooseReference()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a reference track", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File()) return;
        juce::String error;
        if (! proc.loadReference (file, error))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Reference track", error);
    });
}

void AnalyserPanel::showReference()
{
    shownRefVersion = proc.refVersion.load();
    shownRefGain = proc.referenceGainDb();
    const auto t = proc.reference();
    if (t == nullptr || std::abs (t->rate - spectrum.sampleRate()) > 0.5)
    {
        spectrum.setReference ({}, 0.0f, {});
        refKey.setTooltip ("Load a reference track first");
        return;
    }
    const juce::String label = "REF  " + t->file.getFileNameWithoutExtension() + "   " + juce::String (t->lufs, 1) + " LUFS   "
                             + juce::String (t->truePeak, 1) + " dBTP   LRA " + juce::String (t->range, 1);
    spectrum.setReference (t->spectrum, shownRefGain, label);
    refKey.setTooltip ("Play " + t->file.getFileName() + " instead of the console, at the console's loudness ("
                       + juce::String (shownRefGain, 1) + " dB)");
}

void AnalyserPanel::useSource (bool output)
{
    showingOutput = output;
    auto* m = output ? &proc.outMeters : &proc.inMeters;
    shownRate = proc.sampleRateHz.load();
    meters.setSource (m);
    spectrum.setSource (m, shownRate);
    gonio.setSource (m);
    codec.setSource (&proc.outMeters, shownRate);   // always the output: it is what would be encoded
    shownRefVersion = -1;
    inKey.setToggleState (! output, juce::dontSendNotification);
    outKey.setToggleState (output, juce::dontSendNotification);
}

void AnalyserPanel::refreshSource()
{
    if (std::abs (proc.sampleRateHz.load() - shownRate) > 0.5) useSource (showingOutput);
}

void AnalyserPanel::tick (float)
{
    refreshSource();
    if (proc.bypassAll.load() != shownBypass)
    {
        shownBypass = ! shownBypass;
        bypassKey.setToggleState (shownBypass, juce::dontSendNotification);
        repaint (0, 0, getWidth(), 44);
    }
    matchKey.setToggleState (proc.matchCompare.load(), juce::dontSendNotification);
    {
        // with MATCH on, the key shows the gain the untouched mix gets
        const float m = proc.matchGainDb();
        matchKey.setButtonText (proc.matchCompare.load() ? "MATCH " + juce::String (m >= 0.0f ? "+" : "") + juce::String (m, 1) : juce::String ("MATCH"));
    }
    refKey.setToggleState (proc.refPlay.load(), juce::dontSendNotification);
    refKey.setAlpha (proc.reference() != nullptr ? 1.0f : 0.4f);
    if (proc.refVersion.load() != shownRefVersion || std::abs (proc.referenceGainDb() - shownRefGain) > 0.2f) showReference();
    if (frozen) return;
    codec.tick();
    {
        juce::String note = "codec est.";
        for (int c = 0; c < bbui::CodecPreview::kCodecs; ++c)
        {
            const float tp = codec.truePeakDb (c);
            note << "   " << bbui::CodecPreview::name (c) << " " << (tp > -100.0f ? juce::String (tp, 1) : juce::String::fromUTF8 ("\xe2\x80\x94"));
        }
        spectrum.setNote (note + " dBTP");
    }
    meters.tick();   // even while hidden, so the loudness history has no gap when the analyser comes back
    if (isVisible())
    {
        spectrum.tick();
        spectrogram.push (spectrum.frame(), spectrum.sampleRate());
        gonio.tick();
    }
}

void AnalyserPanel::resized()
{
    // the control strip beside the name
    auto row = juce::Rectangle<int> (120, 12, getWidth() - 134, 22);
    freezeKey.setBounds (row.removeFromLeft (76));
    row.removeFromLeft (6);
    resetKey.setBounds (row.removeFromLeft (68));
    row.removeFromLeft (6);
    bypassKey.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (4);
    matchKey.setBounds (row.removeFromLeft (84));
    row.removeFromLeft (16);
    loadRefKey.setBounds (row.removeFromLeft (76));
    row.removeFromLeft (4);
    refKey.setBounds (row.removeFromLeft (52));
    target.setBounds (row.removeFromRight (190).withSizeKeepingCentre (190, 24));
    row.removeFromRight (64);   // TARGET
    outKey.setBounds (row.removeFromRight (52));
    row.removeFromRight (6);
    inKey.setBounds (row.removeFromRight (46));

    auto r = getLocalBounds().reduced (14, 0);
    r.removeFromTop (44);
    r.removeFromBottom (14);
    screen = r;
    auto s = r.reduced (10);
    auto left = s.removeFromLeft (500);
    meters.setBounds (left);
    s.removeFromLeft (10);
    auto lower = s.removeFromBottom (220);
    spectrum.setBounds (s.withTrimmedBottom (10));
    gonio.setBounds (lower.removeFromLeft (220));
    lower.removeFromLeft (10);
    spectrogram.setBounds (lower);
}

void AnalyserPanel::paint (juce::Graphics& g)
{
    paintFinish (g, juce::Colour (0xff1b1c1e), Finish::powder);
    drawEngraved (g, "BB-A", { 24, 8, 80, 28 }, juce::Colour (0xffe8e8e8), 22.0f, true, juce::Justification::left);
    drawEngraved (g, "METER", inKey.getBounds().translated (-56, 0).withWidth (50).toFloat(), consoleInk, 8.0f, true,
                  juce::Justification::right);
    drawEngraved (g, "TARGET", target.getBounds().translated (-64, 0).withWidth (58).toFloat(), consoleInk, 8.0f, true,
                  juce::Justification::right);
    // the screen
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (screen.toFloat(), 6.0f);
    g.setColour (bbui::col::bg);
    g.fillRoundedRectangle (screen.toFloat().reduced (4.0f), 4.0f);
}
} // namespace kit
