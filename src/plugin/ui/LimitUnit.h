// The limiter as a rack unit: a scrolling display of input, output and gain reduction, a
// gain-reduction meter, and the drive, ceiling, style and release controls.
#pragma once

#include "RackControls.h"
#include "dyn/Limiter.h"

#include <array>
#include <memory>

namespace bbui::rack
{
// six seconds of the limiter at work: input peaks (grey), output peaks (amber), the ceiling,
// and gain reduction hanging from the top (red)
class LimitHistory final : public juce::Component
{
public:
    void push (float inDb, float outDb, float reductionDb);   // once per frame
    void paint (juce::Graphics&) override;
    float ceilingDb = -1.0f;

private:
    static constexpr int kFrames = 180;
    std::array<float, kFrames> in {}, out {}, red {};
    int pos = 0;
    bool primed = false;
};

class LimitUnit final : public Unit
{
public:
    LimitUnit (Apvts&, bb::Limiter&, std::function<bb::Limiter::Settings()> settings);
    void tick() override;

private:
    void layout() override;

    bb::Limiter& limiter;
    std::function<bb::Limiter::Settings()> settings;
    LimitHistory history;
    GrMeter meter { "LIMITER", 12.0f };
    std::unique_ptr<Knob> drive, ceiling, style, release, link;
    std::unique_ptr<Toggle> autoRel, truePeak;
};
} // namespace bbui::rack
