# Technical spec

The reference for how the console is put together: signal flow, timing, parameters, saved formats, threads and tests. The README says what each control does. This file says how it's built. The last review is in the project's notes (`codebase-review-2026-10-24.md`), and the plan for what comes next is in [ROADMAP.md](ROADMAP.md).

## Signal flow

```
host in ─┬─► IN meters (MeterSuite, before trim) ─► assistant capture (when listening)
         │
         ├─► dry delay line (chain latency) ───────────────────────────────┐
         │                                                                 │
         └─► INPUT TRIM ─► chain: 5 slots in the user's order ─► VOLUME ─► MONO ─► BYPASS ALL crossfade ─► OUT meters ─► host out
                           (each: module ∥ delayed dry, 10 ms bypass fade)                ▲
                                                                          dry delay line ─┘
```

- **Internal format**: 64-bit float, stereo. A mono layout copies L into R and returns only L. The host can call in float or double; both go through one template (`processImpl`).
- **Chunking**: the processor works in chunks of the block size it was prepared with (at least 64), so a host that sends larger blocks still works. The chain splits further if it needs to.
- **Order of the output stage** (`PluginProcessor.cpp`): the fader and MONO come *after* the chain and so after the limiter. The limiter's ceiling is the console's ceiling only with the fader at 0 dB or below, MONO off, and the limiter last. See ROADMAP Phase A (OVER lamp, CEILING LOCK).
- **BYPASS ALL** and **the host's bypass** both output the dry delay line, so they stay time-aligned with the processed signal. BYPASS ALL crossfades over 10 ms with a raised cosine and keeps the chain and both meters running. The host's bypass (`processBlockBypassed`) outputs the delayed dry signal and runs nothing else.
- **Slots** (fixed ids, used in parameter ids and saved state): `eq`, `comp`, `tape`, `deess`, `limit`. This is the default order. A saved order lists slot ids, so a later version that adds a slot still loads old sessions.

## Modules

| Faceplate | Slot | Engine | Runs at | Notes |
|---|---|---|---|---|
| BB-5 | `eq` | `ClassAEq` (`src/eq`) | 4× below 88.2 kHz, 2× below 176.4 kHz, 1× above | Five RBJ bands with museq-style Q rules (`EqDesign.h`, shared with the scope); 12/24 dB HP, 12 dB LP, side-only HP for mono bass; Warm adds 2nd and 3rd harmonic and a 4 Hz DC block; controls update every 32 oversampled samples with a 20 ms glide |
| BB-2 | `comp` | `DualCompressor` (`src/dyn`) | base rate | Optical stage (≈3:1, 12 dB knee, release 60 ms to 1.5 s depending on history), then the discrete stage (6 ratios, 6 attacks, 6 recoveries including Dual); max-of-channels RMS detection, always linked; optional 90 Hz sidechain HP; transformer saturation on a 150 Hz one-pole low band |
| BB-80 | `tape` | `TapeMachine` (`src/tape`) | hysteresis and emphasis oversampled like the EQ; head response and transport at base rate | Jiles–Atherton core (`src/dsp/TapeHysteresis.h`); drive calibrated per rate so 3% THD at 1 kHz falls at MOL (456 +8 dB, GP9 +12 dB); two complete paths crossfade over 30 ms on a switch change; wow and flutter as four modulated-delay components around a 24-sample centre |
| BB-S4 | `deess` | `DeEsser` (`src/deess`) | base rate | Four `x − depth·bandpass(x)` cuts half an octave apart; detection relative to a 300 Hz–3 kHz body; 1 ms lookahead |
| BB-L1 | `limit` | `Limiter` (`src/dyn`) | base rate; true-peak detection 8× / 4× / 2× | 32-tap-per-phase Kaiser sinc with parabolic crest refinement; 3 ms lookahead minimum, then a style-dependent hold, two-speed release and one or two moving averages; all three styles run at once and are mixed by weight, so style changes crossfade; −0.04 dB margin, then a hard clamp as the final guard |

### Latency (samples)

It is fixed for each sample rate, and doesn't change with bypass or order. It is reported to the host in `prepareToPlay`.

| Rate | EQ | Comp | Tape | De-esser | Limiter | Total |
|---|---|---|---|---|---|---|
| 44.1 kHz | 3 | 0 | 27 | 44 | 148 | 222 (5.0 ms) |
| 48 kHz | 3 | 0 | 27 | 48 | 160 | 238 (5.0 ms) |
| 96 kHz | 3 | 0 | 27 | 96 | 304 | 430 (4.5 ms) |
| 192 kHz | 0 | 0 | 24 | 192 | 592 | 808 (4.2 ms) |

The EQ's and the tape's oversampling is IIR, so their figure is the low-frequency group delay, rounded. The phase above that isn't linear.

### CPU (percent of one core, real time, 512-sample blocks, measured on the Linux build machine)

| Rate | EQ | Comp | Tape | De-esser | Limiter | Meter suite (×2 in the plugin) |
|---|---|---|---|---|---|---|
| 48 kHz | 0.8 | 1.2 | 2.6 | 1.2 | 3.2 | 1.5 |
| 96 kHz | 1.0 | 2.5 | 3.5 | 2.6 | 4.8 | 2.1 |

Bypassed modules keep running (so they come back in seamlessly), so these costs don't go down when a module is bypassed.

## Parameters

There are 63 parameters, all version 1 (`juce::ParameterID { id, 1 }`). The ids are stable. Renaming one breaks saved sessions and presets, so don't. The values below are plain values. "OFF" ranges reserve the first 6% of travel (or the last 6%, for the low-pass) for 0 = off.

### Console

| Id | Name | Range | Default |
|---|---|---|---|
| `in_trim` | Input Trim | −24 … +24 dB, 0.1 | 0 |
| `volume` | Volume | −60 (= off) … +12 dB, skew 2.5 | 0 |
| `out_mono` | Output Mono | bool | off |
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

### Not parameters (editor or session only)

- **BYPASS ALL** (`BigBossProcessor::bypassAll`): atomic, not saved, so a session never opens bypassed.
- **Analyser** FREEZE, IN/OUT source and TARGET belong to the editor. Which page was showing (`editorAnalysis`) is remembered while the plugin is loaded, but not saved.
- **Chain order** and **preset name** are saved in the state (below).

## Saved formats

**Session state** (`getStateInformation`): the APVTS `ValueTree` as XML (tag `BigBoss`), wrapped by `copyXmlToBinary`. There is one `<PARAM id="…" value="…"/>` per parameter, plus two properties on the root:
- `order`: slot ids, comma-separated, first to run first, for example `eq,comp,tape,deess,limit`. Unknown ids are skipped, and missing slots are appended in default order.
- `preset`: the name on the preset display.

On restore the order is applied first and the parameters after, and every value is written back explicitly. That way a value the host left between a switch's steps is snapped to a step.

**User preset** (`.bbpreset`): the same XML, written as a plain file, with a `preset` attribute. The location is `juce::File::userApplicationDataDirectory/Big Boss Mastering Console/Presets` (`%APPDATA%\…` on Windows). A preset loads every `PARAM` it holds and the order, then resets every other parameter to its default. Today that includes `in_trim`, `volume` and `out_mono` (ROADMAP Phase A changes this).

**Factory presets** are compiled in (`src/plugin/Presets.cpp`). Each one names only the values it changes. They are also the host's program list.

## Threads

| Thread | Does | Talks through |
|---|---|---|
| Audio | `processBlock`: reads parameter atomics once per block into each module's `Settings`, runs the chain and both `MeterSuite`s, fills the assistant's capture buffer | `std::atomic` readings; single-producer rings (`ScopeRing`) for scope samples and loudness history; `Chain::setOrder` / `setBypassed` take effect at the next block |
| Message | Editor timer at 30 Hz (`tickAll`) reads meters and rings and drives every panel, hidden ones included, so the history has no gaps; presets; assistant UI | — |
| Assistant worker | `bb::assist::plan` on a captured copy: analysis, then 6–12 offline renders through a private `Chain` | `workerDone` atomic; polls `cancel` between renders |

Rules, which `bb_fuzz` and pluginval exercise: nothing allocates, locks or does I/O on the audio thread after `prepareToPlay`. Every module's `prepare` may allocate. `TapeMachine::prepare` also calibrates (and caches per rate, under a mutex), so it must be called off the audio thread.

## Tests

| Target | Built when | Covers |
|---|---|---|
| `bb_engine_tests` | `BB_BUILD_TESTS` (default on) | K-weighting; EBU Tech 3341 cases 1–5 and 15–19; Tech 3342 cases 1–4 at 44.1/48/96/192 kHz; true peak at 20 kHz; block-size independence; correlation; chain latency, bypass and reorder |
| `bb_module_tests` | `BB_BUILD_TESTS` | EQ flat when idle, audio matches the drawn curve within 0.1 dB, Q rules, filters, Warm, click-free switches, latency; tape calibration (3% at MOL), unity gain, latency, formulation and bias ordering, speed, head bump, wow within spec, switching, Link; reordering the full chain |
| `bb_dynamics_tests` | `BB_BUILD_TESTS` | Compressor curves, attack ordering, opto memory, transformer harmonics, sidechain HP; limiter ceiling (sample and true peak) at three rates in every style, 19 kHz tone, style switching, bit-exact below the ceiling, latency, link; prints the THD table |
| `bb_deesser_tests` | `BB_BUILD_TESTS` | Bit-exact when idle, band selectivity, vowel untouched, level independence, soft "s" left alone, latency, LISTEN |
| `bb_assist_tests` | `BB_BUILD_TESTS` | Every goal within 0.5 LU and under the ceiling on synthetic mixes; compression amount; EQ direction; de-esser and mono-bass decisions; trim for a hot mix |
| `bb_fuzz [s]` | `BB_BUILD_TESTS` | Random rates, blocks, signals and settings through the chain and meters; fails on any non-finite value |
| `bb_plugin_tests` | `BB_BUILD_SNAPSHOT=ON` only | BYPASS ALL bit-exact and time-aligned, click-free, recovers; host bypass aligned; MONO |
| `bb_snapshot` | `BB_BUILD_SNAPSHOT=ON` | Renders the rack, the assistant, the analyser and an odd-shaped window to PNGs |
| pluginval 1.0.4 | by hand | Strictness 10, in process |

Not yet covered (ROADMAP Phase A): session-state and preset round trips, the output ceiling with the fader up or the limiter not last, the editor following an order change from the host, and golden-render regression.
