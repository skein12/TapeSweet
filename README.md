# TapeSweet

A tape-inspired sweetener: saturation, NAB-style emphasis/de-emphasis EQ,
head bump, parallel glue, varispeed pitch shift, wow/flutter, and hiss. Aimed at
the territory of 80s pop records like *Into the Groove* where the multitrack
was played back slightly fast for sheen. Not a physically convincing tape
model, just the broad-strokes character of that production trick.

Built with JUCE 8.

## Magic-knob philosophy

Six knobs. Each one drives multiple internal stages on a designed curve so the
plugin feels musical without exposing tape-nerd parameters. The goal is "turn a
few knobs, get a finished-feeling 80s-ish sweetened sound."

| Knob    | Role                                                            |
|---------|-----------------------------------------------------------------|
| Speed   | Pitch / varispeed amount                                        |
| Natural | Varispeed quality (smoothness, anti-artifact). Nothing else.    |
| Warm    | Saturation density: drive, head bump, parallel glue compression |
| Color    | Imperfection: wow, flutter, hiss (subtle below 50 %)            |
| Mix     | True wet/dry. Mix = 0 % is full bypass (latency-matched dry)    |
| Output  | Final trim                                                      |

| Param   | Range          | Default | Notes                                    |
|---------|----------------|---------|------------------------------------------|
| Speed   | -10 to +10 %   | 0 %     | At 0 % the varispeed engine is bypassed with a constant-latency delay. |
| Natural | 0-100 %        | 60 %    | Varispeed grain length (40-80 ms), grain jitter, WSOLA search width. |
| Warm    | 0-100 %        | 25 %    | Mid-focused tape colour. Drives the saturator hard (0-14 dB) with a +0-8 dB peak at 3 kHz going in (matched cut going out), then parallel-blends a quadratic-curved 0-12 % of that heavily-saturated leg back with the pre-sat signal. Also lifts the head bump (0-3 dB) and adds hidden glue compression (0-15 %). Auto-makeup. |
| Color    | 0-100 %        | 0 %     | Tape transport imperfection. Wow (slow ~0.45 Hz capstan-rate pitch drift) + flutter (~9 Hz mechanical jitter) scale linearly. Hiss (pink-tinted noise floor that breathes with the program; auto-mutes during silence) scales quadratically so the bottom half of the knob stays clean. At Color = 0 the plugin is stable and silent; at 100 % it sounds like a worn machine. |
| Mix     | 0-100 %        | 100 %   | Wet vs latency-matched dry. Mix = 0 % is a literal bypass of the entire plugin chain. |
| Output  | -12 to +12 dB  | 0 dB    |                                          |

## Signal chain

```
Input
  +-> dry delay (latency-matched, for true Mix at end)
  +-> transient detector (drives varispeed wrap alignment + tap-ducking)
  +-> NAB pre-emphasis
  |   Drive (Warm)
  |   Tape saturator with memory feedback (oversampled 4x)
  |   NAB de-emphasis
  |   Speed-coupled head bump (peak filter, gain from Warm)
  |   Parallel glue compressor (blend from Warm)
  |   Varispeed (8-tap windowed-sinc, WSOLA-aligned wraps with transient
  |              awareness, sample-accurate wow/flutter from Color)
  |              -- or constant-latency bypass when Speed AND Color are 0
  |   Tape hiss (Color, signal-modulated)
  |   30 Hz HPF
  |   Output trim
  +-> Mix(wet, delayed_dry)
  +-> NaN/clip safety scrub
```

## Latency

`oversampler_latency + varispeed_latency`, constant regardless of param
settings. About 40 ms + a handful of samples at 44.1 kHz. Reported to the host
so DAW PDC compensates during playback.

## Download

Prebuilt binaries for macOS (universal) and Windows (x64) are attached to each
[release](https://github.com/skein12/TapeSweet/releases/latest):

- `TapeSweet-macos.zip` — AU, VST3, and standalone `.app`.
- `TapeSweet-windows.zip` — VST3 and standalone `.exe`.

Install paths:

- **macOS:** `~/Library/Audio/Plug-Ins/Components/` (AU),
  `~/Library/Audio/Plug-Ins/VST3/` (VST3). After copying, clear quarantine
  with the `xattr -dr com.apple.quarantine ...` command from the Build
  section below.
- **Windows:** copy `TapeSweet.vst3` to `C:\Program Files\Common Files\VST3\`.

### Expected warnings on first launch

The binaries are unsigned, so each OS will warn the first time you load them:

- **macOS Gatekeeper** — "TapeSweet can't be opened because the developer
  cannot be verified." The `xattr -dr com.apple.quarantine` step (Build
  section) removes the quarantine flag and prevents this.
- **Windows SmartScreen** — only fires on the standalone `TapeSweet.exe`,
  not on the VST3 loaded by your DAW. Click "More info" → "Run anyway".

## Build (macOS)

Requires CMake >= 3.22 and Xcode Command Line Tools.

```sh
git clone --recursive https://github.com/skein12/TapeSweet.git
cd TapeSweet
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Artifacts:

- `build/TapeSweet_artefacts/Release/AU/TapeSweet.component`
- `build/TapeSweet_artefacts/Release/VST3/TapeSweet.vst3`
- `build/TapeSweet_artefacts/Release/Standalone/TapeSweet.app`

`COPY_PLUGIN_AFTER_BUILD` is `FALSE`. Install manually:

```sh
cp -R build/TapeSweet_artefacts/Release/AU/TapeSweet.component ~/Library/Audio/Plug-Ins/Components/
cp -R build/TapeSweet_artefacts/Release/VST3/TapeSweet.vst3 ~/Library/Audio/Plug-Ins/VST3/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/TapeSweet.component ~/Library/Audio/Plug-Ins/VST3/TapeSweet.vst3
```

Then rescan in your DAW.

## Status

v0.11.0 - Color rename + Ableton-Overdrive-style parallel sat balance.

- Wear knob renamed to "Color" (param ID kept as "wear" so existing host
  state still loads).
- Parallel sat blend max reduced 55 % -> 12 % to match the Ableton
  Overdrive Dry/Wet recipe. To keep the 12 % audibly potent, drive max
  pushed 10 -> 14 dB and band emphasis 6 -> 8 dB. Curve on the blend is
  now quadratic so the bottom of the Warm knob stays clean and character
  only blooms in the upper half.

v0.10.0 - mid-focused saturation overhaul.

- Replaced wide-band NAB pre/de-emphasis (HF shelf at 3.2 kHz) with a
  3 kHz peak filter rolling off naturally by ~5 kHz on either side, Q 1.4.
  Saturator now sees a mid-boosted signal, so harmonics generate primarily
  in the upper-mid band - the "tape-mid" character.
- Internal parallel sat blend (0-55 %, driven by Warm). The heavily
  saturated leg is blended back with the pre-sat signal so high Warm
  values give obvious harmonic character without amplitude crush.
- Reduced varispeed grain-length jitter (3 ms -> 2 ms) and widened the
  WSOLA correlation window (2 ms -> 4 ms) to reduce stutter/pop artefacts
  on pitch-shifted dynamic content.

v0.9.0 - UI redesign and code cleanup pass.

- Two-tier knob layout: Speed and Natural as the featured large rotaries
  (the expressive featured controls), Warm and Color as smaller rotaries
  below.
- Mix is now a horizontal slider across the bottom of the window. Output
  is a vertical fader on the right (mixing-console style).
- Dead code removed: unused tap-mute accessors in Varispeed, unused `sr`
  members across modules, stale history comments referencing earlier
  versions.

v0.8.0 - magic-knob refactor.

- 6 knobs (was 7). Drive folded into Warm. Sparkle module removed.
- Natural now only controls varispeed quality (no hidden brightness).
- Warm is now the saturation density macro - drives saturator, head bump,
  AND a hidden parallel glue compressor (max 18 % blend).
- Color is back as a separate imperfection knob (wow + flutter + hiss).
- Fixed 18 kHz HF rolloff removed so Warm = 0 + Speed = 0 + Color = 0 +
  Mix = 100 % is now genuinely transparent (subject to NAB shelves
  cancelling, which they do for linear signal).
