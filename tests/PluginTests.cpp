// Plugin-level tests: the processor as a host drives it (JUCE). Run via ctest.
//
//   bb_plugin_tests <golden file>           checks the factory presets' renders against it
//   bb_plugin_tests <golden file> --write   writes it (after a deliberate change to the sound)
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"
#include "plugin/Params.h"
#include "plugin/rack/Panels.h"
#include "meter/TruePeak.h"
#include "plugin/ui/Spectrum.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kSr = 48000;
static constexpr int kBlock = 512;

static float input (int i) { return 0.3f * (float) std::sin (2 * kPi * 110.0 * i / kSr) + 0.1f * (float) std::sin (2 * kPi * 2500.0 * i / kSr); }

// runs blocks through the processor; bypassed(k) says whether block k goes through the host's bypass
template <typename Bypassed, typename BeforeBlock>
static std::vector<float> run (BigBossProcessor& proc, int blocks, Bypassed bypassed, BeforeBlock before)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buf (2, kBlock);
    juce::MidiBuffer midi;
    for (int k = 0; k < blocks; ++k)
    {
        before (k);
        for (int i = 0; i < kBlock; ++i) buf.setSample (0, i, input (k * kBlock + i)), buf.setSample (1, i, input (k * kBlock + i));
        if (bypassed (k)) proc.processBlockBypassed (buf, midi);
        else proc.processBlock (buf, midi);
        out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + kBlock);
    }
    return out;
}

// the largest difference from the input delayed by the latency, over [from, to)
static double offDry (const std::vector<float>& out, int latency, int from, int to)
{
    double worst = 0;
    for (int i = juce::jmax (from, latency); i < to; ++i) worst = juce::jmax (worst, (double) std::abs (out[(size_t) i] - input (i - latency)));
    return worst;
}

// the largest 4th difference: tiny for smooth signals, large at a step (a click)
static double maxD4 (const std::vector<float>& out, int from, int to)
{
    double worst = 0;
    for (int i = from + 4; i < to; ++i)
    {
        const auto x = [&] (int k) { return (double) out[(size_t) (i - k)]; };
        worst = juce::jmax (worst, std::abs (x (0) - 4 * x (1) + 6 * x (2) - 4 * x (3) + x (4)));
    }
    return worst;
}

static void set (BigBossProcessor& p, const juce::String& id, float plain)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}
static float get (BigBossProcessor& p, const juce::String& id)
{
    auto* param = p.apvts.getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

// a short, deterministic mix (kick, bass, a noisy pad, hats), the same on every platform: its noise is
// our own generator, not the standard library's distributions
struct TestMix
{
    std::uint32_t rng = 0x2545f491u;
    double t = 0, padL = 0, padR = 0, hp = 0;
    double noise()   // about unit variance
    {
        double sum = 0;
        for (int k = 0; k < 4; ++k)
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            sum += (double) rng / 4294967296.0 - 0.5;
        }
        return sum * 1.732;
    }
    void fill (juce::AudioBuffer<float>& buf, double sr)
    {
        for (int i = 0; i < buf.getNumSamples(); ++i, t += 1 / sr)
        {
            const double beat = std::fmod (t, 0.5), bar = std::fmod (t, 2.0);
            const double kick = 0.55 * std::exp (-beat * 18) * std::sin (2 * kPi * (50 + 120 * std::exp (-beat * 40)) * beat);
            const double bass = 0.18 * std::sin (2 * kPi * (bar < 1 ? 55 : 41.2) * t);
            padL += (0.05 * noise() - padL) * 0.02; padR += (0.05 * noise() - padR) * 0.02;
            const double n = noise(); hp += (n - hp) * 0.3;
            const double ph = std::fmod (t + 0.25, 0.5);
            const double hat = ph < 0.04 ? 0.05 * (n - hp) * std::exp (-ph * 60) : 0;
            buf.setSample (0, i, (float) (kick + bass + 3 * padL + hat));
            buf.setSample (1, i, (float) (kick + bass + 3 * padR + 0.7 * hat));
        }
    }
};

// counts the change gestures a processor sends its host
struct GestureCounter final : juce::AudioProcessorListener
{
    int gestures = 0;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++gestures; }
};

template <typename T> static T* findChild (juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (child)) return t;
        if (auto* t = findChild<T> (*child)) return t;
    }
    return nullptr;
}

// a factory preset's render of the test mix: integrated loudness, true peak, and the level in six bands
struct Render
{
    double lufs = -200, truePeak = -200;
    std::array<double, 6> bands {};
};
static constexpr double kBandEdges[7] = { 20, 100, 300, 1000, 3000, 8000, 20000 };

static Render renderPreset (int preset)
{
    BigBossProcessor p;
    p.setPlayConfigDetails (2, 2, kSr, kBlock);
    p.prepareToPlay (kSr, kBlock);
    p.loadFactoryPreset (preset);
    p.reset();
    TestMix mix;
    juce::AudioBuffer<float> buf (2, kBlock);
    juce::MidiBuffer midi;
    constexpr int kFft = 1 << 14;
    std::vector<float> tail;
    bb::TruePeak tp;
    tp.prepare (kSr, 2);
    const int blocks = (int) (8.0 * kSr / kBlock);
    for (int k = 0; k < blocks; ++k)
    {
        mix.fill (buf, kSr);
        p.processBlock (buf, midi);
        if (k * kBlock >= (int) kSr)   // after the first second, which settles the dynamics
        {
            std::vector<double> l (buf.getReadPointer (0), buf.getReadPointer (0) + kBlock), r (buf.getReadPointer (1), buf.getReadPointer (1) + kBlock);
            const double* chs[2] { l.data(), r.data() };
            tp.process (chs, kBlock);
            for (int i = 0; i < kBlock; ++i) tail.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
        }
    }
    Render out;
    out.lufs = p.outMeters.readings.integrated.load();
    out.truePeak = juce::Decibels::gainToDecibels (juce::jmax (tp.heldPeak (0), tp.heldPeak (1)), -200.0);
    // band levels: Hann-windowed FFTs of the mid, averaged, power summed per band
    juce::dsp::FFT fft (14);
    std::vector<float> frame ((size_t) (2 * kFft));
    std::array<double, 6> power {};
    int frames = 0;
    for (size_t at = 0; at + kFft <= tail.size(); at += kFft / 2, ++frames)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int i = 0; i < kFft; ++i) frame[(size_t) i] = tail[at + (size_t) i] * (float) (0.5 - 0.5 * std::cos (2 * kPi * i / kFft));
        fft.performFrequencyOnlyForwardTransform (frame.data());
        for (int b = 0; b < kFft / 2; ++b)
        {
            const double hz = b * kSr / kFft;
            for (int band = 0; band < 6; ++band)
                if (hz >= kBandEdges[band] && hz < kBandEdges[band + 1]) power[(size_t) band] += (double) frame[(size_t) b] * frame[(size_t) b];
        }
    }
    for (int band = 0; band < 6; ++band)
        out.bands[(size_t) band] = 10 * std::log10 (power[(size_t) band] / juce::jmax (1, frames) + 1e-30);
    return out;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File golden = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    const bool writeGolden = argc > 2 && juce::String (argv[2]) == "--write";
    auto make = []
    {
        auto p = std::make_unique<BigBossProcessor>();
        p->setPlayConfigDetails (2, 2, kSr, kBlock);
        p->prepareToPlay (kSr, kBlock);
        if (auto* d = p->apvts.getParameter ("limit_drive")) d->setValueNotifyingHost (d->convertTo0to1 (8.0f));   // audibly different from dry
        return p;
    };
    const auto none = [] (int) { return false; };
    const auto nothing = [] (int) {};

    // BYPASS ALL from the start: the output is exactly the input, delayed by the reported latency
    {
        auto p = make();
        const int lat = p->getLatencySamples();
        CHECK (lat > 0, "the chain reports latency (%d)", lat);
        p->bypassAll = true;
        p->prepareToPlay (kSr, kBlock);
        const auto out = run (*p, 40, none, nothing);
        const double off = offDry (out, lat, 0, (int) out.size());
        CHECK (off < 1e-6, "bypass all passes the input untouched (off by %g)", off);
    }

    // switching BYPASS ALL in and out mid-stream: no step bigger than the signal itself makes, and
    // fully dry once the fade is done
    {
        auto p = make();
        const int lat = p->getLatencySamples();
        BigBossProcessor* pp = p.get();
        const auto out = run (*p, 120, none, [pp] (int k) { if (k == 40) pp->bypassAll = true; if (k == 80) pp->bypassAll = false; });
        const double natural = juce::jmax (maxD4 (out, 20 * kBlock, 40 * kBlock), maxD4 (out, 60 * kBlock, 80 * kBlock));
        const double across = juce::jmax (maxD4 (out, 39 * kBlock, 44 * kBlock), maxD4 (out, 79 * kBlock, 84 * kBlock));
        CHECK (across < natural * 2, "bypass all switches without a click (4th difference %g, the signal's own %g)", across, natural);
        const double off = offDry (out, lat, 42 * kBlock, 80 * kBlock);
        CHECK (off < 1e-6, "bypass all is dry after its fade (off by %g)", off);
        const double wet = offDry (out, lat, 100 * kBlock, 120 * kBlock);
        CHECK (wet > 1e-3, "the console comes back after bypass all (differs from dry by %g)", wet);
    }

    // the host's bypass after normal processing: dry and time-aligned from its first sample
    {
        auto p = make();
        const int lat = p->getLatencySamples();
        const auto out = run (*p, 60, [] (int k) { return k >= 40; }, nothing);
        const double off = offDry (out, lat, 40 * kBlock, 60 * kBlock);
        CHECK (off < 1e-6, "the host's bypass is the input delayed by the latency, with no stale stretch (off by %g)", off);
    }

    // the output's MONO: both sides identical once it is in, switched without a click, stereo again after
    {
        auto p = make();
        auto* mono = p->apvts.getParameter ("out_mono");
        std::vector<float> l, r;
        juce::AudioBuffer<float> buf (2, kBlock);
        juce::MidiBuffer midi;
        for (int k = 0; k < 120; ++k)
        {
            if (k == 40) mono->setValueNotifyingHost (1.0f);
            if (k == 80) mono->setValueNotifyingHost (0.0f);
            for (int i = 0; i < kBlock; ++i)
            {
                const int n = k * kBlock + i;
                buf.setSample (0, i, input (n));
                buf.setSample (1, i, 0.3f * (float) std::sin (2 * kPi * 330.0 * n / kSr));   // a different right side
            }
            p->processBlock (buf, midi);
            l.insert (l.end(), buf.getReadPointer (0), buf.getReadPointer (0) + kBlock);
            r.insert (r.end(), buf.getReadPointer (1), buf.getReadPointer (1) + kBlock);
        }
        double side = 0, sideAfter = 0;
        for (int i = 42 * kBlock; i < 80 * kBlock; ++i) side = juce::jmax (side, (double) std::abs (l[(size_t) i] - r[(size_t) i]));
        for (int i = 100 * kBlock; i < 120 * kBlock; ++i) sideAfter = juce::jmax (sideAfter, (double) std::abs (l[(size_t) i] - r[(size_t) i]));
        CHECK (side < 1e-6, "MONO makes both sides the same (differ by %g)", side);
        CHECK (sideAfter > 0.05, "stereo comes back after MONO (sides differ by %g)", sideAfter);
        const double natural = juce::jmax (maxD4 (l, 20 * kBlock, 40 * kBlock), maxD4 (l, 60 * kBlock, 80 * kBlock));
        const double across = juce::jmax (maxD4 (l, 39 * kBlock, 44 * kBlock), maxD4 (l, 79 * kBlock, 84 * kBlock));
        CHECK (across < natural * 2, "MONO switches without a click (4th difference %g, the signal's own %g)", across, natural);
    }

    // the state round trip: every parameter, the order, the preset name and the host program come back
    {
        auto a = make();
        set (*a, pid::limLast, 0.0f);
        set (*a, pid::inTrim, 3.5f);
        set (*a, pid::volume, -4.0f);
        set (*a, pid::outDither, 2.0f);
        set (*a, pid::eqFreq (2), 1800.0f);
        set (*a, pid::compXfmr, 2.0f);
        a->loadFactoryPreset (3);
        bb::Chain::Order o { 4, 2, 0, 1, 3 };   // the limiter first
        a->setOrder (o);
        a->setPresetName ("My master");
        const auto before = a->currentValues();
        juce::MemoryBlock mb;
        a->getStateInformation (mb);

        auto b = std::make_unique<BigBossProcessor>();
        GestureCounter counter;
        b->addListener (&counter);
        b->setStateInformation (mb.getData(), (int) mb.getSize());
        b->removeListener (&counter);
        int differ = 0;
        for (const auto& [id, v] : b->currentValues())
            if (std::abs (v - before.at (id)) > 1e-4f) { ++differ; std::printf ("  %s: %g, was %g\n", id.toRawUTF8(), v, before.at (id)); }
        CHECK (differ == 0, "a session restores every parameter (%d differ)", differ);
        CHECK (b->getOrder() == a->getOrder(), "a session restores the order");
        CHECK (b->getPresetName() == "My master", "a session restores the preset name (%s)", b->getPresetName().toRawUTF8());
        CHECK (b->getCurrentProgram() == 3, "a session restores the host program (%d)", b->getCurrentProgram());
        CHECK (counter.gestures == 0, "restoring a session makes no change gestures (%d)", counter.gestures);
    }

    // a session from before STAY LAST, with the limiter in the middle, keeps its order
    {
        auto a = make();
        set (*a, pid::limLast, 0.0f);
        a->setOrder ({ 0, 4, 1, 2, 3 });
        juce::MemoryBlock mb;
        a->getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
            if (e->getStringAttribute ("id") == pid::limLast) { xml->removeChildElement (e, true); break; }
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        auto b = make();
        b->setStateInformation (old.getData(), (int) old.getSize());
        CHECK (b->getOrder() == a->getOrder(), "an old session's order is kept");
        CHECK (get (*b, pid::limLast) < 0.5f, "an old session with the limiter mid-chain has STAY LAST off");
    }

    // STAY LAST: whatever order is asked for, the limiter ends up last; switched off, any order goes
    {
        auto p = make();
        const int lim = p->limiterSlot();
        p->setOrder ({ lim, 0, 1, 2, 3 });
        CHECK (p->getOrder()[BigBossProcessor::kNumSlots - 1] == lim, "STAY LAST keeps the limiter last");
        CHECK (p->getOrder()[0] == 0 && p->getOrder()[1] == 1, "STAY LAST keeps the others in order");
        set (*p, pid::limLast, 0.0f);
        p->setOrder ({ lim, 0, 1, 2, 3 });
        CHECK (p->getOrder()[0] == lim, "with STAY LAST off the limiter can go first");
        set (*p, pid::limLast, 1.0f);
        run (*p, 2, none, nothing);   // the audio thread puts it back
        CHECK (p->getOrder()[BigBossProcessor::kNumSlots - 1] == lim, "switching STAY LAST on moves the limiter back to the end");
    }

    // presets leave gain staging and the output alone, loading and saving
    {
        auto p = make();
        set (*p, pid::inTrim, 4.0f);
        set (*p, pid::volume, -3.0f);
        set (*p, pid::outMono, 1.0f);
        set (*p, pid::outDither, 3.0f);
        p->loadFactoryPreset (5);
        CHECK (std::abs (get (*p, pid::inTrim) - 4.0f) < 0.01f, "a factory preset keeps the input trim (%g)", get (*p, pid::inTrim));
        CHECK (std::abs (get (*p, pid::volume) + 3.0f) < 0.01f, "a factory preset keeps the fader (%g)", get (*p, pid::volume));
        CHECK (get (*p, pid::outMono) > 0.5f, "a factory preset keeps MONO");
        CHECK (std::abs (get (*p, pid::outDither) - 3.0f) < 0.01f, "a factory preset keeps the dither");

        const juce::String name = "bb test preset " + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30));
        CHECK (p->saveUserPreset (name), "a user preset saves");
        const auto file = presets::userFolder().getChildFile (name + presets::extension);
        auto xml = juce::XmlDocument::parse (file);
        int sessionParams = 0;
        if (xml != nullptr)
            for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                if (BigBossProcessor::sessionOnly (e->getStringAttribute ("id"))) ++sessionParams;
        CHECK (xml != nullptr && sessionParams == 0, "a user preset holds no gain staging or output settings (%d)", sessionParams);
        set (*p, pid::inTrim, -2.0f);
        set (*p, pid::volume, 1.0f);
        CHECK (p->loadUserPreset (file), "a user preset loads");
        CHECK (std::abs (get (*p, pid::inTrim) + 2.0f) < 0.01f && std::abs (get (*p, pid::volume) - 1.0f) < 0.01f,
               "a user preset keeps the trim and fader (%g, %g)", get (*p, pid::inTrim), get (*p, pid::volume));
        CHECK (p->getCurrentProgram() == 5, "a user preset leaves the host program where it was (%d)", p->getCurrentProgram());
        file.deleteFile();
    }

    // the output ceiling, with the fader pushing +3 dB and the limiter first in the chain
    {
        auto measure = [&] (bool lock, float& overDb, float& lockDb)
        {
            auto p = make();
            set (*p, pid::limLast, 0.0f);
            set (*p, pid::outLock, lock ? 1.0f : 0.0f);
            set (*p, pid::volume, 3.0f);
            set (*p, pid::limCeiling, -1.0f);
            for (const char* m : { "eq", "comp", "tape", "deess" }) set (*p, pid::bypass (m), 1.0f);   // only the limiter's level
            p->setOrder ({ 4, 0, 1, 2, 3 });
            p->reset();
            TestMix mix;
            juce::AudioBuffer<float> buf (2, kBlock);
            juce::MidiBuffer midi;
            bb::TruePeak tp;
            tp.prepare (kSr, 2);
            overDb = lockDb = 0;
            for (int k = 0; k < (int) (4 * kSr / kBlock); ++k)
            {
                mix.fill (buf, kSr);
                p->processBlock (buf, midi);
                if (k < 40) { p->takeOverDb(); p->takeLockDb(); continue; }   // past the reorder's fade
                std::vector<double> l (buf.getReadPointer (0), buf.getReadPointer (0) + kBlock), r (buf.getReadPointer (1), buf.getReadPointer (1) + kBlock);
                const double* chs[2] { l.data(), r.data() };
                tp.process (chs, kBlock);
                overDb = juce::jmax (overDb, p->takeOverDb());
                lockDb = juce::jmax (lockDb, p->takeLockDb());
            }
            return juce::Decibels::gainToDecibels (juce::jmax (tp.heldPeak (0), tp.heldPeak (1)));
        };
        float over = 0, caught = 0;
        const double locked = measure (true, over, caught);
        CHECK (locked <= -1.0 + 0.02, "CEILING LOCK holds the ceiling after the fader (%.3f dBTP)", locked);
        CHECK (over == 0.0f, "the OVER lamp stays dark with CEILING LOCK on (%.2f dB)", over);
        CHECK (caught > 1.0f, "the lock reports what it caught (%.2f dB)", caught);
        const double open = measure (false, over, caught);
        CHECK (open > 0.0, "without the lock the fader takes the output over the ceiling (%.2f dBTP)", open);
        CHECK (over > 1.0f && std::abs (over - (float) (open + 1.0)) < 0.5f, "the OVER lamp reports how far (%.2f dB, the output %.2f dBTP)", over, open);
    }

    // the editor follows an order set behind its back (a session restored, a host undo)
    {
        auto p = make();
        set (*p, pid::limLast, 0.0f);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        auto* editor = dynamic_cast<BigBossEditor*> (ed.get());
        auto* limitPanel = findChild<kit::LimitPanel> (*ed);
        auto* eqPanel = findChild<kit::EqPanel> (*ed);
        CHECK (editor != nullptr && limitPanel != nullptr && eqPanel != nullptr, "the editor has the modules");
        if (editor != nullptr && limitPanel != nullptr && eqPanel != nullptr)
        {
            const int firstY = eqPanel->getY();
            p->setOrder ({ 4, 0, 1, 2, 3 });
            editor->refreshNow();
            CHECK (limitPanel->getY() == firstY, "the editor moves the limiter to the top when the order changes (%d, %d)", limitPanel->getY(), firstY);
        }
    }

    // the assistant belongs to the processor: closing the window mid-listen neither stops nor drops it
    {
        auto p = make();
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            p->assistant().listen();
        }
        TestMix mix;
        juce::AudioBuffer<float> buf (2, kBlock);
        juce::MidiBuffer midi;
        for (int k = 0; k < (int) (11 * kSr / kBlock); ++k) { mix.fill (buf, kSr); p->processBlock (buf, midi); }
        for (int wait = 0; wait < 3000 && p->assistant().busy(); ++wait) juce::Thread::sleep (10);
        CHECK (p->assistant().canApply(), "the assistant finishes its plan with the window closed (state %d)", (int) p->assistant().getState());
        set (*p, pid::volume, 5.0f);
        CHECK (p->assistant().apply(), "the plan applies");
        CHECK (std::abs (get (*p, pid::volume)) < 0.01f, "applying a plan puts the fader at 0 dB (%g)", get (*p, pid::volume));
        CHECK (p->assistant().undo() && std::abs (get (*p, pid::volume) - 5.0f) < 0.01f, "UNDO puts the fader back (%g)", get (*p, pid::volume));
    }

    // runs the test mix for some seconds; returns the output's short-term loudness at the end
    auto playMix = [&] (BigBossProcessor& p, TestMix& mix, double seconds, std::function<void (int)> before = {})
    {
        juce::AudioBuffer<float> buf (2, kBlock);
        juce::MidiBuffer midi;
        for (int k = 0; k < (int) (seconds * kSr / kBlock); ++k)
        {
            if (before) before (k);
            mix.fill (buf, kSr);
            p.processBlock (buf, midi);
        }
        return p.outMeters.readings.shortTerm.load();
    };

    // MATCH: with the console 6 dB louder than the mix, BYPASS ALL plays the mix at the console's loudness
    {
        auto p = make();   // drive 8 dB into the limiter
        TestMix mix;
        const float console = playMix (*p, mix, 8.0);
        CHECK (std::abs (p->matchGainDb() - (console - p->inMeters.readings.shortTerm.load())) < 0.5f,
               "MATCH follows the loudness difference (%.2f dB)", p->matchGainDb());
        p->bypassAll = true;
        const float plain = playMix (*p, mix, 4.0);
        p->matchCompare = true;
        const float matched = playMix (*p, mix, 4.0);
        CHECK (console - plain > 3.0f, "without MATCH the bypassed mix is quieter (%.1f against %.1f LUFS)", plain, console);
        CHECK (std::abs (matched - console) < 1.0f, "with MATCH the bypassed mix is as loud as the console (%.1f against %.1f LUFS)", matched, console);
    }

    // the reference track: loaded, measured, and played at the console's loudness
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("bb-test-reference.wav");
        {
            juce::AudioBuffer<float> ref (2, (int) (6 * 44100));
            TestMix other;
            juce::AudioBuffer<float> block (2, 441);
            for (int off = 0; off < ref.getNumSamples(); off += 441)
            {
                other.fill (block, 44100);
                for (int c = 0; c < 2; ++c) ref.copyFrom (c, off, block, c, 0, 441);
            }
            ref.applyGain (0.5f);   // about 6 dB under the raw mix
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
            auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100).withNumChannels (2).withBitsPerSample (24));
            CHECK (w != nullptr, "a reference file is written");
            if (w != nullptr) w->writeFromAudioSampleBuffer (ref, 0, ref.getNumSamples());
        }
        auto p = make();
        juce::String error;
        CHECK (p->loadReference (file, error), "a reference track loads (%s)", error.toRawUTF8());
        const auto t = p->reference();
        CHECK (t != nullptr && std::abs (t->rate - kSr) < 0.5 && std::abs (t->audio.getNumSamples() - 6 * kSr) < 100,
               "the reference is resampled to the console's rate");
        CHECK (t != nullptr && t->lufs > -40 && t->lufs < -10 && ! t->spectrum.empty(), "the reference is measured (%.1f LUFS)", t != nullptr ? t->lufs : 0.0);
        TestMix mix;
        const float console = playMix (*p, mix, 8.0);
        p->refPlay = true;
        const float refLoud = playMix (*p, mix, 4.0);
        CHECK (std::abs (refLoud - console) < 1.0f, "REF plays the reference at the console's loudness (%.1f against %.1f LUFS)", refLoud, console);
        juce::MemoryBlock mb;
        p->getStateInformation (mb);
        auto q = make();
        q->setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (q->reference() != nullptr && q->reference()->file == file, "the session remembers the reference track");
        p->refPlay = false;
        p->clearReference();
        CHECK (p->reference() == nullptr, "the reference clears");
        file.deleteFile();
    }

    // A/B/C/D: each slot keeps its own settings and order; the session keeps them all
    {
        auto p = make();
        set (*p, pid::limLast, 0.0f);
        set (*p, pid::eqGain (2), 3.0f);
        p->selectSlot (1);   // B starts as a copy
        CHECK (std::abs (get (*p, pid::eqGain (2)) - 3.0f) < 0.01f, "an empty slot starts as a copy");
        set (*p, pid::eqGain (2), 7.0f);
        p->setOrder ({ 4, 0, 1, 2, 3 });
        p->selectSlot (0);
        CHECK (std::abs (get (*p, pid::eqGain (2)) - 3.0f) < 0.01f && p->getOrder()[0] == 0, "slot A comes back as it was");
        p->selectSlot (1);
        CHECK (std::abs (get (*p, pid::eqGain (2)) - 7.0f) < 0.01f && p->getOrder()[0] == 4, "slot B comes back as it was");
        set (*p, pid::volume, -5.0f);
        p->selectSlot (0);
        CHECK (std::abs (get (*p, pid::volume) + 5.0f) < 0.01f, "switching slots leaves the fader alone");
        p->copyToSlot (2);
        juce::MemoryBlock mb;
        p->getStateInformation (mb);
        auto q = make();
        q->setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (q->activeSlot() == 0 && q->slotUsed (1) && q->slotUsed (2) && ! q->slotUsed (3), "the session keeps the slots");
        q->selectSlot (1);
        CHECK (std::abs (get (*q, pid::eqGain (2)) - 7.0f) < 0.01f && q->getOrder()[0] == 4, "a restored slot holds its settings and order");
    }

    // UNDO / REDO: each finished edit is a step
    {
        auto p = make();
        p->checkpoint();
        auto* g = p->apvts.getParameter (pid::compOptoThr);
        auto gesture = [&] (float v) { g->beginChangeGesture(); g->setValueNotifyingHost (g->convertTo0to1 (v)); g->endChangeGesture(); p->checkpoint(); };
        const float start = get (*p, pid::compOptoThr);
        gesture (-30.0f);
        gesture (-20.0f);
        p->loadFactoryPreset (4);
        p->checkpoint();
        CHECK (p->undo(), "undo goes back");
        CHECK (std::abs (get (*p, pid::compOptoThr) + 20.0f) < 0.01f, "undo takes back a preset (%g)", get (*p, pid::compOptoThr));
        CHECK (p->undo() && std::abs (get (*p, pid::compOptoThr) + 30.0f) < 0.01f, "undo again takes back a control move");
        CHECK (p->undo() && std::abs (get (*p, pid::compOptoThr) - start) < 0.01f, "and again, to the start");
        CHECK (! p->canUndo(), "nothing before the start");
        CHECK (p->redo() && std::abs (get (*p, pid::compOptoThr) + 30.0f) < 0.01f, "redo steps forward");
        gesture (-10.0f);
        CHECK (! p->canRedo(), "a new edit drops what could be redone");
    }

    // the preset browser's search: names and tags, any order of words
    {
        const auto all = presets::all();
        int loud = 0, clubElectronic = 0;
        for (const auto& e : all)
        {
            if (presets::matches (e, {}, "loud")) ++loud;
            if (presets::matches (e, "electronic CLUB", {})) ++clubElectronic;
        }
        CHECK (loud == 2, "the LOUD tag finds two factory presets (%d)", loud);
        CHECK (clubElectronic == 1, "a search across name and tag finds Club Master (%d)", clubElectronic);
    }

    // the codec preview: band-limiting leaves a 1 kHz tone alone and removes an 18 kHz one for MP3 (16 kHz)
    {
        for (double hz : { 1000.0, 18000.0 })
        {
            bb::MeterSuite suite;
            suite.prepare (kSr, 2);
            bbui::CodecPreview codec;
            codec.setSource (&suite, kSr);
            std::vector<double> x (4800);
            for (int b = 0; b < 20; ++b)
            {
                for (size_t i = 0; i < x.size(); ++i)
                {
                    const double n = (double) (b * 4800 + (int) i);
                    const double fade = std::min (1.0, n / 4800.0);   // eased in: an abrupt start splatters past the cutoff
                    x[i] = 0.891 * fade * fade * std::sin (2 * kPi * hz * n / kSr);
                }
                suite.process (x.data(), x.data(), (int) x.size());
                codec.tick();
            }
            const float mp3 = codec.truePeakDb (0), aac = codec.truePeakDb (1);
            if (hz < 10000)
                CHECK (std::abs (mp3 + 1.0f) < 0.1f && std::abs (aac + 1.0f) < 0.1f, "codec preview: 1 kHz at -1 dBTP reads -1 (%.2f, %.2f)", mp3, aac);
            else
                CHECK (mp3 < -30.0f && std::abs (aac + 1.0f) < 0.2f, "codec preview: 18 kHz is gone for MP3, kept for AAC (%.1f, %.1f)", mp3, aac);
        }
    }

    // the recall sheet: the rack and every setting
    {
        auto p = make();
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        if (auto* editor = dynamic_cast<BigBossEditor*> (ed.get()))
        {
            const auto sheet = editor->recallSheet();
            CHECK (sheet.getWidth() == BigBossEditor::designWidth && sheet.getHeight() > BigBossEditor::designHeight + 300,
                   "the recall sheet holds the rack and the settings (%d x %d)", sheet.getWidth(), sheet.getHeight());
        }
    }

    // golden renders: each factory preset on the test mix, against the stored numbers, so a change to
    // the sound shows up as a failure until the file is rewritten on purpose
    if (golden != juce::File())
    {
        const auto& f = presets::factory();
        juce::StringArray lines;
        std::map<juce::String, juce::StringArray> stored;
        if (! writeGolden)
            for (const auto& line : juce::StringArray::fromLines (golden.loadFileAsString()))
                if (line.isNotEmpty() && ! line.startsWith ("#"))
                    stored[line.upToFirstOccurrenceOf ("|", false, false).trim()] = juce::StringArray::fromTokens (line.fromFirstOccurrenceOf ("|", false, false).trim(), " ", "");
        lines.add ("# factory presets on the test mix (tests/PluginTests.cpp): name | LUFS dBTP, then band levels (dB) "
                   "20-100 100-300 300-1k 1k-3k 3k-8k 8k-20k. Rewrite with: bb_plugin_tests <this file> --write");
        for (int i = 0; i < (int) f.size(); ++i)
        {
            const auto r = renderPreset (i);
            juce::String nums = juce::String (r.lufs, 2) + " " + juce::String (r.truePeak, 2);
            for (auto b : r.bands) nums << " " << juce::String (b, 2);
            lines.add (juce::String (f[(size_t) i].name) + " | " + nums);
            if (writeGolden) continue;
            const auto it = stored.find (f[(size_t) i].name);
            CHECK (it != stored.end(), "golden numbers for %s", f[(size_t) i].name);
            if (it == stored.end()) continue;
            const auto& want = it->second;
            std::vector<double> got { r.lufs, r.truePeak };
            for (auto b : r.bands) got.push_back (b);
            bool same = want.size() == (int) got.size();
            for (int k = 0; same && k < (int) got.size(); ++k)
                same = std::abs (got[(size_t) k] - want[k].getDoubleValue()) <= (k == 1 ? 0.1 : 0.3);
            CHECK (same, "%s renders as before (now %s, was %s)", f[(size_t) i].name, nums.toRawUTF8(), want.joinIntoString (" ").toRawUTF8());
        }
        if (writeGolden)
        {
            golden.replaceWithText (lines.joinIntoString ("\n") + "\n");
            std::printf ("wrote %s\n", golden.getFullPathName().toRawUTF8());
        }
    }

    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
