// Renders the console to PNGs after running a synthetic mix through it, so the UI can be checked
// without a host. Usage: bb_snapshot out.png [seconds]
// Writes out.png (the analysis tab) and out-<tab>.png for each module tab, with a demo setting on
// the EQ and the tape so their panels have something to show.
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

#include <random>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshot.png"));
    const double secs = argc > 2 ? std::atof (argv[2]) : 20.0;

    BigBossProcessor proc;
    const double sr = 48000;
    const int block = 512;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);
    auto set = [&proc] (const char* id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    set ("eq_b0_gain", 2.5f); set ("eq_b0_freq", 50.0f);
    set ("eq_b2_gain", 2.0f); set ("eq_b2_cut", 1.0f); set ("eq_b2_freq", 400.0f);
    set ("eq_b3_gain", 1.5f); set ("eq_b3_freq", 3500.0f);
    set ("eq_b4_gain", 3.0f); set ("eq_b4_freq", 14000.0f);
    set ("eq_hpf", 25.0f); set ("eq_mono", 120.0f);
    set ("tape_in", -6.0f);   // the demo mix is hot: bring it to about 0 VU
    set ("comp_opto_thr", -22.0f); set ("comp_vca_thr", -16.0f); set ("comp_xfmr", 1.0f);
    set ("limit_drive", 12.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = static_cast<BigBossEditor*> (ed.get());

    // a rough "mix": four-on-the-floor kick, bass, stereo pads (decorrelated noise through one-poles), hats
    std::mt19937 rng (3);
    std::normal_distribution<float> g (0, 1);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    double t = 0, padL = 0, padR = 0, hp = 0;
    auto nextBlock = [&]
    {
        for (int i = 0; i < block; ++i, t += 1 / sr)
        {
            const double beat = std::fmod (t, 0.5), bar = std::fmod (t, 2.0);
            const double kick = 0.55 * std::exp (-beat * 18) * std::sin (2 * juce::MathConstants<double>::pi * (50 + 120 * std::exp (-beat * 40)) * beat);
            const double bass = 0.18 * std::sin (2 * juce::MathConstants<double>::pi * (bar < 1 ? 55 : 41.2) * t);
            padL += (0.05 * g (rng) - padL) * 0.02; padR += (0.05 * g (rng) - padR) * 0.02;
            const double n = g (rng); hp += (n - hp) * 0.3;
            const double hat = std::fmod (t + 0.25, 0.5) < 0.04 ? 0.05 * (n - hp) * std::exp (-std::fmod (t + 0.25, 0.5) * 60) : 0;
            const double swell = 0.6 + 0.4 * std::sin (t * 0.4);
            buf.setSample (0, i, (float) ((kick + bass) + swell * 3 * padL + hat));
            buf.setSample (1, i, (float) ((kick + bass) + swell * 3 * padR + 0.7 * hat));
        }
        proc.processBlock (buf, midi);
    };
    const auto total = (long) (secs * sr);
    for (long done = 0; done < total; done += block)
    {
        nextBlock();
        if ((done / block) % 50 == 0) editor->refreshNow();
    }
    const char* names[] = { "", "eq", "comp", "tape", "deess", "limit" };
    for (int tab = 0; tab < BigBossEditor::kNumTabs; ++tab)
    {
        editor->showTab (tab);
        for (int f = 0; f < 30; ++f)   // a second of frames, so the VU needles and the strip settle
        {
            for (int k = 0; k < 3; ++k) nextBlock();
            editor->refreshNow();
        }
        const auto file = tab == 0 ? out : out.getSiblingFile (out.getFileNameWithoutExtension() + "-" + names[tab] + ".png");
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat png;
        if (! os.openedOk() || ! png.writeImageToStream (img, os)) { std::printf ("could not write %s\n", file.getFullPathName().toRawUTF8()); return 1; }
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    }
    ed.reset();
    return 0;
}
