// Rack hardware for the module tabs: faceplates with ears and screws, machined knobs, bat-handle
// toggles, selector knobs, VU meters and blanking plates.
#pragma once

#include "Look.h"

#include <functional>
#include <vector>

namespace bbui::rack
{
// A faceplate's finish: plate colours and the colour its legends are printed in
struct Finish
{
    juce::Colour top, bottom, legend, accent;
    bool brushed = true;
};

juce::Font engraving (float height, bool bold = true);
void paintScrew (juce::Graphics&, juce::Point<float> c, float r);

// Knobs and switches. A control's look comes from its properties:
//   Slider  "style": "big" (machined aluminium), "small", "chicken" (pointer knob for selectors)
//           "scale": "0:OFF|0.5:100|1:300", legends printed round the knob at those positions
//   Button  "up", "down": legends above and below a bat-handle toggle (up = on)
class RackLook final : public juce::LookAndFeel_V4
{
public:
    RackLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    juce::Font getSliderPopupFont (juce::Slider&) override { return mono (12.0f); }
    juce::Colour legendFor (const juce::Component& c) const;
};

// One rack unit: ears and faceplate; controls are children placed inside plate()
class Unit : public juce::Component
{
public:
    Unit (juce::String title, juce::String model, Finish finish);
    ~Unit() override { setLookAndFeel (nullptr); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    virtual void tick();                         // once per editor frame while the unit is showing

    juce::Rectangle<int> plate() const;          // the faceplate, inside the ears
    juce::Rectangle<int> content() const;        // the plate below the title band
    const Finish& finish() const { return fin; }

    // the IN lamp in the title band: lit while the module is in; clicking it bypasses
    std::function<bool()> isIn;
    std::function<void()> toggleIn;

protected:
    virtual void layout() {}                      // place controls and legends inside content()
    virtual void paintPlate (juce::Graphics&) {}  // decoration under the controls
    void engrave (juce::Graphics&, const juce::String&, juce::Rectangle<int>, float size,
                  juce::Justification j = juce::Justification::centred, bool bold = true) const;

    // legends and rules printed on the plate, rebuilt by layout()
    struct Legend { juce::String text; juce::Rectangle<int> area; float size; bool bold; juce::Justification just; };
    std::vector<Legend> legends;
    std::vector<juce::Line<float>> rules;
    void legend (juce::String text, juce::Rectangle<int> area, float size = 10.0f, bool bold = true,
                 juce::Justification j = juce::Justification::centred) { legends.push_back ({ std::move (text), area, size, bold, j }); }

    juce::String title, model;
    Finish fin;
    juce::Image texture;
    RackLook look;

private:
    juce::Rectangle<int> lampArea() const;
    bool lit = false;
};

// Analog VU meter: cream face, -20 .. +3 scale, needle with VU ballistics (300 ms)
class VuMeter final : public juce::Component
{
public:
    explicit VuMeter (juce::String label);
    void push (float meanSquareRelOp, float fps);   // feed once per frame
    void paint (juce::Graphics&) override;

private:
    juce::String label;
    float needle = -20.0f, ms = 0.0f;
};

// Blank 3U plate for a module that is not built yet
class BlankUnit final : public Unit
{
public:
    BlankUnit (juce::String title, juce::String when);

private:
    void paintPlate (juce::Graphics&) override;
    juce::String when;
};
} // namespace bbui::rack
