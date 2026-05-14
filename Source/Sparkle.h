#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

/*
    HF "Air" exciter — synthesises even-harmonic content from the input's
    upper band, adds it back at a controlled level. Gives sped-up content
    the "shine" that real tape varispeed produces (the original recording's
    HF gets shifted up, and tape rolloff is gentler at higher speeds).

    Algorithm (Aphex-Aural-Exciter style):
      1. 1-pole HPF at 4 kHz isolates upper band.
      2. Soft asymmetric rectification (|x| + small tanh component) generates
         primarily 2nd-harmonic content at 2× the HPF passband frequencies.
      3. Subtract a slow envelope to remove DC creep.
      4. Mix the excited signal back into the input at a controlled gain.
*/
class Sparkle
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;

        const float hpfCutoff = 4000.0f;
        hpfCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * hpfCutoff / sampleRate);

        // ~30 ms envelope smoothing to chase out DC component
        envCoef = (float) std::exp (-1.0 / (sampleRate * 0.030));

        // Final HF shelf-style smoother so the excitation doesn't get too spitty
        smoothCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * 12000.0 / sampleRate);

        hp.assign  ((size_t) numChannels, 0.0f);
        prev.assign((size_t) numChannels, 0.0f);
        env.assign ((size_t) numChannels, 0.0f);
        smooth.assign((size_t) numChannels, 0.0f);
    }

    void reset() noexcept
    {
        std::fill (hp.begin(),     hp.end(),     0.0f);
        std::fill (prev.begin(),   prev.end(),   0.0f);
        std::fill (env.begin(),    env.end(),    0.0f);
        std::fill (smooth.begin(), smooth.end(), 0.0f);
    }

    void setAmount (float a) noexcept { amount = juce::jlimit (0.0f, 1.0f, a); }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (amount < 1e-4f) return;

        const int numSamples = buffer.getNumSamples();
        const int numCh      = juce::jmin ((int) hp.size(), buffer.getNumChannels());

        // Excitation gain — kept modest so the effect is "air" not "fizz"
        const float exciteGain = amount * 0.45f;

        for (int c = 0; c < numCh; ++c)
        {
            auto* d = buffer.getWritePointer (c);
            for (int s = 0; s < numSamples; ++s)
            {
                const float in = d[s];

                // 1-pole HPF: y = a * (y_prev + x - x_prev)
                const float hpOut = hpfCoef * (hp[(size_t) c] + in - prev[(size_t) c]);
                hp[(size_t) c]   = hpOut;
                prev[(size_t) c] = in;

                // Soft asymmetric rectifier → 2nd-harmonic generator
                // (|x| + 0.3*tanh(x) blends pure rectification with mild soft clip)
                const float excited = std::abs (hpOut) + 0.3f * std::tanh (hpOut);

                // Subtract slow envelope to remove DC creep
                env[(size_t) c] = envCoef * env[(size_t) c] + (1.0f - envCoef) * excited;
                float airBand = excited - env[(size_t) c];

                // Light smoothing to avoid pure-noise edge cases
                smooth[(size_t) c] = smoothCoef * smooth[(size_t) c] + (1.0f - smoothCoef) * airBand;
                airBand = smooth[(size_t) c];

                d[s] = in + airBand * exciteGain;
            }
        }
    }

private:
    double sr = 44100.0;
    float hpfCoef = 0.0f, envCoef = 0.0f, smoothCoef = 0.0f;
    float amount = 0.0f;
    std::vector<float> hp, prev, env, smooth;
};
