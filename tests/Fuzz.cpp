// Fuzzing: random sample rates, block sizes, signals, bypass and order changes through the whole chain
// and both meter suites. Fails on any non-finite output or reading. Argument: seconds of audio per run.
#include "chain/Chain.h"
#include "chain/CompModule.h"
#include "chain/EqModule.h"
#include "chain/LimitModule.h"
#include "chain/TapeModule.h"
#include "meter/MeterSuite.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>

int main (int argc, char** argv)
{
    const double secs = argc > 1 ? std::atof (argv[1]) : 0.25;
    std::mt19937 rng (12345);
    int bad = 0;
    for (int run = 0; run < 12; ++run)
    {
        const double rates[] = { 44100, 48000, 88200, 96000, 176400, 192000 };
        const double sr = rates[rng() % 6];
        const int maxBlock = 1 + (int) (rng() % 2048);

        bb::Chain chain;
        auto* tape = new bb::TapeModule();
        auto* eq = new bb::EqModule();
        auto* comp = new bb::CompModule();
        auto* lim = new bb::LimitModule();
        chain.add (std::unique_ptr<bb::Module> (eq));
        chain.add (std::unique_ptr<bb::Module> (comp));
        chain.add (std::unique_ptr<bb::Module> (tape));
        chain.add (std::make_unique<bb::EmptySlot> ("deess", "De-ess"));
        chain.add (std::unique_ptr<bb::Module> (lim));
        chain.prepare (sr, maxBlock);
        bb::MeterSuite in, out;
        in.prepare (sr, 2);
        out.prepare (sr, 2);

        std::vector<double> l ((size_t) maxBlock), r ((size_t) maxBlock);
        const auto total = (long) (secs * sr);
        std::uniform_real_distribution<double> u (-1, 1);
        for (long done = 0; done < total;)
        {
            const int n = 1 + (int) (rng() % (unsigned) maxBlock);
            const int kind = (int) (rng() % 6);
            for (int i = 0; i < n; ++i)
            {
                double v = 0;
                switch (kind)
                {
                    case 0: v = u (rng); break;                                  // full-scale noise
                    case 1: v = 4.0 * u (rng); break;                            // way over full scale
                    case 2: v = 1e-310; break;                                   // denormal
                    case 3: v = (i & 1) ? 1.0 : -1.0; break;                     // Nyquist
                    case 4: v = 1.0; break;                                      // DC
                    default: v = 0; break;                                       // silence
                }
                l[(size_t) i] = v;
                r[(size_t) i] = (rng() & 1) ? v : -v;
            }
            if (rng() % 8 == 0) chain.setBypassed ((int) (rng() % 5), rng() & 1);
            if (rng() % 16 == 0)
            {
                bb::Chain::Order o {};
                for (int k = 0; k < 5; ++k) o[(size_t) k] = k;
                std::shuffle (o.begin(), o.begin() + 5, rng);
                chain.setOrder (o);
            }
            if (rng() % 10 == 0)
            {
                auto& t = tape->settings;
                t.speed = (int) (rng() % 2); t.standard = (int) (rng() % 2); t.tape = (int) (rng() % 2); t.bias = (int) (rng() % 3);
                t.inputDb = 12 * u (rng); t.outputDb = 12 * u (rng); t.wow = 0.5 + 0.5 * u (rng); t.link = rng() & 1;
            }
            if (rng() % 10 == 0)
            {
                auto& e = eq->settings;
                for (int b = 0; b < bb::eq::kBands; ++b)
                {
                    const auto& rg = bb::eq::kRange[b];
                    e.band[b] = { rg.lo * std::pow (rg.hi / rg.lo, 0.5 + 0.5 * u (rng)), 6 + 6 * u (rng), (rng() & 1) != 0, (rng() & 1) != 0 };
                }
                e.warm = rng() & 1;
                e.hpfHz = (rng() & 1) ? 10 + 145 * (1 + u (rng)) : 0; e.hpfOrder = (rng() & 1) ? 4 : 2;
                e.lpfHz = (rng() & 1) ? 15000 + 5000 * u (rng) : 0;
                e.monoBassHz = (rng() & 1) ? 150 + 100 * u (rng) : 0;
                e.outputDb = 6 * u (rng);
            }
            if (rng() % 10 == 0)
            {
                auto& c = comp->settings;
                c.opto = rng() & 1; c.optoThreshold = -20 + 20 * u (rng); c.optoGain = 6 + 6 * u (rng);
                c.vca = rng() & 1; c.vcaThreshold = -20 + 20 * u (rng); c.vcaGain = 6 + 6 * u (rng);
                c.ratio = (int) (rng() % 6); c.attack = (int) (rng() % 6); c.recovery = (int) (rng() % 6);
                c.transformer = (int) (rng() % 3); c.sidechainHpf = rng() & 1;
            }
            if (rng() % 10 == 0)
            {
                auto& m = lim->settings;
                m.driveDb = 12 + 12 * u (rng); m.ceilingDb = -6 + 6 * u (rng); m.autoRelease = rng() & 1;
                m.releaseMs = 500 + 499 * u (rng); m.style = (int) (rng() % 3); m.truePeak = rng() & 1; m.link = 0.5 + 0.5 * u (rng);
            }
            if (rng() % 50 == 0) out.requestReset();

            in.process (l.data(), r.data(), n);
            chain.process (l.data(), r.data(), n);
            out.process (l.data(), r.data(), n);
            for (int i = 0; i < n; ++i)
                if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (r[(size_t) i])) { ++bad; break; }
            for (auto* m : { &in, &out })
            {
                const auto& rd = m->readings;
                if (! std::isfinite (rd.momentary.load()) || ! std::isfinite (rd.shortTerm.load()) || ! std::isfinite (rd.integrated.load())
                    || ! std::isfinite (rd.range.load()) || ! std::isfinite (rd.truePeakHeld[0].load()) || ! std::isfinite (rd.correlation.load())
                    || ! std::isfinite (rd.plr.load()) || ! std::isfinite (rd.widthDb.load()))
                { ++bad; break; }
            }
            done += n;
        }
        std::printf ("run %2d: %6.0f Hz, max block %4d, latency %d\n", run, sr, maxBlock, chain.latency());
    }
    std::printf ("%s (%d bad blocks)\n", bad ? "FAIL" : "ok", bad);
    return bad ? 1 : 0;
}
