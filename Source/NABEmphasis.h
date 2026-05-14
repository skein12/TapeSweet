#pragma once

#include <juce_dsp/juce_dsp.h>

/*
    NAB-style pre/de-emphasis filter pair, intended to bracket the saturation
    stage. The pre-emphasis HF shelf is identical to the playback de-emphasis
    inverse, so a "clean" signal (no saturation in between) is restored to
    flat. With saturation in the middle, the HF content sees more drive than
    the LF content — which is the canonical reason real tape "softens
    transients" and adds upper-mid colour.
*/
class NABEmphasis
{
public:
    enum Mode { Pre, De };

    void prepare (double sampleRate, int numChannels, Mode mode)
    {
        spec = { sampleRate, 512, (juce::uint32) numChannels };
        filter.prepare (spec);
        emphasisDb = (mode == Pre) ? +6.0f : -6.0f;
        updateCoefficients (sampleRate);
    }

    void reset() { filter.reset(); }

    void process (juce::AudioBuffer<float>& buffer)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        filter.process (ctx);
    }

private:
    void updateCoefficients (double sampleRate)
    {
        // NAB record HF time constant: 50 μs -> ~3183 Hz shelf
        *filter.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, 3183.0f, 0.707f,
            juce::Decibels::decibelsToGain (emphasisDb));
    }

    juce::dsp::ProcessSpec spec { 44100.0, 512, 2 };
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> filter;
    float emphasisDb = 0.0f;
};
