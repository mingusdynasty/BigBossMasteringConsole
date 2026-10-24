# Technical spec

The reference for how the console is put together: signal flow, timing, parameters, saved formats, threads and tests. The README says what each control does. This file says how it's built. The last review is in the project's notes (`codebase-review-2026-10-24.md`), and the plan for what comes next is in [ROADMAP.md](ROADMAP.md).

## Signal flow

```
host in ─┬─► IN meters (MeterSuite, before trim) ─► assistant capture (when listening)
         │
         ├─► dry delay line (chain + guard latency) ─────────────────────────────────────────────────────┐
         │                                                                                               ▼
         └─► INPUT TRIM ─► chain: 5 slots ─► VOLUME ─► MONO ─► CEILING LOCK ─► DITHER ─► OVER meter ─► MATCH tracking ─► BYPASS ALL crossfade ─► REF crossfade ─► OUT meters ─► host out
                           (each: module ∥ delayed dry, 10 ms bypass fade)   (guard limiter, in its own one-slot chain)   (dry × MATCH gain)   (the reference track, looped)
```

- **Internal format**: 64-bit float, stereo. A mono layout copies L into R and returns only L. The host can call in float or double; both go through one template (`processImpl`).
- **Chunking**: the processor works in chunks of the block size it was prepared with (at least 64), so a host that sends larger blocks still works. The chain splits further if it needs to.
- **The output stage** (`PluginProcessor.cpp`, `processImpl`): the fader and MONO come after the chain, and so after the BB-L1. **CEILING LOCK** (`out_ceiling_lock`, on by default) is a second `Limiter` after them: Transparent, no drive, true peak, fully linked, at the BB-L1's ceiling. It sits in a one-slot `Chain`, so switching it crossfades against its own delayed dry path like any module bypass, and its latency counts whether it is on or not. Below the ceiling it is a pure delay. Then **dither** (`out_dither`, `src/dsp/Dither.h`), then the **OVER meter**: a `TruePeak` on what leaves the console, which reports how far it went more than 0.05 dB over the ceiling (`takeOverDb`), next to how much the lock caught (`takeLockDb`). With the lock on, the guard's −0.04 dB margin keeps the lamp dark.
- **STAY LAST** (`limit_last`, on by default): `setOrder` moves the limiter to the end of any order it is given (the others keep their order), and the audio thread re-applies that each block, so switching it on moves the limiter back.
- **BYPASS ALL** and **the host's bypass** both output the dry delay line, so they stay time-aligned with the processed signal. BYPASS ALL crossfades over 10 ms with a raised cosine and keeps the chain and both meters running. The host's bypass (`processBlockBypassed`) outputs the delayed dry signal and runs nothing else.
- **MATCH** (level-matched compare): while the console's own output is heard (no bypass, no reference) for more than 3.2 s, each chunk compares the IN and OUT meters' short-term loudness and glides (2 s) an estimate of the console's gain, clamped to ±24 dB. With MATCH on, BYPASS ALL plays the dry signal at that gain (ramped per chunk), so the untouched mix is heard as loud as the processed one.
- **REF** (reference track, `src/plugin/Reference.h`): a WAV/AIFF/FLAC up to 600 s, loaded and analysed on the message thread (resampled to the session rate with a windowed sinc; loudness, true peak, LRA, an 8192-point average spectrum and a `bb::assist::Profile`). The audio thread plays it looped, at the console's output loudness (`loudOut` minus the track's integrated loudness), crossfading over 10 ms. It reaches the track only through a `ScopedTryLock` on a `SpinLock` that guards a `shared_ptr`; a block that misses the lock plays nothing new. `prepareToPlay` re-rates it outside the lock.
- **Slots** (fixed ids, used in parameter ids and saved state): `eq`, `comp`, `tape`, `deess`, `limit`. This is the default order. A saved order lists slot ids, so a later version that adds a slot still loads old sessions.

## Modules

| Faceplate | Slot | Engine | Runs at | Notes |
|---|---|---|---|---|
| BB-5 | `eq` | `ClassAEq` (`src/eq`) | 4× below 88.2 kHz, 2× below 176.4 kHz, 1× above | Five RBJ bands with museq-style Q rules (`EqDesign.h`, shared with the scope); the bands work on both channels, the mid or the side (`eq_stereo`; a change fades every band to 0 dB, switches and fades back); 12/24 dB HP, 12 dB LP, side-only HP for mono bass; Warm adds 2nd and 3rd harmonic and a 4 Hz DC block; controls update every 32 oversampled samples with a 20 ms glide |
| BB-2 | `comp` | `DualCompressor` (`src/dyn`) | base rate | Optical stage (≈3:1, 12 dB knee; attack 10 ms, quickening to 3 ms for large moves; release in two parts, a 60 ms one and a 0.4–1.5 s one whose share, 35–85%, grows with how long the cell has worked), then the discrete stage (6 ratios, 6 attacks, 6 recoveries including Dual); detectors and cells per channel, fed the max of both channels when linked (`comp_link`, so both gains are equal) or their own channel when not (dual mono); optional 90 Hz sidechain HP; output transformers modelled on flux (`TransformerCore`, `src/dyn/Transformer.h`): a 40 Hz leaky integrator into a Jiles–Atherton core and its exact inverse, so distortion rises about 6 dB per octave as the frequency falls. Nickel, Iron (biased: mostly 2nd harmonic) and Steel (mostly 3rd) crossfade over 20 ms. At 50 Hz, −6 dBFS: 0.12%, 1.3%, 2.4% THD |
| BB-80 | `tape` | `TapeMachine` (`src/tape`) | hysteresis and emphasis oversampled like the EQ; head response and transport at base rate | Jiles–Atherton core (`src/dsp/TapeHysteresis.h`); drive calibrated per rate so 3% THD at 1 kHz falls at MOL (456 +8 dB, GP9 +12 dB); two complete paths crossfade over 30 ms on a switch change; bias trim interpolates reversibility, makeup and tilt between the bias positions (gliding block by block, half a step either way); wow and flutter as four modulated-delay components around a 24-sample centre. Opt-in, off by default: crosstalk (the other track, 100 Hz–2 kHz, −42 dB), azimuth (the right track up to 20 µs late, through the same Hermite delay), hiss (high-passed white noise, −84 dBFS RMS on 456 at 15 ips, 2 dB less on GP9 and at 30 ips, before the playback level) |
| BB-S4 | `deess` | `DeEsser` (`src/deess`) | base rate | Four `x − depth·bandpass(x)` cuts half an octave apart, capped at min(16 kHz, 0.42 × rate); a band that would sit within a quarter octave of the one below is dropped (shown as "—"); detection relative to a 300 Hz–3 kHz body; 1 ms lookahead; a mode change fades the cuts out (2 ms) before switching |
| BB-L1 | `limit` | `Limiter` (`src/dyn`) | base rate; true-peak detection 8× / 4× / 2× | 32-tap-per-phase Kaiser sinc with parabolic crest refinement; 3 ms lookahead minimum, then a style-dependent hold, two-speed release and one or two moving averages; all three styles run at once and are mixed by weight, so style changes crossfade (the costly parts, the reconstruction FIR and the sliding minimum, are shared or O(1)); −0.04 dB margin, then a hard clamp as the final guard |

### Latency (samples)

It is fixed for each sample rate, and doesn't change with bypass or order. It is reported to the host in `prepareToPlay`.

| Rate | EQ | Comp | Tape | De-esser | Limiter | CEILING LOCK | Total |
|---|---|---|---|---|---|---|---|
| 44.1 kHz | 3 | 0 | 27 | 44 | 148 | 148 | 370 (8.4 ms) |
| 48 kHz | 3 | 0 | 27 | 48 | 160 | 160 | 398 (8.3 ms) |
| 96 kHz | 3 | 0 | 27 | 96 | 304 | 304 | 734 (7.6 ms) |
| 192 kHz | 0 | 0 | 24 | 192 | 592 | 592 | 1400 (7.3 ms) |

The EQ's and the tape's oversampling is IIR, so their figure is the low-frequency group delay, rounded. The phase above that isn't linear. For the same reason the EQ's bypass path is its own oversampler's round trip (`Module::dry`), not a plain delay: a plain delay is up to 110° out of phase with the oversampled path at 15 kHz, which dipped the top by up to 4.8 dB during a bypass fade.

### CPU (percent of one core, real time, 512-sample blocks, measured on the Linux build machine)

| Rate | EQ | Comp | Tape | De-esser | Limiter (and the same again for CEILING LOCK) | Meter suite (×2 in the plugin) |
|---|---|---|---|---|---|---|
| 48 kHz | 0.9 | 1.5 | 2.9 | 1.4 | 2.1 | 1.1 |
| 96 kHz | 1.1 | 3.1 | 4.0 | 2.7 | 3.1 | 1.7 |

The compressor went from 1.2 and 2.5 when its detectors became per channel (Phase C). The tape's extras add about 0.5 at 48 kHz when all are on. Bypassed modules keep running (so they come back in seamlessly), so these costs don't go down when a module is bypassed. The limiter was 3.2 and 4.8 before its FIR was written to vectorise and its sliding minimum lost a modulo per sample (24 October 2026). Running only the selected style would not have been safe: a style switched in cold could let overs through while it caught up.

## Parameters

There are 72 parameters, all version 1 (`juce::ParameterID { id, 1 }`). The ids are stable. Renaming one breaks saved sessions and presets, so don't. The values below are plain values. "OFF" ranges reserve the first 6% of travel (or the last 6%, for the low-pass) for 0 = off.

### Console

| Id | Name | Range | Default |
|---|---|---|---|
| `in_trim` | Input Trim | −24 … +24 dB, 0.1 | 0 |
| `volume` | Volume | −60 (= off) … +12 dB, skew 2.5 | 0 |
| `out_mono` | Output Mono | bool | off |
| `out_ceiling_lock` | Output Ceiling Lock | bool | on |
| `out_dither` | Output Dither | Off, 24-bit, 16-bit, 16-bit shaped | Off |
| `eq_bypass`, `comp_bypass`, `tape_bypass`, `deess_bypass`, `limit_bypass` | *Module* Bypass | bool | off |

### BB-5 EQ

| Id | Range | Default |
|---|---|---|
| `eq_b{0..4}_freq` | log: 20–300 Hz, 40–800 Hz, 200 Hz–4 kHz, 800 Hz–12 kHz, 2.5–28 kHz | 60, 200, 1000, 4000, 12000 Hz |
| `eq_b{0..4}_gain` | 0 … 12 dB, 0.1 | 0 |
| `eq_b{0..4}_cut` | bool (cut instead of boost) | off |
| `eq_b0_shelf`, `eq_b4_shelf` | bool | on |
| `eq_warm` | bool | off |
| `eq_hpf` | OFF, 10–300 Hz log | OFF |
| `eq_hpf_24` | bool (24 dB/oct) | off |
| `eq_lpf` | 4–30 kHz log, OFF at the top | OFF |
| `eq_mono` | OFF, 40–300 Hz log | OFF |
| `eq_stereo` | Stereo, Mid, Side (where the five bands work; the filters stay stereo) | Stereo |
| `eq_out` | −12 … +12 dB | 0 |

### BB-2 compressor

| Id | Range | Default |
|---|---|---|
| `comp_opto` | bool | on |
| `comp_opto_thr` | −40 … 0 dBFS | −16 |
| `comp_opto_gain` | −6 … +12 dB | 0 |
| `comp_vca` | bool | on |
| `comp_vca_thr` | −40 … 0 dBFS | −12 |
| `comp_ratio` | 1.2:1, 2:1, 3:1, 4:1, 6:1, Flood (20:1) | 2:1 |
| `comp_attack` | 0.1, 0.3, 1, 3, 10, 30 ms | 3 ms |
| `comp_recovery` | 100, 250, 400, 800 ms, 1.2 s, Dual | Dual |
| `comp_vca_gain` | −6 … +12 dB | 0 |
| `comp_xfmr` | Nickel, Iron, Steel | Nickel |
| `comp_sc_hpf` | bool (90 Hz) | on |
| `comp_link` | bool (off: dual mono) | on |

### BB-80 tape

| Id | Range | Default |
|---|---|---|
| `tape_speed` | 15 ips, 30 ips | 15 |
| `tape_eq` | NAB, CCIR (30 ips is always AES) | NAB |
| `tape_type` | 456, GP9 | 456 |
| `tape_bias` | Under, Nominal, Over | Nominal |
| `tape_in` | −12 … +12 dB (0 dB: −18 dBFS RMS = 0 VU) | 0 |
| `tape_out` | −12 … +12 dB | 0 |
| `tape_link` | bool (output follows input inversely) | on |
| `tape_wow` | 0 … 100 % of the A80's specified maximum | 25 |
| `tape_xtalk` | bool (crosstalk) | off |
| `tape_azimuth` | 0 … 20 µs (the right track's lag) | 0 |
| `tape_hiss` | bool | off |
| `tape_bias_trim` | −1 … +1 (half a bias step toward under … over) | 0 |

### BB-S4 de-esser

| Id | Range | Default |
|---|---|---|
| `deess_thresh` | −24 … +12 dB (band over body) | −6 |
| `deess_range` | 0 … 18 dB | 8 |
| `deess_freq` | 3–8 kHz log (lowest band) | 4.5 kHz |
| `deess_release` | 10–200 ms log | 50 |
| `deess_mode` | Stereo, Mid, Side | Mid |
| `deess_listen` | bool | off |

### BB-L1 limiter

| Id | Range | Default |
|---|---|---|
| `limit_drive` | 0 … 24 dB | 0 |
| `limit_ceiling` | −12 … 0 dBTP, 0.05 | −1 |
| `limit_style` | Transparent, Punch, Dense | Transparent |
| `limit_auto` | bool (auto release) | on |
| `limit_release` | 1–1000 ms log (manual) | 100 |
| `limit_tp` | bool (true peak; off = sample peak) | on |
| `limit_link` | 0 … 100 % | 100 |
| `limit_last` | bool (STAY LAST: the limiter stays at the end of the chain) | on |

**Session-only** (`BigBossProcessor::sessionOnly`): `in_trim`, `volume`, `out_mono`, `out_ceiling_lock`, `out_dither` and `limit_last` belong to the session. Presets neither save nor change them.

### Not parameters (editor or session only)

- **BYPASS ALL** (`BigBossProcessor::bypassAll`): atomic, not saved, so a session never opens bypassed.
- **Analyser** FREEZE, IN/OUT source and TARGET belong to the editor. Which page was showing (`editorAnalysis`) is remembered while the plugin is loaded, but not saved.
- **Chain order**, **preset name** and **host program** are saved in the state (below).
- **The assistant** (`AssistantHost`, owned by the processor): its goal, plan and undo live as long as the plugin, not the window. Not saved. The MATCH REF goal passes the loaded reference's `Profile` to `bb::assist::plan`.
- **MATCH** and **REF** play state: atomics, not saved. The reference's file is saved (below), so it comes back with the session.
- **Undo and redo** (`BigBossProcessor::undo`, `redo`): up to 100 snapshots of every parameter and the order. The editor's timer calls `checkpoint()`, which records a snapshot when an edit is pending (the last gesture ended, or a preset, plan or order change called `noteEdit`) and the state differs from the last snapshot. Restoring a session clears the history. Not saved.
- **A/B/C/D** (`selectSlot`, `copyToSlot`): four snapshots of the sound (every parameter but the session-only ones, and the order). Selecting a slot stores the current settings in the one being left; an empty slot starts as a copy.

## Saved formats

**Session state** (`getStateInformation`): the APVTS `ValueTree` as XML (tag `BigBoss`), wrapped by `copyXmlToBinary`. There is one `<PARAM id="…" value="…"/>` per parameter, plus two properties on the root:
- `order`: slot ids, comma-separated, first to run first, for example `eq,comp,tape,deess,limit`. Unknown ids are skipped, and missing slots are appended in default order.
- `preset`: the name on the preset display.
- `reference`: the reference track's full path, if one is loaded. It is reloaded on restore when the file still exists.
- a `SLOTS` child (attribute `active`), with one `SLOT` per used compare slot (`index`, `order`, and `PARAM` children).
- `program`: the factory preset the host's program list shows. A user preset or a plan leaves it where it was, so a host that pushes the program back after a restore doesn't reload a factory preset over the session.

On restore the parameters are set first, then the order (so STAY LAST has its saved value when the order is applied). Every value is written back explicitly, so a value the host left between a switch's steps is snapped to a step, but without change gestures: a restore makes no undo steps and doesn't mark the project changed. A session saved before `limit_last` existed gets it off if its limiter wasn't last, so it keeps its order.

**User preset** (`.bbpreset`): the same XML without `reference` or `SLOTS`, written as a plain file, with a `preset` attribute and an optional `tags` attribute (comma-separated, searched by the preset browser with the factory presets' built-in tags). The location is `juce::File::userApplicationDataDirectory/Big Boss Mastering Console/Presets` (`%APPDATA%\…` on Windows). A preset is saved without the session-only parameters, and loading one sets every `PARAM` it holds and the order (STAY LAST still applies), then resets every other parameter except the session-only ones to its default. Older presets that hold session-only parameters have them ignored.

**Factory presets** are compiled in (`src/plugin/Presets.cpp`). Each one names only the values it changes. They are also the host's program list.

## Threads

| Thread | Does | Talks through |
|---|---|---|
| Audio | `processBlock`: reads parameter atomics once per block into each module's `Settings`, runs the chain and both `MeterSuite`s, fills the assistant's capture buffer | `std::atomic` readings; single-producer rings (`ScopeRing`) for scope samples and loudness history; `Chain::setOrder` / `setBypassed` take effect at the next block |
| Message | Editor timer at 30 Hz (`tickAll`) reads meters and rings and drives every panel, hidden ones included, so the history has no gaps; re-lays out the rack when the order changed under it (session restore, host undo, program change) and no drag is in progress; undo checkpoints; presets; loading and analysing a reference track; the codec preview (two 127-tap linear-phase low-passes and true-peak meters, reading the OUT meter suite's sample ring); the assistant panel, a view of `AssistantHost` | `presetName` behind a `SpinLock`; the reference as a `shared_ptr` behind a `SpinLock` the audio thread only tries |
| Assistant worker (`AssistantHost`) | Waits for the capture, then `bb::assist::plan` on a copy: analysis, then 6–12 offline renders through a private `Chain`. Joined when LISTEN is pressed again or the processor goes | `state` atomic; the plan behind a mutex as a `shared_ptr`; polls `cancel` between renders |

The capture starts on the audio thread: `startCapture` only sets a request, which the next block takes and resets the position. The block writes under a `SpinLock` it only ever tries (a block that misses it skips capturing), and `takeCapture` and `prepareToPlay` take it.

Rules, which `bb_fuzz` and pluginval exercise: nothing allocates, locks or does I/O on the audio thread after `prepareToPlay`. Every module's `prepare` may allocate. `TapeMachine::prepare` also calibrates (and caches per rate, under a mutex), so it must be called off the audio thread.

## Tools and delivery

- **`bb_bench`** (`tools/bench/`, built with the tests, no JUCE): `render` writes the bench's test set as 32-bit float WAV at 48 kHz, levels over 0 dBFS intended (tones 3 s, programmes 16–20 s, each with a 0.3 s tail), plus the BB-L1's renders (drive 0, −1 dBTP, true peak, latency removed so files line up with their inputs). Programmes are gained by bisection until the DENSE style lands on the target (or as near as 30 dB of gain gets it), and every limiter gets the same file. `compare` measures what two folders share: tones from 1 to 3 s (THD+N by a least-squares sine fit; harmonics, SMPTE sidebands at 7 kHz ± 60/120/180 Hz and CCIF products at 1, 18 and 21 kHz by a Hann-windowed DFT at the exact frequencies); programmes for integrated loudness, LRA, true peak, true peak after a 255-tap Kaiser low-pass at 16 and 19.5 kHz, and transient retention (hits found in the dry input; each hit's first 10 ms peak against the 30–150 ms after it, output minus input, aligned by 1 ms envelope cross-correlation). Small differences count as ties.
- **CLAP** (`BB_BUILD_CLAP`, off by default): clap-juce-extensions pinned to a main-branch commit, because its 0.26.0 release doesn't build against JUCE 8.0.15's split of `juce_audio_processors`. Id `com.mingusdynasty.bigboss`.
- **Installer** (`installer/BigBoss.iss`, Inno Setup 6): VST3 to `{commoncf64}\VST3` (fixed), CLAP to `{commoncf64}\CLAP` when it was built, the Standalone app optional; admin rights once; user presets are left alone on uninstall.

## Tests

| Target | Built when | Covers |
|---|---|---|
| `bb_engine_tests` | `BB_BUILD_TESTS` (default on) | K-weighting; EBU Tech 3341 cases 1–5 and 15–19; Tech 3342 cases 1–4 at 44.1/48/96/192 kHz; true peak at 20 kHz; block-size independence; correlation; chain latency, bypass and reorder |
| `bb_module_tests` | `BB_BUILD_TESTS` | Dither (on the grid, TPDF 0.5 LSB rms, channels independent, shaping moves noise up); EQ bypass fade with no dip at 15 kHz; EQ flat when idle, audio matches the drawn curve within 0.1 dB, Q rules, filters, Warm, click-free switches, latency, MID and SIDE touch only their component and switch smoothly; tape calibration (3% at MOL), unity gain, latency, formulation and bias ordering, speed, head bump, wow within spec, switching, Link, crosstalk level, azimuth's mono loss at 15 kHz, hiss level by tape and speed, bias trim between the positions and without steps; reordering the full chain |
| `bb_dynamics_tests` | `BB_BUILD_TESTS` | Compressor curves, attack ordering, opto memory, the opto's two-part release and level-dependent attack, LINK against dual mono, transformer harmonics (distortion falls with frequency, Iron mostly H2, Steel mostly H3, clean when quiet, switching without a step), sidechain HP; limiter ceiling (sample and true peak) at three rates in every style, 19 kHz tone, style switching, bit-exact below the ceiling, latency, link; prints the THD table |
| `bb_deesser_tests` | `BB_BUILD_TESTS` | Bands under the cap at 44.1 kHz; mode change without a jump in the cut; bit-exact when idle, band selectivity, vowel untouched, level independence, soft "s" left alone, latency, LISTEN |
| `bb_assist_tests` | `BB_BUILD_TESTS` | Every goal within 0.5 LU and under the ceiling on synthetic mixes; compression amount; EQ direction; de-esser and mono-bass decisions; trim for a hot mix; a resonance found and cut (and none on the plain mix); wide highs pointed out; MATCH REF's loudness, tone and limiter style, and its fallback without a reference |
| `bb_fuzz [s]` | `BB_BUILD_TESTS` | Random rates, blocks, signals and settings (LINK and the tape extras included) through the chain and meters; fails on any non-finite value |
| `bb_bench selftest` | `BB_BUILD_TESTS` | The bench's measurements on signals with known answers (THD+N, SMPTE, CCIF, transient retention), then a quick render: the BB-L1 lands on its target under −1 dBTP, renders keep their length, compare runs |
| `bb_plugin_tests <golden>` | `BB_BUILD_PLUGIN_TESTS` (default on, with the plugin and tests) | BYPASS ALL bit-exact and time-aligned, click-free, recovers; host bypass aligned; MONO; session round trip (every parameter, order, name, program, no gestures); old sessions keep their order; STAY LAST; presets leave session-only settings alone, saving and loading; CEILING LOCK holds −1 dBTP with the fader at +3 dB and the limiter first, and OVER reports the overshoot with it off; the editor follows an order change; the assistant finishes and applies with the window closed; MATCH plays the bypassed mix within 1 LU of the console; a reference track loads, resamples, measures, plays level-matched, comes back with the session and clears; slots and their state round trip; undo and redo; preset search by tag and words; the codec preview's band-limits; the recall sheet's size; golden renders of every factory preset (LUFS, true peak, six band levels) against `tests/golden/presets.txt` (rewrite with `--write`) |
| `bb_snapshot` | `BB_BUILD_SNAPSHOT=ON` | Renders the rack, the assistant, the analyser and an odd-shaped window to PNGs |
| pluginval 1.0.4 | by hand | Strictness 10, in process |

Still by hand: the host program list in Reaper (load a factory preset, tweak, save, reopen).
