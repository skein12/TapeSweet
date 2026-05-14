#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

/*
    Asymmetric tape saturator with single-pole memory feedback.

    Models the soft-knee, history-dependent character of magnetic tape
    saturation: the current sample's nonlinear gain depends not only on the
    instantaneous input, but on a leaky-integrator memory of recent output
    (loosely emulating the hysteresis loop). At low drive this manifests as
    gentle program-dependent compression / glue; at higher drive as the
    rounded-off transient response that defines "tape" vs. generic clipper.

    The asymmetry (DC bias of 0.1) generates 2nd-harmonic content for warmth.
    The feedback is small enough to stay stable for any input within ±2.
    Designed to be called at the oversampled rate (4×) for aliasing control.
*/
class TapeSaturator
{
public:
    void prepare (int numChannels)
    {
        memory.assign ((size_t) numChannels, 0.0f);
    }

    void reset() noexcept
    {
        std::fill (memory.begin(), memory.end(), 0.0f);
    }

    inline float processSample (int channel, float x) noexcept
    {
        constexpr float bias      = 0.10f;   // DC bias for 2nd-harmonic asymmetry
        constexpr float biasOut   = 0.09966799f; // std::tanh(0.10)
        constexpr float feedback  = 0.18f;   // memory-driven compression amount
        constexpr float memDecay  = 0.55f;   // leaky integrator pole

        const float driven = x + bias - feedback * memory[(size_t) channel];
        const float y      = std::tanh (driven) - biasOut;
        memory[(size_t) channel] = memDecay * memory[(size_t) channel] + (1.0f - memDecay) * y;
        return y;
    }

private:
    std::vector<float> memory;
};
