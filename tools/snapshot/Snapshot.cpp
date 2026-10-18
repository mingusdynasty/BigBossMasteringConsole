// Renders the console to a PNG after running a synthetic mix through it, so the UI can be checked
// without a host. Usage: bb_snapshot out.png [seconds]
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
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = static_cast<BigBossEditor*> (ed.get());

    // a rough "mix": four-on-the-floor kick, bass, stereo pads (decorrelated noise through one-poles), hats
    std::mt19937 rng (3);
    std::normal_distribution<float> g (0, 1);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    double t = 0, padL = 0, padR = 0, hp = 0;
    const auto total = (long) (secs * sr);
    for (long done = 0; done < total; done += block)
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
        if ((done / block) % 50 == 0) editor->refreshNow();
    }
    editor->refreshNow();
    const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    if (! os.openedOk() || ! png.writeImageToStream (img, os)) { std::printf ("could not write %s\n", out.getFullPathName().toRawUTF8()); return 1; }
    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    ed.reset();
    return 0;
}
