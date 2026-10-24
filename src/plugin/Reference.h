// A reference track for the analyser: a commercial master (or any file) to compare against. It is
// played level-matched to the console's output, and its loudness and average spectrum are drawn
// over the analyser's own. Loaded on the message thread; the audio thread only reads it.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "assist/Assistant.h"

#include <memory>
#include <vector>

struct ReferenceTrack
{
    juce::File file;
    juce::AudioBuffer<float> source;   // as loaded, stereo
    double sourceRate = 48000;

    juce::AudioBuffer<float> audio;    // at the console's rate
    double rate = 48000;
    double lufs = -200, truePeak = -200, range = 0;   // integrated LUFS, dBTP, LRA
    // the average mid spectrum, dB per bin, laid out like the analyser's (kSpectrumSize-point FFT at
    // the console's rate, 4.5 dB/oct tilt), so it can be drawn over the live one
    std::vector<float> spectrum;
    // the assistant's measurements of it (tonal balance, width, loudness), for its MATCH REF goal
    bb::assist::Profile profile;

    static constexpr int kSpectrumOrder = 13, kSpectrumSize = 1 << kSpectrumOrder;
    static constexpr double kMaxSeconds = 600.0;

    // null and an error message if the file can't be read
    static std::shared_ptr<ReferenceTrack> load (const juce::File&, double rate, juce::String& error);
    // a copy at another rate (the measurements are made again)
    std::shared_ptr<ReferenceTrack> atRate (double newRate) const;

private:
    void analyse();
};
