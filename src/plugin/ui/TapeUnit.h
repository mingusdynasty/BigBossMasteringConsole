// The tape machine as a rack unit: record-level VU meters, transport and tape switches, levels.
#pragma once

#include "RackControls.h"
#include "tape/TapeMachine.h"

#include <memory>

namespace bbui::rack
{
class TapeUnit final : public Unit
{
public:
    TapeUnit (Apvts&, const bb::TapeMachine&, std::function<bb::TapeMachine::Settings()> settings);
    void tick() override;

private:
    void layout() override;
    void paintPlate (juce::Graphics&) override;

    const bb::TapeMachine& machine;
    std::function<bb::TapeMachine::Settings()> settings;
    VuMeter vuL { "LEFT  RECORD" }, vuR { "RIGHT  RECORD" };
    std::unique_ptr<Toggle> speed, standard, link;
    std::unique_ptr<Knob> tape, bias, in, out, wow;
    juce::Rectangle<int> molArea, eqNoteArea;
    int shownTape = -1, shownSpeed = -1;
};
} // namespace bbui::rack
