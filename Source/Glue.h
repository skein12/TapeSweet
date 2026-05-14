#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

/*
    Parallel-blend feedforward compressor for tape "glue."

    The compressor itself is a fixed-program-dependent soft-knee design with
    moderate ratio, slow attack, and program-aware release. It is never
    user-visible - only the parallel blend amount is exposed. The intent is
    to make the wet signal feel denser and held-together at moderate-to-
    high macro settings without the user ever having to think about
    threshold / ratio / attack / release.

    Threshold: -14 dBFS (linked, peak detected)
    Ratio:     2.5 : 1
    Attack:    25 ms
    Release:   220 ms
    Makeup:    +2 dB on the compressed leg
*/
class Glue
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        attackCoef  = (float) std::exp (-1.0 / (sampleRate * 0.025));
        releaseCoef = (float) std::exp (-1.0 / (sampleRate * 0.220));
        env.assign ((size_t) numChannels, 0.0f);
    }

    void reset() noexcept
    {
        std::fill (env.begin(), env.end(), 0.0f);
    }

    // 0..1: how much of the compressed leg to blend with the original.
    // Typical musical range is 0.05..0.2; above 0.3 the compression becomes audible
    // as a compressor rather than as glue.
    void setBlend (float b) noexcept { blend = juce::jlimit (0.0f, 1.0f, b); }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (blend < 1e-4f) return;

        const int numSamples = buffer.getNumSamples();
        const int numCh      = juce::jmin ((int) env.size(), buffer.getNumChannels());

        constexpr float thresh = 0.2f;            // ~ -14 dBFS
        constexpr float ratio  = 2.5f;
        constexpr float ratioInv = 1.0f / ratio;
        constexpr float makeup = 1.26f;           // ~ +2 dB

        for (int s = 0; s < numSamples; ++s)
        {
            // Linked peak detector (max abs across channels)
            float peak = 0.0f;
            for (int c = 0; c < numCh; ++c)
                peak = juce::jmax (peak, std::abs (buffer.getSample (c, s)));

            // Envelope follower (max-of-channels into single env for stereo-linked behaviour)
            float& e = env[0];
            if (peak > e) e = attackCoef  * e + (1.0f - attackCoef)  * peak;
            else          e = releaseCoef * e + (1.0f - releaseCoef) * peak;

            // Soft-knee gain reduction. gr = (env/thresh)^((1/ratio)-1) for env > thresh
            float gr = 1.0f;
            if (e > thresh)
            {
                const float over = e / thresh;
                gr = std::pow (over, ratioInv - 1.0f);
            }

            const float compGain = gr * makeup;

            for (int c = 0; c < numCh; ++c)
            {
                const float dry = buffer.getSample (c, s);
                const float compressed = dry * compGain;
                buffer.setSample (c, s, dry + (compressed - dry) * blend);
            }
        }
    }

private:
    float attackCoef = 0.0f, releaseCoef = 0.0f;
    float blend = 0.0f;
    std::vector<float> env;
};
