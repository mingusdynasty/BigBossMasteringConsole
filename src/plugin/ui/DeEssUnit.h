// The multiband de-esser as a rack unit: an LED ladder of gain reduction for each of the four
// bands, labelled with the bands' centres, then threshold and range, then frequency, release,
// mode and listen.
#pragma once

#include "RackControls.h"
#include "deess/DeEsser.h"

#include <array>
#include <functional>
#include <memory>

namespace bbui::rack
{
// four LED columns, one per band: reduction 0.5 .. 18 dB, amber to red, with a short peak hold
class BandLadders final : public juce::Component
{
public:
    void push (int band, float reductionDb, float fps);   // once per frame per band
    void paint (juce::Graphics&) override;
    std::array<juce::String, bb::DeEsser::kBands> names;

private:
    std::array<float, bb::DeEsser::kBands> level {}, peak {}, held {};
};

class DeEssUnit final : public Unit
{
public:
    DeEssUnit (Apvts&, const bb::DeEsser&, std::function<bb::DeEsser::Settings()> settings);
    void tick() override;

private:
    void layout() override;

    const bb::DeEsser& deesser;
    std::function<bb::DeEsser::Settings()> settings;
    BandLadders ladders;
    std::unique_ptr<Knob> threshold, range, freq, release, mode;
    std::unique_ptr<Toggle> listen;
};
} // namespace bbui::rack
