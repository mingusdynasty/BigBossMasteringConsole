# Roadmap

What comes after step 6, in the order it should be done. It comes out of the full review of 24 October 2026 (at `04b92eb`); file references in Phase A point at that commit. Ticked items are done on PR #7. What is still open needs Nate (the Reaper check, the Pro-L 2 renders and the listening that follows them), an Avid account (AAX), or measurements we don't have (the BB-5 curves).

## Phase A: correctness and safety

- [x] **OVER lamp** on the OUTPUT unit: lit when the output's true peak goes above the limiter's ceiling. The fader (up to +12 dB) and MONO run after the limiter (`PluginProcessor.cpp`, `processImpl`), and modules can be dragged below it.
- [x] **CEILING LOCK** on the OUTPUT unit: a final true-peak guard at the limiter's ceiling, after the fader and MONO. On by default.
- [x] **Limiter stays last**: an optional order lock, on by default.
- [x] **The assistant includes the output stage**: its plan sets the fader to 0 dB, or its renders include the fader.
- [x] **Presets leave gain staging alone**: `in_trim`, `volume` and `out_mono` are neither saved in presets nor reset by them (`applyValues (…, true)`).
- [x] **The rack follows the host**: `tickAll` re-lays out the modules when `proc.getOrder()` changes and no drag is in progress (session restore, host undo, host program change).
- [x] **Restore state quietly**: no change gestures or host notifications from `setStateInformation`.
- [x] **`presetName` thread-safe**: move it into the state tree or guard it.
- [x] **The assistant moves to the processor**: capture, worker and plan are owned by the processor, the panel is a view, and closing the window doesn't drop or block anything. Fix the race between `startCapture` and `captureBlock`.
- [ ] **Check the host program list in Reaper**: load a factory preset, tweak it, save, reopen. The session now saves the program, so the tweak should stay; if it is lost, report one program only.
- [x] **De-esser**: cap the top band below Nyquist (at 44.1 kHz it clamps from FREQ ≈ 7 kHz up), and fade the cuts on a mode change instead of clearing them.
- [x] **Preset blurbs**: drop the loudness promises that fixed drives can't keep (*Streaming Master*, *Club Master*).
- [x] **Limiter CPU**: vectorised FIR and an O(1) sliding minimum (3.2 → 2.1% at 48 kHz). All three styles still run, so a style switch stays safe.
- [x] **EQ bypass**: the bypass path goes through the EQ's own oversampler, so a bypass fade no longer dips the top end.
- [x] **Tests**: `BB_BUILD_PLUGIN_TESTS` option (on with the plugin); state round trip; preset round trip; output ceiling with the fader up and a shuffled order; editor follows the order; golden renders of every factory preset.

## Phase B: mastering essentials

- [x] **Dither and word length** on the OUTPUT unit: OFF / 24 / 16 bit, TPDF, optional noise shaping. Last in the chain, off by default.
- [x] **Level-matched compare**: a MATCH key for BYPASS ALL, so a louder path doesn't win just by being louder.
- [x] **A/B/C/D compare slots** on the master section, with COPY.
- [x] **Codec preview** in the analyser: estimated true-peak overshoot after AAC or MP3 (from band-limiting; a floor, since the encoder's noise adds a little more).
- [x] **Reference track** in the analyser: a level-matched WAV, with spectrum and loudness overlays.
- [x] **M/S on the BB-5**: one ST / MID / SIDE switch for the whole unit (the filters stay stereo).

## Phase C: hardware authenticity

- [x] **BB-2 transformers modelled on flux** (port the iron model from chocolatefudgeverb's `PlateAmp`). Re-voice Iron and Steel, and re-measure.
- [x] **BB-2 opto cell, second pass** (two time constants, level-dependent attack); stereo / dual-mono switch (LINK).
- [x] **BB-80 extras**, each opt-in: crosstalk, azimuth error, hiss; a continuous bias trim. (The head bump already followed speed.)
- [ ] **BB-5 curves** re-voiced from museq sweeps or published plots, if any can be found. Needs measurements: none we can use are published.

## Phase D: the Pro-L 2 benchmark

- [x] A `tools/bench/` renderer (`bb_bench render`): the test set through the BB-L1 at −14, −11 and −9 LUFS, in all three styles.
- [ ] Nate renders the same set through Pro-L 2 at matched settings, on his PC (`README.txt` in the render folder says how).
- [x] The comparison report (`bb_bench compare`): THD+N, IMD (SMPTE, CCIF), transient retention, low-frequency distortion, true-peak overs after a codec, loudness at equal ceiling.
- [ ] Blind ABX on the worst cases, then limiter work driven by the report. A first finding without Pro-L 2: on the bench's raw synthetic mix, TRANSPARENT stops getting louder at about −12.5 LUFS however hard it is driven (its slow release follower holds the gain down between hits); DENSE reaches −9. Worth checking against Pro-L 2's Transparent before changing anything.

## Phase E: workflow and UI

- [x] Undo and redo inside the plugin.
- [x] Double-click to reset, Ctrl-drag for fine moves, value readout while dragging.
- [x] Preset tags and search.
- [x] An exportable recall sheet (PNG).

## Phase F: formats and delivery

- [x] A Windows installer (Inno Setup, built locally) that installs into `Common Files\VST3`: `installer/BigBoss.iss`. Not yet compiled on Windows.
- [ ] AAX (Avid developer account, AAX SDK, PACE signing). Needs Nate's Avid account.
- [x] CLAP, via `clap-juce-extensions` (`-DBB_BUILD_CLAP=ON`; off by default). Builds on Linux; not yet tried in a CLAP host.

## Phase G: assistant v2

- [x] Match a reference track's tonal balance and loudness (the MATCH REF goal).
- [x] A resonance finder that suggests narrow cuts.
- [x] Stereo advice per band.
