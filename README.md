# Big Boss Mastering Console

A mastering console as one VST3: detailed loudness and level metering, and a chain modelled on mastering hardware rather than on other plugins.

![Big Boss Mastering Console](docs/screenshot.png)

## Plan

| Step | What | State |
|---|---|---|
| 1 | Foundation and metering: project, chain host, meters | this version |
| 2 | Elysia museq-style mastering parametric, filters, Studer A80 tape | next |
| 3 | Shadow Hills-style mastering compressor; our own limiter, scored against FabFilter Pro-L 2 | |
| 4 | A new multiband de-esser; the full console UI | |

None of the hardware has public schematics, so the models are voiced from published specs, manuals and measurements of the units. Each module's notes will say which.

## The chain

Five slots: **Parametric EQ, Compressor, Tape, De-esser, Limiter**. Drag a module to reorder it; click its lamp to bypass it. Only Tape is built so far. It is the chocolatefudgeverb tape model (Hot Sauce), and step 2 turns it into the A80. The other slots pass audio through untouched until their step lands.

- **Latency never changes.** It is the sum of every module's latency, whatever is bypassed or reordered, so the host's delay compensation stays put. A bypassed module's dry path is delayed by the same amount.
- **Bypass is a 10 ms crossfade**, and bypassed modules keep running, so switching one back in is seamless.
- **Reordering mutes for about 10 ms** (fade out, switch, let the old audio clear, fade in) rather than glitching.
- **Input and Output trims** sit either side of the chain (±24 dB).
- The host's own bypass delays the dry signal by the chain's latency, so A/B against the host bypass stays time-aligned.

## Metering

Switch the meters between **IN** (the signal arriving, before the input trim) and **OUT** (what leaves the console). **RESET** restarts integrated loudness, LRA, the maxima and the peak holds.

| Reading | How it is measured |
|---|---|
| Momentary, short-term, integrated | ITU-R BS.1770-4 / EBU R128: K-weighting re-derived for every sample rate, 400 ms and 3 s windows, absolute (−70 LUFS) and relative (−10 LU) gates |
| LRA | EBU Tech 3342: short-term values, −20 LU relative gate, 10th to 95th percentile |
| True peak | 4x oversampling below 88.2 kHz and 2x below 176.4 kHz, with a 32-tap-per-phase Kaiser sinc, and each crest refined with a parabola. Within 0.05 dB up to 20 kHz at 44.1 kHz; plain 4x under-reads a 20 kHz tone there by up to 0.2 dB |
| PLR | Highest true peak minus integrated loudness |
| Max S, Max M | Highest short-term and momentary since reset |
| Sample peak | Highest sample, since reset |
| Correlation, balance, S/M | Integrated over 300 ms, like a hardware correlation meter |
| Spectrum | Mid (amber, with a slow peak trace) and side (blue), 8192-point FFT, tilted 4.5 dB/oct around 1 kHz so pink noise reads flat |
| Stereo field | Goniometer with auto-gain: mono is vertical, out-of-phase is horizontal |
| History | Short-term (amber) and momentary (blue) over the last 60 s, with the integrated value dashed |

The target menu (−9 to −23 LUFS) only moves the reference line on the bars and the history. It never changes the audio.

The engine is real-time safe: nothing allocates or locks on the audio thread, gated loudness uses fixed 0.01 LU histograms (so an hour of programme costs the same as a second), and the editor reads atomics and lock-free rings.

## Building

CMake 3.22+ and a C++17 compiler. JUCE 8.0.15 is fetched automatically; to use a local checkout, add `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The VST3 lands in `build/BigBoss_artefacts/Release/VST3/`, next to a Standalone app.

Linux needs: `libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libgl1-mesa-dev`.

### Tests

- `bb_engine_tests`: K-weighting against the BS.1770 coefficient table; EBU Tech 3341 cases 1–5 (M, S and I, gating) and 15–19 (true peak); Tech 3342 cases 1–4 (LRA), all at 44.1, 48, 96 and 192 kHz; the 20 kHz true-peak accuracy; block-size independence; correlation; the chain's latency, bypass and reorder behaviour; and the tape slot's reported latency, measured by cross-correlation.
- `bb_fuzz [seconds]`: random rates, block sizes, extreme signals, bypass and order changes through the chain and both meter suites; fails on any non-finite value.
- pluginval at strictness 10: `pluginval --strictness-level 10 --validate-in-process "build/BigBoss_artefacts/Release/VST3/Big Boss Mastering Console.vst3"` (on Linux, under `xvfb-run -a`).
- `-DBB_BUILD_SNAPSHOT=ON` builds `bb_snapshot out.png [seconds]`, which runs a synthetic mix through the console and renders the UI to a PNG (the screenshot above).

GitHub Actions (`.github/workflows/build.yml`) runs **only when started by hand**: Actions → Build → Run workflow (Windows, or all platforms). It never runs on push or on pull requests.

## Code map

| Path | What |
|---|---|
| `src/meter/` | Loudness (`LoudnessMeter`), true peak (`TruePeak`), stereo (`StereoMeter`) and the bundle the plugin uses (`MeterSuite`). No JUCE |
| `src/chain/` | `Module` interface, `Chain` (order, bypass, latency), `TapeModule`. No JUCE |
| `src/dsp/` | Shared with mingusdynasty/chocolatefudgeverb, copied at `6edfcdc` (namespace `cf`): tape heads and Jiles–Atherton hysteresis, Hot Sauce saturator, tube and transformer stages, half-band filters, level match. Keep changes in step with that repo |
| `src/plugin/` | JUCE processor and editor; `ui/` holds the look, meter panel, spectrum and goniometer, and chain strip |
| `tests/`, `tools/snapshot/` | Engine tests, fuzzer, UI snapshot tool |

Personal use. JUCE is AGPLv3 unless you hold a commercial licence.
