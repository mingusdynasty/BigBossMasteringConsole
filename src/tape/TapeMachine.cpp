#include "TapeMachine.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace bb
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kOpRms = 0.12589254117941673;   // 0 VU: -18 dBFS RMS
constexpr double kCoercivity[2] = { 0.40, 0.46 };   // 456-type, GP9-type (higher coercivity)
constexpr double kReversible[3] = { 0.86, 0.93, 0.965 };   // under, nominal, over bias
constexpr double kTiltDb[3] = { 0.8, 0.0, -1.2 };          // under-bias brightens, over-bias dulls the top
constexpr double kCrosstalk = 0.0079432823;                 // -42 dB
constexpr double kHissRms = 6.309573e-05;                   // -84 dBFS: 456 at 15 ips

// a value at a bias position between the three settings; exact at the settings themselves
double atBias (const double* v, double pos)
{
    const int i = std::clamp ((int) std::floor (pos), 0, 2);
    const double f = pos - i;
    return i == 2 || f <= 0 ? v[i] : v[i] + (v[i + 1] - v[i]) * f;
}

// first-order shelf (s + wz) / (s + wp) * norm, bilinear, prewarped at wz
void shelf1 (double& b0, double& b1, double& a1, double fz, double fp, double rate, bool highShelf)
{
    const double wz = 2 * kPi * fz, wp = 2 * kPi * fp;
    const double K = wz / std::tan (wz / (2 * rate));
    if (highShelf)
    {
        // (1 + s/wz) / (1 + s/wp): flat below, rising from fz to fp
        const double a0 = 1 + K / wp;
        b0 = (1 + K / wz) / a0; b1 = (1 - K / wz) / a0; a1 = (1 - K / wp) / a0;
    }
    else
    {
        // (s + wz) / (s + wp): flat above, rising below fz down to fp
        const double a0 = K + wp;
        b0 = (K + wz) / a0; b1 = (wz - K) / a0; a1 = (wp - K) / a0;
    }
}

void rbj (double& b0, double& b1, double& b2, double& a1, double& a2, int type, double f, double q, double db, double rate)
{
    // type 0 peak, 1 high shelf, 2 high-pass
    f = std::min (f, 0.45 * rate);
    const double A = std::pow (10.0, db / 40), w = 2 * kPi * f / rate, cs = std::cos (w), al = std::sin (w) / (2 * q);
    double B0, B1, B2, A0, A1, A2;
    if (type == 0) { B0 = 1 + al * A; B1 = -2 * cs; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cs; A2 = 1 - al / A; }
    else if (type == 1)
    {
        const double sA = 2 * std::sqrt (A) * al;
        B0 = A * ((A + 1) + (A - 1) * cs + sA); B1 = -2 * A * ((A - 1) + (A + 1) * cs); B2 = A * ((A + 1) + (A - 1) * cs - sA);
        A0 = (A + 1) - (A - 1) * cs + sA;        A1 = 2 * ((A - 1) - (A + 1) * cs);      A2 = (A + 1) - (A - 1) * cs - sA;
    }
    else { B0 = (1 + cs) / 2; B1 = -(1 + cs); B2 = (1 + cs) / 2; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; }
    b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
}

struct Cal { double rate; double drive[2]; double makeup[2][3]; };
std::mutex calLock;
std::vector<Cal> calCache;
} // namespace

// ---------------------------------------------------------------- calibration

double TapeMachine::thdAt (double rate, double k, double c, double drive, double amp)
{
    cf::TapeHysteresis h;
    h.setTape (k, c);
    h.prepare (rate);
    const int per = (int) std::lround (rate / 1000.0);
    const int settle = 4 * per, meas = 4 * per;
    double re[8] {}, im[8] {};
    for (int n = 0; n < settle + meas; ++n)
    {
        const double ph = 2 * kPi * n / per;
        const double m = h.process (drive * amp * std::sin (ph));
        if (n < settle) continue;
        for (int k2 = 1; k2 < 8; ++k2) { re[k2] += m * std::sin (k2 * ph); im[k2] += m * std::cos (k2 * ph); }
    }
    const double fund = std::hypot (re[1], im[1]);
    double harm = 0;
    for (int k2 = 2; k2 < 8; ++k2) harm += re[k2] * re[k2] + im[k2] * im[k2];
    return fund > 0 ? std::sqrt (harm) / fund : 1.0;
}

double TapeMachine::gainAt (double rate, double k, double c, double drive, double amp)
{
    cf::TapeHysteresis h;
    h.setTape (k, c);
    h.prepare (rate);
    const int per = (int) std::lround (rate / 250.0);
    double re = 0, im = 0;
    for (int n = 0; n < 6 * per; ++n)
    {
        const double ph = 2 * kPi * n / per;
        const double m = h.process (drive * amp * std::sin (ph));
        if (n >= 2 * per) { re += m * std::sin (ph); im += m * std::cos (ph); }
    }
    return 2 * std::hypot (re, im) / (4 * per) / amp;
}

void TapeMachine::calibrate()
{
    const double rate = path[0].os.rate();
    std::lock_guard<std::mutex> guard (calLock);
    for (const auto& c : calCache)
        if (c.rate == rate)
        {
            std::copy (c.drive, c.drive + 2, driveCal);
            for (int t = 0; t < 2; ++t) std::copy (c.makeup[t], c.makeup[t] + 3, makeupCal[t]);
            return;
        }

    for (int t = 0; t < 2; ++t)
    {
        // the drive that puts 3% THD at MOL, at nominal bias
        const double amp = std::sqrt (2.0) * kOpRms * std::pow (10.0, molDb (t) / 20);
        double lo = std::log (0.05), hi = std::log (200.0);
        for (int it = 0; it < 40; ++it)
        {
            const double mid = 0.5 * (lo + hi);
            if (thdAt (rate, kCoercivity[t], kReversible[Nominal], std::exp (mid), amp) > 0.03) hi = mid; else lo = mid;
        }
        driveCal[t] = std::exp (0.5 * (lo + hi));
        // makeup per bias: unity gain for a quiet (-30 dBFS RMS) 250 Hz tone
        for (int b = 0; b < 3; ++b)
            makeupCal[t][b] = 1.0 / gainAt (rate, kCoercivity[t], kReversible[b], driveCal[t], std::sqrt (2.0) * 0.0316227766);
    }
    Cal c { rate, { driveCal[0], driveCal[1] }, {} };
    for (int t = 0; t < 2; ++t) std::copy (makeupCal[t], makeupCal[t] + 3, c.makeup[t]);
    calCache.push_back (c);
}

// ---------------------------------------------------------------- set-up

void TapeMachine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    for (auto& p : path) { p.os.prepare (sr, maxBlock); p.speed = -1; }
    calibrate();
    altL.assign ((size_t) maxBlock, 0.0);
    altR.assign ((size_t) maxBlock, 0.0);
    dcC = 1 - std::exp (-2 * kPi * 5.0 / sr);
    glide = 1 - std::exp (-1 / (0.02 * sr));
    xfadeStep = 1.0 / (0.03 * sr);
    int len = 1;
    while (len < 2 * kTransportDelay + 8) len <<= 1;
    for (auto& l : line) l.assign ((size_t) len, 0.0);
    lineMask = len - 1;
    active = 0;
    configure (path[0], target);
    inGain = std::pow (10.0, target.inputDb / 20);
    outGain = std::pow (10.0, (target.outputDb - (target.link ? target.inputDb : 0.0)) / 20);
    wowAmt = target.wow;
    xtHpC = 1 - std::exp (-2 * kPi * 100.0 / sr);
    xtLpC = 1 - std::exp (-2 * kPi * 2000.0 / sr);
    hissHpC = 1 - std::exp (-2 * kPi * 300.0 / sr);
    reset();
}

void TapeMachine::configure (Path& p, const Settings& s)
{
    p.speed = s.speed; p.standard = s.standard; p.tape = s.tape; p.bias = s.bias;
    const double hr = p.os.rate();
    const bool fast = s.speed == ips30;

    // record EQ: NAB 50 us + 3180 us, CCIR 35 us, AES (30 ips) 17.5 us; the HF boost tops out at +10 dB
    const double hfZero = fast ? 9095.0 : (s.standard == NAB ? 3183.0 : 4547.0);
    shelf1 (p.recHF.b0, p.recHF.b1, p.recHF.a1, hfZero, hfZero * std::sqrt (10.0), hr, true);
    if (! fast && s.standard == NAB)
        shelf1 (p.recLF.b0, p.recLF.b1, p.recLF.a1, 50.0, 50.0 / std::sqrt (10.0), hr, false);
    else
        { p.recLF.b0 = 1; p.recLF.b1 = 0; p.recLF.a1 = 0; }
    // playback EQ: the exact inverse (zeros and poles swapped)
    auto invert = [] (const FirstOrder& f, FirstOrder& g) { g.b0 = 1 / f.b0; g.b1 = f.a1 / f.b0; g.a1 = f.b1 / f.b0; };
    invert (p.recHF, p.pbHF);
    invert (p.recLF, p.pbLF);

    p.drive = driveCal[s.tape];
    for (auto& h : p.hyst) h.prepare (hr);

    // playback head: low-end roll-off, head bump with the dip below it, gap and spacing loss
    auto& b = p;
    rbj (b.hpf.b0, b.hpf.b1, b.hpf.b2, b.hpf.a1, b.hpf.a2, 2, fast ? 18.0 : 10.0, 0.6, 0, sr);
    rbj (b.bump.b0, b.bump.b1, b.bump.b2, b.bump.a1, b.bump.a2, 0, fast ? 100.0 : 50.0, 1.1, fast ? 1.0 : 1.2, sr);
    rbj (b.dip.b0, b.dip.b1, b.dip.b2, b.dip.a1, b.dip.a2, 0, fast ? 42.0 : 22.0, 1.2, -1.5, sr);
    rbj (b.gap.b0, b.gap.b1, b.gap.b2, b.gap.a1, b.gap.a2, 1, fast ? 20000.0 : 16000.0, 0.7, fast ? -0.3 : -1.0, sr);
    p.trimPos = -1;
    trim (p, std::clamp (s.bias + 0.5 * std::clamp (s.biasTrim, -1.0, 1.0), 0.0, 2.0));
}

void TapeMachine::trim (Path& p, double pos)
{
    if (pos == p.trimPos) return;
    p.trimPos = pos;
    p.makeup = atBias (makeupCal[p.tape], pos);
    for (auto& h : p.hyst) h.setTape (kCoercivity[p.tape], atBias (kReversible, pos));
    rbj (p.tilt.b0, p.tilt.b1, p.tilt.b2, p.tilt.a1, p.tilt.a2, 1, p.speed == ips30 ? 15000.0 : 10000.0, 0.7, atBias (kTiltDb, pos), sr);
}

void TapeMachine::reset (Path& p)
{
    p.os.reset();
    for (auto& h : p.hyst) h.reset();
    for (auto* f : { &p.recHF, &p.recLF, &p.pbHF, &p.pbLF }) f->clear();
    for (auto* f : { &p.hpf, &p.bump, &p.dip, &p.gap, &p.tilt }) f->clear();
}

void TapeMachine::reset()
{
    reset (path[0]);
    reset (path[1]);
    fading = false;
    xfade = 0;
    dcIn = { 0, 0 };
    for (auto& l : line) std::fill (l.begin(), l.end(), 0.0);
    linePos = 0;
    for (int k = 0; k < 4; ++k) { phase[k] = 0.7 * k; rateDrift[k] = 1; }
    for (int c = 0; c < 2; ++c) xtHp[c] = xtLp[c] = hissLp[c] = 0;
    xtMix = target.crosstalk ? 1 : 0;
    hissMix = target.hiss ? 1 : 0;
    azNow = target.azimuthUs * 1e-6 * sr;
    trimNow = 0.5 * std::clamp (target.biasTrim, -1.0, 1.0);
}

// ---------------------------------------------------------------- audio

void TapeMachine::run (Path& p, double* l, double* r, int n)
{
    p.os.up (l, r, n);
    double* L = p.os.hiL();
    double* R = p.os.hiR();
    const int N = n * p.os.factor();
    double* io[2] { L, R };
    for (int c = 0; c < 2; ++c)
    {
        auto& h = p.hyst[c];
        double* x = io[c];
        for (int i = 0; i < N; ++i)
        {
            const double e = p.recLF.run (c, p.recHF.run (c, x[i]));
            const double m = h.process (p.drive * e) * p.makeup;
            x[i] = p.pbLF.run (c, p.pbHF.run (c, m));
        }
    }
    p.os.down (l, r, n);
    double* out[2] { l, r };
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            out[c][i] = p.tilt.run (c, p.gap.run (c, p.dip.run (c, p.bump.run (c, p.hpf.run (c, out[c][i])))));
}

void TapeMachine::process (double* l, double* r, int n)
{
    for (int off = 0; off < n; off += maxBlock)
    {
        const int k = std::min (maxBlock, n - off);
        double* bl = l + off;
        double* br = r + off;

        // record level, DC coupling, VU
        double vuAcc[2] {};
        const double inT = std::pow (10.0, target.inputDb / 20);
        const double outT = std::pow (10.0, (target.outputDb - (target.link ? target.inputDb : 0.0)) / 20);
        for (int i = 0; i < k; ++i)
        {
            inGain += (inT - inGain) * glide;
            double* x[2] { bl + i, br + i };
            for (int c = 0; c < 2; ++c)
            {
                double v = *x[c] * inGain;
                dcIn[(size_t) c] += dcC * (v - dcIn[(size_t) c]);
                v -= dcIn[(size_t) c];
                vuAcc[c] += v * v;
                *x[c] = v;
            }
        }
        for (int c = 0; c < 2; ++c) vu[c].store ((float) (vuAcc[c] / k / (kOpRms * kOpRms)), std::memory_order_relaxed);

        // a switch changed: thread a fresh path with the new settings and crossfade to it
        if (! fading && ! path[active].matches (target))
        {
            auto& nx = path[1 - active];
            configure (nx, target);
            reset (nx);
            fading = true;
            xfade = 0;
        }
        // bias trim: glides, block by block, and moves each path from its own switch position
        {
            const double want = 0.5 * std::clamp (target.biasTrim, -1.0, 1.0);
            trimNow = std::abs (want - trimNow) < 1e-4 ? want : trimNow + (want - trimNow) * std::min (1.0, k / (0.05 * sr));
            for (auto& p : path)
                if (p.speed >= 0) trim (p, std::clamp (p.bias + trimNow, 0.0, 2.0));
        }
        if (fading)
        {
            std::copy (bl, bl + k, altL.begin());
            std::copy (br, br + k, altR.begin());
        }
        run (path[active], bl, br, k);
        if (fading)
        {
            run (path[1 - active], altL.data(), altR.data(), k);
            for (int i = 0; i < k; ++i)
            {
                xfade = std::min (1.0, xfade + xfadeStep);
                bl[i] += xfade * (altL[(size_t) i] - bl[i]);
                br[i] += xfade * (altR[(size_t) i] - br[i]);
            }
            if (xfade >= 1.0) { active = 1 - active; fading = false; }
        }

        // crosstalk: each track picks up the other, band-limited, through the heads
        const double xtT = target.crosstalk ? 1 : 0;
        if (xtT > 0 || xtMix > 0)
            for (int i = 0; i < k; ++i)
            {
                xtMix += (xtT - xtMix) * glide;
                const double in[2] { bl[i], br[i] };
                double bp[2];
                for (int c = 0; c < 2; ++c)
                {
                    xtHp[c] += xtHpC * (in[c] - xtHp[c]);   // the part below 100 Hz, taken away next
                    xtLp[c] += xtLpC * ((in[c] - xtHp[c]) - xtLp[c]);
                    bp[c] = xtLp[c];
                }
                bl[i] += xtMix * kCrosstalk * bp[1];
                br[i] += xtMix * kCrosstalk * bp[0];
            }

        // transport: wow (slow, from the reels and tension) and flutter (capstan, idlers, scrape),
        // as a modulated delay common to both tracks. Depths: share of the specified 0.05% peak
        // speed deviation; each line's rate wanders a little
        // speed deviation; each line's rate wanders a little. Azimuth error delays the right track
        static constexpr double kHz[4] = { 0.55, 1.3, 7.3, 31.0 }, kShare[4] = { 0.4, 0.25, 0.25, 0.1 };
        const double azT = std::clamp (target.azimuthUs, 0.0, 20.0) * 1e-6 * sr;
        const double hissT = target.hiss ? 1 : 0;
        const double hissAmp = kHissRms * std::sqrt (12.0) * (target.tape == GP9 ? 0.794 : 1.0) * (target.speed == ips30 ? 0.794 : 1.0);
        for (int i = 0; i < k; ++i)
        {
            wowAmt += (target.wow - wowAmt) * glide;
            azNow += (azT - azNow) * glide;
            double d = kTransportDelay;
            for (int j = 0; j < 4; ++j)
            {
                rng = rng * 1664525u + 1013904223u;
                const double u = ((rng >> 8) / 16777216.0) - 0.5;
                rateDrift[j] = std::clamp (rateDrift[j] + u * 2e-4, 0.9, 1.1);
                phase[j] += 2 * kPi * kHz[j] * rateDrift[j] / sr;
                if (phase[j] > 2 * kPi) phase[j] -= 2 * kPi;
                d += wowAmt * sr * 0.0005 * kShare[j] / (2 * kPi * kHz[j]) * std::sin (phase[j]);
            }
            for (int c = 0; c < 2; ++c)
            {
                const double dc = c == 0 ? d : d + azNow;
                const int di = (int) dc;
                const double fr = dc - di;
                auto& ln = line[(size_t) c];
                double* x = c == 0 ? bl + i : br + i;
                ln[(size_t) linePos] = *x;
                // 4-point Hermite around the delayed position
                auto at = [&] (int back) { return ln[(size_t) ((linePos - back) & lineMask)]; };
                const double y0 = at (di - 1), y1 = at (di), y2 = at (di + 1), y3 = at (di + 2);
                const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
                // fr moves toward the older sample (y2)
                *x = ((c3 * fr + c2) * fr + c1) * fr + y1;
            }
            linePos = (linePos + 1) & lineMask;
            hissMix += (hissT - hissMix) * glide;
            if (hissMix > 1e-6)
                for (int c = 0; c < 2; ++c)
                {
                    hissRng[c] = hissRng[c] * 1664525u + 1013904223u;
                    const double u = ((hissRng[c] >> 8) / 16777216.0) - 0.5;
                    hissLp[c] += hissHpC * (u - hissLp[c]);
                    (c == 0 ? bl[i] : br[i]) += hissMix * hissAmp * (u - hissLp[c]);
                }
            outGain += (outT - outGain) * glide;
            bl[i] *= outGain;
            br[i] *= outGain;
        }
    }
}
} // namespace bb
