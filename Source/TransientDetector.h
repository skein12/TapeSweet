#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

/*
    Lightweight peak-based transient detector.

    Tracks a fast envelope follower (~1 ms) and a slow envelope follower
    (~80 ms). When the fast/slow ratio exceeds a threshold, we report a
    transient. A short refractory period prevents re-triggering on the
    decay of the same event.

    Output is a per-sample 0/1 flag - consumers can use it to force grain
    realignment, gate envelopes, or anything else that benefits from
    transient awareness.
*/
class TransientDetector
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        fastCoef = (float) std::exp (-1.0 / (sampleRate * 0.001));   // 1 ms
        slowCoef = (float) std::exp (-1.0 / (sampleRate * 0.080));   // 80 ms
        refractoryLen = (int) (sampleRate * 0.020);                  // 20 ms hold

        fast.assign ((size_t) numChannels, 0.0f);
        slow.assign ((size_t) numChannels, 0.0f);
        refractory.assign ((size_t) numChannels, 0);
    }

    void reset() noexcept
    {
        std::fill (fast.begin(), fast.end(), 0.0f);
        std::fill (slow.begin(), slow.end(), 0.0f);
        std::fill (refractory.begin(), refractory.end(), 0);
    }

    inline bool detect (int channel, float x) noexcept
    {
        const float absX = std::abs (x);
        auto& f = fast[(size_t) channel];
        auto& s = slow[(size_t) channel];
        auto& r = refractory[(size_t) channel];

        if (absX > f) f = absX;                              // instant peak grab
        else          f = fastCoef * f + (1.0f - fastCoef) * absX;
        s = slowCoef * s + (1.0f - slowCoef) * absX;

        if (r > 0) { --r; return false; }

        const bool fired = (f > s * 2.5f + 0.005f);
        if (fired) r = refractoryLen;
        return fired;
    }

private:
    float fastCoef = 0.0f, slowCoef = 0.0f;
    int refractoryLen = 0;
    std::vector<float> fast, slow;
    std::vector<int> refractory;
};
