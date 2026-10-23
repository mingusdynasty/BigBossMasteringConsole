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
    // the output fader, last in the chain: -inf to +12 dB, scaled like a console fader
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { volume, 1 }, "Volume", juce::NormalisableRange<float> (volumeMinDb, 12.0f, 0.1f, 2.5f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        { return v <= volumeMinDb ? juce::String ("-inf") : (v > 0.05f ? "+" : "") + juce::String (v, 1); })));
    // sums the output to mono after the fader, to check how the master holds up on one speaker
    layout.add (std::make_unique<juce::AudioParameterBool> (P { outMono, 1 }, "Output Mono", false));
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

    // compressor
    auto thr = [] (float lo, float hi) { return juce::NormalisableRange<float> (lo, hi, 0.1f); };
    layout.add (std::make_unique<juce::AudioParameterBool> (P { compOpto, 1 }, "Comp Optical In", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { compOptoThr, 1 }, "Comp Optical Threshold", thr (-40.0f, 0.0f), -16.0f, dbAttr()));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { compOptoGain, 1 }, "Comp Optical Gain", thr (-6.0f, 12.0f), 0.0f, dbAttr()));
    layout.add (std::make_unique<juce::AudioParameterBool> (P { compVca, 1 }, "Comp Discrete In", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { compVcaThr, 1 }, "Comp Discrete Threshold", thr (-40.0f, 0.0f), -12.0f, dbAttr()));
    layout.add (std::make_unique<juce::AudioParameterChoice> (P { compRatio, 1 }, "Comp Ratio",
                                                              juce::StringArray { "1.2:1", "2:1", "3:1", "4:1", "6:1", "Flood" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (P { compAttack, 1 }, "Comp Attack",
                                                              juce::StringArray { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }, 3));
    layout.add (std::make_unique<juce::AudioParameterChoice> (P { compRecovery, 1 }, "Comp Recovery",
                                                              juce::StringArray { "100 ms", "250 ms", "400 ms", "800 ms", "1.2 s", "Dual" }, 5));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { compVcaGain, 1 }, "Comp Discrete Gain", thr (-6.0f, 12.0f), 0.0f, dbAttr()));
    layout.add (std::make_unique<juce::AudioParameterChoice> (P { compXfmr, 1 }, "Comp Transformer", juce::StringArray { "Nickel", "Iron", "Steel" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (P { compScHpf, 1 }, "Comp Sidechain High-pass", true));

    // limiter
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { limDrive, 1 }, "Limiter Drive", thr (0.0f, 24.0f), 0.0f, dbAttr()));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { limCeiling, 1 }, "Limiter Ceiling", juce::NormalisableRange<float> (-12.0f, 0.0f, 0.05f), -1.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dBTP").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " dBTP"; })));
    layout.add (std::make_unique<juce::AudioParameterChoice> (P { limStyle, 1 }, "Limiter Style", juce::StringArray { "Transparent", "Punch", "Dense" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (P { limAuto, 1 }, "Limiter Auto Release", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { limRelease, 1 }, "Limiter Release", logRange (1.0f, 1000.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10 ? 1 : 0) + " ms"; })));
    layout.add (std::make_unique<juce::AudioParameterBool> (P { limTp, 1 }, "Limiter True Peak", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { limLink, 1 }, "Limiter Stereo Link", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })));

    // de-esser
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { deessThr, 1 }, "De-esser Threshold", thr (-24.0f, 12.0f), -6.0f, dbAttr()));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { deessRange, 1 }, "De-esser Range", thr (0.0f, 18.0f), 8.0f, dbAttr()));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { deessFreq, 1 }, "De-esser Frequency", logRange (3000.0f, 8000.0f), 4500.0f, hzAttr));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        P { deessRelease, 1 }, "De-esser Release", logRange (10.0f, 200.0f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })));
    layout.add (std::make_unique<juce::AudioParameterChoice> (P { deessMode, 1 }, "De-esser Mode", juce::StringArray { "Stereo", "Mid", "Side" }, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (P { deessListen, 1 }, "De-esser Listen", false));
}
} // namespace pid
