# TapeSweet

An AU/VST3 plugin that emulates pushing 80s analog multitrack tape a few percent
hotter and a few percent faster. Inspired by the sheen on records like Madonna's
*Like A Virgin*, MJ's *Off The Wall*, and Prince's catalog — where tape varispeed
plus tape compression were the secret to that "shiny" pop sound.

Built with JUCE 8. Black/minimalist UI, designed to be readable at a glance.

## Signal chain

```
Input
  → NAB pre-emphasis (HF shelf, +3 dB @ 3.2 kHz)
  → Drive
  → Asymmetric tanh saturation (4× oversampled)
  → NAB de-emphasis (HF shelf, −3 dB @ 3.2 kHz)
  → Speed-coupled head bump (peak filter, centre tracks Speed)
  → Speed-coupled gap-loss (1-pole LPF, corner tracks Speed)
  → Dry/wet mix
  → Varispeed (2-tap pitch shifter, Hermite interpolation, grain 40–100 ms,
                jittered, anti-phase tap modulation, wow/flutter-modulated)
  → Scrape flutter (HF-noise-FM'd short delay)
  → Tape hiss (pink, signal-modulated, auto-muted on silence)
  → 30 Hz subsonic HPF
  → Output trim
  → NaN/clip safety scrub
```

Saturation sits between NAB pre/de-emphasis, so HF content sees more drive than
LF content — the canonical reason real tape "softens transients" and adds
upper-mid colour. The head bump and gap-loss corners both track Speed, which is
the actual mechanism behind "sped-up tape sounds brighter" on real machines.

## Parameters

| Param   | Range          | Default | Purpose                                            |
|---------|----------------|---------|----------------------------------------------------|
| Speed   | −10 to +10 %   | 0 %     | Tape varispeed. +5 % ≈ "Like A Virgin" territory.  |
| Natural | 0–100 %        | 60 %    | Pitch-shift smoothness. Morphs grain length 40→100 ms and jitter 0→4 ms inside the varispeed. |
| Wear    | 0–100 %        | 15 %    | Wow + flutter depth and scrape flutter level. Tape mechanical character. |
| Hiss    | 0–100 %        | 0 %     | Pink hiss level with program-dependent gain. Auto-mutes below ~−55 dBFS. |
| Drive   | 0–10 dB        | 3 dB    | How hard you push the saturator.                   |
| Warmth  | 0–3 dB         | 1.5 dB  | Head-bump (speed-coupled peak) gain.               |
| Tone    | 8–22 kHz       | 16 kHz  | Gap-loss filter corner. Effective corner is `Tone × (1 + Speed)`, clamped below Nyquist. |
| Mix     | 0–100 %        | 100 %   | Wet/dry blend of the saturation + EQ stage only — Speed/Wear/Hiss always apply. |
| Output  | −12 to +12 dB  | 0 dB    | Post trim.                                         |

Defaults are conservative: Speed 0, Wear 15, Hiss 0. Push Speed +3 to +5 with
Drive 3-4 dB for the classic 80s pop sheen.

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

v0.4.2 — full DSP chain in place, defaults tamed after audition feedback,
two-tap echo issue at extreme Natural settings resolved.

Known limitations: granular pitch shifting has inherent artifacts on percussive
content that can't be fully eliminated by a two-tap algorithm. Future work
(v0.5+) will likely move to a phase-vocoder or PSOLA pitch shifter to push
sound quality further.
