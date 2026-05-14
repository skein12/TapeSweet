# TapeSweet

A subtle tape-sweetening AU/VST3 plugin — emulates the sound of pushing 80s analog
multitrack tape a few percent hotter. Inspired by the warmth on records like
Madonna's *Into the Groove*.

## What it does

Signal flow per channel:

1. **Head bump** — low shelf at 80 Hz (+ Warmth dB)
2. **Drive** — input gain (the "crank it hotter" stage)
3. **Saturation** — asymmetric tanh waveshaper, 4× oversampled, adds even
   and odd harmonics like tape compression
4. **Tone** — gentle 1-pole low-pass (default 16 kHz)
5. **Auto-makeup + Output trim**
6. **Dry/Wet mix**

## Parameters

| Param  | Range          | Default | Purpose                                     |
|--------|----------------|---------|---------------------------------------------|
| Drive  | 0–10 dB        | 3 dB    | How hard you push the saturator             |
| Warmth | 0–3 dB         | 1.5 dB  | Low-shelf head-bump amount                  |
| Tone   | 8–22 kHz       | 16 kHz  | HF rolloff corner                           |
| Mix    | 0–100 %        | 100 %   | Wet/dry blend                               |
| Output | −12 to +12 dB  | 0 dB    | Post trim                                   |

Defaults aim for the "+5% hotter" sweet spot — colour without obvious distortion.

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

v0.1 — generic JUCE UI, working DSP. Custom UI is a follow-up.
