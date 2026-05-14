#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

/*
    2-tap crossfading delay-line pitch shifter.

    Models analog tape varispeed: pitch and formants shift together (no
    formant preservation), exactly how playing a reel-to-reel a few percent
    faster sounds. Two read heads chase the write head through a circular
    buffer, kept half a grain apart and Hann-window crossfaded so the sum of
    window weights is always 1.0.

    Latency: grainSize / 2 samples (~25 ms at 50 ms grain, 44.1 kHz).
*/
class Varispeed
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        N  = juce::jmax (512, (int) (sampleRate * 0.05)); // 50 ms grain
        bufSize = N * 3;

        buffers.assign ((size_t) numChannels, std::vector<float> ((size_t) bufSize, 0.0f));
        writeIdx = 0;
        phase    = 0.0f;
    }

    void reset()
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);
        writeIdx = 0;
        phase = 0.0f;
    }

    void setPitchRatio (float ratio) noexcept { pitchRatio = ratio; }

    int getLatencySamples() const noexcept { return N / 2; }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int numCh = juce::jmin ((int) buffers.size(), buffer.getNumChannels());
        const float deltaPhase = pitchRatio - 1.0f;
        const float Nf = (float) N;
        const float twoPiOverN = juce::MathConstants<float>::twoPi / Nf;

        for (int s = 0; s < numSamples; ++s)
        {
            for (int c = 0; c < numCh; ++c)
                buffers[(size_t) c][(size_t) writeIdx] = buffer.getSample (c, s);

            const float ph1 = phase;
            float ph2 = phase + Nf * 0.5f;
            if (ph2 >= Nf) ph2 -= Nf;

            // Read positions: writeIdx - N + ph (so phase=0 means N samples lag)
            float rp1 = (float) writeIdx - Nf + ph1;
            float rp2 = (float) writeIdx - Nf + ph2;
            while (rp1 < 0.0f) rp1 += (float) bufSize;
            while (rp2 < 0.0f) rp2 += (float) bufSize;

            // Hann-style windows, offset by π so they sum to 1.0
            const float w1 = 0.5f * (1.0f - std::cos (twoPiOverN * ph1));
            const float w2 = 0.5f * (1.0f - std::cos (twoPiOverN * ph2));

            for (int c = 0; c < numCh; ++c)
            {
                const float a = readInterp ((size_t) c, rp1);
                const float b = readInterp ((size_t) c, rp2);
                buffer.setSample (c, s, a * w1 + b * w2);
            }

            phase += deltaPhase;
            while (phase >= Nf) phase -= Nf;
            while (phase <  0.0f) phase += Nf;

            writeIdx = (writeIdx + 1) % bufSize;
        }
    }

private:
    float readInterp (size_t ch, float pos) const noexcept
    {
        const auto& b = buffers[ch];
        const int i0 = (int) pos;
        const int i1 = (i0 + 1) % bufSize;
        const float frac = pos - (float) i0;
        return b[(size_t) i0] * (1.0f - frac) + b[(size_t) i1] * frac;
    }

    double sr = 44100.0;
    int N = 2205;
    int bufSize = 6615;
    int writeIdx = 0;
    float phase = 0.0f;
    float pitchRatio = 1.0f;
    std::vector<std::vector<float>> buffers;
};
