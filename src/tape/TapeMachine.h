// Two-track mastering tape machine in the manner of the Studer A80: 15 or 30 ips, NAB / CCIR / AES
// equalisation, two tape formulations, three bias settings, and the transport's wow and flutter.
//
// Signal path (oversampled to 176.4 kHz or more):
//   input -> record EQ (the standard's pre-emphasis) -> Jiles-Atherton tape -> playback EQ (its exact
//   inverse) -> back to the session rate -> playback head response (head bump, low-end roll-off, gap
//   and spacing loss, bias's HF tilt) -> crosstalk -> transport (wow and flutter, azimuth) -> hiss
//   -> output
//
// The machine's imperfections beyond wow and flutter are opt-in and start off: crosstalk between
// the tracks (the other track, band-limited to 100 Hz .. 2 kHz, 42 dB down), azimuth error (the right
// track a few microseconds late, so the top cancels a little in mono) and tape hiss (about -84 dBFS
// RMS on 456 at 15 ips, quieter on GP9 and at 30 ips; it rides the playback level, so hitting the
// tape harder with LINK on lowers it). Bias trim moves continuously between the bias positions,
// half a step either way.
//
// Calibration: 0 dB input puts a 0 VU tone (-18 dBFS RMS) at the tape's operating level. The drive is
// solved so that 3% THD (MOL) falls 8 dB above that on the 456-type tape and 12 dB above on the
// GP9-type, which is the headroom difference between those formulations. Bias changes the tape's
// reversibility at the same drive: under-bias distorts more and is brighter, over-bias is cleaner
// and duller. Response curves are voiced from published A80 specifications and plots, not measured
// from a machine.
#pragma once

#include "dsp/Oversampler.h"
#include "dsp/TapeHysteresis.h"

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace bb
{
class TapeMachine
{
public:
    enum Speed { ips15 = 0, ips30 = 1 };
    enum Standard { NAB = 0, CCIR = 1 };     // at 30 ips the machine is always AES
    enum Formulation { T456 = 0, GP9 = 1 };
    enum Bias { Under = 0, Nominal = 1, Over = 2 };

    struct Settings
    {
        int speed = ips15, standard = NAB, tape = T456, bias = Nominal;
        double inputDb = 0;    // record level, -12 .. +12
        double outputDb = 0;   // playback level, -12 .. +12
        double wow = 0.25;     // wow and flutter, 0 .. 1 (1 = the A80's specified maximum)
        bool link = true;      // output follows input inversely, so Input changes drive, not level
        bool crosstalk = false;
        double azimuthUs = 0;  // right track's lag, 0 .. 20 us
        bool hiss = false;
        double biasTrim = 0;   // -1 .. +1: half a bias step under .. over
    };

    void prepare (double sampleRate, int maxBlock);   // allocates and calibrates; off the audio thread
    void reset();
    void setSettings (const Settings& s) { target = s; }
    void process (double* l, double* r, int n);
    int latency() const { return (int) std::lround (path[0].os.latency()) + kTransportDelay; }

    // record level of the last block, mean square relative to the operating level (for VU meters)
    float recordLevel (int ch) const { return vu[ch].load (std::memory_order_relaxed); }

    // MOL above operating level, dB (for the tests and the panel legend)
    static double molDb (int tape) { return tape == GP9 ? 12.0 : 8.0; }

    static constexpr int kTransportDelay = 24;   // samples: centre of the wow/flutter delay line

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
        inline double run (int c, double x)
        {
            const double y = b0 * x + z1[c];
            z1[c] = b1 * x - a1 * y + z2[c];
            z2[c] = b2 * x - a2 * y;
            return y;
        }
        void clear() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    };
    struct FirstOrder   // y = b0 x + b1 x1 - a1 y1
    {
        double b0 = 1, b1 = 0, a1 = 0, x1[2] {}, y1[2] {};
        inline double run (int c, double x) { const double y = b0 * x + b1 * x1[c] - a1 * y1[c]; x1[c] = x; y1[c] = y; return y; }
        void clear() { x1[0] = x1[1] = y1[0] = y1[1] = 0; }
    };

    // A complete record/playback path for one setting of the switches. Two exist so that a switch
    // change crossfades from the old path to a freshly threaded new one instead of clicking.
    struct Path
    {
        int speed = -1, standard = -1, tape = -1, bias = -1;
        Oversampler os;
        cf::TapeHysteresis hyst[2];
        FirstOrder recHF, recLF, pbHF, pbLF;   // emphasis (oversampled rate) and its exact inverse
        Biquad hpf, bump, dip, gap, tilt;      // playback head response (session rate)
        double drive = 1, makeup = 1;
        double trimPos = -1;   // the bias position (0 under .. 2 over) its tape and tilt are set for
        bool matches (const Settings& s) const { return s.speed == speed && s.standard == standard && s.tape == tape && s.bias == bias; }
    };

    void configure (Path& p, const Settings& s);
    void trim (Path& p, double biasPos);   // reversibility, makeup and tilt for a bias position
    void reset (Path& p);
    void run (Path& p, double* l, double* r, int n);   // in place, session rate
    void calibrate();
    static double thdAt (double rate, double k, double c, double drive, double amp);
    static double gainAt (double rate, double k, double c, double drive, double amp);

    double sr = 48000;
    int maxBlock = 512;
    Settings target;
    Path path[2];
    int active = 0;
    double xfade = 0, xfadeStep = 0.01;   // 0 .. 1 toward the other path while fading
    bool fading = false;
    std::vector<double> altL, altR;       // the incoming path's block
    std::array<double, 2> dcIn {};
    double dcC = 0;

    // calibrated drive per formulation, makeup per formulation x bias (rate-specific)
    double driveCal[2] { 1, 1 }, makeupCal[2][3] { { 1, 1, 1 }, { 1, 1, 1 } };

    double inGain = 1, outGain = 1, glide = 0.01;
    double trimNow = 0;   // bias positions, -0.5 .. +0.5

    // crosstalk band-pass (one-pole high- and low-pass per track) and its fade
    double xtHpC = 0, xtLpC = 0, xtHp[2] {}, xtLp[2] {}, xtMix = 0;
    double azNow = 0;   // samples
    // hiss: white noise, one-pole high-passed, per track
    double hissMix = 0, hissHpC = 0, hissLp[2] {};
    unsigned hissRng[2] { 0x9e3779b9u, 0x7f4a7c15u };

    // transport
    std::array<std::vector<double>, 2> line;
    int linePos = 0, lineMask = 0;
    double wowAmt = 0, phase[4] {}, rateDrift[4] {};
    unsigned rng = 12345;

    std::atomic<float> vu[2] { { 0.0f }, { 0.0f } };
};
} // namespace bb
