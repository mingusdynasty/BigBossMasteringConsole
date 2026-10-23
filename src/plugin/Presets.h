// Presets: the factory set, compiled in, and the user's own, saved as files. A preset holds every
// parameter and the chain order; parameters a factory preset doesn't name are at their defaults.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <vector>

namespace presets
{
using Values = std::map<juce::String, float>;   // parameter id -> plain value

struct Factory
{
    const char* name;
    const char* blurb;   // one line for the tooltip
    Values values;
};
const std::vector<Factory>& factory();

// user presets live in <user app data>/Big Boss Mastering Console/Presets, one .bbpreset (XML) each
juce::File userFolder();
juce::Array<juce::File> userPresets();   // sorted by name
inline const juce::String extension = ".bbpreset";
} // namespace presets
