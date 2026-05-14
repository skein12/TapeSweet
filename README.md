# TapeSweet

An AU/VST3 plugin that emulates pushing 80s analog multitrack tape a few percent
hotter and a few percent faster — the trick behind records like *Into the Groove*
and other 80s hits where the multitrack was played back slightly fast for sheen.

Built with JUCE 8. Black/minimalist UI, designed to be readable at a glance.

## Signal chain

```
Input
  → NAB pre-emphasis (HF shelf, +3 dB @ 3.2 kHz)
  → Drive
  → Asymmetric tape saturation with memory feedback (4× oversampled)
  → NAB de-emphasis (HF shelf, −3 dB @ 3.2 kHz)
  → Speed-coupled head bump (peak filter, centre tracks Speed)
  → Speed-coupled gap-loss (1-pole LPF, corner tracks Speed)
  → Dry/wet mix
  → Varispeed: 2-tap shifter, 8-tap windowed-sinc interpolation,
                WSOLA-aligned grain wraps, sample-accurate wow/flutter
  → HF Sparkle exciter (even-harmonic synthesis post-pitch)
  → Scrape flutter (HF-noise-FM'd short delay)
  → Tape hiss (pink, signal-modulated, auto-muted on silence)
  → 30 Hz subsonic HPF
  → Output trim
  → NaN/clip safety scrub
```

Saturation sits between NAB pre/de-emphasis with a leaky-integrator memory term
that models tape hysteresis — the canonical reason real tape "softens transients"
and adds upper-mid colour. The head bump and gap-loss corners both track Speed,
which is the actual mechanism behind "sped-up tape sounds brighter" on real
machines. WSOLA grain alignment minimises the pop/glitch artefact that 2-tap
shifters typically produce at wrap boundaries.

## Parameters

Seven knobs, each one designed to do a coherent thing across multiple DSP stages
rather than expose every sub-parameter individually.

| Param   | Range          | Default | What it controls |
|---------|----------------|---------|------------------|
| Speed   | −10 to +10 %   | 0 %     | Tape varispeed. Drives pitch + formant shift, plus the speed-coupled head bump and gap-loss corners. |
| Natural | 0–100 %        | 60 %    | Pitch-shifter smoothness AND HF sparkle. Morphs varispeed grain length, grain jitter, and the HF exciter amount in lockstep. |
| Drive   | 0–10 dB        | 3 dB    | Saturator input gain. |
| Warm    | 0–100 %        | 20 %    | Tape mechanical character — head-bump gain, wow/flutter depth, scrape flutter, and (at high settings, quadratically scaled) hiss level. |
| Tone    | 8–22 kHz       | 16 kHz  | Gap-loss filter corner. Effective corner is `Tone × (1 + Speed)`, clamped below Nyquist. |
| Mix     | 0–100 %        | 100 %   | Wet/dry blend of the saturation + EQ stage only — Speed/Warm/Sparkle always apply. |
| Output  | −12 to +12 dB  | 0 dB    | Post trim. |

Defaults are conservative. Push Speed +3 to +5 with Drive 3-4 dB for the classic
80s pop sheen; bump Warm to taste for tape mechanical character.

## Latency

About 50 ms (half the max grain length of 100 ms), reported to the host so
Ableton's PDC compensates during playback. Live monitoring will feel slightly
delayed.

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
(`COPY_PLUGIN_AFTER_BUILD FALSE`). Install manually:

```sh
cp -R build/TapeSweet_artefacts/Release/AU/TapeSweet.component ~/Library/Audio/Plug-Ins/Components/
cp -R build/TapeSweet_artefacts/Release/VST3/TapeSweet.vst3 ~/Library/Audio/Plug-Ins/VST3/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/TapeSweet.component ~/Library/Audio/Plug-Ins/VST3/TapeSweet.vst3
```

Then rescan in your DAW.

## Status

v0.5.0 — major DSP overhaul. Reduced from 9 knobs to 7 (Hiss + Wear merged into
Warm; Sparkle absorbed into Natural). Algorithmic improvements:

- 8-tap windowed-sinc interpolation in the varispeed (was Hermite cubic)
- WSOLA-aligned grain wraps to eliminate pop/glitch at boundaries
- Sample-accurate wow/flutter (was block-averaged)
- Tape saturator with memory feedback (was plain tanh)
- HF "Air" exciter post-varispeed for the sparkle that sped-up tape produces
