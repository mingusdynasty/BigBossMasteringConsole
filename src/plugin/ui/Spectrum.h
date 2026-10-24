// Spectrum analyser (mid and side, 8192-point FFT, 4.5 dB/oct slope), spectrogram and goniometer.
#pragma once

#include "Look.h"
#include "meter/MeterSuite.h"
#include "meter/TruePeak.h"

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

    // a reference's average spectrum (same bin layout), drawn as a line, shifted by offsetDb; an empty
    // vector hides it. The label is drawn in the plot's corner
    void setReference (std::vector<float> db, float offsetDb, const juce::String& label);
    void setNote (const juce::String& s) { if (s != note) { note = s; repaint(); } }   // centre of the title row

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
    std::vector<float> refDb;
    float refOffset = 0;
    juce::String refLabel, note;
};

// What a lossy codec would do to the output's true peak, estimated: the output band-limited as an
// encoder would (16 kHz for MP3 at 128 kb/s, 19.5 kHz for AAC at 256 kb/s) with a linear-phase
// low-pass, then true-peak metered. Band-limiting is the larger, predictable part of a codec's
// overshoot; the encoder's own noise adds a little more, so treat these as a floor. Runs on the
// message thread from the meter suite's sample ring.
class CodecPreview
{
public:
    CodecPreview();
    void setSource (bb::MeterSuite* m, double sampleRate);
    void tick();
    void reset();
    static constexpr int kCodecs = 2;
    static const char* name (int codec);
    float truePeakDb (int codec) const { return held[(size_t) codec]; }   // since the last reset; -200 before any

private:
    static constexpr int kTaps = 127;
    bb::MeterSuite* src = nullptr;
    double sr = 48000;
    std::uint64_t readPos = 0;
    std::array<std::vector<double>, kCodecs> coef;
    std::array<std::array<std::array<double, 2 * kTaps>, 2>, kCodecs> hist {};
    int pos = 0;
    std::array<bb::TruePeak, kCodecs> meter;
    std::array<float, kCodecs> held {};
    std::vector<double> outL, outR;
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
