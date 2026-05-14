#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

/*
    Tape wow & flutter generator.

    Returns a per-sample pitch modulation value in cents that emulates a real
    tape transport's mechanical pitch instability. Real Studer A800-class
    machines spec ~0.03-0.04 % WRMS at 30 ips, so depth is small even at the
    "worn" extreme - typically ±5 cents wow, ±2 cents flutter.

    Wow:    0.4 Hz capstan-rate sine + noise-shaped brown drift (LPF ~3 Hz)
    Flutter: 6-12 Hz band-passed white noise
*/
class WowFlutter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        wowPhaseInc     = (float) (0.45 / sampleRate);   // 0.45 Hz capstan
        wowDriftCoef    = (float) std::exp (-juce::MathConstants<double>::twoPi * 3.0 / sampleRate); // ~3 Hz LP
        flutterCoef     = (float) std::exp (-juce::MathConstants<double>::twoPi * 9.0 / sampleRate); // centre ~9 Hz

        reset();
    }

    void reset()
    {
        wowPhase = 0.0f;
        wowDrift = 0.0f;
        flutter  = 0.0f;
    }

    // wow01 / flutter01 are 0..1 user amounts. At 1.0:
    //   wow:     ±5 cents
    //   flutter: ±2 cents
    void setAmounts (float wow01, float flutter01) noexcept
    {
        wowAmount     = wow01;
        flutterAmount = flutter01;
    }

    // Returns total modulation in cents
    inline float tick() noexcept
    {
        // Wow: capstan sine + slow brown drift
        wowPhase += wowPhaseInc;
        if (wowPhase >= 1.0f) wowPhase -= 1.0f;
        const float capstan = std::sin (juce::MathConstants<float>::twoPi * wowPhase);

        const float whiteForDrift = random.nextFloat() * 2.0f - 1.0f;
        wowDrift = wowDriftCoef * wowDrift + (1.0f - wowDriftCoef) * whiteForDrift;

        const float wow = (capstan * 0.6f + wowDrift * 1.4f) * 5.0f * wowAmount;

        // Flutter: filtered noise (one-pole resonator-ish via simple LPF on noise)
        const float whiteForFlutter = random.nextFloat() * 2.0f - 1.0f;
        flutter = flutterCoef * flutter + (1.0f - flutterCoef) * whiteForFlutter;
        const float flut = flutter * 2.0f * 2.0f * flutterAmount; // up to ±2 cents

        return wow + flut;
    }

private:
    double sr = 44100.0;
    float wowPhase = 0.0f, wowPhaseInc = 0.0f;
    float wowDrift = 0.0f, wowDriftCoef = 0.0f;
    float flutter = 0.0f, flutterCoef = 0.0f;
    float wowAmount = 0.0f, flutterAmount = 0.0f;
    juce::Random random;
};
