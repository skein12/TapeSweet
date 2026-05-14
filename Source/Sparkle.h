#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

/*
    Multi-band HF excitation. Two bands, each with its own excitation
    character:

      Presence (2.5 – 7 kHz): isolated via 1-pole BPF, lightly soft-clipped
        (tanh) to add a warm tube-style midrange character that helps
        upper-mid formants stand out without sibilance.

      Air (8+ kHz): isolated via 1-pole HPF, asymmetric half-wave rectified
        (|x|) with DC creep removed by envelope subtraction. Generates
        even-harmonic content that lives well above the source spectrum —
        the "shine" of sped-up tape.

    Both bands are mixed back into the dry signal at controlled levels.
*/
class Sparkle
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;

        const auto twoPi = juce::MathConstants<double>::twoPi;

        // Presence BPF: parallel LP @ 7k and HP @ 2.5k → band 2.5-7k
        presLpCoef = (float) std::exp (-twoPi * 7000.0  / sampleRate);
        presHpCoef = (float) std::exp (-twoPi * 2500.0  / sampleRate);

        // Air HPF: 1-pole at 8 kHz
        airHpfCoef = (float) std::exp (-twoPi * 8000.0  / sampleRate);

        // DC envelope follower for air band (30 ms)
        airEnvCoef = (float) std::exp (-1.0 / (sampleRate * 0.030));

        // Final smoothing LP @ 14 kHz on excitation to avoid pure-noise edge
        smoothCoef = (float) std::exp (-twoPi * 14000.0 / sampleRate);

        presLp.assign((size_t) numChannels, 0.0f);
        presHp.assign((size_t) numChannels, 0.0f);
        airHp.assign ((size_t) numChannels, 0.0f);
        airPrev.assign((size_t) numChannels, 0.0f);
        airEnv.assign((size_t) numChannels, 0.0f);
        smoothPres.assign((size_t) numChannels, 0.0f);
        smoothAir.assign ((size_t) numChannels, 0.0f);
    }

    void reset() noexcept
    {
        auto clear = [] (std::vector<float>& v) { std::fill (v.begin(), v.end(), 0.0f); };
        clear (presLp); clear (presHp); clear (airHp); clear (airPrev);
        clear (airEnv); clear (smoothPres); clear (smoothAir);
    }

    void setAmount (float a) noexcept { amount = juce::jlimit (0.0f, 1.0f, a); }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (amount < 1e-4f) return;

        const int numSamples = buffer.getNumSamples();
        const int numCh      = juce::jmin ((int) presLp.size(), buffer.getNumChannels());

        // Per-band gain envelopes: presence is subtle, air can be obvious
        const float presGain = amount * 0.30f;
        const float airGain  = amount * 0.70f;

        for (int c = 0; c < numCh; ++c)
        {
            auto* d = buffer.getWritePointer (c);
            auto& pLp  = presLp[(size_t) c];
            auto& pHp  = presHp[(size_t) c];
            auto& aHp  = airHp [(size_t) c];
            auto& aPrev= airPrev[(size_t) c];
            auto& aEnv = airEnv[(size_t) c];
            auto& sPres= smoothPres[(size_t) c];
            auto& sAir = smoothAir [(size_t) c];

            for (int s = 0; s < numSamples; ++s)
            {
                const float in = d[s];

                // ---- Presence band (BPF: HP @ 2.5k → LP @ 7k) ----
                pLp = presLpCoef * pLp + (1.0f - presLpCoef) * in;
                pHp = presHpCoef * pHp + (1.0f - presHpCoef) * pLp;
                const float presBand = pLp - pHp;

                // Soft tube-like saturation in the presence band
                const float presExcited = std::tanh (presBand * 2.5f) * 0.4f;
                sPres = smoothCoef * sPres + (1.0f - smoothCoef) * presExcited;

                // ---- Air band (1-pole HPF @ 8 kHz) ----
                const float airBand = airHpfCoef * (aHp + in - aPrev);
                aHp   = airBand;
                aPrev = in;

                // Even-harmonic generation + DC removal
                const float rect = std::abs (airBand) + 0.25f * std::tanh (airBand);
                aEnv = airEnvCoef * aEnv + (1.0f - airEnvCoef) * rect;
                const float airShimmer = rect - aEnv;
                sAir = smoothCoef * sAir + (1.0f - smoothCoef) * airShimmer;

                d[s] = in + sPres * presGain + sAir * airGain;
            }
        }
    }

private:
    double sr = 44100.0;
    float presLpCoef = 0.0f, presHpCoef = 0.0f;
    float airHpfCoef = 0.0f, airEnvCoef = 0.0f;
    float smoothCoef = 0.0f;
    float amount = 0.0f;

    std::vector<float> presLp, presHp;
    std::vector<float> airHp, airPrev, airEnv;
    std::vector<float> smoothPres, smoothAir;
};
