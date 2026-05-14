#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <array>
#include <limits>

/*
    Tape-flavoured pitch shifter, v0.6.

    Improvements over v0.5:
      - Transient-aware tap ducking. When the caller signals a transient,
        we predict which tap will replay the transient (the one with longer
        remaining grain travel) and duck its window weight briefly at that
        future sample. The transient plays through the OTHER tap and isn't
        duplicated. Eliminates the percussion-doubling artefact that's the
        signature failure mode of 2-tap shifters.
      - Wider WSOLA search (±5 ms baseline, ±10 ms post-transient) for
        better wrap alignment on dynamic content.
      - Cleaner internal state, no unused parameters.

    Latency: maxGrain / 2 (~40 ms at 80 ms grain @ 44.1 kHz).
*/
class Varispeed
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;

        maxGrain = (int) (sampleRate * 0.08);   // 80 ms ceiling
        minGrain = (int) (sampleRate * 0.04);   // 40 ms floor
        bufSize  = maxGrain * 4;

        muteHalfSamples = (int) (sampleRate * 0.006); // 6 ms duck half-width

        buffers.assign ((size_t) numChannels, std::vector<float> ((size_t) bufSize, 0.0f));

        reset();
        initSincTable();
    }

    void reset() noexcept
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);

        writeIdx          = 0;
        phase             = 0.0f;
        antiphase         = 0.0f;
        antiphaseRate     = (float) (0.27 / sr);
        tap1Offset        = 0;
        tap2Offset        = 0;
        currentGrain      = (float) ((minGrain + maxGrain) / 2);
        sampleClock       = 0;
        tap1MuteAt        = -1;
        tap2MuteAt        = -1;
        samplesSinceTransient = std::numeric_limits<int>::max() / 2;
    }

    void setPitchRatio (float ratio) noexcept { ratioBase = ratio; }
    void setNatural   (float n01)   noexcept  { natural   = juce::jlimit (0.0f, 1.0f, n01); }

    int getLatencySamples() const noexcept { return maxGrain / 2; }

    // modCents / transientFlags are optional per-sample arrays.
    void process (juce::AudioBuffer<float>& buffer,
                  const float* modCentsPerSample = nullptr,
                  const float* transientFlags    = nullptr) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int numCh      = juce::jmin ((int) buffers.size(), buffer.getNumChannels());

        const float targetGrain      = juce::jmap (natural, (float) minGrain, (float) maxGrain);
        const float jitterMaxSamples = natural * (float) sr * 0.003f;

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

            // ---- Transient handling ----
            const bool transientNow = (transientFlags != nullptr) && (transientFlags[s] > 0.5f);
            if (transientNow)
            {
                scheduleTransientMute (phase, ratio);
                samplesSinceTransient = 0;
            }
            else if (samplesSinceTransient < std::numeric_limits<int>::max() / 2 - 1)
            {
                ++samplesSinceTransient;
            }

            const float mute1 = computeMute (tap1MuteAt, sampleClock);
            const float mute2 = computeMute (tap2MuteAt, sampleClock);

            // ---- Compute tap positions and window weights ----
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
            while (rp1 < 0.0f)               rp1 += (float) bufSize;
            while (rp1 >= (float) bufSize)   rp1 -= (float) bufSize;
            while (rp2 < 0.0f)               rp2 += (float) bufSize;
            while (rp2 >= (float) bufSize)   rp2 -= (float) bufSize;

            for (int c = 0; c < numCh; ++c)
            {
                const float a = sincRead ((size_t) c, rp1);
                const float b = sincRead ((size_t) c, rp2);
                buffer.setSample (c, s, a * w1 * mute1 + b * w2 * mute2);
            }

            // ---- Phase advance + wrap detection ----
            const float prevPhase = phase;
            phase += deltaPhase;

            while (phase >= 1.0f)
            {
                phase -= 1.0f;
                jitterGrain (targetGrain, jitterMaxSamples);
                tap1Offset = computeWsolaOffset();
            }
            while (phase < 0.0f)
            {
                phase += 1.0f;
                jitterGrain (targetGrain, jitterMaxSamples);
                tap1Offset = computeWsolaOffset();
            }

            const bool tap2Wrap = (prevPhase < 0.5f && phase >= 0.5f)
                               || (prevPhase >= 0.5f && phase < 0.5f);
            if (tap2Wrap)
                tap2Offset = computeWsolaOffset();

            antiphase += antiphaseRate;
            if (antiphase >= 1.0f) antiphase -= 1.0f;

            writeIdx = (writeIdx + 1) % bufSize;
            ++sampleClock;
        }
    }

private:
    // ============================================================================
    // Transient muting — schedule a future duck on whichever tap will replay the
    // just-detected transient. The "later" tap is the one with longer remaining
    // travel until it reaches the current write position.
    //   T_tap = currentGrain * (1 - ph) / ratio    (samples until that tap reads
    //                                                the position we just wrote)
    // ============================================================================
    void scheduleTransientMute (float ph1Now, float ratioNow) noexcept
    {
        const float ph2Now = ph1Now + 0.5f - std::floor (ph1Now + 0.5f);
        const float safeRatio = (std::abs (ratioNow) < 1e-3f) ? 1.0f : ratioNow;

        if (ph1Now < 0.5f)
        {
            // tap 2 reaches the transient first (smaller lag); tap 1 plays it later
            const int t = (int) (currentGrain * (1.0f - ph1Now) / safeRatio);
            tap1MuteAt = sampleClock + t;
        }
        else
        {
            const int t = (int) (currentGrain * (1.0f - ph2Now) / safeRatio);
            tap2MuteAt = sampleClock + t;
        }
    }

    inline float computeMute (int& muteAtRef, int now) const noexcept
    {
        if (muteAtRef < 0) return 1.0f;
        const int delta = now - muteAtRef;
        if (delta > muteHalfSamples) { muteAtRef = -1; return 1.0f; }
        if (delta < -muteHalfSamples) return 1.0f;
        // Inside window: 0 at centre, 1 at edges (smoothstep-ish via squared)
        const float d = (float) std::abs (delta) / (float) muteHalfSamples;
        return d * d * (3.0f - 2.0f * d); // smoothstep
    }

    int& tap1MuteRef() noexcept { return tap1MuteAt; }
    int& tap2MuteRef() noexcept { return tap2MuteAt; }

    // ============================================================================
    // 8-tap windowed-sinc interpolation table
    // ============================================================================
    static constexpr int NUM_FRAC  = 256;
    static constexpr int NUM_TAPS  = 8;
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
    // WSOLA: search a window around (writeIdx - N) for the position whose audio
    // best matches what the other tap is currently reading. Wider search when a
    // transient happened recently — gives the wrap more flexibility on dynamic
    // material.
    // ============================================================================
    int computeWsolaOffset() const noexcept
    {
        const int N = (int) currentGrain;
        const bool nearTransient = samplesSinceTransient < N;
        const int searchHalf = (int) (sr * (nearTransient ? 0.010 : 0.005));
        const int corrWindow = (int) (sr * 0.002);

        const auto& b = buffers[0];
        const int otherTapAnchor = writeIdx - N / 2;

        float bestScore  = -std::numeric_limits<float>::infinity();
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
            if (score > bestScore) { bestScore = score; bestOffset = o; }
        }
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

    // ---- State ----
    double sr = 44100.0;
    int maxGrain = 3528, minGrain = 1764, bufSize = 14112;
    int muteHalfSamples = 264;

    float currentGrain = 2646.0f;
    int writeIdx = 0;
    float phase = 0.0f;
    float antiphase = 0.0f;
    float antiphaseRate = 0.0f;

    int tap1Offset = 0;
    int tap2Offset = 0;

    int sampleClock = 0;
    mutable int tap1MuteAt = -1;
    mutable int tap2MuteAt = -1;

    int samplesSinceTransient = 0;

    float ratioBase = 1.0f;
    float natural   = 0.6f;

    juce::Random random;
    std::vector<std::vector<float>> buffers;
};
