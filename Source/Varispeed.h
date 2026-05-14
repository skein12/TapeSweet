#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

/*
    2-tap crossfading delay-line pitch shifter, tape-flavoured.

    Improvements over the v0.2 version:
      - Hermite/Catmull-Rom 4-point cubic interpolation (replaces linear)
      - Normalised phase [0,1) so grain length can vary at runtime
      - Per-grain length jitter — smears the grain-rate modulation from a
        periodic tone into broadband noise
      - Anti-phase inter-tap distance modulation (Dimension D trick) — the
        two read heads' spacing oscillates slowly, breaking the static comb
      - External per-sample pitch modulation input (in cents) — driven by
        WowFlutter for tape-like pitch instability
      - The "Natural" knob morphs grain length (40–160 ms) and jitter (0–8 ms)

    Latency: maxGrainSize / 2 samples (~80 ms at 160 ms grain, 44.1 kHz).
    Reported as the maximum so PDC stays constant when Natural is automated.
*/
class Varispeed
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        maxGrain = (int) (sampleRate * 0.20);   // 200 ms ceiling
        minGrain = (int) (sampleRate * 0.04);   //  40 ms floor
        currentGrain = (float) ((minGrain + maxGrain) / 2);
        bufSize = maxGrain * 3;

        buffers.assign ((size_t) numChannels, std::vector<float> ((size_t) bufSize, 0.0f));
        writeIdx = 0;
        phase    = 0.0f;
        antiphase = 0.0f;
        antiphaseRate = (float) (0.3 / sampleRate); // 0.3 Hz, normalised
        nextGrainSamples = currentGrain;
    }

    void reset()
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);
        writeIdx = 0;
        phase = 0.0f;
        antiphase = 0.0f;
    }

    void setPitchRatio (float ratio) noexcept             { ratioBase  = ratio; }
    void setPitchModulationCents (float cents) noexcept   { modCents   = cents; }
    void setNatural (float n01) noexcept                  { natural    = juce::jlimit (0.0f, 1.0f, n01); }

    int getLatencySamples() const noexcept { return maxGrain / 2; }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int numCh = juce::jmin ((int) buffers.size(), buffer.getNumChannels());

        const float targetGrain     = juce::jmap (natural, (float) minGrain, (float) maxGrain);
        const float jitterMaxSamples = natural * (float) sr * 0.008f; // up to ±8 ms

        const float grainSmooth = 0.0002f; // simple LP on grain length to avoid pops

        for (int s = 0; s < numSamples; ++s)
        {
            currentGrain += (targetGrain - currentGrain) * grainSmooth;

            for (int c = 0; c < numCh; ++c)
                buffers[(size_t) c][(size_t) writeIdx] = buffer.getSample (c, s);

            const float ratio = ratioBase * std::pow (2.0f, modCents * (1.0f / 1200.0f));
            const float deltaPhase = (ratio - 1.0f) / currentGrain;

            // Anti-phase tap distance modulation: ±0.5 % of grain length
            const float antiphaseOff = std::sin (juce::MathConstants<float>::twoPi * antiphase) * 0.005f;

            float ph1 = phase;
            float ph2 = phase + 0.5f + antiphaseOff;
            ph2 -= std::floor (ph2);

            // Window weights (Hann pair, complementary except for tiny antiphase wobble)
            const float twoPi = juce::MathConstants<float>::twoPi;
            const float w1 = 0.5f * (1.0f - std::cos (twoPi * ph1));
            const float w2 = 0.5f * (1.0f - std::cos (twoPi * ph2));

            const float lag1 = (1.0f - ph1) * currentGrain;
            const float lag2 = (1.0f - ph2) * currentGrain;
            float rp1 = (float) writeIdx - lag1;
            float rp2 = (float) writeIdx - lag2;
            while (rp1 < 0.0f) rp1 += (float) bufSize;
            while (rp2 < 0.0f) rp2 += (float) bufSize;

            for (int c = 0; c < numCh; ++c)
            {
                const float a = readHermite ((size_t) c, rp1);
                const float b = readHermite ((size_t) c, rp2);
                buffer.setSample (c, s, a * w1 + b * w2);
            }

            phase += deltaPhase;
            while (phase >= 1.0f)
            {
                phase -= 1.0f;
                // Re-jitter grain length on each wrap so the grain-rate modulation
                // becomes aperiodic noise rather than a tone
                if (jitterMaxSamples > 1.0f)
                    currentGrain = targetGrain + (random.nextFloat() - 0.5f) * 2.0f * jitterMaxSamples;
            }
            while (phase < 0.0f)
            {
                phase += 1.0f;
                if (jitterMaxSamples > 1.0f)
                    currentGrain = targetGrain + (random.nextFloat() - 0.5f) * 2.0f * jitterMaxSamples;
            }

            antiphase += antiphaseRate;
            if (antiphase >= 1.0f) antiphase -= 1.0f;

            writeIdx = (writeIdx + 1) % bufSize;
        }
    }

private:
    inline float readHermite (size_t ch, float pos) const noexcept
    {
        const auto& b = buffers[ch];
        const int i1 = (int) pos;
        const float frac = pos - (float) i1;

        auto idx = [this] (int i) { return ((i % bufSize) + bufSize) % bufSize; };
        const float x0 = b[(size_t) idx (i1 - 1)];
        const float x1 = b[(size_t) idx (i1    )];
        const float x2 = b[(size_t) idx (i1 + 1)];
        const float x3 = b[(size_t) idx (i1 + 2)];

        // Catmull-Rom / Hermite cubic
        const float c0 = x1;
        const float c1 = 0.5f * (x2 - x0);
        const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    double sr = 44100.0;
    int maxGrain = 8820, minGrain = 1764;
    int bufSize = 26460;
    float currentGrain = 5292.0f;
    float nextGrainSamples = 5292.0f;
    int writeIdx = 0;
    float phase = 0.0f;
    float antiphase = 0.0f;
    float antiphaseRate = 0.0f;

    float ratioBase = 1.0f;
    float modCents  = 0.0f;
    float natural   = 0.6f;

    juce::Random random;
    std::vector<std::vector<float>> buffers;
};
