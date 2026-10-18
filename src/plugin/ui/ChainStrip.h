// The chain as a row of modules: click one to open its tab, drag it to reorder, click its lamp to bypass it.
#pragma once

#include "Look.h"

#include <functional>
#include <vector>

namespace bbui
{
class ChainStrip final : public juce::Component
{
public:
    struct Slot { juce::String name, note; bool built = true; };

    std::vector<Slot> slots;                       // by slot index
    std::function<std::vector<int>()> getOrder;    // slot indices, in running order
    std::function<void (const std::vector<int>&)> setOrder;
    std::function<bool (int)> isBypassed;
    std::function<void (int)> toggleBypass;
    std::function<void (int)> onSelect;            // a module was clicked (not dragged)
    int selected = -1;                             // slot whose tab is showing, outlined

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int position) const;
    juce::Rectangle<float> lamp (juce::Rectangle<float> cell) const;
    int positionAt (float x) const;

    int dragSlot = -1, dragFrom = -1;
    float dragX = 0, grabOffset = 0;
    bool dragging = false;
};
} // namespace bbui
