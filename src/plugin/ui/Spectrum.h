// Spectrum analyser (mid and side, 8192-point FFT, 4.5 dB/oct slope), spectrogram and goniometer.
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
    void clearPeak();

    // the mid spectrum of the latest tick, unsmoothed, with the slope applied (dB per bin)
    const std::vector<float>& frame() const { return raw; }
    double sampleRate() const { return sr; }

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
    std::vector<float> raw;
};

// time across (newest at the right), frequency up on the spectrum's log scale, level as colour; one
// column per frame from the spectrum
class Spectrogram final : public juce::Component
{
public:
    Spectrogram();
    void push (const std::vector<float>& db, double sampleRate);
    void clear();
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr float kFramesPerSecond = 30.0f;   // the editor's frame rate

private:
    juce::Rectangle<int> plot() const;
    juce::Image image;
    std::array<juce::Colour, 256> palette;
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
