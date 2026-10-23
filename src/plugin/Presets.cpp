#include "Presets.h"

#include "Params.h"

namespace presets
{
namespace
{
// an EQ band: frequency, signed gain (negative sets CUT), and the shelf switch on the outer bands
void band (Values& v, int b, float hz, float gainDb, bool shelf = false)
{
    v[pid::eqFreq (b)] = hz;
    v[pid::eqGain (b)] = std::abs (gainDb);
    v[pid::eqCut (b)] = gainDb < 0 ? 1.0f : 0.0f;
    if (b == 0 || b == bb::eq::kBands - 1) v[pid::eqShelf (b)] = shelf ? 1.0f : 0.0f;
}

Values make (std::initializer_list<std::pair<const juce::String, float>> plain, std::function<void (Values&)> eq = {})
{
    Values v (plain);
    if (eq) eq (v);
    return v;
}

std::vector<Factory> build()
{
    using namespace pid;
    // tape choices: speed 0 = 15 ips, 1 = 30 ips; type 0 = 456, 1 = GP9. Comp ratio 0..5 = 1.2:1 .. Flood,
    // attack 0..5 = 0.1 .. 30 ms, recovery 5 = dual. Limiter style 0 transparent, 1 punch, 2 dense.
    std::vector<Factory> f;
    f.push_back ({ "Init", "Everything at its starting point: the chain in, nothing working hard", {} });

    f.push_back ({ "Gentle Polish", "A touch of air, light optical glue, clean tape and a little limiting",
                   make ({ { compVca, 0.0f }, { compOptoThr, -20.0f }, { compOptoGain, 1.0f }, { tapeSpeed, 1.0f }, { tapeType, 1.0f }, { tapeWow, 10.0f },
                           { bypass ("deess"), 1.0f }, { limDrive, 4.0f }, { limCeiling, -1.0f } },
                         [] (Values& v) { band (v, 4, 12000, 1.5f, true); }) });

    f.push_back ({ "Streaming Master", "Balanced for streaming services: around -14 LUFS, -1 dBTP",
                   make ({ { eqHpf, 25.0f }, { compOptoThr, -20.0f }, { compOptoGain, 1.5f }, { compVcaThr, -12.0f }, { compRatio, 1.0f }, { compAttack, 4.0f },
                           { tapeSpeed, 1.0f }, { tapeType, 1.0f }, { tapeWow, 10.0f }, { deessThr, -6.0f }, { limDrive, 7.0f }, { limCeiling, -1.0f } },
                         [] (Values& v) { band (v, 1, 300, -1.0f); band (v, 4, 12000, 1.5f, true); }) });

    f.push_back ({ "Loud & Punchy", "Modern and loud: firmer compression, punchy limiting, 15 ips drive",
                   make ({ { eqHpf, 25.0f }, { compOptoThr, -22.0f }, { compOptoGain, 2.0f }, { compVcaThr, -14.0f }, { compRatio, 2.0f }, { compAttack, 4.0f },
                           { compVcaGain, 2.0f }, { tapeSpeed, 0.0f }, { tapeType, 0.0f }, { tapeIn, 2.0f }, { limDrive, 11.0f }, { limStyle, 1.0f }, { limCeiling, -0.5f } },
                         [] (Values& v) { band (v, 0, 70, 1.5f, true); band (v, 4, 12000, 2.0f, true); }) });

    f.push_back ({ "Warm Tape Glue", "Driven 456 at 15 ips, the EQ's Warm stage and slow optical glue",
                   make ({ { eqWarm, 1.0f }, { compVca, 0.0f }, { compOptoThr, -18.0f }, { compOptoGain, 1.5f }, { compXfmr, 1.0f },
                           { tapeSpeed, 0.0f }, { tapeType, 0.0f }, { tapeIn, 4.0f }, { tapeWow, 30.0f }, { bypass ("deess"), 1.0f }, { limDrive, 3.0f } }) });

    f.push_back ({ "Vocal Forward", "Presence up, low mids cleared, the de-esser catching the lift",
                   make ({ { compOptoThr, -20.0f }, { compOptoGain, 1.0f }, { deessThr, -10.0f }, { deessRange, 8.0f }, { limDrive, 5.0f } },
                         [] (Values& v) { band (v, 1, 250, -1.5f); band (v, 3, 3000, 1.5f); }) });

    f.push_back ({ "Low End Control", "Tight, centred bass: 24 dB high-pass, mono below 120 Hz, a little less low mid",
                   make ({ { eqHpf, 30.0f }, { eqHpf24, 1.0f }, { eqMono, 120.0f }, { compScHpf, 1.0f }, { limDrive, 5.0f } },
                         [] (Values& v) { band (v, 1, 200, -1.5f); }) });

    f.push_back ({ "Bright & Airy", "Open top end with the de-esser guarding it",
                   make ({ { tapeSpeed, 1.0f }, { tapeType, 1.0f }, { deessThr, -8.0f }, { deessRange, 6.0f }, { deessFreq, 5500.0f }, { limDrive, 5.0f } },
                         [] (Values& v) { band (v, 3, 4500, 1.0f); band (v, 4, 12000, 3.0f, true); }) });

    f.push_back ({ "Club Master", "For big systems: mono lows, dense limiting near -8 LUFS",
                   make ({ { eqHpf, 28.0f }, { eqMono, 150.0f }, { compOptoThr, -22.0f }, { compOptoGain, 2.0f }, { compVcaThr, -16.0f }, { compRatio, 2.0f },
                           { compVcaGain, 3.0f }, { tapeSpeed, 0.0f }, { tapeType, 0.0f }, { tapeIn, 3.0f }, { limDrive, 14.0f }, { limStyle, 2.0f },
                           { limCeiling, -0.3f } },
                         [] (Values& v) { band (v, 0, 60, 2.0f, true); }) });

    f.push_back ({ "Transparent", "Acoustic, jazz and classical: compressor, tape and de-esser out, light limiting",
                   make ({ { bypass ("comp"), 1.0f }, { bypass ("tape"), 1.0f }, { bypass ("deess"), 1.0f }, { eqHpf, 20.0f }, { limDrive, 2.0f },
                           { limCeiling, -1.0f } }) });
    return f;
}
} // namespace

const std::vector<Factory>& factory()
{
    static const auto f = build();
    return f;
}

juce::File userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Big Boss Mastering Console")
        .getChildFile ("Presets");
}

juce::Array<juce::File> userPresets()
{
    auto files = userFolder().findChildFiles (juce::File::findFiles, false, "*" + extension);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
    return files;
}
} // namespace presets
