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
    juce::StringArray tags {};   // for the browser's search
};
const std::vector<Factory>& factory();

// user presets live in <user app data>/Big Boss Mastering Console/Presets, one .bbpreset (XML) each
juce::File userFolder();
juce::Array<juce::File> userPresets();   // sorted by name
inline const juce::String extension = ".bbpreset";

// every preset, for the browser: the factory set first, then the user's, each with its tags (a user
// preset's are the comma-separated "tags" attribute of its file)
struct Entry
{
    juce::String name, blurb;
    juce::StringArray tags;
    int factoryIndex = -1;   // -1 for a user preset
    juce::File file;
};
std::vector<Entry> all();
juce::StringArray tagsOf (const juce::File& userPreset);
bool matches (const Entry&, const juce::String& search, const juce::String& tag);   // case-insensitive, name or tags
} // namespace presets
