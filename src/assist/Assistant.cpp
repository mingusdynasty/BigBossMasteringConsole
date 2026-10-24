#include "Assistant.h"

#include "chain/Chain.h"
#include "chain/CompModule.h"
#include "chain/DeEssModule.h"
#include "chain/EqModule.h"
#include "chain/LimitModule.h"
#include "chain/TapeModule.h"
#include "meter/LoudnessMeter.h"
#include "meter/TruePeak.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <memory>

namespace bb::assist
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

double toDb (double power) { return power > 1e-20 ? 10.0 * std::log10 (power) : -200.0; }

std::string fmt (const char* f, double a = 0, double b = 0, double c = 0)
{
    char buf[160];
    std::snprintf (buf, sizeof buf, f, a, b, c);
    return buf;
}

void fft (std::vector<std::complex<double>>& x)
{
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto w = std::polar (1.0, -2.0 * kPi / (double) len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> wk (1.0);
            for (size_t k = 0; k < len / 2; ++k, wk *= w)
            {
                const auto a = x[i + k], b = x[i + k + len / 2] * wk;
                x[i + k] = a + b;
                x[i + k + len / 2] = a - b;
            }
        }
    }
}

// a plain biquad for the sibilance band
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad make (double sr, double hz, bool high)
    {
        const double w = 2 * kPi * hz / sr, cs = std::cos (w), al = std::sin (w) / (2 * 0.7071), a0 = 1 + al;
        Biquad q;
        if (high) { q.b0 = (1 + cs) / 2; q.b1 = -(1 + cs); q.b2 = (1 + cs) / 2; }
        else      { q.b0 = (1 - cs) / 2; q.b1 = 1 - cs;    q.b2 = (1 - cs) / 2; }
        q.b0 /= a0; q.b1 /= a0; q.b2 /= a0;
        q.a1 = -2 * cs / a0; q.a2 = (1 - al) / a0;
        return q;
    }
    double run (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

double percentile (std::vector<double> v, double p)
{
    if (v.empty()) return 0;
    const auto k = (size_t) std::clamp (p * (double) (v.size() - 1), 0.0, (double) (v.size() - 1));
    std::nth_element (v.begin(), v.begin() + (long) k, v.end());
    return v[k];
}

struct Rendered { double lufs = -200, truePeak = -200; };

// the captured audio through the modules in mask (by slot), in the given order, after the input trim;
// loudness and true peak measured after the first second, so attack and release have settled
Rendered render (const std::vector<float>& l, const std::vector<float>& r, double sr, const Plan& p, const Order& order, unsigned mask)
{
    Chain chain;
    for (int k = 0; k < kSlots; ++k)
    {
        const int s = order[(size_t) k];
        if (! ((mask >> s) & 1u) || p.bypass[(size_t) s]) continue;
        switch (s)
        {
            case Eq:    { auto m = std::make_unique<EqModule>();    m->settings = p.eq;    chain.add (std::move (m)); break; }
            case Comp:  { auto m = std::make_unique<CompModule>();  m->settings = p.comp;  chain.add (std::move (m)); break; }
            case Tape:  { auto m = std::make_unique<TapeModule>();  m->settings = p.tape;  chain.add (std::move (m)); break; }
            case Deess: { auto m = std::make_unique<DeEssModule>(); m->settings = p.deess; chain.add (std::move (m)); break; }
            default:    { auto m = std::make_unique<LimitModule>(); m->settings = p.limit; chain.add (std::move (m)); break; }
        }
    }
    const int block = 1024;
    chain.prepare (sr, block);
    LoudnessMeter loud;
    TruePeak tp;
    loud.prepare (sr, 2);
    tp.prepare (sr, 2);

    const double trim = std::pow (10.0, p.inTrimDb / 20.0);
    std::vector<double> bl ((size_t) block), br ((size_t) block);
    const size_t n = l.size(), skip = (size_t) sr + (size_t) chain.latency();
    for (size_t off = 0; off < n; off += (size_t) block)
    {
        const int k = (int) std::min ((size_t) block, n - off);
        for (int i = 0; i < k; ++i) { bl[(size_t) i] = trim * l[off + (size_t) i]; br[(size_t) i] = trim * r[off + (size_t) i]; }
        chain.process (bl.data(), br.data(), k);
        if (off + (size_t) k <= skip) continue;
        const int from = off >= skip ? 0 : (int) (skip - off);
        const double* ch[2] { bl.data() + from, br.data() + from };
        loud.process (ch, k - from);
        tp.process (ch, k - from);
    }
    Rendered out;
    out.lufs = loud.integrated();
    const double peak = std::max (tp.heldPeak (0), tp.heldPeak (1));
    out.truePeak = peak > 1e-10 ? 20.0 * std::log10 (peak) : -200.0;
    return out;
}
} // namespace

double targetLufs (Goal g)
{
    switch (g)
    {
        case Goal::Loud:    return -11.0;
        case Goal::Club:    return -9.0;
        case Goal::Dynamic: return -16.0;
        default:            return -14.0;   // and Reference, until a reference sets it
    }
}

const char* goalName (Goal g)
{
    switch (g)
    {
        case Goal::Loud:    return "loud";
        case Goal::Club:    return "club";
        case Goal::Dynamic: return "dynamic";
        case Goal::Reference: return "reference";
        default:            return "streaming";
    }
}

Profile analyse (const std::vector<float>& l, const std::vector<float>& r, double sr)
{
    Profile pr;
    const size_t n = std::min (l.size(), r.size());
    pr.seconds = (double) n / sr;
    if (n < (size_t) (sr * 0.5)) return pr;

    // loudness and peak
    {
        LoudnessMeter loud;
        loud.prepare (sr, 2);
        std::vector<double> bl (4096), br (4096);
        double peak = 0;
        for (size_t off = 0; off < n; off += 4096)
        {
            const int k = (int) std::min ((size_t) 4096, n - off);
            for (int i = 0; i < k; ++i)
            {
                bl[(size_t) i] = l[off + (size_t) i];
                br[(size_t) i] = r[off + (size_t) i];
                peak = std::max ({ peak, std::abs (bl[(size_t) i]), std::abs (br[(size_t) i]) });
            }
            const double* ch[2] { bl.data(), br.data() };
            loud.process (ch, k);
        }
        pr.integrated = loud.integrated();
        pr.range = loud.range();
        pr.peak = peak > 1e-10 ? 20.0 * std::log10 (peak) : -200.0;
        pr.plr = pr.peak - pr.integrated;
    }

    // long-term spectrum of mid and side: 8192-point Hann frames, half overlapped, skipping near-silence
    {
        const int N = 8192;
        std::vector<double> win ((size_t) N), mid ((size_t) N / 2 + 1), side ((size_t) N / 2 + 1);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5 - 0.5 * std::cos (2 * kPi * i / N);
        std::vector<std::complex<double>> fm ((size_t) N), fs ((size_t) N);
        int frames = 0;
        // resonances: each frame's prominence (dB over the median around it) per bin
        const double hzPerBinR = sr / N;
        const int k0 = std::max (2, (int) (300.0 / hzPerBinR)), k1 = std::min (N / 2 - 2, (int) (8000.0 / hzPerBinR));
        std::vector<std::vector<float>> prominence ((size_t) std::max (0, k1 - k0 + 1));
        std::vector<double> frame ((size_t) N / 2 + 1), around, grid, floorDb;
        for (double kc = k0; kc <= k1 * 1.03; kc *= 1.0293022) grid.push_back (kc);   // 2^(1/24)
        floorDb.resize (grid.size());
        for (size_t off = 0; off + (size_t) N <= n; off += (size_t) N / 2)
        {
            double e = 0;
            for (int i = 0; i < N; ++i)
            {
                const double a = l[off + (size_t) i], b = r[off + (size_t) i];
                fm[(size_t) i] = 0.5 * (a + b) * win[(size_t) i];
                fs[(size_t) i] = 0.5 * (a - b) * win[(size_t) i];
                e += a * a + b * b;
            }
            if (e / (2 * N) < 1e-6) continue;   // below -60 dBFS
            fft (fm);
            fft (fs);
            for (int k = 0; k <= N / 2; ++k)
            {
                frame[(size_t) k] = std::norm (fm[(size_t) k]);
                mid[(size_t) k] += frame[(size_t) k];
                side[(size_t) k] += std::norm (fs[(size_t) k]);
            }
            // the median, not the mean, of the surroundings (+-1/2 octave), so a loud note nearby
            // doesn't hide a bump and the bump doesn't raise its own floor; taken on a 1/24-octave
            // grid and interpolated between
            for (size_t gi = 0; gi < grid.size(); ++gi)
            {
                const double kc = grid[gi];
                const int lo = std::max (1, (int) std::floor (kc / 1.414214)), hi = std::min (N / 2, (int) std::ceil (kc * 1.414214));
                around.assign (frame.begin() + lo, frame.begin() + hi + 1);
                std::nth_element (around.begin(), around.begin() + (long) (around.size() / 2), around.end());
                floorDb[gi] = toDb (around[around.size() / 2] + 1e-30);
            }
            for (int k = k0; k <= k1; ++k)
            {
                const double gpos = std::clamp (24.0 * std::log2 (k / grid.front()), 0.0, (double) grid.size() - 1.0);
                const auto g0 = (size_t) gpos;
                const size_t g1 = std::min (g0 + 1, grid.size() - 1);
                const double fl = floorDb[g0] + (floorDb[g1] - floorDb[g0]) * (gpos - (double) g0);
                prominence[(size_t) (k - k0)].push_back ((float) (toDb (frame[(size_t) k] + 1e-30) - fl));
            }
            ++frames;
        }
        if (frames > 0)
        {
            const double hzPerBin = sr / N;
            // mean power over a frequency range, tilted 4.5 dB/oct about 1 kHz so a typical master reads flat
            auto band = [&] (const std::vector<double>& s, double lo, double hi, bool tilt)
            {
                double sum = 0;
                int count = 0;
                for (int k = std::max (1, (int) (lo / hzPerBin)); k <= std::min (N / 2, (int) (hi / hzPerBin)); ++k)
                {
                    const double f = k * hzPerBin;
                    sum += s[(size_t) k] * (tilt ? std::pow (f / 1000.0, 4.5 / (10 * std::log10 (2.0))) : 1.0);
                    ++count;
                }
                return toDb (count ? sum / count : 0.0);
            };
            // reference, relative to the 100 Hz .. 5 kHz average in the tilted view: a little more low end,
            // highs easing off, from averaged commercial masters
            const double ref = band (mid, 100, 5000, true);
            pr.low     = band (mid, 40, 100, true)     - ref - 1.0;
            pr.lowMid  = band (mid, 200, 500, true)    - ref - 0.0;
            pr.mid     = band (mid, 800, 2000, true)   - ref - 0.0;
            pr.highMid = band (mid, 2500, 5000, true)  - ref - (-1.0);
            pr.air     = band (mid, 8000, 14000, true) - ref - (-3.0);
            pr.bassWidth = band (side, 30, 120, false) - band (mid, 30, 120, false);
            const double edges[5] = { 120, 500, 2000, 6000, 16000 };
            for (int b = 0; b < 4; ++b)
                pr.width[(size_t) b] = band (side, edges[b], std::min (edges[b + 1], 0.45 * sr), false) - band (mid, edges[b], std::min (edges[b + 1], 0.45 * sr), false);

            // resonances: the median prominence holds up only where a peak stays while notes come
            // and go; local maxima over +-1/12 octave, 3 dB or more, a third of an octave apart
            if (frames >= 8)
            {
                std::vector<double> raw (prominence.size()), med (prominence.size());
                for (size_t i = 0; i < raw.size(); ++i)
                {
                    std::vector<double> v (prominence[i].begin(), prominence[i].end());
                    raw[i] = percentile (v, 0.5);
                }
                // a running median a few percent wide: a held note (a line a bin or two wide) and the
                // frame-to-frame jitter drop out, a resonance (a bump) stays
                for (int k = k0; k <= k1; ++k)
                {
                    const int d = std::max (5, (int) std::lround (k * 0.025));   // a windowed line spans 5 bins
                    std::vector<double> v;
                    for (int j = std::max (k0, k - d); j <= std::min (k1, k + d); ++j) v.push_back (raw[(size_t) (j - k0)]);
                    med[(size_t) (k - k0)] = percentile (v, 0.5);
                }
                std::vector<Profile::Resonance> found;
                for (int k = k0; k <= k1; ++k)
                {
                    const double v = med[(size_t) (k - k0)];
                    if (v < 3.0) continue;
                    const int lo = std::max (k0, (int) std::floor (k / 1.059463)), hi = std::min (k1, (int) std::ceil (k * 1.059463));
                    bool top = true;
                    for (int j = lo; j <= hi && top; ++j) top = j == k || med[(size_t) (j - k0)] < v || (med[(size_t) (j - k0)] == v && j > k);
                    if (top) found.push_back ({ k * hzPerBinR, v });
                }
                std::sort (found.begin(), found.end(), [] (const auto& a, const auto& b) { return a.db > b.db; });
                for (const auto& f : found)
                {
                    bool apart = true;
                    for (const auto& kept : pr.resonances) apart = apart && std::abs (std::log2 (f.hz / kept.hz)) >= 1.0 / 3.0;
                    if (apart) pr.resonances.push_back (f);
                    if (pr.resonances.size() == 3) break;
                }
            }
        }
    }

    // sibilance: the share of the 5-10 kHz band in the loudest-in-that-band moments. Where an "s" is
    // what you hear, that band carries most of the energy; cymbals and hats sit on top of the kick, bass
    // and the rest, so their share stays well under half.
    {
        auto hp = Biquad::make (sr, 5000, true), hp2 = hp, lp = Biquad::make (sr, std::min (10000.0, sr * 0.45), false);
        const int frame = std::max (1, (int) (sr * 0.005));
        std::vector<double> share;
        double es = 0, et = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double m = 0.5 * (l[i] + r[i]);
            const double s = lp.run (hp2.run (hp.run (m)));
            es += s * s;
            et += m * m;
            if ((i + 1) % (size_t) frame == 0)
            {
                if (et / frame > 1e-5) share.push_back (toDb (es / et));   // above -50 dBFS
                es = et = 0;
            }
        }
        if (share.size() > 50) pr.sibilance = percentile (share, 0.995);
    }
    return pr;
}

Plan plan (const std::vector<float>& l, const std::vector<float>& r, double sr, Goal goal, const Order& order, const std::atomic<bool>* cancel,
           const Profile* reference)
{
    Plan p;
    p.in = analyse (l, r, sr);
    const auto& in = p.in;
    auto cancelled = [cancel] { return cancel != nullptr && cancel->load(); };
    if (in.integrated < -60.0)
    {
        p.notes.push_back ("Not enough audio to go on: play the loudest part of the song and listen again.");
        return p;
    }
    // with a reference, it stands in for the built-in master curve, and its loudness and density
    // become the goal
    const Profile* ref = goal == Goal::Reference && reference != nullptr && reference->integrated > -60.0 ? reference : nullptr;
    if (goal == Goal::Reference && ref == nullptr)
    {
        goal = Goal::Streaming;
        p.notes.push_back ("No reference loaded (LOAD REF in the analyser), so this is the streaming goal");
    }
    const double target = ref != nullptr ? std::clamp (ref->integrated, -20.0, -6.0) : targetLufs (goal);
    const bool dynamic = ref != nullptr ? target <= -15.0 : goal == Goal::Dynamic;
    if (ref != nullptr)
        p.notes.push_back (fmt ("Matching the reference: %.1f LUFS, %.1f dB peak to loudness", ref->integrated, ref->plr));
    // how far the mix is from the reference (or the master curve), band by band
    auto dev = [ref] (double mine, double theirs) { return mine - (ref != nullptr ? theirs : 0.0); };
    const double dLow = dev (in.low, ref ? ref->low : 0), dLowMid = dev (in.lowMid, ref ? ref->lowMid : 0), dMid = dev (in.mid, ref ? ref->mid : 0);
    const double dHighMid = dev (in.highMid, ref ? ref->highMid : 0), dAir = dev (in.air, ref ? ref->air : 0);

    // ---- EQ: half of what it would take to reach the reference, gently, in the museq's bands
    auto move = [] (double deviation, double most) { const double g = std::clamp (-0.5 * deviation, -most, most); return std::abs (g) < 0.75 ? 0.0 : g; };
    auto setBand = [&] (int b, double hz, double gainDb, bool shelf, const std::string& what)
    {
        if (gainDb == 0.0) return;
        p.eq.band[b].freq = hz;
        p.eq.band[b].gain = std::abs (gainDb);
        p.eq.band[b].cut = gainDb < 0;
        p.eq.band[b].shelf = shelf;
        p.notes.push_back (fmt (gainDb > 0 ? "EQ: +%.1f dB" : "EQ: %.1f dB", gainDb) + what);
    };
    for (int b = 0; b < eq::kBands; ++b) { p.eq.band[b].freq = eq::kRange[b].def; p.eq.band[b].shelf = b == 0 || b == eq::kBands - 1; }
    // the middle bands' moves, before a resonance may claim one of them
    std::array<double, eq::kBands> want { move (dLow, 3.0), std::min (0.0, move (dLowMid, 3.0)), move (dMid, 2.0), move (dHighMid, 2.5), move (dAir, 3.0) };

    // resonances: the strongest one ringing 4 dB or more over its surroundings gets a cut on a
    // middle band that covers it and has nothing (or under a dB) to do otherwise; the rest are
    // pointed out
    int owned = -1;
    for (const auto& res : in.resonances)
    {
        int pick = -1;
        if (owned < 0 && res.db >= 4.0)
            for (int b = 1; b <= 3; ++b)
                if (res.hz >= eq::kRange[b].lo && res.hz <= eq::kRange[b].hi && std::abs (want[(size_t) b]) < 1.0
                    && (pick < 0 || std::abs (want[(size_t) b]) < std::abs (want[(size_t) pick])))
                    pick = b;
        if (pick >= 0)
        {
            const double cut = std::clamp (0.5 * res.db, 1.5, 3.0);   // half of it: a nudge, not a notch
            p.eq.band[pick].freq = res.hz;
            p.eq.band[pick].gain = cut;
            p.eq.band[pick].cut = true;
            p.notes.push_back (fmt ("EQ: -%.1f dB at %.0f Hz, a resonance standing %.0f dB over its surroundings", cut, res.hz, res.db));
            owned = pick;
        }
        else
            p.notes.push_back (fmt ("Resonance at %.0f Hz (%.0f dB over its surroundings): worth a narrow cut in the mix", res.hz, res.db));
    }

    setBand (0, 70, want[0], true, " low shelf at 70 Hz (the low end is " + std::string (dLow > 0 ? "heavy)" : "light)"));
    if (owned != 1) setBand (1, 300, want[1], false, " at 300 Hz to clear mud");
    if (owned != 2) setBand (2, 1200, want[2], false, " at 1.2 kHz");
    if (owned != 3) setBand (3, 3500, want[3], false, dHighMid > 0 ? " at 3.5 kHz to ease harshness" : " at 3.5 kHz for presence");
    setBand (4, 12000, want[4], true, dAir < 0 ? " air shelf at 12 kHz" : " high shelf at 12 kHz to tame the top");
    p.eq.hpfHz = 25;

    if (in.bassWidth > std::max (-12.0, ref != nullptr ? ref->bassWidth + 3.0 : -12.0))
    {
        p.eq.monoBassHz = 100;
        p.notes.push_back ("EQ: mono below 100 Hz (the low end is wide, " + fmt ("%.0f dB of side)", in.bassWidth));
    }

    // ---- tape: the darker 15 ips 456 when the top is harsh, otherwise the cleaner 30 ips GP9
    const bool harsh = dHighMid > 1.5 || dAir > 2.0;
    p.tape.speed = harsh ? TapeMachine::ips15 : TapeMachine::ips30;
    p.tape.tape = harsh ? TapeMachine::T456 : TapeMachine::GP9;
    p.tape.bias = TapeMachine::Nominal;
    p.tape.inputDb = dynamic ? -2.0 : 0.0;
    p.tape.outputDb = 0;
    p.tape.wow = 0.1;
    p.tape.link = true;
    p.notes.push_back (harsh ? "Tape: 15 ips on 456 to round off the top" : "Tape: 30 ips on GP9, for weight without colour");

    // ---- de-esser: only when the "s" sounds stand out
    if (in.sibilance > -4.0)      { p.deess.thresholdDb = -12; p.deess.rangeDb = 8; }
    else if (in.sibilance > -7.0) { p.deess.thresholdDb = -8;  p.deess.rangeDb = 6; }
    else p.bypass[Deess] = true;
    p.deess.freq = 5000;
    p.deess.mode = DeEsser::Mid;
    p.notes.push_back (p.bypass[Deess] ? "De-esser: out, the highs are smooth" : fmt ("De-esser: in, the \"s\" sounds peak at %.0f%% of the energy", 100.0 * std::pow (10.0, in.sibilance / 10.0)));

    // ---- compressor: glue, sized by the goal; out if the mix is already dense
    // with a reference, as much glue as its density suggests
    const double wantGr = ref != nullptr ? (ref->plr < 9.0 ? 2.5 : ref->plr < 12.0 ? 1.5 : 1.0)
                                         : dynamic ? 1.0 : goal == Goal::Streaming ? 1.5 : 2.5;
    p.comp.ratio = wantGr < 2.0 ? 1 : 2;   // 2:1, 3:1
    p.comp.attack = 4;                                                          // 10 ms: let transients through
    p.comp.recovery = 5;                                                        // dual
    p.comp.transformer = harsh ? DualCompressor::Nickel : DualCompressor::Iron;
    p.comp.sidechainHpf = true;
    if (ref != nullptr)
    {
        // the reference's loudness picks the style; a reference mastered close to full scale gets -0.5
        p.limit.ceilingDb = ref->peak > -0.7 ? -0.5 : -1.0;
        p.limit.style = target >= -10.0 ? Limiter::Dense : target >= -12.5 ? Limiter::Punch : Limiter::Transparent;
    }
    else
    {
        p.limit.ceilingDb = goal == Goal::Club ? -0.3 : goal == Goal::Loud ? -0.5 : -1.0;
        p.limit.style = goal == Goal::Club ? Limiter::Dense : goal == Goal::Loud ? Limiter::Punch : Limiter::Transparent;
    }
    p.limit.autoRelease = true;
    p.limit.truePeak = true;
    p.limit.link = 1;
    p.limit.driveDb = 0;

    // a mix already near or past the goal: turn it down at the input, leaving the limiter some work
    if (in.integrated > target - 2.0)
    {
        p.inTrimDb = std::max (-24.0, target - 3.0 - in.integrated);
        p.notes.push_back (fmt ("Input trim: %.1f dB, the mix arrives about as loud as the goal", p.inTrimDb));
    }
    const double level = in.integrated + p.inTrimDb;   // what the modules see
    const unsigned all = (1u << kSlots) - 1, eqOnly = 1u << Eq, eqComp = eqOnly | (1u << Comp);
    if (in.plr < 8.0)
    {
        p.bypass[Comp] = true;
        p.notes.push_back (fmt ("Compressor: out, the mix is already dense (%.1f dB peak to loudness)", in.plr));
    }
    else
    {
        const auto base = render (l, r, sr, p, order, eqOnly);
        double offset = 0;
        auto reduction = [&] (double o)
        {
            p.comp.optoThreshold = std::clamp (level - 3.0 + o, -40.0, 0.0);
            p.comp.vcaThreshold = std::clamp (level + 4.0 + o, -40.0, 0.0);
            p.comp.optoGain = p.comp.vcaGain = 0;
            return base.lufs - render (l, r, sr, p, order, eqComp).lufs;
        };
        double gr = reduction (offset);
        for (int it = 0; it < 4 && std::abs (gr - wantGr) > 0.3 && ! cancelled(); ++it)
        {
            offset += std::clamp ((gr - wantGr) * 1.5, -8.0, 8.0);   // raise the thresholds to do less
            gr = reduction (offset);
        }
        p.compReductionDb = gr;
        p.comp.optoGain = std::clamp (gr, 0.0, 12.0);   // make it loudness-neutral; the limiter sets loudness
        p.notes.push_back (fmt ("Compressor: about %.1f dB of glue, made up", gr));
    }

    // ---- limiter: drive to the target, checked on the whole chain
    auto full = [&] { return render (l, r, sr, p, order, all); };
    auto out = full();
    for (int it = 0; it < 5 && std::abs (out.lufs - target) > 0.2 && ! cancelled(); ++it)
    {
        const double next = p.limit.driveDb + (target - out.lufs);
        if (next < 0.0)
        {
            // still too loud with no drive: trim more, moving the compressor's thresholds with it so
            // it keeps doing the same work
            const double d = std::max (-24.0 - p.inTrimDb, next);
            p.inTrimDb += d;
            p.comp.optoThreshold = std::clamp (p.comp.optoThreshold + d, -40.0, 0.0);
            p.comp.vcaThreshold = std::clamp (p.comp.vcaThreshold + d, -40.0, 0.0);
            p.limit.driveDb = 0;
            out = full();
            continue;
        }
        if (std::min (next, 18.0) == p.limit.driveDb) break;
        p.limit.driveDb = std::min (next, 18.0);
        out = full();
    }
    p.outLufs = out.lufs;
    p.outTruePeak = out.truePeak;
    p.notes.push_back (fmt ("Limiter: +%.1f dB of drive into %.1f dBTP", p.limit.driveDb, p.limit.ceilingDb));
    if (out.lufs < target - 1.0)
        p.notes.push_back (fmt ("This mix tops out at %.1f LUFS here; the %.0f LUFS goal would need more drive than is clean", out.lufs, target));

    // stereo, band by band: advice only
    static const char* const bandName[4] = { "low mids", "mids", "upper mids", "highs" };
    for (int b = 0; b < 4; ++b)
    {
        const double w = in.width[(size_t) b], theirs = ref != nullptr ? ref->width[(size_t) b] : -100.0;
        const std::string name = bandName[b];
        if (w <= -99.0) continue;
        if (w > -3.0)
            p.notes.push_back ("Stereo: the " + name + fmt (" are very wide (side %.0f dB from mid); check them in mono", w));
        else if (b >= 2 && w < -24.0)
            p.notes.push_back ("Stereo: the " + name + fmt (" are close to mono (side %.0f dB under mid); a little width there could open the mix", -w));
        else if (theirs > -99.0 && std::abs (w - theirs) > 6.0)
            p.notes.push_back ("Stereo: the " + name + (w > theirs ? " are wider" : " are narrower") + fmt (" than the reference's, by %.0f dB", std::abs (w - theirs)));
    }
    return p;
}
} // namespace bb::assist
