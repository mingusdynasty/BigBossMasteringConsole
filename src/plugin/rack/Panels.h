// The console as a rack, in the manner of the owner's Vocal Chain plugin: one 1U faceplate per
// module in chain order, then the output fader. The analyser is a 5U unit that takes the modules'
// place when it is switched on. Everything is laid out in a fixed design space and the editor
// scales the whole rack.
#pragma once

#include "Widgets.h"
#include "deess/DeEsser.h"
#include "dyn/DualCompressor.h"
#include "dyn/Limiter.h"
#include "plugin/ui/MeterPanel.h"
#include "plugin/ui/Spectrum.h"
#include "tape/TapeMachine.h"

#include "assist/Assistant.h"
#include "chain/Chain.h"
#include "plugin/AssistantHost.h"
#include "plugin/Presets.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

class BigBossProcessor;

namespace kit
{
inline constexpr int unitHeight = 118;
inline constexpr int earWidth = 44;
inline constexpr int rackWidth = 1280;
inline constexpr int panelWidth = rackWidth - 2 * earWidth;
inline constexpr int nameWidth = 150;   // the name plate at the left of each faceplate

using Apvts = juce::AudioProcessorValueTreeState;

class Panel : public juce::Component
{
public:
    explicit Panel (Apvts& s) : state (s) {}

    virtual void tick (float /*seconds*/) {}   // once per editor frame

    // dragging a module by its name plate moves it in the chain: dy from where the drag started
    std::function<void (int dy)> onDrag;
    std::function<void()> onDrop;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

protected:
    struct Knob
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<Apvts::SliderAttachment> attachment;
        int size = 60;
        juce::String paramId;
    };
    struct Switch
    {
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<Apvts::ButtonAttachment> attachment;
        std::unique_ptr<juce::ParameterAttachment> inverted;
    };

    Knob& addKnob (const juce::String& paramId, KnobStyle, int size, const juce::String& tooltip, bool detents = false);
    Switch& addSwitch (const juce::String& paramId, ButtonStyle, const juce::String& text, const juce::String& tooltip, bool invert = false);
    Switch& addPower (const juce::String& bypassParamId);   // lamp lit while the module is in
    ChoiceButtons& addChoice (const juce::String& paramId, const juce::StringArray& labels, ButtonStyle, bool vertical,
                              const juce::String& tooltip);

    void placeKnob (Knob&, juce::Point<int> centre);
    std::vector<std::pair<float, juce::String>> marksFor (const juce::String& paramId,
                                                          const std::vector<std::pair<float, juce::String>>& valuesAndLabels) const;
    void scale (juce::Graphics&, const Knob&, const std::vector<std::pair<float, juce::String>>& valuesAndLabels,
                juce::Colour ink, float fontHeight = 7.0f) const;

    enum class Finish { brushed, enamel, powder };
    void paintFinish (juce::Graphics&, juce::Colour base, Finish) const;
    void nameplate (juce::Graphics&, const juce::String& name, const juce::String& line1, const juce::String& line2,
                    juce::Colour ink) const;
    void legendOver (juce::Graphics&, const juce::String& text, int centreX, juce::Colour ink, int y = 4, int width = 110) const;
    void rule (juce::Graphics&, int x, juce::Colour ink) const;   // a printed divider between sections

    juce::Rectangle<int> nameArea() const { return { 36, 0, nameWidth - 36, getHeight() }; }
    void placePower (Switch& power) { power.button->setBounds (juce::Rectangle<int> (18, 18).withCentre ({ 22, 59 })); }

    Apvts& state;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Switch>> switches;
    std::vector<std::unique_ptr<ChoiceButtons>> choices;

private:
    bool dragging = false;
};

// ------------------------------------------------------------------- the modules

class EqPanel final : public Panel
{
public:
    explicit EqPanel (Apvts&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Switch* power;
    std::array<Knob*, 5> freq {}, gain {};
    std::array<Switch*, 5> cut {};
    std::array<Switch*, 5> shelf {};   // outer bands only
    Knob *hpf, *lpf, *mono, *out;
    Switch *hpf24, *warm;
};

class CompPanel final : public Panel
{
public:
    CompPanel (Apvts&, const bb::DualCompressor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float seconds) override;

private:
    Switch *power, *optoIn, *vcaIn, *scHpf;
    Knob *optoThr, *optoGain, *vcaThr, *ratio, *attack, *recovery, *vcaGain;
    ChoiceButtons* xfmr;
    VuMeter optoMeter, vcaMeter;
};

class TapePanel final : public Panel
{
public:
    TapePanel (Apvts&, const bb::TapeMachine&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float seconds) override;

private:
    Switch *power, *link;
    ChoiceButtons *speed, *eq, *formulation, *bias;
    Knob *input, *output, *wow;
    VuMeter vuL, vuR;
};

// four horizontal LED rows, one per band: reduction 0.5 .. 18 dB with a short peak hold
class BandLadders final : public juce::Component
{
public:
    void push (int band, float reductionDb, float seconds);
    void paint (juce::Graphics&) override;
    std::array<juce::String, bb::DeEsser::kBands> names;

private:
    std::array<float, bb::DeEsser::kBands> level {}, peak {}, held {};
};

class DeEssPanel final : public Panel
{
public:
    DeEssPanel (Apvts&, const bb::DeEsser&, std::function<double()> lowestBandHz, std::function<double()> sampleRate);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float seconds) override;

private:
    const bb::DeEsser& deesser;
    std::function<double()> lowestBandHz, sampleRate;
    Switch *power, *listen;
    Knob *threshold, *range, *freq, *release;
    ChoiceButtons* mode;
    BandLadders ladders;
};

// the last six seconds of the limiter: input peaks (grey), output peaks (amber), the ceiling,
// and gain reduction hanging from the top (red)
class LimitHistory final : public juce::Component
{
public:
    void push (float inDb, float outDb, float reductionDb);
    void paint (juce::Graphics&) override;
    float ceilingDb = -1.0f;

private:
    static constexpr int kFrames = 180;
    std::array<float, kFrames> in {}, out {}, red {};
    int pos = 0;
    bool primed = false;
};

class LimitPanel final : public Panel
{
public:
    LimitPanel (Apvts&, bb::Limiter&, std::function<bb::Limiter::Settings()> settings);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float seconds) override;

private:
    bb::Limiter& limiter;
    std::function<bb::Limiter::Settings()> settings;
    Switch *power, *autoRel, *truePeak, *stayLast;
    Knob *drive, *ceiling, *release, *link;
    ChoiceButtons* style;
    LimitHistory history;
    VuMeter meter;
    float lastReduction = 0.0f;
};

// ------------------------------------------------------------------- master section

inline constexpr int masterHeight = unitHeight / 2;   // BB-1 is a half-height unit

// the top unit: presets (factory and the user's own), and the keys that open the assistant and the
// analyser
class MasterPanel final : public Panel, public juce::TooltipClient
{
public:
    explicit MasterPanel (BigBossProcessor&);
    juce::String getTooltip() override;   // over the preset display: what the preset does
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float seconds) override;

    std::function<void()> onOrderChanged;     // a preset moved the modules
    std::function<bool()> assistantNews;      // lights the lamp by ASSISTANT
    juce::ToggleButton assistantKey { "ASSISTANT" };
    juce::ToggleButton analyserKey { "ANALYSER" };   // the same switch as the output unit's

private:
    void showPresetMenu();
    void stepPreset (int delta);
    void savePreset();

    BigBossProcessor& proc;
    juce::ToggleButton prev { "<" }, next { ">" }, save { "SAVE" }, browse { "BROWSE" };
    juce::Rectangle<int> presetLcd;
    juce::String shownPreset;
    bool shownLamp = false;
    std::unique_ptr<juce::AlertWindow> saveDialog;

    void mouseUp (const juce::MouseEvent&) override;
};

// the assistant, which listens to the mix and sets the console up for a loudness goal: a 1U panel that
// drops down over the rack from BB-1's ASSISTANT key. It is a view of the processor's AssistantHost, so
// the work goes on while it is folded away or the window is closed
class AssistantPanel final : public Panel
{
public:
    explicit AssistantPanel (BigBossProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float seconds) override;

    static constexpr int shadow = 10;   // drawn below the faceplate, over the rack
    std::function<void()> onOrderChanged;   // undo moved the modules
    std::function<void()> onClose;
    bool hasNews() const { return host.hasNews(); }

    // for the snapshot tool
    void pressListen() { host.listen(); }
    bool busy() const { return host.busy(); }

private:
    using State = AssistantHost::State;

    BigBossProcessor& proc;
    AssistantHost& host;
    juce::ToggleButton listenKey { "LISTEN" }, applyKey { "APPLY" }, undoKey { "UNDO" }, closeKey { "CLOSE" };
    std::array<juce::ToggleButton, 4> goalKeys;
    juce::Rectangle<int> statusLcd;
    State shownState = State::Idle;
};

// ------------------------------------------------------------------- output and analyser

class OutputPanel final : public Panel
{
public:
    explicit OutputPanel (BigBossProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float seconds) override;

    juce::ToggleButton analyserKey { "ANALYSER" };

private:
    BigBossProcessor& proc;
    juce::Slider fader { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    Apvts::SliderAttachment faderAttachment;
    Knob* trim;
    Switch *mono, *lock;
    juce::ComboBox dither;
    std::unique_ptr<Apvts::ComboBoxAttachment> ditherAttachment;   // made once the box has its items
    // the OVER lamp: red while the output went over the ceiling, amber while CEILING LOCK caught it;
    // each held a second so a single over is seen
    float overHold = 0, lockHold = 0;
    int shownLamp = 0;
    PeakMeter inMeter, outMeter;
    juce::Label readout;
    float shortTerm = -200, integrated = -200, truePeak = -200;
    bool shownBypass = false;
};

class AnalyserPanel final : public Panel
{
public:
    explicit AnalyserPanel (BigBossProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float seconds) override;   // the loudness history runs whether or not the analyser shows (unless frozen)
    void refreshSource();                 // after a sample-rate change

private:
    void useSource (bool output);

    void reset();

    BigBossProcessor& proc;
    bbui::MeterPanel meters;
    bbui::Spectrum spectrum;
    bbui::Spectrogram spectrogram;
    bbui::Goniometer gonio;
    juce::ToggleButton freezeKey { "FREEZE" }, resetKey { "RESET" }, bypassKey { "BYPASS ALL" };
    juce::ToggleButton inKey { "IN" }, outKey { "OUT" };
    juce::ComboBox target;
    bool showingOutput = true, frozen = false, shownBypass = false;
    double shownRate = 0;
    juce::Rectangle<int> screen;
};
} // namespace kit
