#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "Varispeed.h"
#include "WowFlutter.h"
#include "TapeHiss.h"
#include "NABEmphasis.h"
#include "TapeSaturator.h"
#include "TransientDetector.h"
#include "Glue.h"

class TapeSweetProcessor : public juce::AudioProcessor
{
public:
    TapeSweetProcessor();
    ~TapeSweetProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TapeSweet"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    using Filter      = juce::dsp::IIR::Filter<float>;
    using FilterCoefs = juce::dsp::IIR::Coefficients<float>;
    using DelayLine   = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>;

    NABEmphasis preEmph, deEmph;
    juce::dsp::ProcessorDuplicator<Filter, FilterCoefs> headBump;
    juce::dsp::ProcessorDuplicator<Filter, FilterCoefs> hpf30;
    juce::dsp::Oversampling<float> oversampler
        { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };

    TapeSaturator     saturator;
    Glue              glue;
    Varispeed         varispeed;
    WowFlutter        wowFlutter;
    TapeHiss          tapeHiss;
    TransientDetector transientDetector;

    DelayLine dryDelay    { 16384 };
    DelayLine bypassDelay { 16384 };

    juce::AudioBuffer<float> modBuffer;
    juce::AudioBuffer<float> transientBuffer;
    juce::AudioBuffer<float> delayedDryBuffer;

    float lastHeadBumpHz = -1.0f;
    float lastHeadBumpDb = -1000.0f;

    int    totalLatencySamples = 0;
    double currentSampleRate   = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeSweetProcessor)
};
