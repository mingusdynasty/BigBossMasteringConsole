// Big Boss Mastering Console: the JUCE side. Builds the chain, maps parameters, runs the meters.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "chain/Chain.h"
#include "chain/CompModule.h"
#include "chain/DeEssModule.h"
#include "chain/EqModule.h"
#include "chain/LimitModule.h"
#include "chain/TapeModule.h"
#include "assist/Assistant.h"
#include "meter/MeterSuite.h"
#include "Presets.h"

class BigBossProcessor final : public juce::AudioProcessor
{
public:
    BigBossProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&,  juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }
    // the host's bypass: the dry signal is delayed by the chain's latency, so nothing shifts in time
    void processBlockBypassed (juce::AudioBuffer<float>&,  juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // the host's program list is the factory presets
    int getNumPrograms() override { return (int) presets::factory().size(); }
    int getCurrentProgram() override { return juce::jmax (0, currentFactory); }
    void setCurrentProgram (int i) override { loadFactoryPreset (i); }
    const juce::String getProgramName (int i) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // the console's slots, in their default order; ids are used in parameter ids and saved state
    struct SlotInfo { const char* id; const char* name; };
    static constexpr int kNumSlots = 5;
    static const SlotInfo kSlots[kNumSlots];

    static juce::String bypassId (int slot) { return juce::String (kSlots[slot].id) + "_bypass"; }

    // chain order, from the editor (message thread); saved with the session
    bb::Chain::Order getOrder() const { return chain.order(); }
    void setOrder (const bb::Chain::Order& o);

    // the modules' settings as the parameters stand now; safe from any thread
    bb::eq::Settings eqSettings() const;
    bb::TapeMachine::Settings tapeSettings() const;
    bb::DualCompressor::Settings compSettings() const;
    bb::Limiter::Settings limitSettings() const;
    bb::DeEsser::Settings deessSettings() const;
    const bb::TapeMachine& tapeMachine() const { return tape->machine; }
    const bb::DualCompressor& compressor() const { return comp->comp; }
    bb::Limiter& limiter() { return limit->limiter; }
    const bb::DeEsser& deesser() const { return deess->deesser; }

    juce::AudioProcessorValueTreeState apvts;
    bb::MeterSuite inMeters, outMeters;
    std::atomic<double> sampleRateHz { 48000.0 };
    bool editorAnalysis = false;   // the editor last showed the analyser in place of the modules
    // the analyser's BYPASS ALL: the output is the input, untouched and delayed by the chain's latency,
    // while the chain keeps running and both meters keep measuring. Not saved, so a session never
    // opens bypassed.
    std::atomic<bool> bypassAll { false };

    // sample peak (dBFS) per channel since the last call, before the input trim or after the fader; for the meters
    float takePeakDb (bool output, int ch);

    // presets (message thread). Loading one sets every parameter (and, for a user preset, the order),
    // telling the host about each change.
    void loadFactoryPreset (int index);
    bool loadUserPreset (const juce::File&);
    bool saveUserPreset (const juce::String& name);   // false if the file couldn't be written
    juce::String presetName { "Init" };                // shown on the preset display; saved with the session
    presets::Values currentValues() const;             // every parameter's plain value
    void applyValues (const presets::Values&, bool othersToDefault);

    // the assistant: capture a stretch of the input (before the trim, skipping near-silence), then
    // apply a plan made from it
    static constexpr double kCaptureSeconds = 10.0;
    void startCapture();
    void cancelCapture() { capturing.store (false); }
    bool isCapturing() const { return capturing.load (std::memory_order_acquire); }
    float captureProgress() const;   // 0 .. 1
    bool takeCapture (std::vector<float>& l, std::vector<float>& r, double& sampleRate);   // once full
    bb::assist::Order assistOrder() const;
    void applyPlan (const bb::assist::Plan&);

private:
    template <typename T> void processImpl (juce::AudioBuffer<T>& buffer);
    template <typename T> void bypassImpl (juce::AudioBuffer<T>& buffer);

    bb::Chain chain;
    bb::EqModule* eq = nullptr;
    bb::TapeModule* tape = nullptr;
    bb::CompModule* comp = nullptr;
    bb::LimitModule* limit = nullptr;
    bb::DeEssModule* deess = nullptr;
    std::array<std::atomic<float>*, kNumSlots> pBypass {};
    std::atomic<float>* pInTrim = nullptr;
    std::atomic<float>* pVolume = nullptr;
    std::array<std::atomic<float>, 2> inPeak {}, outPeak {};
    int currentFactory = 0;

    std::array<std::vector<float>, 2> capture;
    std::atomic<int> capturePos { 0 };
    std::atomic<bool> capturing { false }, captureFull { false };
    void captureBlock (const double* l, const double* r, int n);
    void setOrderFromIds (const juce::String& commaSeparated);

    struct EqParams
    {
        std::array<std::atomic<float>*, bb::eq::kBands> freq {}, gain {}, cut {}, shelf {};   // shelf: outer bands only
        std::atomic<float>* warm = nullptr, *hpf = nullptr, *hpf24 = nullptr, *lpf = nullptr, *mono = nullptr, *out = nullptr;
    } pEq;
    struct TapeParams
    {
        std::atomic<float>* speed = nullptr, *standard = nullptr, *type = nullptr, *bias = nullptr,
                          *in = nullptr, *out = nullptr, *link = nullptr, *wow = nullptr;
    } pTape;
    struct CompParams
    {
        std::atomic<float>* opto = nullptr, *optoThr = nullptr, *optoGain = nullptr, *vca = nullptr, *vcaThr = nullptr, *ratio = nullptr,
                          *attack = nullptr, *recovery = nullptr, *vcaGain = nullptr, *xfmr = nullptr, *scHpf = nullptr;
    } pComp;
    struct LimitParams
    {
        std::atomic<float>* drive = nullptr, *ceiling = nullptr, *style = nullptr, *autoRel = nullptr, *release = nullptr,
                          *tp = nullptr, *link = nullptr;
    } pLimit;
    struct DeEssParams
    {
        std::atomic<float>* thr = nullptr, *range = nullptr, *freq = nullptr, *release = nullptr, *mode = nullptr, *listen = nullptr;
    } pDeess;

    std::vector<double> bufL, bufR;
    double inGain = 1, outGain = 1;
    int latency = 0;

    // the dry signal, delayed by the chain's latency: for the host's bypass and for BYPASS ALL. Fed on
    // every block, so either can switch in without a stale stretch
    std::array<std::vector<double>, 2> bypassLine;
    int bypassPos = 0, bypassMask = 0;
    std::vector<double> dryL, dryR;
    static constexpr double kBypassFade = 0.010;   // seconds
    double dryMix = 0;   // 0 = the console, 1 = bypassed; ramps over kBypassFade
    void delayDry (const double* l, const double* r, double* outL, double* outR, int n);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BigBossProcessor)
};
