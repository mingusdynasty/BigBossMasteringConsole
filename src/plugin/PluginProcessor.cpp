#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"

const BigBossProcessor::SlotInfo BigBossProcessor::kSlots[kNumSlots] = {
    { "eq",    "Parametric EQ", "" },
    { "comp",  "Compressor",    "" },
    { "tape",  "Tape",          "" },
    { "deess", "De-esser",      "step 4" },
    { "limit", "Limiter",       "" },
};

namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const char* ids[BigBossProcessor::kNumSlots];
    const char* names[BigBossProcessor::kNumSlots];
    for (int s = 0; s < BigBossProcessor::kNumSlots; ++s) { ids[s] = BigBossProcessor::kSlots[s].id; names[s] = BigBossProcessor::kSlots[s].name; }
    pid::addLayout (layout, ids, names, BigBossProcessor::kNumSlots);
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
        const juce::String id (kSlots[s].id);
        if (id == "eq")
        {
            eq = new bb::EqModule();
            chain.add (std::unique_ptr<bb::Module> (eq));
        }
        else if (id == "tape")
        {
            tape = new bb::TapeModule();
            chain.add (std::unique_ptr<bb::Module> (tape));
        }
        else if (id == "comp")
        {
            comp = new bb::CompModule();
            chain.add (std::unique_ptr<bb::Module> (comp));
        }
        else if (id == "limit")
        {
            limit = new bb::LimitModule();
            chain.add (std::unique_ptr<bb::Module> (limit));
        }
        else
            chain.add (std::make_unique<bb::EmptySlot> (kSlots[s].id, kSlots[s].name));
        pBypass[(size_t) s] = apvts.getRawParameterValue (bypassId (s));
    }
    pInTrim  = apvts.getRawParameterValue (pid::inTrim);
    pOutTrim = apvts.getRawParameterValue (pid::outTrim);

    auto raw = [this] (const juce::String& pidStr) { return apvts.getRawParameterValue (pidStr); };
    for (int b = 0; b < bb::eq::kBands; ++b)
    {
        pEq.freq[(size_t) b] = raw (pid::eqFreq (b));
        pEq.gain[(size_t) b] = raw (pid::eqGain (b));
        pEq.cut[(size_t) b]  = raw (pid::eqCut (b));
        if (bb::eq::kRange[b].canShelf) pEq.shelf[(size_t) b] = raw (pid::eqShelf (b));
    }
    pEq.warm = raw (pid::eqWarm); pEq.hpf = raw (pid::eqHpf); pEq.hpf24 = raw (pid::eqHpf24);
    pEq.lpf = raw (pid::eqLpf); pEq.mono = raw (pid::eqMono); pEq.out = raw (pid::eqOut);
    pTape = { raw (pid::tapeSpeed), raw (pid::tapeEq), raw (pid::tapeType), raw (pid::tapeBias),
              raw (pid::tapeIn), raw (pid::tapeOut), raw (pid::tapeLink), raw (pid::tapeWow) };
    pComp = { raw (pid::compOpto), raw (pid::compOptoThr), raw (pid::compOptoGain), raw (pid::compVca), raw (pid::compVcaThr),
              raw (pid::compRatio), raw (pid::compAttack), raw (pid::compRecovery), raw (pid::compVcaGain), raw (pid::compXfmr),
              raw (pid::compScHpf) };
    pLimit = { raw (pid::limDrive), raw (pid::limCeiling), raw (pid::limStyle), raw (pid::limAuto), raw (pid::limRelease),
               raw (pid::limTp), raw (pid::limLink) };
}

bb::DualCompressor::Settings BigBossProcessor::compSettings() const
{
    bb::DualCompressor::Settings s;
    s.opto = pComp.opto->load() > 0.5f;
    s.optoThreshold = pComp.optoThr->load();
    s.optoGain = pComp.optoGain->load();
    s.vca = pComp.vca->load() > 0.5f;
    s.vcaThreshold = pComp.vcaThr->load();
    s.ratio = juce::roundToInt (pComp.ratio->load());
    s.attack = juce::roundToInt (pComp.attack->load());
    s.recovery = juce::roundToInt (pComp.recovery->load());
    s.vcaGain = pComp.vcaGain->load();
    s.transformer = juce::roundToInt (pComp.xfmr->load());
    s.sidechainHpf = pComp.scHpf->load() > 0.5f;
    return s;
}

bb::Limiter::Settings BigBossProcessor::limitSettings() const
{
    bb::Limiter::Settings s;
    s.driveDb = pLimit.drive->load();
    s.ceilingDb = pLimit.ceiling->load();
    s.style = juce::roundToInt (pLimit.style->load());
    s.autoRelease = pLimit.autoRel->load() > 0.5f;
    s.releaseMs = pLimit.release->load();
    s.truePeak = pLimit.tp->load() > 0.5f;
    s.link = pLimit.link->load() / 100.0;
    return s;
}

bb::eq::Settings BigBossProcessor::eqSettings() const
{
    bb::eq::Settings s;
    for (size_t b = 0; b < (size_t) bb::eq::kBands; ++b)
    {
        s.band[b].freq  = pEq.freq[b]->load();
        s.band[b].gain  = pEq.gain[b]->load();
        s.band[b].cut   = pEq.cut[b]->load() > 0.5f;
        s.band[b].shelf = pEq.shelf[b] != nullptr && pEq.shelf[b]->load() > 0.5f;
    }
    s.warm = pEq.warm->load() > 0.5f;
    s.hpfHz = pEq.hpf->load();
    s.hpfOrder = pEq.hpf24->load() > 0.5f ? 4 : 2;
    s.lpfHz = pEq.lpf->load();
    s.monoBassHz = pEq.mono->load();
    s.outputDb = pEq.out->load();
    return s;
}

bb::TapeMachine::Settings BigBossProcessor::tapeSettings() const
{
    bb::TapeMachine::Settings s;
    s.speed    = juce::roundToInt (pTape.speed->load());
    s.standard = juce::roundToInt (pTape.standard->load());
    s.tape     = juce::roundToInt (pTape.type->load());
    s.bias     = juce::roundToInt (pTape.bias->load());
    s.inputDb  = pTape.in->load();
    s.outputDb = pTape.out->load();
    s.link     = pTape.link->load() > 0.5f;
    s.wow      = pTape.wow->load() / 100.0;
    return s;
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
    eq->settings = eqSettings();
    tape->settings = tapeSettings();
    comp->settings = compSettings();
    limit->settings = limitSettings();
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

    eq->settings = eqSettings();
    tape->settings = tapeSettings();
    comp->settings = compSettings();
    limit->settings = limitSettings();
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
