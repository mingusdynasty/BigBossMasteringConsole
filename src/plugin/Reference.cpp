#include "Reference.h"

#include "meter/LoudnessMeter.h"
#include "meter/TruePeak.h"

#include <juce_dsp/juce_dsp.h>

std::shared_ptr<ReferenceTrack> ReferenceTrack::load (const juce::File& file, double rate, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) { error = "Can't read " + file.getFileName() + " (WAV, AIFF, FLAC or Ogg)"; return nullptr; }
    if (reader->sampleRate < 8000 || reader->lengthInSamples < (juce::int64) (reader->sampleRate * 2))
    {
        error = file.getFileName() + " is too short to compare with";
        return nullptr;
    }
    auto t = std::make_shared<ReferenceTrack>();
    t->file = file;
    t->sourceRate = reader->sampleRate;
    const auto len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (kMaxSeconds * reader->sampleRate));
    t->source.setSize (2, len);
    reader->read (&t->source, 0, len, 0, true, true);   // mono files fill both channels
    if (reader->numChannels == 1) t->source.copyFrom (1, 0, t->source, 0, 0, len);
    auto r = t->atRate (rate);
    return r;
}

std::shared_ptr<ReferenceTrack> ReferenceTrack::atRate (double newRate) const
{
    auto t = std::make_shared<ReferenceTrack>();
    t->file = file;
    t->source = source;
    t->sourceRate = sourceRate;
    t->rate = newRate;
    const int inLen = source.getNumSamples();
    if (std::abs (newRate - sourceRate) < 0.5)
        t->audio = source;
    else
    {
        const double ratio = sourceRate / newRate;   // input samples per output sample
        const int outLen = (int) std::floor (inLen / ratio) - 8;
        t->audio.setSize (2, juce::jmax (1, outLen));
        for (int c = 0; c < 2; ++c)
        {
            juce::WindowedSincInterpolator interp;
            interp.process (ratio, source.getReadPointer (c), t->audio.getWritePointer (c), t->audio.getNumSamples(), inLen, 0);
        }
    }
    t->analyse();
    return t;
}

void ReferenceTrack::analyse()
{
    const int n = audio.getNumSamples();
    // loudness and true peak, as the analyser measures them
    {
        bb::LoudnessMeter loud;
        loud.prepare (rate, 2);
        bb::TruePeak tp;
        tp.prepare (rate, 2);
        std::vector<double> l (4096), r (4096);
        for (int off = 0; off < n; off += 4096)
        {
            const int k = juce::jmin (4096, n - off);
            for (int i = 0; i < k; ++i) { l[(size_t) i] = audio.getSample (0, off + i); r[(size_t) i] = audio.getSample (1, off + i); }
            const double* ch[2] { l.data(), r.data() };
            loud.process (ch, k);
            tp.process (ch, k);
        }
        lufs = loud.integrated();
        range = loud.range();
        truePeak = juce::Decibels::gainToDecibels (juce::jmax (tp.heldPeak (0), tp.heldPeak (1)), -200.0);
    }
    // the average mid spectrum: power averaged over half-overlapped Hann frames, scaled and tilted as
    // the analyser's live one is
    {
        juce::dsp::FFT fft (kSpectrumOrder);
        std::vector<float> win ((size_t) kSpectrumSize), work ((size_t) (2 * kSpectrumSize));
        for (int i = 0; i < kSpectrumSize; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) kSpectrumSize);
        std::vector<double> power ((size_t) kSpectrumSize / 2 + 1, 0.0);
        int frames = 0;
        for (int off = 0; off + kSpectrumSize <= n; off += kSpectrumSize / 2, ++frames)
        {
            for (int i = 0; i < kSpectrumSize; ++i)
                work[(size_t) i] = 0.5f * (audio.getSample (0, off + i) + audio.getSample (1, off + i)) * win[(size_t) i];
            std::fill (work.begin() + kSpectrumSize, work.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (work.data(), true);
            for (size_t b = 0; b < power.size(); ++b) power[b] += (double) work[b] * work[b];
        }
        const double norm = 2.0 / (0.5 * kSpectrumSize);
        spectrum.assign (power.size(), -200.0f);
        for (size_t b = 1; b < power.size() && frames > 0; ++b)
        {
            const double hz = (double) b * rate / kSpectrumSize;
            const double amp = std::sqrt (power[b] / frames) * norm;
            spectrum[b] = (float) (juce::Decibels::gainToDecibels (amp, -200.0) + 4.5 * std::log2 (hz / 1000.0));
        }
    }
    // the assistant's view of it
    {
        std::vector<float> l (audio.getReadPointer (0), audio.getReadPointer (0) + n), r (audio.getReadPointer (1), audio.getReadPointer (1) + n);
        profile = bb::assist::analyse (l, r, rate);
    }
}
