# TapeSweet

An AU/VST3 plugin that emulates pushing 80s analog multitrack tape a few percent
hotter and a few percent faster — the trick behind records like *Into the Groove*
and other 80s hits where the multitrack was played back slightly fast for sheen.

Built with JUCE 8. Black/minimalist UI, designed to be readable at a glance.

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
  → Multi-band HF Sparkle exciter (presence 2.5–7 kHz, air 8+ kHz)
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
| Speed   | −10 to +10 %   | 0 %     | Tape varispeed. Drives pitch + formant shift and the speed-coupled head bump centre. |
| Natural | 0–100 %        | 60 %    | Pitch-shifter smoothness AND HF sparkle. Morphs varispeed grain length (40→80 ms), grain jitter, and multi-band exciter level. Sparkle is quadratic — blooms in the upper half. |
| Drive   | 0–10 dB        | 3 dB    | Saturator input gain. |
| Warm    | 0–100 %        | 25 %    | Tape mechanical character — head-bump gain (0–3.5 dB), wow/flutter depth (0–70%), and (at high settings, quadratically) hiss level. |
| Mix     | 0–100 %        | 100 %   | Wet/dry blend of the saturation + EQ stage only — Speed/Warm/Sparkle always apply. |
| Output  | −12 to +12 dB  | 0 dB    | Post trim. |

Push Speed +3 to +5 with Drive 3-4 dB for the classic 80s pop sheen. Crank
Natural to 80–100 for the airy "sparkled" character; Warm 30–50 brings in
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

`COPY_PLUGIN_AFTER_BUILD` is `FALSE` — the build never copies into
`~/Library/Audio/Plug-Ins/` automatically. Install manually:

```sh
cp -R build/TapeSweet_artefacts/Release/AU/TapeSweet.component ~/Library/Audio/Plug-Ins/Components/
cp -R build/TapeSweet_artefacts/Release/VST3/TapeSweet.vst3 ~/Library/Audio/Plug-Ins/VST3/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/TapeSweet.component ~/Library/Audio/Plug-Ins/VST3/TapeSweet.vst3
```

Then rescan in your DAW.

## Status

v0.6.0 — major refactor.

- Reduced from 9 knobs (v0.4) to 6 (v0.6). Each knob now drives multiple
  coordinated DSP stages.
- Transient-aware varispeed: detects transients in the input and ducks the
  read tap that would otherwise duplicate them, eliminating the percussion
  echo that's the signature artefact of 2-tap pitch shifters.
- Multi-band HF Sparkle exciter (presence + air bands) — much more audible
  shimmer when Natural is in the upper half of its range.
- Tape saturator with program-dependent compression, asymmetric soft-knee,
  and memory feedback — closer to real tape "glue" than a generic waveshaper.
- Tone parameter removed (the speed-coupled HF behaviour is now a fixed
  internal 18 kHz rolloff; user-controlled gap-loss frequency was opaque).
- ScrapeFlutter module removed — minor contribution, simpler code.
