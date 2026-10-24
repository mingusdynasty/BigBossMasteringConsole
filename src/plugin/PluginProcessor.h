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
#include "dsp/Dither.h"
#include "meter/MeterSuite.h"
#include "AssistantHost.h"
#include "Presets.h"
#include "Reference.h"

class BigBossProcessor final : public juce::AudioProcessor, private juce::AudioProcessorListener
{
public:
    BigBossProcessor();
    ~BigBossProcessor() override;

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
    // a session or user preset reports the factory preset it was last on, as it did when it was saved,
    // so a host that pushes the program back after restoring doesn't reload that preset over it
    int getCurrentProgram() override { return juce::jmax (0, currentFactory.load()); }
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

    // chain order, from the editor (message thread); saved with the session. With STAY LAST on
    // (limit_last), any order is applied with the limiter moved to the end
    bb::Chain::Order getOrder() const { return chain.order(); }
    void setOrder (const bb::Chain::Order& o);
    bb::Chain::Order constrained (bb::Chain::Order o) const;
    int limiterSlot() const;

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
    // MATCH: BYPASS ALL plays the dry signal at the console's loudness, so the compare is about sound,
    // not level. The difference (output minus input short-term loudness) is followed while the console
    // is heard, and held while it isn't. Not saved.
    std::atomic<bool> matchCompare { false };
    float matchGainDb() const { return matchDb.load (std::memory_order_relaxed); }

    // the analyser's reference track: REF plays it, looping, at the console's loudness. The file is
    // remembered with the session
    bool loadReference (const juce::File&, juce::String& error);
    void clearReference();
    std::shared_ptr<const ReferenceTrack> reference() const;   // null if none
    std::atomic<bool> refPlay { false };
    std::atomic<int> refVersion { 0 };   // changes with every load or clear
    float referenceGainDb() const { return refGainDb.load (std::memory_order_relaxed); }

    // A/B/C/D: four sets of settings (everything a preset holds, and the order) to switch between.
    // Saved with the session
    static constexpr int kCompareSlots = 4;
    int activeSlot() const { return compareActive; }
    bool slotUsed (int i) const { return compareSlots[(size_t) i].used; }
    void selectSlot (int i);   // keeps the current settings in the active slot, then loads i (an empty
                               // slot starts as a copy of the current settings)
    void copyToSlot (int i);   // the current settings into slot i, staying where we are

    // UNDO / REDO inside the plugin: a snapshot of every parameter and the order after each edit (a
    // control let go, a preset, a slot, a plan, a reorder). Kept while the plugin is loaded, not saved
    bool canUndo() const { return historyAt > 0; }
    bool canRedo() const { return historyAt + 1 < (int) history.size(); }
    bool undo();
    bool redo();
    void noteEdit() { editPending.store (true); }   // anything that changed settings outside a gesture
    void checkpoint();   // message thread: records a snapshot if an edit has finished since the last

    // sample peak (dBFS) per channel since the last call, before the input trim or after the fader; for the meters
    float takePeakDb (bool output, int ch);
    // since the last call: how far (dB) the output's true peak went over the limiter's ceiling (0 if it
    // didn't), and the most the CEILING LOCK had to take off (dB)
    float takeOverDb() { return overDb.exchange (0.0f, std::memory_order_relaxed); }
    float takeLockDb() { return lockDb.exchange (0.0f, std::memory_order_relaxed); }

    // presets (message thread). Loading one sets every parameter (and, for a user preset, the order),
    // telling the host about each change.
    void loadFactoryPreset (int index);
    bool loadUserPreset (const juce::File&);
    bool saveUserPreset (const juce::String& name, const juce::StringArray& tags = {});   // false if the file couldn't be written
    juce::String getPresetName() const;               // shown on the preset display; saved with the session
    void setPresetName (const juce::String&);
    presets::Values currentValues() const;             // every parameter's plain value
    // How a set of values is applied. Preset: gain staging and output settings (sessionOnly()) are left
    // alone. Gestures: each change is a user gesture to the host (off while restoring a session, so the
    // host records no undo steps and doesn't mark the project changed)
    enum class Apply { Normal, Preset, Restore };
    void applyValues (const presets::Values&, bool othersToDefault, Apply how = Apply::Normal);
    // trim, fader, MONO, CEILING LOCK, dither and STAY LAST: they belong to the session, not to presets
    static bool sessionOnly (const juce::String& paramId);

    // the assistant: capture a stretch of the input (before the trim, skipping near-silence), then
    // apply a plan made from it. The capture starts on the audio thread at its next block, so a start
    // never races a block already writing
    static constexpr double kCaptureSeconds = 10.0;
    void startCapture();
    void cancelCapture() { captureRequest.store (false); capturing.store (false); }
    bool isCapturing() const { return capturing.load (std::memory_order_acquire); }
    bool capturePending() const { return captureRequest.load() || isCapturing(); }
    float captureProgress() const;   // 0 .. 1
    bool takeCapture (std::vector<float>& l, std::vector<float>& r, double& sampleRate);   // once full
    bb::assist::Order assistOrder() const;
    void applyPlan (const bb::assist::Plan&);
    AssistantHost& assistant() { return *assistantHost; }

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
    std::atomic<float>* pOutMono = nullptr;
    std::atomic<float>* pLock = nullptr, *pDither = nullptr, *pLimitLast = nullptr;
    double monoMix = 0;   // 0 = stereo, 1 = summed to mono; ramps over kBypassFade
    std::array<std::atomic<float>, 2> inPeak {}, outPeak {};
    std::atomic<float> overDb { 0.0f }, lockDb { 0.0f };
    std::atomic<int> currentFactory { 0 };

    // MATCH and REF levels: the output's and input's short-term loudness, followed while the console
    // is what's heard
    double loudIn = -200, loudOut = -200, matchEst = 0, dryGain = 1, refGain = 1, refMix = 0;
    std::atomic<float> matchDb { 0.0f }, refGainDb { 0.0f };
    std::shared_ptr<ReferenceTrack> ref;
    juce::SpinLock refLock;   // the audio thread only ever tries it
    int refPos = 0, heardFor = 0;

    struct Snapshot
    {
        presets::Values values;
        bb::Chain::Order order {};
        bool used = false;
        bool operator== (const Snapshot& o) const { return values == o.values && order == o.order; }
    };
    Snapshot snapshot (bool sessionToo) const;
    void restore (const Snapshot&, Apply how);
    std::array<Snapshot, kCompareSlots> compareSlots;
    int compareActive = 0;

    std::vector<Snapshot> history;
    int historyAt = -1;
    std::atomic<bool> editPending { true };
    std::atomic<int> gesturesOpen { 0 };
    bool applyingHistory = false;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++gesturesOpen; }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override
    {
        if (--gesturesOpen <= 0) { gesturesOpen = 0; editPending.store (true); }
    }
    juce::String presetName { "Init" };
    mutable juce::SpinLock presetNameLock;

    // CEILING LOCK: a second limiter after the fader and MONO, at the BB-L1's ceiling. It's a pure delay
    // until something would go over, and it runs in a one-slot chain so switching it fades
    bb::Chain guard;
    bb::LimitModule* guardLimit = nullptr;
    bb::TruePeak overMeter;   // the console's own output, for the OVER lamp
    bb::Dither dither;

    std::array<std::vector<float>, 2> capture;
    juce::SpinLock captureLock;   // the audio thread only ever tries it
    std::atomic<int> capturePos { 0 };
    std::atomic<bool> captureRequest { false }, capturing { false }, captureFull { false };
    void captureBlock (const double* l, const double* r, int n);
    // applies the order (or, given parsed, only reads it into that); false (and nothing changed) if the
    // list names no slot
    bool setOrderFromIds (const juce::String& commaSeparated, bb::Chain::Order* parsed = nullptr);

    struct EqParams
    {
        std::array<std::atomic<float>*, bb::eq::kBands> freq {}, gain {}, cut {}, shelf {};   // shelf: outer bands only
        std::atomic<float>* warm = nullptr, *hpf = nullptr, *hpf24 = nullptr, *lpf = nullptr, *mono = nullptr, *out = nullptr, *stereo = nullptr;
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

    // last, so it is destroyed first: its worker uses the rest
    std::unique_ptr<AssistantHost> assistantHost;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BigBossProcessor)
};
