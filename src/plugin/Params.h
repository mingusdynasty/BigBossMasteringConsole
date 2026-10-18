// Parameter ids, ranges and the layout, shared by the processor and the editor.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "eq/EqDesign.h"

namespace pid
{
inline const juce::String inTrim = "in_trim", outTrim = "out_trim";

inline juce::String bypass (const char* slotId) { return juce::String (slotId) + "_bypass"; }

// EQ
inline juce::String eqFreq (int b)  { return "eq_b" + juce::String (b) + "_freq"; }
inline juce::String eqGain (int b)  { return "eq_b" + juce::String (b) + "_gain"; }
inline juce::String eqCut (int b)   { return "eq_b" + juce::String (b) + "_cut"; }
inline juce::String eqShelf (int b) { return "eq_b" + juce::String (b) + "_shelf"; }
inline const juce::String eqWarm = "eq_warm", eqHpf = "eq_hpf", eqHpf24 = "eq_hpf_24", eqLpf = "eq_lpf",
                          eqMono = "eq_mono", eqOut = "eq_out";

// tape
inline const juce::String tapeSpeed = "tape_speed", tapeEq = "tape_eq", tapeType = "tape_type", tapeBias = "tape_bias",
                          tapeIn = "tape_in", tapeOut = "tape_out", tapeLink = "tape_link", tapeWow = "tape_wow";

juce::String hzText (float hz);   // "OFF", "80 Hz", "4.5 kHz"

// a log frequency range whose bottom (or top) few percent of travel is OFF (value 0)
juce::NormalisableRange<float> offRange (float lo, float hi, bool offAtTop);

void addLayout (juce::AudioProcessorValueTreeState::ParameterLayout&, const char* const* slotIds, const char* const* slotNames, int numSlots);
} // namespace pid
