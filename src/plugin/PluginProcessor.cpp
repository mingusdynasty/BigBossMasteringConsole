#include "PluginProcessor.h"
#include "PluginEditor.h"

const BigBossProcessor::SlotInfo BigBossProcessor::kSlots[kNumSlots] = {
    { "eq",    "Parametric EQ", "step 2" },
    { "comp",  "Compressor",    "step 3" },
    { "tape",  "Tape",          "" },
    { "deess", "De-esser",      "step 4" },
    { "limit", "Limiter",       "step 3" },
};

namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto db = juce::AudioParameterFloatAttributes().withLabel ("dB")
                  .withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; });
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { BigBossProcessor::kInTrimId, 1 }, "Input Trim",
                                                             juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { BigBossProcessor::kOutTrimId, 1 }, "Output Trim",
                                                             juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { BigBossProcessor::kTapeAmountId, 1 }, "Tape Drive", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })));
    for (int s = 0; s < BigBossProcessor::kNumSlots; ++s)
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { BigBossProcessor::bypassId (s), 1 },
                                                                juce::String (BigBossProcessor::kSlots[s].name) + " Bypass", false));
    return layout;
}
}

BigBossProcessor::BigBossProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BigBoss", createLayout())
{
    for (int s = 0; s < kNumSlots; ++s)
    {
        if (juce::String (kSlots[s].id) == "tape")
        {
            tape = new bb::TapeModule();
            chain.add (std::unique_ptr<bb::Module> (tape));
        }
        else
            chain.add (std::make_unique<bb::EmptySlot> (kSlots[s].id, kSlots[s].name));
        pBypass[(size_t) s] = apvts.getRawParameterValue (bypassId (s));
    }
    pInTrim  = apvts.getRawParameterValue (kInTrimId);
    pOutTrim = apvts.getRawParameterValue (kOutTrimId);
    pTape    = apvts.getRawParameterValue (kTapeAmountId);
}

bool BigBossProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void BigBossProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz.store (sampleRate);
    const int block = juce::jmax (64, samplesPerBlock);
    bufL.assign ((size_t) block, 0.0);
    bufR.assign ((size_t) block, 0.0);
    tape->amount.store (pTape->load() / 100.0);
    for (int s = 0; s < kNumSlots; ++s) chain.setBypassed (s, pBypass[(size_t) s]->load() > 0.5f);
    chain.prepare (sampleRate, block);
    const int nch = juce::jmax (1, getTotalNumInputChannels());
    inMeters.prepare (sampleRate, nch);
    outMeters.prepare (sampleRate, nch);

    latency = chain.latency();
    setLatencySamples (latency);
    int len = 1;
    while (len < latency + 1) len <<= 1;
    bypassMask = len - 1;
    for (auto& l : bypassLine) l.assign ((size_t) len, 0.0);
    bypassPos = 0;
    inGain  = juce::Decibels::decibelsToGain ((double) pInTrim->load());
    outGain = juce::Decibels::decibelsToGain ((double) pOutTrim->load());
}

void BigBossProcessor::reset()
{
    chain.reset();
    for (auto& l : bypassLine) std::fill (l.begin(), l.end(), 0.0);
}

void BigBossProcessor::setOrder (const bb::Chain::Order& o)
{
    chain.setOrder (o);
}

template <typename T>
void BigBossProcessor::processImpl (juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nIn = getTotalNumInputChannels();
    for (int c = nIn; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (nIn == 0 || bufL.empty()) return;

    tape->amount.store (pTape->load() / 100.0);
    for (int s = 0; s < kNumSlots; ++s) chain.setBypassed (s, pBypass[(size_t) s]->load() > 0.5f);
    const double inTarget  = juce::Decibels::decibelsToGain ((double) pInTrim->load());
    const double outTarget = juce::Decibels::decibelsToGain ((double) pOutTrim->load());

    T* outL = buffer.getWritePointer (0);
    T* outR = nIn > 1 ? buffer.getWritePointer (1) : nullptr;
    const int chunk = (int) bufL.size();
    for (int off = 0; off < n; off += chunk)
    {
        const int k = juce::jmin (chunk, n - off);
        double* l = bufL.data();
        double* r = bufR.data();
        for (int i = 0; i < k; ++i) { l[i] = (double) outL[off + i]; r[i] = outR ? (double) outR[off + i] : l[i]; }

        inMeters.process (l, outR ? r : l, k);

        // trims glide linearly across the chunk
        const double gi = (inTarget - inGain) / k, go = (outTarget - outGain) / k;
        for (int i = 0; i < k; ++i) { inGain += gi; l[i] *= inGain; r[i] *= inGain; }
        inGain = inTarget;

        chain.process (l, r, k);

        for (int i = 0; i < k; ++i) { outGain += go; l[i] *= outGain; r[i] *= outGain; }
        outGain = outTarget;

        outMeters.process (l, outR ? r : l, k);

        for (int i = 0; i < k; ++i) outL[off + i] = (T) l[i];
        if (outR) for (int i = 0; i < k; ++i) outR[off + i] = (T) r[i];
    }
}

template <typename T>
void BigBossProcessor::bypassImpl (juce::AudioBuffer<T>& buffer)
{
    const int n = buffer.getNumSamples(), nch = juce::jmin (2, buffer.getNumChannels());
    if (latency == 0 || bypassLine[0].empty()) return;
    for (int c = 0; c < nch; ++c)
    {
        T* p = buffer.getWritePointer (c);
        auto& line = bypassLine[(size_t) c];
        int w = bypassPos;
        for (int i = 0; i < n; ++i)
        {
            line[(size_t) w] = (double) p[i];
            p[i] = (T) line[(size_t) ((w - latency) & bypassMask)];
            w = (w + 1) & bypassMask;
        }
    }
    bypassPos = (bypassPos + n) & bypassMask;
}

void BigBossProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)  { processImpl (b); }
void BigBossProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer&) { processImpl (b); }
void BigBossProcessor::processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer&)  { bypassImpl (b); }
void BigBossProcessor::processBlockBypassed (juce::AudioBuffer<double>& b, juce::MidiBuffer&) { bypassImpl (b); }

juce::AudioProcessorEditor* BigBossProcessor::createEditor() { return new BigBossEditor (*this); }

void BigBossProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::StringArray ids;
    const auto o = chain.order();
    for (int k = 0; k < kNumSlots; ++k) ids.add (kSlots[o[(size_t) k]].id);
    state.setProperty ("order", ids.joinIntoString (","), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BigBossProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);

    // the order is saved by slot id, so adding slots in later versions keeps old sessions loading
    const auto ids = juce::StringArray::fromTokens (state.getProperty ("order").toString(), ",", "");
    bb::Chain::Order o {};
    int k = 0;
    std::array<bool, kNumSlots> used {};
    for (const auto& id : ids)
        for (int s = 0; s < kNumSlots; ++s)
            if (id == kSlots[s].id && ! used[(size_t) s] && k < kNumSlots) { o[(size_t) k++] = s; used[(size_t) s] = true; }
    for (int s = 0; s < kNumSlots; ++s)
        if (! used[(size_t) s]) o[(size_t) k++] = s;
    chain.setOrder (o);

    state.removeProperty ("order", nullptr);
    apvts.replaceState (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BigBossProcessor();
}
