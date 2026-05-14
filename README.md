# TapeSweet

A tape-inspired sweetener: saturation, NAB-style emphasis/de-emphasis EQ,
head bump, varispeed pitch shift, wow/flutter, HF sparkle, and hiss. Aimed at
the territory of 80s pop records like *Into the Groove* where the multitrack
was played back slightly fast for sheen - not a physically convincing tape
model, just the broad-strokes character of that production trick.

Built with JUCE 8.

## Signal chain

```
Input
  → Transient detection (peak / envelope ratio, drives varispeed alignment)
  → NAB pre-emphasis (HF shelf, +3 dB @ 3.2 kHz)
  → Drive
  → Tape saturator (oversampled 4×): program-dependent input gain,
                                       asymmetric soft-knee, memory feedback
  → NAB de-emphasis (HF shelf, −3 dB @ 3.2 kHz)
  → Speed-coupled head bump (peak filter, centre tracks Speed)
  → Fixed air rolloff (gentle 18 kHz LPF)
  → Dry/wet mix
  → Varispeed: 8-tap windowed-sinc, WSOLA-aligned wraps with wider search
                near transients, predictive tap-ducking so transients don't
                duplicate, sample-accurate wow/flutter
  → Multi-band HF Sparkle exciter (presence 2.5-7 kHz, air 8+ kHz)
  → Tape hiss (pink, signal-modulated, auto-muted on silence)
  → 30 Hz subsonic HPF
  → Output trim
  → NaN/clip safety scrub
```

## Parameters

Six knobs, each one drives multiple DSP stages in a musically coherent way
rather than exposing every sub-parameter individually.

| Param   | Range          | Default | What it controls |
|---------|----------------|---------|------------------|
| Speed   | −10 to +10 %   | 0 %     | Tape varispeed (whole-signal pitch shift; spectral content moves up/down with it). Also drives the speed-coupled head-bump centre. |
| Natural | 0-100 %        | 60 %    | Pitch-shifter smoothness AND HF sparkle. Morphs varispeed grain length (40→80 ms), grain jitter, and multi-band exciter level. Sparkle is quadratic - blooms in the upper half. |
| Drive   | 0-10 dB        | 3 dB    | Saturator input gain. |
| Warm    | 0-100 %        | 25 %    | Tape mechanical character - head-bump gain (0-3.5 dB), wow/flutter depth (0-70%), and (at high settings, quadratically) hiss level. |
| Mix     | 0-100 %        | 100 %   | True wet/dry blend. Mix = 0 % outputs the latency-matched dry input (effective bypass); Mix = 100 % outputs the fully processed signal. |
| Output  | −12 to +12 dB  | 0 dB    | Post trim. |

Push Speed +3 to +5 with Drive 3-4 dB for the classic 80s pop sheen. Crank
Natural to 80-100 for the airy "sparkled" character; Warm 30-50 brings in
wow/flutter and head-bump body.

## Latency

~40 ms (half the 80 ms max grain length), reported to the host so the DAW's
PDC compensates during playback. Live monitoring will feel slightly delayed.

## Build (macOS)

Requires CMake ≥ 3.22 and Xcode Command Line Tools.

```sh
git clone --recursive https://github.com/skein12/TapeSweet.git
cd TapeSweet
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Built artifacts:

- `build/TapeSweet_artefacts/Release/AU/TapeSweet.component`
- `build/TapeSweet_artefacts/Release/VST3/TapeSweet.vst3`
- `build/TapeSweet_artefacts/Release/Standalone/TapeSweet.app`

`COPY_PLUGIN_AFTER_BUILD` is `FALSE` - the build never copies into
`~/Library/Audio/Plug-Ins/` automatically. Install manually:

```sh
cp -R build/TapeSweet_artefacts/Release/AU/TapeSweet.component ~/Library/Audio/Plug-Ins/Components/
cp -R build/TapeSweet_artefacts/Release/VST3/TapeSweet.vst3 ~/Library/Audio/Plug-Ins/VST3/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/TapeSweet.component ~/Library/Audio/Plug-Ins/VST3/TapeSweet.vst3
```

Then rescan in your DAW.

## Status

v0.7.0 - correctness pass after audition feedback.

- **Mix is now a true wet/dry knob.** Mix = 0 % outputs the latency-matched
  dry input (effective bypass); Mix = 100 % outputs the fully processed
  signal. Internally a DelayLine holds the dry signal delayed by exactly
  `oversampler_latency + varispeed_latency` so wet and dry are sample-
  aligned at the blend point.
- **Speed = 0 % is now transparent.** Previously the varispeed always ran,
  introducing 2-tap comb filtering even at unity pitch ratio. When Speed
  and Warm are both at zero the varispeed is bypassed in favour of a
  constant-latency delay so PDC reporting stays consistent.
- **Latency** now reported correctly as `oversampler + varispeed`
  (≈40 ms + a few samples at 44.1 kHz).
- **No more per-block allocations.** Dry buffer, mod buffer, transient
  buffer, and delayed-dry buffer are all pre-allocated in `prepareToPlay`.
  Head-bump coefficients are cached and only rebuilt when the parameters
  meaningfully change. Stray `juce::BigInteger()` line in the pink-noise
  generator removed.
