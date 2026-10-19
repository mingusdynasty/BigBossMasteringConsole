// Big Boss Mastering Console: the JUCE side. Builds the chain, maps parameters, runs the meters.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "chain/Chain.h"
#include "chain/CompModule.h"
#include "chain/EqModule.h"
#include "chain/LimitModule.h"
#include "chain/TapeModule.h"
#include "meter/MeterSuite.h"

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // the console's slots, in their default order; ids are used in parameter ids and saved state
    struct SlotInfo { const char* id; const char* name; const char* comingIn; };
    static constexpr int kNumSlots = 5;
    static const SlotInfo kSlots[kNumSlots];

    static juce::String bypassId (int slot) { return juce::String (kSlots[slot].id) + "_bypass"; }

    // chain order, from the editor (message thread); saved with the session
    bb::Chain::Order getOrder() const { return chain.order(); }
    void setOrder (const bb::Chain::Order& o);
    bool slotIsBuilt (int slot) const { return std::strlen (kSlots[slot].comingIn) == 0; }

    // the modules' settings as the parameters stand now; safe from any thread
    bb::eq::Settings eqSettings() const;
    bb::TapeMachine::Settings tapeSettings() const;
    bb::DualCompressor::Settings compSettings() const;
    bb::Limiter::Settings limitSettings() const;
    const bb::TapeMachine& tapeMachine() const { return tape->machine; }
    const bb::DualCompressor& compressor() const { return comp->comp; }
    bb::Limiter& limiter() { return limit->limiter; }

    juce::AudioProcessorValueTreeState apvts;
    bb::MeterSuite inMeters, outMeters;
    std::atomic<double> sampleRateHz { 48000.0 };
    int editorTab = 0;   // the tab the editor last showed, so reopening it lands there

private:
    template <typename T> void processImpl (juce::AudioBuffer<T>& buffer);
    template <typename T> void bypassImpl (juce::AudioBuffer<T>& buffer);

    bb::Chain chain;
    bb::EqModule* eq = nullptr;
    bb::TapeModule* tape = nullptr;
    bb::CompModule* comp = nullptr;
    bb::LimitModule* limit = nullptr;
    std::array<std::atomic<float>*, kNumSlots> pBypass {};
    std::atomic<float>* pInTrim = nullptr;
    std::atomic<float>* pOutTrim = nullptr;

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

    std::vector<double> bufL, bufR;
    double inGain = 1, outGain = 1;
    int latency = 0;

    std::array<std::vector<double>, 2> bypassLine;
    int bypassPos = 0, bypassMask = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BigBossProcessor)
};
