#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

/*
    Tape hiss - pink-tinted noise generated post-effect, level-modulated by the
    program envelope so it "breathes" with the signal. Auto-mutes below the
    silence threshold so quiet passages stay quiet.

    Voss-McCartney pink noise (5-row, cheap and accurate enough).
*/
class TapeHiss
{
public:
    void prepare (double sampleRate)
    {
        envAttack  = (float) std::exp (-1.0 / (sampleRate * 0.020));  // 20 ms attack
        envRelease = (float) std::exp (-1.0 / (sampleRate * 0.150));  // 150 ms release
        reset();
    }

    void reset()
    {
        for (auto& v : pinkRows) v = 0.0f;
        env = 0.0f;
    }

    // 0..1 user amount. At 1.0, base hiss ~ -55 dBFS, with up to +6 dB program-dependent boost.
    void setAmount (float a) noexcept { amount = juce::jlimit (0.0f, 1.0f, a); }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (amount < 1e-4f) return;

        const int numSamples = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();
        const float baseLevel = amount * juce::Decibels::decibelsToGain (-55.0f);

        for (int s = 0; s < numSamples; ++s)
        {
            // Envelope follower across channels
            float peak = 0.0f;
            for (int c = 0; c < numCh; ++c)
                peak = juce::jmax (peak, std::abs (buffer.getSample (c, s)));

            if (peak > env) env = envAttack * env + (1.0f - envAttack) * peak;
            else            env = envRelease * env + (1.0f - envRelease) * peak;

            // Auto-mute: ramp hiss to zero when program is below ~-55 dBFS
            const float envDb = juce::Decibels::gainToDecibels (env + 1e-9f);
            const float gate = juce::jlimit (0.0f, 1.0f, (envDb + 55.0f) / 10.0f);

            // Modulation: louder material -> more hiss, up to +6 dB
            const float modGain = 1.0f + env * 1.0f; // peak ~2x = +6 dB at unity input

            const float hissGain = baseLevel * modGain * gate;

            for (int c = 0; c < numCh; ++c)
            {
                const float pink = nextPink();
                buffer.setSample (c, s, buffer.getSample (c, s) + pink * hissGain);
            }
        }
    }

private:
    inline float nextPink() noexcept
    {
        // Voss-McCartney with 5 rows
        pinkCounter++;
        const int trailingZeros = countTrailingZeros (pinkCounter);
        const int row = juce::jmin (trailingZeros, 4);
        pinkRows[row] = random.nextFloat() * 2.0f - 1.0f;
        float sum = 0.0f;
        for (auto v : pinkRows) sum += v;
        return sum * 0.4f;
    }

    static int countTrailingZeros (uint32_t v) noexcept
    {
        if (v == 0) return 32;
        int count = 0;
        while ((v & 1) == 0) { v >>= 1; ++count; }
        return count;
    }

    float envAttack = 0.0f, envRelease = 0.0f;
    float env = 0.0f;
    float amount = 0.0f;
    juce::Random random;
    float pinkRows[5] { 0, 0, 0, 0, 0 };
    uint32_t pinkCounter = 0;
};
