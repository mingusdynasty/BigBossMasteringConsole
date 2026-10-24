// The limiter bench (roadmap Phase D): renders a test set through the BB-L1 and measures any limiter's
// renders of the same set, so the BB-L1 can be held up against FabFilter Pro-L 2 (or anything else)
// at matched settings. No JUCE; builds with the engine.
//
//   bb_bench render <dir> [--music <folder>] [--quick]
//       writes <dir>/input (the stimuli, already gained, 32-bit float), the BB-L1's renders of them in
//       its three styles (<dir>/bbl1-transparent, -punch, -dense; drive 0, ceiling -1 dBTP, true peak
//       on), and <dir>/README.txt saying how to make the other limiter's renders
//   bb_bench measure <file.wav> [--input <dry.wav>]
//   bb_bench compare <dirA> <dirB> [--input <dir>] [--report <file.md>]
//       measures every file the two folders share and writes a side-by-side report
//   bb_bench selftest
//       checks the measurements on signals with known answers, and a quick render
//
// The set: 1 kHz and 40 Hz sines, SMPTE (60 Hz + 7 kHz, 4:1) and CCIF (19 + 20 kHz) twin tones at
// 0, 3, 6 and 10 dB over the ceiling; a drum loop and a full mix (both synthetic) and any WAVs from
// --music, each gained so the BB-L1 in its dense style (the loudest) lands on -14, -11 and -9 LUFS.
// Every limiter and style gets the same input files, so differences in loudness at the same ceiling
// show up in the report.
#include "Wav.h"

#include "dyn/Limiter.h"
#include "meter/LoudnessMeter.h"
#include "meter/TruePeak.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using bench::Audio;

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kCeiling = -1.0;
constexpr double kRate = 48000;
const char* const kStyleName[3] = { "transparent", "punch", "dense" };

double dB (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }
double lin (double db) { return std::pow (10.0, db / 20.0); }

// ---------------------------------------------------------------- stimuli

Audio tones (double secs, std::initializer_list<std::pair<double, double>> parts)   // (Hz, amplitude)
{
    Audio a;
    a.rate = kRate;
    a.l.resize ((size_t) (secs * kRate));
    for (size_t i = 0; i < a.size(); ++i)
    {
        double v = 0;
        for (const auto& [hz, amp] : parts) v += amp * std::sin (2 * kPi * hz * (double) i / kRate);
        a.l[i] = v;
    }
    a.r = a.l;
    return a;
}

// a drum loop at 120 bpm: kick on every beat, snare on 2 and 4, closed hats on the eighths
Audio drums (double secs, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> g (0, 1);
    Audio a;
    a.rate = kRate;
    a.l.assign ((size_t) (secs * kRate), 0.0);
    a.r = a.l;
    double snLp = 0, hatHp = 0, hatX = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const double t = (double) i / kRate, beat = std::fmod (t, 0.5), eighth = std::fmod (t, 0.25);
        const int beatNo = (int) (t / 0.5) % 4;
        const double kick = 0.8 * std::exp (-beat * 12) * std::sin (2 * kPi * (45 * beat + 160 / 30.0 * (1 - std::exp (-beat * 30))));
        const double click = beat < 0.0015 ? 0.6 * g (rng) * (1 - beat / 0.0015) : 0.0;
        const double n = g (rng);
        snLp += (n - snLp) * 0.35;
        const double snare = (beatNo == 1 || beatNo == 3)
                                 ? std::exp (-beat * 18) * (0.35 * snLp + 0.3 * std::sin (2 * kPi * 190 * beat))
                                 : 0.0;
        hatHp = 0.85 * (hatHp + n - hatX); hatX = n;
        const double hat = 0.12 * std::exp (-eighth * 60) * hatHp;
        const double pan = 0.15 * hat;
        a.l[i] = kick + click + snare + hat + pan;
        a.r[i] = kick + click + snare + hat - pan;
    }
    return a;
}

// a full mix: the drums, a bass line, a sustained chord pad and a bright lead, in stereo
Audio mix (double secs, unsigned seed)
{
    Audio a = drums (secs, seed);
    std::mt19937 rng (seed + 1);
    std::uniform_real_distribution<double> u (-1, 1);
    const double bassNotes[4] = { 55.0, 55.0, 65.41, 49.0 };
    const double chord[4][3] = { { 220, 261.63, 329.63 }, { 220, 261.63, 329.63 }, { 261.63, 329.63, 392 }, { 196, 246.94, 293.66 } };
    double bassLp = 0, padPh[2][3] {}, leadPh = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const double t = (double) i / kRate;
        const int bar = (int) (t / 2.0) % 4;
        const double inBeat = std::fmod (t, 0.5);
        // bass: a saw, low-passed, plucked on each beat
        const double f = bassNotes[bar], saw = 2 * std::fmod (f * t, 1.0) - 1;
        bassLp += (saw - bassLp) * 0.04;
        const double bass = 0.45 * bassLp * (0.6 + 0.4 * std::exp (-inBeat * 6));
        // pad: detuned saws, one set per side
        double pad[2] {};
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 3; ++k)
            {
                padPh[c][k] = std::fmod (padPh[c][k] + chord[bar][k] * (c == 0 ? 0.998 : 1.002) / kRate, 1.0);
                pad[c] += 0.04 * (2 * padPh[c][k] - 1);
            }
        // lead: a square an octave up on the eighths, with a little noise for air
        leadPh = std::fmod (leadPh + 2 * chord[bar][(int) (t / 0.25) % 3] / kRate, 1.0);
        const double lead = 0.06 * (leadPh < 0.5 ? 1.0 : -1.0) * std::exp (-std::fmod (t, 0.25) * 8) + 0.004 * u (rng);
        a.l[i] = 0.8 * a.l[i] + bass + pad[0] + 0.7 * lead;
        a.r[i] = 0.8 * a.r[i] + bass + pad[1] + 1.0 * lead;
    }
    return a;
}

Audio gained (const Audio& a, double db, double tailSecs)
{
    Audio out = a;
    const double g = lin (db);
    for (auto* ch : { &out.l, &out.r })
    {
        for (auto& v : *ch) v *= g;
        ch->resize (ch->size() + (size_t) (tailSecs * a.rate), 0.0);
    }
    return out;
}

double samplePeak (const Audio& a)
{
    double p = 0;
    for (size_t i = 0; i < a.size(); ++i) p = std::max ({ p, std::abs (a.l[i]), std::abs (a.r[i]) });
    return p;
}

// ---------------------------------------------------------------- the BB-L1

Audio limit (const Audio& in, int style)
{
    bb::Limiter lim;
    bb::Limiter::Settings s;
    s.driveDb = 0;
    s.ceilingDb = kCeiling;
    s.style = style;
    s.truePeak = true;
    lim.setSettings (s);
    const int block = 512;
    lim.prepare (in.rate, block);
    const int lat = lim.latency();
    Audio buf = in;
    for (auto* ch : { &buf.l, &buf.r }) ch->resize (ch->size() + (size_t) lat, 0.0);
    for (size_t pos = 0; pos < buf.size(); pos += block)
        lim.process (buf.l.data() + pos, buf.r.data() + pos, (int) std::min<size_t> (block, buf.size() - pos));
    Audio out;
    out.rate = in.rate;
    out.l.assign (buf.l.begin() + lat, buf.l.end());
    out.r.assign (buf.r.begin() + lat, buf.r.end());
    return out;
}

// ---------------------------------------------------------------- measurements

struct Loud { double integrated, range, truePeak; };

Loud loudness (const Audio& a)
{
    bb::LoudnessMeter m;
    m.prepare (a.rate, 2);
    bb::TruePeak tp;
    tp.prepare (a.rate, 2);
    const double* ch[2] = { a.l.data(), a.r.data() };
    m.process (ch, (int) a.size());
    tp.process (ch, (int) a.size());
    return { m.integrated(), m.range(), dB (std::max (tp.heldPeak (0), tp.heldPeak (1))) };
}

// amplitude of the component at hz over [from, to) of x, Hann-windowed, read at the exact frequency
double amplitude (const std::vector<double>& x, double rate, double hz, size_t from, size_t to)
{
    std::complex<double> acc = 0;
    double wsum = 0;
    const size_t n = to - from;
    const std::complex<double> step = std::polar (1.0, -2 * kPi * hz / rate);
    std::complex<double> ph = std::polar (1.0, -2 * kPi * hz * (double) from / rate);
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2 * kPi * (double) i / (double) n);
        acc += x[from + i] * w * ph;
        wsum += w;
        ph *= step;
        if ((i & 1023) == 1023) ph /= std::abs (ph);
    }
    return 2 * std::abs (acc) / wsum;
}

// THD+N: what is left after the best-fitting sine at hz (and DC) is taken out, against that sine
double thdN (const std::vector<double>& x, double rate, double hz, size_t from, size_t to)
{
    // least squares on sin, cos and 1
    double ss = 0, cc = 0, sc = 0, s1 = 0, c1 = 0, n1 = 0, xs = 0, xc = 0, x1 = 0;
    for (size_t i = from; i < to; ++i)
    {
        const double ph = 2 * kPi * hz * (double) i / rate, s = std::sin (ph), c = std::cos (ph);
        ss += s * s; cc += c * c; sc += s * c; s1 += s; c1 += c; n1 += 1; xs += x[i] * s; xc += x[i] * c; x1 += x[i];
    }
    // solve the 3x3 normal equations by Cramer's rule
    const double m[3][3] = { { ss, sc, s1 }, { sc, cc, c1 }, { s1, c1, n1 } }, v[3] = { xs, xc, x1 };
    auto det = [] (const double a[3][3]) { return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]); };
    const double d = det (m);
    double coef[3];
    for (int k = 0; k < 3; ++k)
    {
        double t[3][3];
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) t[r][c] = c == k ? v[r] : m[r][c];
        coef[k] = det (t) / d;
    }
    double eFit = 0, eRes = 0;
    for (size_t i = from; i < to; ++i)
    {
        const double ph = 2 * kPi * hz * (double) i / rate;
        const double fit = coef[0] * std::sin (ph) + coef[1] * std::cos (ph);
        eFit += fit * fit;
        const double res = x[i] - fit - coef[2];
        eRes += res * res;
    }
    return std::sqrt (eRes / eFit);
}

double thd (const std::vector<double>& x, double rate, double hz, size_t from, size_t to, int maxHarmonic)
{
    const double f = amplitude (x, rate, hz, from, to);
    double h = 0;
    for (int k = 2; k <= maxHarmonic && k * hz < 0.48 * rate; ++k) { const double a = amplitude (x, rate, k * hz, from, to); h += a * a; }
    return std::sqrt (h) / f;
}

// SMPTE IMD (60 Hz + 7 kHz, 4:1): the sidebands at 7 kHz +- 60, 120, 180 Hz against the 7 kHz tone
double smpte (const std::vector<double>& x, double rate, size_t from, size_t to)
{
    const double carrier = amplitude (x, rate, 7000, from, to);
    double e = 0;
    for (int k = 1; k <= 3; ++k)
        for (int sgn : { -1, 1 }) { const double a = amplitude (x, rate, 7000 + sgn * 60.0 * k, from, to); e += a * a; }
    return std::sqrt (e) / carrier;
}

// CCIF twin tone (19 + 20 kHz, equal): the 2nd-order difference at 1 kHz and the 3rd-order products
// at 18 and 21 kHz, each against the two tones together
std::pair<double, double> ccif (const std::vector<double>& x, double rate, size_t from, size_t to)
{
    const double tones = amplitude (x, rate, 19000, from, to) + amplitude (x, rate, 20000, from, to);
    const double d2 = amplitude (x, rate, 1000, from, to);
    const double a18 = amplitude (x, rate, 18000, from, to), a21 = 21000 < 0.48 * rate ? amplitude (x, rate, 21000, from, to) : 0.0;
    return { d2 / tones, std::sqrt (a18 * a18 + a21 * a21) / tones };
}

// the delay of b against a, in samples, from their 1 ms energy envelopes (within +-200 ms)
long lagOf (const Audio& a, const Audio& b)
{
    const size_t w = (size_t) (a.rate / 1000);
    auto env = [w] (const Audio& x)
    {
        std::vector<double> e (x.size() / w);
        for (size_t k = 0; k < e.size(); ++k)
            for (size_t i = k * w; i < (k + 1) * w; ++i) { const double m = x.l[i] + x.r[i]; e[k] += m * m; }
        return e;
    };
    const auto ea = env (a), eb = env (b);
    long best = 0;
    double bestC = -1;
    for (long lag = -200; lag <= 200; ++lag)
    {
        double c = 0;
        for (long k = 0; k < (long) ea.size(); ++k)
        {
            const long j = k + lag;
            if (j >= 0 && j < (long) eb.size()) c += std::sqrt (ea[(size_t) k] * eb[(size_t) j]);
        }
        if (c > bestC) { bestC = c; best = lag; }
    }
    return best * (long) w;
}

// transient retention: for each hit found in the dry input, how far its first 10 ms stands above the
// 30 .. 150 ms that follow, out against in (dB; 0 keeps the hits as they were, negative flattens them)
double transients (const Audio& out, const Audio& in, int* hits = nullptr)
{
    const long lag = lagOf (in, out);
    const size_t ms = (size_t) (in.rate / 1000);
    const size_t blocks = in.size() / ms;
    std::vector<double> e (blocks);
    double mean = 0;
    for (size_t k = 0; k < blocks; ++k)
    {
        for (size_t i = k * ms; i < (k + 1) * ms; ++i) { const double m = 0.5 * (in.l[i] + in.r[i]); e[k] += m * m; }
        mean += e[k] / (double) blocks;
    }
    auto peak = [&] (const Audio& x, long shift, size_t from, size_t to)
    {
        double p = 0;
        for (size_t i = from; i < to; ++i)
        {
            const long j = (long) i + shift;
            if (j >= 0 && j < (long) x.size()) p = std::max (p, std::abs (0.5 * (x.l[(size_t) j] + x.r[(size_t) j])));
        }
        return p;
    };
    auto rms = [&] (const Audio& x, long shift, size_t from, size_t to)
    {
        double s = 0;
        size_t n = 0;
        for (size_t i = from; i < to; ++i)
        {
            const long j = (long) i + shift;
            if (j >= 0 && j < (long) x.size()) { const double m = 0.5 * (x.l[(size_t) j] + x.r[(size_t) j]); s += m * m; ++n; }
        }
        return n > 0 ? std::sqrt (s / (double) n) : 0.0;
    };
    double sum = 0;
    int count = 0;
    size_t last = 0;
    for (size_t k = 30; k + 160 < blocks; ++k)
    {
        double before = 0;
        for (size_t j = k - 30; j < k; ++j) before += e[j] / 30;
        if (e[k] < 8 * before || e[k] < 0.5 * mean || (count > 0 && k - last < 80)) continue;
        last = k;
        const size_t s0 = (k - 2) * ms, s1 = (k + 10) * ms, b0 = (k + 30) * ms, b1 = (k + 150) * ms;
        const double inR = dB (peak (in, 0, s0, s1)) - dB (rms (in, 0, b0, b1));
        const double outR = dB (peak (out, lag, s0, s1)) - dB (rms (out, lag, b0, b1));
        sum += outR - inR;
        ++count;
        k += 10;
    }
    if (hits != nullptr) *hits = count;
    return count > 0 ? sum / count : 0.0;
}

// the true peak after band-limiting as a lossy encoder would (a linear-phase low-pass at the codec's
// cutoff). A floor: the encoder's own noise adds a little more
double codecTruePeak (const Audio& a, double cutoffHz)
{
    const int taps = 255, mid = taps / 2;
    std::vector<double> h ((size_t) taps);
    auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
    const double fc = cutoffHz / a.rate, beta = 8.0;
    double sum = 0;
    for (int i = 0; i < taps; ++i)
    {
        const double n = i - mid, r = n / mid;
        const double sinc = i == mid ? 2 * fc : std::sin (2 * kPi * fc * n) / (kPi * n);
        h[(size_t) i] = sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / bessel0 (beta);
        sum += h[(size_t) i];
    }
    for (auto& v : h) v /= sum;
    Audio f;
    f.rate = a.rate;
    for (int c = 0; c < 2; ++c)
    {
        const auto& x = c == 0 ? a.l : a.r;
        auto& y = c == 0 ? f.l : f.r;
        y.assign (x.size() + (size_t) mid, 0.0);
        for (size_t i = 0; i < y.size(); ++i)
        {
            double acc = 0;
            for (int k = 0; k < taps; ++k)
            {
                const long j = (long) i - k;
                if (j >= 0 && j < (long) x.size()) acc += h[(size_t) k] * x[(size_t) j];
            }
            y[i] = acc;
        }
    }
    return loudness (f).truePeak;
}

// ---------------------------------------------------------------- per-file metrics

struct Metric
{
    std::string name;
    double value;
    int better;   // -1 lower is better, +1 higher is better, 0 neither
    const char* format;
    double same;  // differences smaller than this are a tie
};

std::string kindOf (const std::string& stem) { return stem.substr (0, stem.find ('_')); }

std::vector<Metric> measure (const Audio& a, const Audio* dry, const std::string& stem)
{
    std::vector<Metric> m;
    const auto ld = loudness (a);
    const std::string kind = kindOf (stem);
    const bool tone = kind == "sine1k" || kind == "sine40" || kind == "smpte" || kind == "ccif";
    const size_t from = std::min (a.size(), (size_t) (a.rate * 1.0)), to = std::min (a.size(), (size_t) (a.rate * 3.0));
    m.push_back ({ "true peak dBTP", ld.truePeak, -1, "%.2f", 0.01 });
    if (! tone)
    {
        m.push_back ({ "loudness LUFS", ld.integrated, +1, "%.2f", 0.02 });
        m.push_back ({ "loudness range LU", ld.range, +1, "%.1f", 0.1 });
        m.push_back ({ "TP after MP3-like 16 kHz", codecTruePeak (a, 16000), -1, "%.2f", 0.02 });
        m.push_back ({ "TP after AAC-like 19.5 kHz", codecTruePeak (a, 19500), -1, "%.2f", 0.02 });
        if (dry != nullptr) m.push_back ({ "transient retention dB", transients (a, *dry), +1, "%.2f", 0.05 });
    }
    else if (to > from + (size_t) a.rate / 2)
    {
        if (kind == "sine1k") m.push_back ({ "THD+N %", 100 * thdN (a.l, a.rate, 1000, from, to), -1, "%.4f", 0.0005 });
        if (kind == "sine40") m.push_back ({ "THD at 40 Hz %", 100 * thd (a.l, a.rate, 40, from, to, 10), -1, "%.4f", 0.0005 });
        if (kind == "smpte") m.push_back ({ "SMPTE IMD %", 100 * smpte (a.l, a.rate, from, to), -1, "%.4f", 0.0005 });
        if (kind == "ccif")
        {
            const auto [d2, d3] = ccif (a.l, a.rate, from, to);
            // below -140 dB is numerical noise, not distortion
            m.push_back ({ "CCIF d2 (1 kHz) dB", std::max (-140.0, dB (d2)), -1, "%.1f", 0.2 });
            m.push_back ({ "CCIF d3 (18/21 kHz) dB", std::max (-140.0, dB (d3)), -1, "%.1f", 0.2 });
        }
    }
    return m;
}

std::vector<std::string> wavsIn (const fs::path& dir)
{
    std::vector<std::string> v;
    if (! fs::is_directory (dir)) return v;
    for (const auto& e : fs::directory_iterator (dir))
        if (e.is_regular_file())
        {
            auto ext = e.path().extension().string();
            std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
            if (ext == ".wav") v.push_back (e.path().filename().string());
        }
    std::sort (v.begin(), v.end());
    return v;
}

// ---------------------------------------------------------------- commands

int render (const fs::path& dir, const fs::path& music, bool quick)
{
    fs::create_directories (dir / "input");
    for (const char* s : kStyleName) fs::create_directories (dir / (std::string ("bbl1-") + s));
    std::FILE* manifest = std::fopen ((dir / "input" / "manifest.txt").string().c_str(), "w");

    auto emit = [&] (const std::string& stem, const Audio& in, const std::string& note)
    {
        bench::writeWav ((dir / "input" / (stem + ".wav")).string(), in);
        for (int s = 0; s < 3; ++s)
            bench::writeWav ((dir / (std::string ("bbl1-") + kStyleName[s]) / (stem + ".wav")).string(), limit (in, s));
        if (manifest != nullptr) std::fprintf (manifest, "%s.wav  %s\n", stem.c_str(), note.c_str());
        std::printf ("  %s  (%s)\n", stem.c_str(), note.c_str());
    };

    const std::vector<double> overs = quick ? std::vector<double> { 6 } : std::vector<double> { 0, 3, 6, 10 };
    for (double over : overs)
    {
        char tag[32];
        std::snprintf (tag, sizeof tag, "over%+03d", (int) over);
        const double peak = lin (kCeiling + over);
        char note[96];
        std::snprintf (note, sizeof note, "sample peak %.1f dB over the -1 dBTP ceiling", over);
        emit (std::string ("sine1k_") + tag, gained (tones (3.0, { { 1000, peak } }), 0, 0.3), note);
        emit (std::string ("sine40_") + tag, gained (tones (3.0, { { 40, peak } }), 0, 0.3), note);
        emit (std::string ("smpte_") + tag, gained (tones (3.0, { { 60, peak / 1.25 }, { 7000, peak / 5 } }), 0, 0.3), note);
        emit (std::string ("ccif_") + tag, gained (tones (3.0, { { 19000, peak / 2 }, { 20000, peak / 2 } }), 0, 0.3), note);
    }

    // programme: gained so the dense BB-L1 lands on each target (or as near as 30 dB of gain gets it)
    std::vector<std::pair<std::string, Audio>> programmes;
    programmes.push_back ({ "drums", drums (quick ? 6.0 : 16.0, 3) });
    programmes.push_back ({ "mix", mix (quick ? 6.0 : 20.0, 5) });
    for (const auto& name : music.empty() ? std::vector<std::string> {} : wavsIn (music))
    {
        Audio a;
        std::string error;
        if (! bench::readWav ((music / name).string(), a, error)) { std::printf ("skipping %s: %s\n", name.c_str(), error.c_str()); continue; }
        std::string stem = fs::path (name).stem().string();
        std::replace (stem.begin(), stem.end(), '_', '-');
        std::replace (stem.begin(), stem.end(), ' ', '-');
        programmes.push_back ({ "song-" + stem, std::move (a) });
    }
    const std::vector<double> targets = quick ? std::vector<double> { -11 } : std::vector<double> { -14, -11, -9 };
    for (const auto& [name, raw] : programmes)
    {
        const double rawPeak = dB (samplePeak (raw));
        for (double target : targets)
        {
            // bisect the gain: the limited loudness rises with it
            double lo = -30 - rawPeak, hi = 30 - rawPeak, got = 0;
            for (int it = 0; it < 24 && hi - lo > 0.01; ++it)
            {
                const double mid = 0.5 * (lo + hi);
                got = loudness (limit (gained (raw, mid, 0.3), bb::Limiter::Dense)).integrated;
                if (std::abs (got - target) < 0.02) { lo = hi = mid; break; }
                (got < target ? lo : hi) = mid;
            }
            const double gain = 0.5 * (lo + hi);
            char tag[48], note[128];
            std::snprintf (tag, sizeof tag, "%s_lufs%+03d", name.c_str(), (int) target);
            const double reads = loudness (limit (gained (raw, gain, 0.3), bb::Limiter::Dense)).integrated;
            std::snprintf (note, sizeof note, "gained %+.2f dB; the dense BB-L1 reads %.2f LUFS%s", gain, reads,
                           std::abs (reads - target) > 0.1 ? " (target out of reach)" : "");
            emit (tag, gained (raw, gain, 0.3), note);
        }
    }
    if (manifest != nullptr) std::fclose (manifest);

    if (std::FILE* f = std::fopen ((dir / "README.txt").string().c_str(), "w"))
    {
        std::fprintf (f,
            "Big Boss limiter bench\n"
            "======================\n\n"
            "input/            the test set, 32-bit float WAV at 48 kHz (levels over 0 dBFS are intended)\n"
            "bbl1-transparent/ the BB-L1's renders, style Transparent (and -punch, -dense)\n\n"
            "To add another limiter (for example Pro-L 2):\n"
            "  1. Make a folder next to these, e.g. prol2-transparent/.\n"
            "  2. For every file in input/, render it through the limiter with: input gain 0 dB, output\n"
            "     (ceiling) -1.0 dBTP, true peak limiting on, oversampling as you would use it, no dither,\n"
            "     32-bit float WAV at 48 kHz, the same file name. Keep the length (no tail trimming) and make\n"
            "     sure the render is delay-compensated (the DAW does this when the plug-in is on a track).\n"
            "  3. Run: bb_bench compare <this folder>/bbl1-transparent <this folder>/prol2-transparent\n"
            "          --input <this folder>/input --report report.md\n\n"
            "Each programme file is gained so the BB-L1 in its dense (loudest) style lands on its LUFS\n"
            "target; every limiter and style gets the same file, so the report shows which is louder at the\n"
            "same ceiling. manifest.txt lists the gains.\n");
        std::fclose (f);
    }
    std::printf ("wrote %s\n", dir.string().c_str());
    return 0;
}

int measureOne (const std::string& file, const std::string& input)
{
    Audio a, dry;
    std::string error;
    if (! bench::readWav (file, a, error)) { std::printf ("%s\n", error.c_str()); return 1; }
    const bool haveDry = ! input.empty() && bench::readWav (input, dry, error);
    if (! input.empty() && ! haveDry) std::printf ("%s\n", error.c_str());
    const auto stem = fs::path (file).stem().string();
    for (const auto& m : measure (a, haveDry ? &dry : nullptr, stem))
    {
        std::printf ("%-28s ", m.name.c_str());
        std::printf (m.format, m.value);
        std::printf ("\n");
    }
    return 0;
}

int compare (const fs::path& a, const fs::path& b, const fs::path& input, const std::string& reportPath)
{
    const auto names = wavsIn (a);
    std::string md = "# Limiter bench: " + a.filename().string() + " against " + b.filename().string() + "\n\n";
    md += "A is `" + a.string() + "`, B is `" + b.string() + "`. Delta is B minus A; the last column says which is better on that line.\n\n";
    int compared = 0;
    std::map<std::string, std::vector<std::string>> byKind;
    for (const auto& n : names)
        if (fs::exists (b / n)) byKind[kindOf (fs::path (n).stem().string())].push_back (n);
    for (const auto& [kind, files] : byKind)
    {
        md += "## " + kind + "\n\n| file | measure | A | B | delta | better |\n|---|---|---:|---:|---:|---|\n";
        for (const auto& n : files)
        {
            Audio xa, xb, dry;
            std::string error;
            if (! bench::readWav ((a / n).string(), xa, error) || ! bench::readWav ((b / n).string(), xb, error))
                { std::printf ("%s\n", error.c_str()); continue; }
            const bool haveDry = ! input.empty() && bench::readWav ((input / n).string(), dry, error);
            const auto stem = fs::path (n).stem().string();
            const auto ma = measure (xa, haveDry ? &dry : nullptr, stem), mb = measure (xb, haveDry ? &dry : nullptr, stem);
            for (size_t k = 0; k < ma.size() && k < mb.size(); ++k)
            {
                char va[32], vb[32], vd[32];
                std::snprintf (va, sizeof va, ma[k].format, ma[k].value);
                std::snprintf (vb, sizeof vb, mb[k].format, mb[k].value);
                const double d = mb[k].value - ma[k].value;
                std::snprintf (vd, sizeof vd, "%+.3g", d);
                const char* better = ma[k].better == 0 || std::abs (d) < ma[k].same ? "" : ((d > 0) == (ma[k].better > 0) ? "B" : "A");
                md += "| " + stem + " | " + ma[k].name + " | " + va + " | " + vb + " | " + vd + " | " + better + " |\n";
            }
            ++compared;
        }
        md += "\n";
    }
    md += "TP after a codec is a floor (band-limiting only). Transient retention needs `--input`.\n";
    if (! reportPath.empty())
    {
        if (std::FILE* f = std::fopen (reportPath.c_str(), "w")) { std::fputs (md.c_str(), f); std::fclose (f); }
        std::printf ("compared %d files; report in %s\n", compared, reportPath.c_str());
    }
    else std::fputs (md.c_str(), stdout);
    return compared > 0 ? 0 : 1;
}

// ---------------------------------------------------------------- self-test

int selftest()
{
    int failures = 0, checks = 0;
    auto check = [&] (bool ok, const char* what, double v) { ++checks; if (! ok) { ++failures; std::printf ("FAIL: %s (%.6g)\n", what, v); } };
    const size_t from = (size_t) kRate, to = (size_t) (3 * kRate);

    {   // THD+N reads a known third harmonic
        auto a = tones (3.0, { { 1000, 0.5 }, { 3000, 0.005 } });
        const double v = thdN (a.l, kRate, 1000, from, to);
        check (std::abs (v - 0.01) < 0.0002, "THD+N of 1% third harmonic", v);
        const double h = thd (a.l, kRate, 1000, from, to, 7);
        check (std::abs (h - 0.01) < 0.0002, "THD of 1% third harmonic", h);
        auto clean = tones (3.0, { { 1000, 0.5 } });
        check (thdN (clean.l, kRate, 1000, from, to) < 1e-6, "THD+N of a clean sine", thdN (clean.l, kRate, 1000, from, to));
    }
    {   // SMPTE: sidebands of 0.5% each side at 60 Hz
        auto a = tones (3.0, { { 60, 0.4 }, { 7000, 0.1 }, { 6940, 0.0005 }, { 7060, 0.0005 } });
        const double v = smpte (a.l, kRate, from, to);
        check (std::abs (v - 0.00707) < 0.0002, "SMPTE IMD of two 0.5% sidebands", v);
    }
    {   // CCIF: a 1 kHz product 60 dB under the two tones
        auto a = tones (3.0, { { 19000, 0.25 }, { 20000, 0.25 }, { 1000, 0.0005 } });
        const auto [d2, d3] = ccif (a.l, kRate, from, to);
        check (std::abs (dB (d2) + 60) < 0.2, "CCIF d2 at -60 dB", dB (d2));
        check (dB (d3) < -100, "CCIF d3 of clean tones", dB (d3));
    }
    {   // transient retention: 0 for a copy (even a delayed one), negative for a clipped copy
        auto dry = drums (6.0, 3);
        Audio late = dry;
        late.l.insert (late.l.begin(), 480, 0.0); late.r.insert (late.r.begin(), 480, 0.0);
        int hits = 0;
        const double same = transients (late, dry, &hits);
        check (std::abs (same) < 0.01 && hits >= 8, "transient retention of a delayed copy", same);
        Audio clipped = dry;
        for (auto* ch : { &clipped.l, &clipped.r }) for (auto& v : *ch) v = std::tanh (3 * v) / 3;
        const double flat = transients (clipped, dry);
        check (flat < -1.0, "transient retention after clipping", flat);
    }
    {   // WAV round trip and a quick render: the BB-L1 hits its target under its ceiling
        const auto dir = fs::temp_directory_path() / "bb_bench_selftest";
        fs::remove_all (dir);
        render (dir, {}, true);
        Audio a, b;
        std::string error;
        const auto f = dir / "bbl1-dense" / "mix_lufs-11.wav";
        check (bench::readWav (f.string(), a, error), "the quick render reads back", 0);
        const auto ld = loudness (a);
        check (std::abs (ld.integrated + 11) < 0.1, "the BB-L1 lands on -11 LUFS", ld.integrated);
        check (ld.truePeak <= kCeiling + 0.05, "the BB-L1 holds -1 dBTP", ld.truePeak);
        Audio in;
        bench::readWav ((dir / "input" / "mix_lufs-11.wav").string(), in, error);
        check (in.size() == a.size(), "renders keep the input's length", (double) a.size() - (double) in.size());
        const double tr = transients (a, in);
        check (tr < 0 && tr > -12, "the BB-L1 at -11 LUFS softens the hits a little", tr);
        check (compare (dir / "bbl1-transparent", dir / "bbl1-transparent", dir / "input", (dir / "report.md").string()) == 0, "compare runs", 0);
        fs::remove_all (dir);
    }
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    std::vector<std::string> args (argv + 1, argv + argc);
    auto option = [&] (const std::string& name) -> std::string
    {
        for (size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == name) return args[i + 1];
        return {};
    };
    auto flag = [&] (const std::string& name) { return std::find (args.begin(), args.end(), name) != args.end(); };
    const std::string cmd = args.empty() ? "" : args[0];
    if (cmd == "render" && args.size() >= 2) return render (args[1], option ("--music"), flag ("--quick"));
    if (cmd == "measure" && args.size() >= 2) return measureOne (args[1], option ("--input"));
    if (cmd == "compare" && args.size() >= 3) return compare (args[1], args[2], option ("--input"), option ("--report"));
    if (cmd == "selftest") return selftest();
    std::printf ("usage:\n"
                 "  bb_bench render <dir> [--music <folder>] [--quick]\n"
                 "  bb_bench measure <file.wav> [--input <dry.wav>]\n"
                 "  bb_bench compare <dirA> <dirB> [--input <dir>] [--report <file.md>]\n"
                 "  bb_bench selftest\n");
    return 2;
}
