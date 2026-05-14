#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <array>

/*
    Tape-flavoured pitch shifter, v0.5.

    Improvements over v0.4:
      - 8-tap Hann-windowed sinc interpolation (lookup table, 256 fractional
        positions × 8 taps). Replaces Hermite cubic. Audibly more HF on the
        shifted signal — closer to what real continuous resampling gives.
      - WSOLA-aligned grain wraps. When a read tap wraps, we search ±3 ms
        for the position with the best cross-correlation against the OTHER
        tap's current window, and use that as a persistent integer offset
        until the tap wraps again. Eliminates the "pop / glitch" at grain
        boundaries that's the main artifact of 2-tap shifters at extreme
        settings.
      - Per-sample wow/flutter modulation. Caller passes a pointer to a
        modulation buffer (cents per sample), so the pitch wobble actually
        moves at audio rate instead of being averaged across blocks.
      - Max grain reduced to 80 ms (taps 40 ms apart) — research-confirmed
        safe range where inter-tap echo blurs into the original instead of
        registering as a discrete slap-back.
*/
class Varispeed
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;

        maxGrain = (int) (sampleRate * 0.08);   // 80 ms ceiling — taps 40 ms apart max
        minGrain = (int) (sampleRate * 0.04);   // 40 ms floor

        bufSize = maxGrain * 4;                 // headroom for WSOLA search + sinc taps

        buffers.assign ((size_t) numChannels, std::vector<float> ((size_t) bufSize, 0.0f));

        writeIdx = 0;
        phase    = 0.0f;
        antiphase = 0.0f;
        antiphaseRate = (float) (0.27 / sampleRate);
        tap1Offset = 0;
        tap2Offset = 0;
        currentGrain = (float) ((minGrain + maxGrain) / 2);

        initSincTable();
    }

    void reset()
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);
        writeIdx = 0;
        phase = 0.0f;
        antiphase = 0.0f;
        tap1Offset = 0;
        tap2Offset = 0;
    }

    void setPitchRatio (float ratio) noexcept             { ratioBase = ratio; }
    void setNatural   (float n01)   noexcept              { natural   = juce::jlimit (0.0f, 1.0f, n01); }

    int getLatencySamples() const noexcept { return maxGrain / 2; }

    // modCentsPerSample: optional per-sample pitch modulation in cents.
    // If nullptr, no modulation is applied beyond the base ratio.
    void process (juce::AudioBuffer<float>& buffer,
                  const float* modCentsPerSample = nullptr) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int numCh = juce::jmin ((int) buffers.size(), buffer.getNumChannels());

        const float targetGrain      = juce::jmap (natural, (float) minGrain, (float) maxGrain);
        const float jitterMaxSamples = natural * (float) sr * 0.003f; // up to ±3 ms

        constexpr float grainSmooth = 0.0002f;
        const float twoPi = juce::MathConstants<float>::twoPi;

        for (int s = 0; s < numSamples; ++s)
        {
            currentGrain += (targetGrain - currentGrain) * grainSmooth;
            currentGrain = juce::jlimit ((float) minGrain, (float) maxGrain, currentGrain);

            for (int c = 0; c < numCh; ++c)
                buffers[(size_t) c][(size_t) writeIdx] = buffer.getSample (c, s);

            const float modCents = (modCentsPerSample != nullptr) ? modCentsPerSample[s] : 0.0f;
            const float ratio    = ratioBase * std::pow (2.0f, modCents * (1.0f / 1200.0f));
            const float deltaPhase = (ratio - 1.0f) / currentGrain;

            const float antiphaseOff = std::sin (twoPi * antiphase) * 0.002f;

            const float ph1 = phase;
            float ph2 = phase + 0.5f + antiphaseOff;
            ph2 -= std::floor (ph2);

            const float w1 = 0.5f * (1.0f - std::cos (twoPi * ph1));
            const float w2 = 0.5f * (1.0f - std::cos (twoPi * ph2));

            const float lag1 = (1.0f - ph1) * currentGrain;
            const float lag2 = (1.0f - ph2) * currentGrain;

            float rp1 = (float) writeIdx - lag1 + (float) tap1Offset;
            float rp2 = (float) writeIdx - lag2 + (float) tap2Offset;
            while (rp1 < 0.0f) rp1 += (float) bufSize;
            while (rp2 < 0.0f) rp2 += (float) bufSize;
            while (rp1 >= (float) bufSize) rp1 -= (float) bufSize;
            while (rp2 >= (float) bufSize) rp2 -= (float) bufSize;

            for (int c = 0; c < numCh; ++c)
            {
                const float a = sincRead ((size_t) c, rp1);
                const float b = sincRead ((size_t) c, rp2);
                buffer.setSample (c, s, a * w1 + b * w2);
            }

            // Phase advance + wrap detection
            const float prevPhase = phase;
            phase += deltaPhase;

            // tap1 wraps when phase crosses 0/1 boundary
            while (phase >= 1.0f)
            {
                phase -= 1.0f;
                jitterGrain (targetGrain, jitterMaxSamples);
                tap1Offset = computeWsolaOffset (1);
            }
            while (phase < 0.0f)
            {
                phase += 1.0f;
                jitterGrain (targetGrain, jitterMaxSamples);
                tap1Offset = computeWsolaOffset (1);
            }

            // tap2 wraps when phase crosses 0.5 boundary
            const bool tap2WrapUp   = (prevPhase < 0.5f && phase >= 0.5f);
            const bool tap2WrapDown = (prevPhase >= 0.5f && phase < 0.5f);
            if (tap2WrapUp || tap2WrapDown)
                tap2Offset = computeWsolaOffset (2);

            antiphase += antiphaseRate;
            if (antiphase >= 1.0f) antiphase -= 1.0f;

            writeIdx = (writeIdx + 1) % bufSize;
        }
    }

private:
    // ============================================================================
    // 8-tap windowed-sinc interpolation table.
    // 256 fractional positions × 8 taps; coefficients are (sinc * Hann window).
    // ============================================================================
    static constexpr int NUM_FRAC = 256;
    static constexpr int NUM_TAPS = 8;
    static constexpr int HALF_TAPS = NUM_TAPS / 2;

    static std::array<std::array<float, NUM_TAPS>, NUM_FRAC>& sincTable()
    {
        static std::array<std::array<float, NUM_TAPS>, NUM_FRAC> table;
        return table;
    }

    void initSincTable()
    {
        static bool initialized = false;
        if (initialized) return;
        initialized = true;

        auto& table = sincTable();
        const float pi = juce::MathConstants<float>::pi;
        for (int f = 0; f < NUM_FRAC; ++f)
        {
            const float frac = (float) f / (float) NUM_FRAC;
            for (int k = 0; k < NUM_TAPS; ++k)
            {
                const float x = (float) (k - HALF_TAPS + 1) - frac;
                float sinc_x;
                if (std::abs (x) < 1.0e-6f) sinc_x = 1.0f;
                else                        sinc_x = std::sin (pi * x) / (pi * x);

                const float window = 0.5f * (1.0f + std::cos (pi * x / (float) HALF_TAPS));
                table[(size_t) f][(size_t) k] = sinc_x * window;
            }
        }
    }

    inline float sincRead (size_t ch, float pos) const noexcept
    {
        const auto& b = buffers[ch];
        const int center = (int) pos;
        const float frac = pos - (float) center;
        int fracIdx = (int) (frac * (float) NUM_FRAC);
        if (fracIdx < 0) fracIdx = 0;
        if (fracIdx >= NUM_FRAC) fracIdx = NUM_FRAC - 1;

        const auto& coefs = sincTable()[(size_t) fracIdx];

        float sum = 0.0f;
        for (int k = 0; k < NUM_TAPS; ++k)
        {
            int idx = center + k - HALF_TAPS + 1;
            idx = ((idx % bufSize) + bufSize) % bufSize;
            sum += b[(size_t) idx] * coefs[(size_t) k];
        }
        return sum;
    }

    // ============================================================================
    // WSOLA: find a small integer offset (in samples) such that the wrapping
    // tap's new read position aligns its audio with the OTHER tap's current
    // output. Searches ±3 ms with a 2 ms correlation window. Uses channel 0
    // only and applies the same offset to all channels.
    // ============================================================================
    int computeWsolaOffset (int wrappingTap) const noexcept
    {
        const int searchHalf = (int) (sr * 0.003);  // ±3 ms
        const int corrWindow = (int) (sr * 0.002);  //  2 ms
        const int N = (int) currentGrain;

        const auto& b = buffers[0];

        // The wrapping tap is about to start reading from (writeIdx - N).
        // The other tap is currently reading from approximately (writeIdx - N/2).
        // We search around (writeIdx - N) for the position whose audio best
        // matches the audio near (writeIdx - N/2).
        const int otherTapAnchor = writeIdx - N / 2;

        float bestScore = -std::numeric_limits<float>::infinity();
        int   bestOffset = 0;

        for (int o = -searchHalf; o <= searchHalf; ++o)
        {
            float score = 0.0f;
            for (int k = 0; k < corrWindow; ++k)
            {
                const int idxA = ((writeIdx - N + o + k) % bufSize + bufSize) % bufSize;
                const int idxB = ((otherTapAnchor + k) % bufSize + bufSize) % bufSize;
                score += b[(size_t) idxA] * b[(size_t) idxB];
            }
            if (score > bestScore)
            {
                bestScore = score;
                bestOffset = o;
            }
        }

        (void) wrappingTap; // same algorithm for both taps in this implementation
        return bestOffset;
    }

    void jitterGrain (float targetGrain, float jitterMaxSamples) noexcept
    {
        if (jitterMaxSamples > 1.0f)
        {
            const float j = (random.nextFloat() - 0.5f) * 2.0f * jitterMaxSamples;
            currentGrain = juce::jlimit ((float) minGrain, (float) maxGrain, targetGrain + j);
        }
    }

    double sr = 44100.0;
    int maxGrain = 3528, minGrain = 1764;
    int bufSize = 14112;
    float currentGrain = 2646.0f;
    int writeIdx = 0;
    float phase = 0.0f;
    float antiphase = 0.0f;
    float antiphaseRate = 0.0f;

    int tap1Offset = 0;
    int tap2Offset = 0;

    float ratioBase = 1.0f;
    float natural   = 0.6f;

    juce::Random random;
    std::vector<std::vector<float>> buffers;
};
