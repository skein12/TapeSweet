#pragma once

#include <juce_dsp/juce_dsp.h>

/*
    Adjustable-gain peak filter at 3 kHz, intended to bracket the saturation
    stage. With a positive-gain instance before the saturator and a matched
    negative-gain instance after, the linear signal path cancels to unity
    while the saturator sees a mid-boosted version of the input.

    The non-cancelling component is the saturator's harmonic generation:
    because mid frequencies hit the curve hardest, the generated harmonics
    sit primarily in the 2-5 kHz region (post de-emphasis), giving the
    "mid-focused tape" character. Q is moderate (1.4) so the band falls off
    naturally by ~5 kHz on either side of 3 kHz.
*/
class BandEmphasis
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        spec = { sampleRate, 512, (juce::uint32) numChannels };
        filter.prepare (spec);
        sr = sampleRate;
        updateCoefficients();
    }

    void reset() { filter.reset(); }

    // gainDb: positive for pre-emphasis, negative (matched) for de-emphasis.
    // Only rebuild the filter when the gain meaningfully changes.
    void setGainDb (float gainDb) noexcept
    {
        if (std::abs (gainDb - currentGainDb) > 0.05f)
        {
            currentGainDb = gainDb;
            updateCoefficients();
        }
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        filter.process (ctx);
    }

private:
    void updateCoefficients()
    {
        *filter.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            sr, 3000.0f, 1.4f,
            juce::Decibels::decibelsToGain (currentGainDb));
    }

    juce::dsp::ProcessSpec spec { 44100.0, 512, 2 };
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> filter;
    double sr = 44100.0;
    float currentGainDb = -1000.0f;  // forces first updateCoefficients
};
