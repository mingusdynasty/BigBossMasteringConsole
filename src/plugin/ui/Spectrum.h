// Spectrum analyser (mid and side, 8192-point FFT, 4.5 dB/oct slope) and goniometer.
#pragma once

#include "Look.h"
#include "meter/MeterSuite.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

namespace bbui
{
class Spectrum final : public juce::Component
{
public:
    Spectrum();
    void setSource (bb::MeterSuite* m, double sampleRate);
    void tick();
    void paint (juce::Graphics&) override;

    static constexpr int kOrder = 13, kSize = 1 << kOrder;

private:
    float xOfHz (float hz, juce::Rectangle<float> r) const;
    void buildCurve (const std::vector<float>& db, juce::Rectangle<float> r, juce::Path& out, bool closed) const;

    bb::MeterSuite* src = nullptr;
    double sr = 48000;
    std::uint64_t readPos = 0;
    std::vector<float> ringL, ringR;   // the last kSize samples
    int ringPos = 0;
    juce::dsp::FFT fft { kOrder };
    std::vector<float> window, work;
    std::vector<float> mid, side, peak;   // smoothed dB per bin
};

class Goniometer final : public juce::Component
{
public:
    void setSource (bb::MeterSuite* m) { src = m; readPos = m != nullptr ? m->samples.head() : 0; }
    void tick();
    void paint (juce::Graphics&) override;

private:
    bb::MeterSuite* src = nullptr;
    std::uint64_t readPos = 0;
    std::vector<bb::StereoSample> pts;
    float scale = 1.0f;
};
} // namespace bbui
