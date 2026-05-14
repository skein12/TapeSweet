#include "PluginProcessor.h"

TapeSweetProcessor::TapeSweetProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
TapeSweetProcessor::createParameterLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<P>("drive",  "Drive",  R(0.0f, 10.0f,    0.01f), 3.0f,     "dB"));
    params.push_back (std::make_unique<P>("warmth", "Warmth", R(0.0f, 3.0f,     0.01f), 1.5f,     "dB"));
    params.push_back (std::make_unique<P>("tone",   "Tone",   R(8000.0f, 22000.0f, 1.0f), 16000.0f, "Hz"));
    params.push_back (std::make_unique<P>("mix",    "Mix",    R(0.0f, 100.0f,   0.1f),  100.0f,   "%"));
    params.push_back (std::make_unique<P>("output", "Output", R(-12.0f, 12.0f,  0.01f), 0.0f,     "dB"));
    return { params.begin(), params.end() };
}

void TapeSweetProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  2 };
    headBump.prepare (spec);
    hfRolloff.prepare (spec);
    oversampler.initProcessing ((size_t) samplesPerBlock);
    oversampler.reset();
}

void TapeSweetProcessor::releaseResources() {}

bool TapeSweetProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void TapeSweetProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    const float drive  = apvts.getRawParameterValue ("drive") ->load();
    const float warmth = apvts.getRawParameterValue ("warmth")->load();
    const float toneHz = apvts.getRawParameterValue ("tone")  ->load();
    const float mixPct = apvts.getRawParameterValue ("mix")   ->load();
    const float outDb  = apvts.getRawParameterValue ("output")->load();

    const float mix       = mixPct * 0.01f;
    const float driveGain = juce::Decibels::decibelsToGain (drive);
    const float outGain   = juce::Decibels::decibelsToGain (outDb);
    // Partial auto-makeup so the perceived level stays close as you push drive
    const float makeup    = juce::Decibels::decibelsToGain (-drive * 0.5f);

    *headBump.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        currentSampleRate, 80.0f, 0.707f,
        juce::Decibels::decibelsToGain (warmth));
    *hfRolloff.state = *juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (
        currentSampleRate, toneHz);

    juce::AudioBuffer<float> dry;
    dry.makeCopyOf (buffer);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);

    headBump.process (ctx);
    block.multiplyBy (driveGain);

    auto upBlock = oversampler.processSamplesUp (block);
    for (size_t ch = 0; ch < upBlock.getNumChannels(); ++ch)
    {
        auto* data = upBlock.getChannelPointer (ch);
        const size_t n = upBlock.getNumSamples();
        // Asymmetric tanh: small DC bias creates 2nd-harmonic colour,
        // subtracted out so we don't leave a DC offset downstream.
        const float bias = 0.1f;
        const float biasOut = std::tanh (bias);
        for (size_t i = 0; i < n; ++i)
            data[i] = std::tanh (data[i] + bias) - biasOut;
    }
    oversampler.processSamplesDown (block);

    hfRolloff.process (ctx);
    block.multiplyBy (makeup * outGain);

    if (mix < 0.999f)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* wet    = buffer.getWritePointer (ch);
            auto* dryPtr = dry.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                wet[i] = wet[i] * mix + dryPtr[i] * (1.0f - mix);
        }
    }
}

juce::AudioProcessorEditor* TapeSweetProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void TapeSweetProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        juce::MemoryOutputStream stream (destData, false);
        state.writeToStream (stream);
    }
}

void TapeSweetProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (tree.isValid())
        apvts.replaceState (tree);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TapeSweetProcessor();
}
