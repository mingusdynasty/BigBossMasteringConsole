# Roadmap

What comes after step 6, in the order it should be done. It comes out of the full review of 24 October 2026 (at `04b92eb`). Each phase is meant to be one PR. File references point at that commit.

## Phase A: correctness and safety

- [ ] **OVER lamp** on the OUTPUT unit: lit when the output's true peak goes above the limiter's ceiling. The fader (up to +12 dB) and MONO run after the limiter (`PluginProcessor.cpp`, `processImpl`), and modules can be dragged below it.
- [ ] **CEILING LOCK** on the OUTPUT unit: a final true-peak guard at the limiter's ceiling, after the fader and MONO. On by default.
- [ ] **Limiter stays last**: an optional order lock, on by default.
- [ ] **The assistant includes the output stage**: its plan sets the fader to 0 dB, or its renders include the fader.
- [ ] **Presets leave gain staging alone**: `in_trim`, `volume` and `out_mono` are neither saved in presets nor reset by them (`applyValues (…, true)`).
- [ ] **The rack follows the host**: `tickAll` re-lays out the modules when `proc.getOrder()` changes and no drag is in progress (session restore, host undo, host program change).
- [ ] **Restore state quietly**: no change gestures or host notifications from `setStateInformation`.
- [ ] **`presetName` thread-safe**: move it into the state tree or guard it.
- [ ] **The assistant moves to the processor**: capture, worker and plan are owned by the processor, the panel is a view, and closing the window doesn't drop or block anything. Fix the race between `startCapture` and `captureBlock`.
- [ ] **Check the host program list in Reaper**: load a factory preset, tweak it, save, reopen. If the tweak is lost, report one program only.
- [ ] **De-esser**: cap the top band below Nyquist (at 44.1 kHz it clamps from FREQ ≈ 7 kHz up), and fade the cuts on a mode change instead of clearing them.
- [ ] **Preset blurbs**: drop the loudness promises that fixed drives can't keep (*Streaming Master*, *Club Master*).
- [ ] **Tests**: `BB_BUILD_PLUGIN_TESTS` option (on with the plugin); state round trip; preset round trip; output ceiling with the fader up and a shuffled order; editor follows the order; golden renders of every factory preset.

## Phase B: mastering essentials

- [ ] **Dither and word length** on the OUTPUT unit: OFF / 24 / 16 bit, TPDF, optional noise shaping. Last in the chain, off by default.
- [ ] **Level-matched compare**: a MATCH key for BYPASS ALL, so a louder path doesn't win just by being louder.
- [ ] **A/B/C/D compare slots** on the master section, with COPY.
- [ ] **Codec preview** in the analyser: estimated true-peak overshoot after AAC or MP3.
- [ ] **Reference track** in the analyser: a level-matched WAV, with spectrum and loudness overlays.
- [ ] **M/S on the BB-5**: STEREO / MID / SIDE per band, or one switch for the whole unit.

## Phase C: hardware authenticity

- [ ] **BB-2 transformers modelled on flux** (port the iron model from chocolatefudgeverb's `PlateAmp`). Re-voice Iron and Steel, and re-measure.
- [ ] **BB-2 opto cell, second pass** (two time constants, level-dependent attack); stereo / dual-mono switch.
- [ ] **BB-80 extras**, each opt-in: crosstalk, azimuth error, hiss; a continuous bias trim; head bump that follows speed.
- [ ] **BB-5 curves** re-voiced from museq sweeps or published plots, if any can be found.

## Phase D: the Pro-L 2 benchmark

- [ ] A `tools/bench/` renderer: the test set through the BB-L1 at −14, −11 and −9 LUFS.
- [ ] Nate renders the same set through Pro-L 2 at matched settings, on his PC.
- [ ] The comparison report: THD+N, IMD (SMPTE, CCIF), transient retention, low-frequency distortion, true-peak overs after a codec, loudness at equal ceiling.
- [ ] Blind ABX on the worst cases, then limiter work driven by the report.

## Phase E: workflow and UI

- [ ] Undo and redo inside the plugin.
- [ ] Double-click to reset, Ctrl-drag for fine moves, value readout while dragging.
- [ ] Preset tags and search.
- [ ] An exportable recall sheet (PNG or PDF).

## Phase F: formats and delivery

- [ ] A Windows installer (Inno Setup, built locally) that installs into `Common Files\VST3`.
- [ ] AAX (Avid developer account, AAX SDK, PACE signing).
- [ ] CLAP, via `clap-juce-extensions`.

## Phase G: assistant v2

- [ ] Match a reference track's tonal balance and loudness.
- [ ] A resonance finder that suggests narrow cuts.
- [ ] Stereo advice per band.
