#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

/*
    Tape saturator with three coordinated nonlinear stages - together they
    deliver the "glue + warmth + asymmetric harmonics" character that
    separates real tape from a generic waveshaper:

      1. Program-dependent input gain: a slow envelope follower (50 ms
         attack / 200 ms release) gently rolls input gain back when the
         signal is hot - emulating the natural compression a tape head
         applies as the medium approaches magnetic saturation.

      2. Asymmetric soft-knee curve: x · (1 / (1 + |k·x|))^(1/k) with a
         DC bias to favour 2nd-harmonic generation. The knee is gentle
         below ±0.6 and progressively tightens beyond.

      3. Leaky-integrator memory feedback: a fraction of recent output
         subtracts from the input to the curve, modelling tape hysteresis
         loops where the previous magnetic state biases the next response.

    Designed to be called at the oversampled (4×) rate.
*/
class TapeSaturator
{
public:
    void prepare (double oversampledSampleRate, int numChannels)
    {
        envAttack  = (float) std::exp (-1.0 / (oversampledSampleRate * 0.050));
        envRelease = (float) std::exp (-1.0 / (oversampledSampleRate * 0.200));
        memory.assign ((size_t) numChannels, 0.0f);
        env.assign    ((size_t) numChannels, 0.0f);
    }

    void reset() noexcept
    {
        std::fill (memory.begin(), memory.end(), 0.0f);
        std::fill (env.begin(),    env.end(),    0.0f);
    }

    inline float processSample (int channel, float x) noexcept
    {
        constexpr float bias       = 0.08f;
        constexpr float feedback   = 0.15f;
        constexpr float memDecay   = 0.55f;
        constexpr float compAmount = 0.25f; // how much hot signals get squashed
        constexpr float compThresh = 0.4f;

        auto& mem = memory[(size_t) channel];
        auto& e   = env[(size_t) channel];

        // Program-dependent gain reduction
        const float absX = std::abs (x);
        if (absX > e) e = envAttack * e + (1.0f - envAttack) * absX;
        else          e = envRelease * e + (1.0f - envRelease) * absX;

        const float gainReduction = 1.0f - compAmount * juce::jlimit (0.0f, 1.0f, (e - compThresh));

        // Asymmetric soft-knee waveshaper with memory feedback
        const float driven = x * gainReduction + bias - feedback * mem;
        const float sign   = driven >= 0.0f ? 1.0f : -1.0f;
        const float ad     = std::abs (driven);
        // x / (1 + k|x|)^(1/k) with k=2 → x / sqrt(1 + 4x²) · 2 (curve normalised)
        const float y = sign * ad / std::sqrt (1.0f + ad * ad);
        const float biasOut = bias / std::sqrt (1.0f + bias * bias);
        const float yCentred = y - biasOut;

        mem = memDecay * mem + (1.0f - memDecay) * yCentred;
        return yCentred;
    }

private:
    float envAttack = 0.0f, envRelease = 0.0f;
    std::vector<float> memory, env;
};
