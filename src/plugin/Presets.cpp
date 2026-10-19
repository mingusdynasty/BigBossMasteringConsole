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
                   make ({ { compVca, 0 }, { compOptoThr, -20 }, { compOptoGain, 1 }, { tapeSpeed, 1 }, { tapeType, 1 }, { tapeWow, 10 },
                           { bypass ("deess"), 1 }, { limDrive, 4 }, { limCeiling, -1 } },
                         [] (Values& v) { band (v, 4, 12000, 1.5f, true); }) });

    f.push_back ({ "Streaming Master", "Balanced for streaming services: around -14 LUFS, -1 dBTP",
                   make ({ { eqHpf, 25 }, { compOptoThr, -20 }, { compOptoGain, 1.5f }, { compVcaThr, -12 }, { compRatio, 1 }, { compAttack, 4 },
                           { tapeSpeed, 1 }, { tapeType, 1 }, { tapeWow, 10 }, { deessThr, -6 }, { limDrive, 7 }, { limCeiling, -1 } },
                         [] (Values& v) { band (v, 1, 300, -1.0f); band (v, 4, 12000, 1.5f, true); }) });

    f.push_back ({ "Loud & Punchy", "Modern and loud: firmer compression, punchy limiting, 15 ips drive",
                   make ({ { eqHpf, 25 }, { compOptoThr, -22 }, { compOptoGain, 2 }, { compVcaThr, -14 }, { compRatio, 2 }, { compAttack, 4 },
                           { compVcaGain, 2 }, { tapeSpeed, 0 }, { tapeType, 0 }, { tapeIn, 2 }, { limDrive, 11 }, { limStyle, 1 }, { limCeiling, -0.5f } },
                         [] (Values& v) { band (v, 0, 70, 1.5f, true); band (v, 4, 12000, 2.0f, true); }) });

    f.push_back ({ "Warm Tape Glue", "Driven 456 at 15 ips, the EQ's Warm stage and slow optical glue",
                   make ({ { eqWarm, 1 }, { compVca, 0 }, { compOptoThr, -18 }, { compOptoGain, 1.5f }, { compXfmr, 1 },
                           { tapeSpeed, 0 }, { tapeType, 0 }, { tapeIn, 4 }, { tapeWow, 30 }, { bypass ("deess"), 1 }, { limDrive, 3 } }) });

    f.push_back ({ "Vocal Forward", "Presence up, low mids cleared, the de-esser catching the lift",
                   make ({ { compOptoThr, -20 }, { compOptoGain, 1 }, { deessThr, -10 }, { deessRange, 8 }, { limDrive, 5 } },
                         [] (Values& v) { band (v, 1, 250, -1.5f); band (v, 3, 3000, 1.5f); }) });

    f.push_back ({ "Low End Control", "Tight, centred bass: 24 dB high-pass, mono below 120 Hz, a little less low mid",
                   make ({ { eqHpf, 30 }, { eqHpf24, 1 }, { eqMono, 120 }, { compScHpf, 1 }, { limDrive, 5 } },
                         [] (Values& v) { band (v, 1, 200, -1.5f); }) });

    f.push_back ({ "Bright & Airy", "Open top end with the de-esser guarding it",
                   make ({ { tapeSpeed, 1 }, { tapeType, 1 }, { deessThr, -8 }, { deessRange, 6 }, { deessFreq, 5500 }, { limDrive, 5 } },
                         [] (Values& v) { band (v, 3, 4500, 1.0f); band (v, 4, 12000, 3.0f, true); }) });

    f.push_back ({ "Club Master", "For big systems: mono lows, dense limiting near -8 LUFS",
                   make ({ { eqHpf, 28 }, { eqMono, 150 }, { compOptoThr, -22 }, { compOptoGain, 2 }, { compVcaThr, -16 }, { compRatio, 2 },
                           { compVcaGain, 3 }, { tapeSpeed, 0 }, { tapeType, 0 }, { tapeIn, 3 }, { limDrive, 14 }, { limStyle, 2 },
                           { limCeiling, -0.3f } },
                         [] (Values& v) { band (v, 0, 60, 2.0f, true); }) });

    f.push_back ({ "Transparent", "Acoustic, jazz and classical: compressor, tape and de-esser out, light limiting",
                   make ({ { bypass ("comp"), 1 }, { bypass ("tape"), 1 }, { bypass ("deess"), 1 }, { eqHpf, 20 }, { limDrive, 2 },
                           { limCeiling, -1 } }) });
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
