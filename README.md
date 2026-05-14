# TapeSweet

A subtle tape-sweetening AU/VST3 plugin — emulates the sound of pushing 80s analog
multitrack tape a few percent hotter. Inspired by the warmth on records like
Madonna's *Into the Groove*.

## What it does

Signal flow per channel:

1. **Speed** — tape varispeed (pitch + formant shift, no formant preservation).
   Emulates running the reel-to-reel a few percent faster, the trick on records
   like *Like A Virgin*, *Off The Wall*, and most of Prince's 80s catalog.
   Implemented as a 2-tap crossfading delay-line pitch shifter.
2. **Head bump** — low shelf at 80 Hz (+ Warmth dB)
3. **Drive** — input gain (the "crank it hotter" stage)
4. **Saturation** — asymmetric tanh waveshaper, 4× oversampled, adds even
   and odd harmonics like tape compression
5. **Tone** — gentle 1-pole low-pass (default 16 kHz)
6. **Auto-makeup + Output trim**
7. **Dry/Wet mix** (blends post-Speed signal, so Mix < 100 % doesn't phase
   against an unshifted dry — the Speed effect is always 100 % wet)

## Parameters

| Param  | Range          | Default | Purpose                                                 |
|--------|----------------|---------|---------------------------------------------------------|
| Speed  | −10 to +10 %   | 0 %     | Tape varispeed. +5 % ≈ "Like A Virgin" territory        |
| Drive  | 0–10 dB        | 3 dB    | How hard you push the saturator                         |
| Warmth | 0–3 dB         | 1.5 dB  | Low-shelf head-bump amount                              |
| Tone   | 8–22 kHz       | 16 kHz  | HF rolloff corner                                       |
| Mix    | 0–100 %        | 100 %   | Wet/dry blend (saturation/EQ stage only)                |
| Output | −12 to +12 dB  | 0 dB    | Post trim                                               |

Latency: ~25 ms (half a grain) — reported to the host so Ableton's PDC handles
it during playback. Live monitoring will feel slightly delayed.

## Build (macOS)

Requires CMake ≥ 3.22 and Xcode Command Line Tools.

```sh
git clone --recursive https://github.com/skein12/TapeSweet.git
cd TapeSweet
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Built artifacts land in:

- `build/TapeSweet_artefacts/Release/AU/TapeSweet.component`
- `build/TapeSweet_artefacts/Release/VST3/TapeSweet.vst3`
- `build/TapeSweet_artefacts/Release/Standalone/TapeSweet.app`

The build never copies into `~/Library/Audio/Plug-Ins/` automatically
(`COPY_PLUGIN_AFTER_BUILD FALSE`). Install manually when ready.

## Status

v0.2 — added tape varispeed (Speed parameter). Still generic JUCE UI.
Custom UI and authentic wow/flutter are future work.
