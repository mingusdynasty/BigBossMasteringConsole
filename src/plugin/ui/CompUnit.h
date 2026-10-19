// The two-stage compressor as a rack unit: optical and discrete sections, each with its own
// gain-reduction meter, and the output transformer selector between them.
#pragma once

#include "RackControls.h"
#include "dyn/DualCompressor.h"

#include <memory>

namespace bbui::rack
{
class CompUnit final : public Unit
{
public:
    CompUnit (Apvts&, const bb::DualCompressor&);
    void tick() override;

private:
    void layout() override;

    const bb::DualCompressor& comp;
    GrMeter optoMeter { "OPTICAL", 20.0f }, vcaMeter { "DISCRETE", 20.0f };
    std::unique_ptr<Toggle> optoIn, vcaIn, scHpf;
    std::unique_ptr<Knob> optoThr, optoGain, vcaThr, ratio, attack, recovery, vcaGain, xfmr;
};
} // namespace bbui::rack
