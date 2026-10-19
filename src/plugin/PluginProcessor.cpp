#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"

const BigBossProcessor::SlotInfo BigBossProcessor::kSlots[kNumSlots] = {
    { "eq",    "Parametric EQ", "" },
    { "comp",  "Compressor",    "" },
    { "tape",  "Tape",          "" },
    { "deess", "De-esser",      "" },
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

namespace
{
double volumeGain (float db) { return db <= pid::volumeMinDb ? 0.0 : juce::Decibels::decibelsToGain ((double) db); }

void notePeaks (std::array<std::atomic<float>, 2>& peak, const double* l, const double* r, int n)
{
    const double* ch[2] = { l, r };
    for (int c = 0; c < 2; ++c)
    {
        double m = 0;
        for (int i = 0; i < n; ++i) m = std::max (m, std::abs (ch[c][i]));
        if ((float) m > peak[(size_t) c].load (std::memory_order_relaxed)) peak[(size_t) c].store ((float) m, std::memory_order_relaxed);
    }
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
        else if (id == "deess")
        {
            deess = new bb::DeEssModule();
            chain.add (std::unique_ptr<bb::Module> (deess));
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
    pVolume  = apvts.getRawParameterValue (pid::volume);

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
    pDeess = { raw (pid::deessThr), raw (pid::deessRange), raw (pid::deessFreq), raw (pid::deessRelease), raw (pid::deessMode),
               raw (pid::deessListen) };
}

bb::DeEsser::Settings BigBossProcessor::deessSettings() const
{
    bb::DeEsser::Settings s;
    s.thresholdDb = pDeess.thr->load();
    s.rangeDb = pDeess.range->load();
    s.freq = pDeess.freq->load();
    s.releaseMs = pDeess.release->load();
    s.mode = juce::roundToInt (pDeess.mode->load());
    s.listen = pDeess.listen->load() > 0.5f;
    return s;
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
    deess->settings = deessSettings();
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
    outGain = volumeGain (pVolume->load());

    capturing.store (false);
    captureFull.store (false);
    for (auto& c : capture) c.assign ((size_t) (kCaptureSeconds * sampleRate), 0.0f);
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

float BigBossProcessor::takePeakDb (bool output, int ch)
{
    const float p = (output ? outPeak : inPeak)[(size_t) juce::jlimit (0, 1, ch)].exchange (0.0f, std::memory_order_relaxed);
    return p > 1e-5f ? juce::Decibels::gainToDecibels (p) : -100.0f;
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
    deess->settings = deessSettings();
    for (int s = 0; s < kNumSlots; ++s) chain.setBypassed (s, pBypass[(size_t) s]->load() > 0.5f);
    const double inTarget  = juce::Decibels::decibelsToGain ((double) pInTrim->load());
    const double outTarget = volumeGain (pVolume->load());

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
        notePeaks (inPeak, l, outR ? r : l, k);
        if (capturing.load (std::memory_order_relaxed)) captureBlock (l, outR ? r : l, k);

        // trims glide linearly across the chunk
        const double gi = (inTarget - inGain) / k, go = (outTarget - outGain) / k;
        for (int i = 0; i < k; ++i) { inGain += gi; l[i] *= inGain; r[i] *= inGain; }
        inGain = inTarget;

        chain.process (l, r, k);

        for (int i = 0; i < k; ++i) { outGain += go; l[i] *= outGain; r[i] *= outGain; }
        outGain = outTarget;

        outMeters.process (l, outR ? r : l, k);
        notePeaks (outPeak, l, outR ? r : l, k);

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

void BigBossProcessor::captureBlock (const double* l, const double* r, int n)
{
    // keep blocks with something in them (above -50 dBFS), so pauses don't count
    double peak = 0;
    for (int i = 0; i < n; ++i) peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
    if (peak < 0.00316) return;
    const int size = (int) capture[0].size();
    int pos = capturePos.load (std::memory_order_relaxed);
    const int k = std::min (n, size - pos);
    for (int i = 0; i < k; ++i) { capture[0][(size_t) (pos + i)] = (float) l[i]; capture[1][(size_t) (pos + i)] = (float) r[i]; }
    pos += k;
    capturePos.store (pos, std::memory_order_relaxed);
    if (pos >= size)
    {
        captureFull.store (true, std::memory_order_release);
        capturing.store (false, std::memory_order_release);
    }
}

void BigBossProcessor::startCapture()
{
    if (capture[0].empty()) return;
    capturing.store (false);
    captureFull.store (false);
    capturePos.store (0);
    capturing.store (true, std::memory_order_release);
}

float BigBossProcessor::captureProgress() const
{
    return capture[0].empty() ? 0.0f : (float) capturePos.load (std::memory_order_relaxed) / (float) capture[0].size();
}

bool BigBossProcessor::takeCapture (std::vector<float>& l, std::vector<float>& r, double& rate)
{
    if (! captureFull.exchange (false, std::memory_order_acquire)) return false;
    l = capture[0];
    r = capture[1];
    rate = sampleRateHz.load();
    return true;
}

bb::assist::Order BigBossProcessor::assistOrder() const
{
    // the assistant's slots are the processor's, in the same order
    static_assert (kNumSlots == bb::assist::kSlots);
    const auto o = chain.order();
    bb::assist::Order a {};
    for (int k = 0; k < kNumSlots; ++k) a[(size_t) k] = o[(size_t) k];
    return a;
}

presets::Values BigBossProcessor::currentValues() const
{
    presets::Values v;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            v[rp->getParameterID()] = rp->convertFrom0to1 (rp->getValue());
    return v;
}

void BigBossProcessor::applyValues (const presets::Values& values, bool othersToDefault)
{
    for (auto* p : getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr) continue;
        const auto it = values.find (rp->getParameterID());
        if (it == values.end() && ! othersToDefault) continue;
        const float norm = it != values.end() ? rp->convertTo0to1 (it->second) : rp->getDefaultValue();
        if (std::abs (norm - rp->getValue()) < 1e-6f) continue;
        rp->beginChangeGesture();
        rp->setValueNotifyingHost (norm);
        rp->endChangeGesture();
    }
}

const juce::String BigBossProcessor::getProgramName (int i)
{
    const auto& f = presets::factory();
    return i >= 0 && i < (int) f.size() ? juce::String (f[(size_t) i].name) : juce::String();
}

void BigBossProcessor::loadFactoryPreset (int i)
{
    const auto& f = presets::factory();
    if (i < 0 || i >= (int) f.size()) return;
    applyValues (f[(size_t) i].values, true);
    bb::Chain::Order o {};
    for (int k = 0; k < kNumSlots; ++k) o[(size_t) k] = k;
    chain.setOrder (o);
    currentFactory = i;
    presetName = f[(size_t) i].name;
}

bool BigBossProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    const auto dir = presets::userFolder();
    if (! dir.createDirectory()) return false;
    juce::MemoryBlock mb;
    getStateInformation (mb);
    auto xml = getXmlFromBinary (mb.getData(), (int) mb.getSize());
    if (xml == nullptr) return false;
    xml->setAttribute ("preset", clean);
    if (! xml->writeTo (dir.getChildFile (clean + presets::extension))) return false;
    presetName = clean;
    currentFactory = -1;
    return true;
}

bool BigBossProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    presets::Values v;
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        v[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");
    applyValues (v, true);
    setOrderFromIds (xml->getStringAttribute ("order"));
    presetName = file.getFileNameWithoutExtension();
    currentFactory = -1;
    return true;
}

void BigBossProcessor::setOrderFromIds (const juce::String& list)
{
    // the order is saved by slot id, so adding slots in later versions keeps old sessions loading
    const auto ids = juce::StringArray::fromTokens (list, ",", "");
    bb::Chain::Order o {};
    int k = 0;
    std::array<bool, kNumSlots> used {};
    for (const auto& id : ids)
        for (int s = 0; s < kNumSlots; ++s)
            if (id == kSlots[s].id && ! used[(size_t) s] && k < kNumSlots) { o[(size_t) k++] = s; used[(size_t) s] = true; }
    for (int s = 0; s < kNumSlots; ++s)
        if (! used[(size_t) s]) o[(size_t) k++] = s;
    chain.setOrder (o);
}

void BigBossProcessor::applyPlan (const bb::assist::Plan& p)
{
    using namespace pid;
    presets::Values v;
    for (int b = 0; b < bb::eq::kBands; ++b)
    {
        v[eqFreq (b)] = (float) p.eq.band[b].freq;
        v[eqGain (b)] = (float) p.eq.band[b].gain;
        v[eqCut (b)] = p.eq.band[b].cut ? 1.0f : 0.0f;
        if (b == 0 || b == bb::eq::kBands - 1) v[eqShelf (b)] = p.eq.band[b].shelf ? 1.0f : 0.0f;
    }
    v[eqWarm] = p.eq.warm ? 1.0f : 0.0f;
    v[eqHpf] = (float) p.eq.hpfHz;
    v[eqHpf24] = p.eq.hpfOrder == 4 ? 1.0f : 0.0f;
    v[eqLpf] = (float) p.eq.lpfHz;
    v[eqMono] = (float) p.eq.monoBassHz;
    v[eqOut] = (float) p.eq.outputDb;

    v[compOpto] = p.comp.opto ? 1.0f : 0.0f;
    v[compOptoThr] = (float) p.comp.optoThreshold;
    v[compOptoGain] = (float) p.comp.optoGain;
    v[compVca] = p.comp.vca ? 1.0f : 0.0f;
    v[compVcaThr] = (float) p.comp.vcaThreshold;
    v[compRatio] = (float) p.comp.ratio;
    v[compAttack] = (float) p.comp.attack;
    v[compRecovery] = (float) p.comp.recovery;
    v[compVcaGain] = (float) p.comp.vcaGain;
    v[compXfmr] = (float) p.comp.transformer;
    v[compScHpf] = p.comp.sidechainHpf ? 1.0f : 0.0f;

    v[tapeSpeed] = (float) p.tape.speed;
    v[tapeEq] = (float) p.tape.standard;
    v[tapeType] = (float) p.tape.tape;
    v[tapeBias] = (float) p.tape.bias;
    v[tapeIn] = (float) p.tape.inputDb;
    v[tapeOut] = (float) p.tape.outputDb;
    v[tapeLink] = p.tape.link ? 1.0f : 0.0f;
    v[tapeWow] = (float) (p.tape.wow * 100.0);

    v[deessThr] = (float) p.deess.thresholdDb;
    v[deessRange] = (float) p.deess.rangeDb;
    v[deessFreq] = (float) p.deess.freq;
    v[deessRelease] = (float) p.deess.releaseMs;
    v[deessMode] = (float) p.deess.mode;
    v[deessListen] = 0.0f;

    v[limDrive] = (float) p.limit.driveDb;
    v[limCeiling] = (float) p.limit.ceilingDb;
    v[limStyle] = (float) p.limit.style;
    v[limAuto] = p.limit.autoRelease ? 1.0f : 0.0f;
    v[limRelease] = (float) p.limit.releaseMs;
    v[limTp] = p.limit.truePeak ? 1.0f : 0.0f;
    v[limLink] = (float) (p.limit.link * 100.0);

    for (int s = 0; s < kNumSlots; ++s) v[bypassId (s)] = p.bypass[(size_t) s] ? 1.0f : 0.0f;
    v[inTrim] = (float) p.inTrimDb;
    applyValues (v, false);
    presetName = "Assistant";
    currentFactory = -1;
}

void BigBossProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", presetName, nullptr);
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

    setOrderFromIds (state.getProperty ("order").toString());
    presetName = state.getProperty ("preset", "Init").toString();
    currentFactory = -1;

    state.removeProperty ("order", nullptr);
    state.removeProperty ("preset", nullptr);
    apvts.replaceState (state);

    // replaceState only touches parameters whose stored value changed; one the host left between
    // steps (a switch at 0.3, say) keeps that raw value unless it is set here
    presets::Values v;
    for (const auto& child : state)
        if (child.hasType ("PARAM")) v[child.getProperty ("id").toString()] = (float) child.getProperty ("value");
    applyValues (v, false);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BigBossProcessor();
}
