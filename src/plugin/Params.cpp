#include "Params.h"

namespace pid
{
namespace
{
constexpr float kOff = 0.06f;   // share of a knob's travel that is OFF

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float a, float b, float t) { return a * std::pow (b / a, t); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); } };
}

juce::AudioParameterFloatAttributes dbAttr()
{
    return juce::AudioParameterFloatAttributes().withLabel ("dB")
        .withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; });
}
}

juce::String hzText (float hz)
{
    if (hz <= 0.0f) return "OFF";
    if (hz >= 1000.0f) return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String (juce::roundToInt (hz)) + " Hz";
}

juce::NormalisableRange<float> offRange (float lo, float hi, bool offAtTop)
{
    if (offAtTop)
        return { 0.0f, hi,
                 [lo, hi] (float, float, float t) { return t > 1.0f - kOff ? 0.0f : lo * std::pow (hi / lo, t / (1.0f - kOff)); },
                 [lo, hi] (float, float, float v) { return v <= 0.0f ? 1.0f : (1.0f - kOff) * std::log (juce::jlimit (lo, hi, v) / lo) / std::log (hi / lo); } };
    return { 0.0f, hi,
             [lo, hi] (float, float, float t) { return t < kOff ? 0.0f : lo * std::pow (hi / lo, (t - kOff) / (1.0f - kOff)); },
             [lo, hi] (float, float, float v) { return v <= 0.0f ? 0.0f : kOff + (1.0f - kOff) * std::log (juce::jlimit (lo, hi, v) / lo) / std::log (hi / lo); } };
}

void addLayout (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* const* slotIds, const char* const* slotNames, int numSlots)
{
    using P = juce::ParameterID;
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { inTrim, 1 }, "Input Trim", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, dbAttr()));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { outTrim, 1 }, "Output Trim", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, dbAttr()));
    for (int s = 0; s < numSlots; ++s)
        layout.add (std::make_unique<juce::AudioParameterBool> (P { bypass (slotIds[s]), 1 }, juce::String (slotNames[s]) + " Bypass", false));

    // EQ
    auto hzAttr = juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return hzText (v); });
    for (int b = 0; b < bb::eq::kBands; ++b)
    {
        const auto& r = bb::eq::kRange[b];
        const juce::String n = juce::String ("EQ ") + juce::String (r.name).toLowerCase().replace ("mid", "mid").replace ("high", "High").replace ("low", "Low");
        layout.add (std::make_unique<juce::AudioParameterFloat> (P { eqFreq (b), 1 }, n + " Freq", logRange ((float) r.lo, (float) r.hi), (float) r.def, hzAttr));
        layout.add (std::make_unique<juce::AudioParameterFloat> (P { eqGain (b), 1 }, n + " Gain",
                                                                 juce::NormalisableRange<float> (0.0f, (float) bb::eq::kMaxGain, 0.1f), 0.0f, dbAttr()));
        layout.add (std::make_unique<juce::AudioParameterBool> (P { eqCut (b), 1 }, n + " Cut", false));
        if (r.canShelf)
            layout.add (std::make_unique<juce::AudioParameterBool> (P { eqShelf (b), 1 }, n + " Shelf", true));
    }
    layout.add (std::make_unique<juce::AudioParameterBool> (P { eqWarm, 1 }, "EQ Warm", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { eqHpf, 1 }, "EQ High-pass", offRange (10.0f, 300.0f, false), 0.0f, hzAttr));
    layout.add (std::make_unique<juce::AudioParameterBool> (P { eqHpf24, 1 }, "EQ High-pass 24 dB", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { eqLpf, 1 }, "EQ Low-pass", offRange (4000.0f, 30000.0f, true), 0.0f, hzAttr));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { eqMono, 1 }, "EQ Mono Bass", offRange (40.0f, 300.0f, false), 0.0f, hzAttr));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { eqOut, 1 }, "EQ Output", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr()));

    // tape
    layout.add (std::make_unique<juce::AudioParameterChoice> (P { tapeSpeed, 1 }, "Tape Speed", juce::StringArray { "15 ips", "30 ips" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (P { tapeEq, 1 }, "Tape EQ", juce::StringArray { "NAB", "CCIR" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (P { tapeType, 1 }, "Tape Formulation", juce::StringArray { "456", "GP9" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (P { tapeBias, 1 }, "Tape Bias", juce::StringArray { "Under", "Nominal", "Over" }, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { tapeIn, 1 }, "Tape Input", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr()));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { tapeOut, 1 }, "Tape Output", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr()));
    layout.add (std::make_unique<juce::AudioParameterBool> (P { tapeLink, 1 }, "Tape Level Link", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { tapeWow, 1 }, "Tape Wow & Flutter", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })));
}
} // namespace pid
