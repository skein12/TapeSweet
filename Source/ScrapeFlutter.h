#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

/*
    Scrape flutter — the "fuzz" or "veil" on top of cymbals and HF content from
    tape singing against stationary guide pillars between the heads.

    A short delay line whose read tap is FM-modulated by 2-5 kHz band-passed
    noise. Total pitch deviation is tiny (sub-cent), but it smears HF content
    in a way that's signature to tape.
*/
class ScrapeFlutter
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        bufSize = (int) (sampleRate * 0.01); // 10 ms buffer
        baseDelay = (float) (sampleRate * 0.002); // 2 ms centre
        buffers.assign ((size_t) numChannels, std::vector<float> ((size_t) bufSize, 0.0f));
        writeIdx = 0;

        // One-pole BPF centred ~3.5 kHz via parallel HP+LP
        bpfHpCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * 2000.0 / sampleRate);
        bpfLpCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * 5000.0 / sampleRate);
    }

    void reset()
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);
        writeIdx = 0;
        bpHp = 0.0f;
        bpLp = 0.0f;
    }

    // amount 0..1; at 1.0 the modulation peaks at about ±0.5 samples
    void setAmount (float a) noexcept { amount = juce::jlimit (0.0f, 1.0f, a); }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (amount < 1e-4f) return;

        const int numSamples = buffer.getNumSamples();
        const int numCh = juce::jmin ((int) buffers.size(), buffer.getNumChannels());

        for (int s = 0; s < numSamples; ++s)
        {
            // Generate band-passed noise for modulation
            const float white = random.nextFloat() * 2.0f - 1.0f;
            bpLp = bpfLpCoef * bpLp + (1.0f - bpfLpCoef) * white;
            bpHp = bpfHpCoef * bpHp + (1.0f - bpfHpCoef) * bpLp;
            const float bpNoise = bpLp - bpHp;

            const float modSamples = bpNoise * amount * 0.25f; // tiny FM, capped to stay subtle
            const float readPos = (float) writeIdx - baseDelay + modSamples;

            for (int c = 0; c < numCh; ++c)
            {
                auto& b = buffers[(size_t) c];
                b[(size_t) writeIdx] = buffer.getSample (c, s);

                float rp = readPos;
                while (rp < 0.0f) rp += (float) bufSize;
                while (rp >= (float) bufSize) rp -= (float) bufSize;
                const int i0 = (int) rp;
                const int i1 = (i0 + 1) % bufSize;
                const float frac = rp - (float) i0;
                buffer.setSample (c, s, b[(size_t) i0] * (1.0f - frac) + b[(size_t) i1] * frac);
            }

            writeIdx = (writeIdx + 1) % bufSize;
        }
    }

private:
    double sr = 44100.0;
    int bufSize = 441;
    float baseDelay = 88.0f;
    int writeIdx = 0;
    float bpHp = 0.0f, bpLp = 0.0f;
    float bpfHpCoef = 0.0f, bpfLpCoef = 0.0f;
    float amount = 0.0f;
    juce::Random random;
    std::vector<std::vector<float>> buffers;
};
