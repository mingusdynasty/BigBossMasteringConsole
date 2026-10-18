// Chocolate Fudge — tape echo record and playback heads.
//
// Record:   record EQ (pre-emphasis) -> 4x oversampling -> magnetic hysteresis (Jiles-Atherton) -> back to
//           the plate rate. The head's small residual DC field skews the loop slightly, as a lightly
//           magnetised head does, which adds a little even-harmonic content.
// Playback: the matching de-emphasis, exactly inverse to the record EQ, so a quiet signal comes back
//           unchanged and only loud highs pay for the boost (tape's high-frequency self-erasure).
#pragma once

#include "HalfBand.h"
#include "TapeHysteresis.h"

#include <array>

namespace cf
{
// Per-rate data shared by both channels: filters, emphasis and the drive calibration.
class TapeHeadSetup
{
public:
    // baseRate: the rate record() runs at; passbandHz: audio band to keep; oversample: 1, 2 or 4;
    // driveMin/Max: the record level range the Saturation control sweeps
    void prepare (double baseRate, double passbandHz = 11000.0, int oversample = 4,
                  double driveMin = kDriveMin, double driveMax = kDriveMax);

    // Saturation 0..1 -> record level into the tape (field per unit of signal)
    double driveFor (double sat) const;
    // makeup gain so a quiet signal passes at unity, for any saturation (interpolated table)
    double makeupFor (double sat) const;
    // low-frequency delay of the oversampling filters, in base-rate samples
    double latency() const { return lat; }

    // tape and head constants (normalised: Ms = 1, field in units of the Langevin shape a)
    static constexpr double kCoercivity = 0.4;     // loop width
    static constexpr double kReversible = 0.93;    // well-biased tape is mostly reversible
    static constexpr double kHeadField  = 0.06;    // residual DC field from a lightly magnetised head
    // record level range, at 0 VU (-18 dBFS RMS): 0.5% THD at 0%, 1.5% at the 30% default (3% about
    // 5 dB hotter), 14% at 100%; roughly the drive of the earlier tanh model, with tape's curve
    static constexpr double kDriveMin = 2.5, kDriveMax = 30.0;

    static constexpr int kStage1 = 7, kStage2 = 3;
    double hb1[kStage1] {}, hb2[kStage2] {};       // esr <-> 2 esr (steep), 2 esr <-> 4 esr (wide)
    double peB0 = 1, peB1 = 0, peA1 = 0;           // record EQ: e = b0 x + b1 x[-1] - a1 e[-1]
    double rateOS = 96000;                         // the rate the tape itself runs at
    int factor = 4;
    double restM = 0;                              // magnetisation at rest under the head's DC field

private:
    static constexpr int kCal = 21;
    std::array<double, kCal> makeup {};
    double lat = 0, dMin = kDriveMin, dMax = kDriveMax;
};

// Record and playback heads for both channels.
class TapeHeads
{
public:
    void prepare (const TapeHeadSetup& s);
    void reset();

    // signal in -> what the tape holds (plate rate). drive and makeup from TapeHeadSetup.
    void record (const double* in, double drive, double makeup, double* out);
    // what was just recorded as playback will hear it (after the playback EQ), for level detection
    double heard (int ch) const { return dtX1[ch]; }
    // playback de-emphasis for one channel
    inline double playback (int ch, double y)
    {
        // exact inverse of the record EQ (its zero becomes this filter's pole)
        const double x = (y - setup->peB1 * pbX1[ch] + setup->peA1 * pbE1[ch]) / setup->peB0;
        pbX1[ch] = x; pbE1[ch] = y;
        return x;
    }

private:
    const TapeHeadSetup* setup = nullptr;
    TapeHysteresis hyst[2];
    StereoHalfBand up1, up2, dn2, dn1;
    double reX1[2] {}, reE1[2] {};   // record EQ state
    double pbX1[2] {}, pbE1[2] {};   // playback EQ state
    double dtX1[2] {}, dtE1[2] {};   // a detector's own copy of the playback EQ
};
} // namespace cf
