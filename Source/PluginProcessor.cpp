#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    params.push_back (std::make_unique<P>("speed",   "Speed",   R(-10.0f, 10.0f,  0.01f),  0.0f,  "%"));
    params.push_back (std::make_unique<P>("natural", "Natural", R(0.0f,   100.0f, 0.1f),  60.0f,  "%"));
    params.push_back (std::make_unique<P>("drive",   "Drive",   R(0.0f,   10.0f,  0.01f),  3.0f, "dB"));
    params.push_back (std::make_unique<P>("warm",    "Warm",    R(0.0f,   100.0f, 0.1f),  25.0f,  "%"));
    params.push_back (std::make_unique<P>("mix",     "Mix",     R(0.0f,   100.0f, 0.1f), 100.0f,  "%"));
    params.push_back (std::make_unique<P>("output",  "Output",  R(-12.0f, 12.0f,  0.01f),  0.0f, "dB"));
    return { params.begin(), params.end() };
}

void TapeSweetProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };

    preEmph.prepare  (sampleRate, 2, NABEmphasis::Pre);
    deEmph.prepare   (sampleRate, 2, NABEmphasis::De);
    headBump.prepare (spec);
    fixedAir.prepare (spec);
    hpf30.prepare    (spec);

    oversampler.initProcessing ((size_t) samplesPerBlock);
    oversampler.reset();
    saturator.prepare (sampleRate * 4.0, 2);   // saturator runs at oversampled rate

    varispeed.prepare         (sampleRate, 2);
    wowFlutter.prepare        (sampleRate);
    sparkle.prepare           (sampleRate, 2);
    tapeHiss.prepare          (sampleRate);
    transientDetector.prepare (sampleRate, 2);

    modBuffer.setSize       (1, samplesPerBlock, false, false, true);
    transientBuffer.setSize (1, samplesPerBlock, false, false, true);

    *hpf30.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate, 30.0f, 0.707f);

    // Fixed gentle HF rolloff at 18 kHz — replaces user-controlled Tone
    *fixedAir.state = *juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (
        sampleRate, juce::jmin (18000.0f, (float) sampleRate * 0.45f));

    setLatencySamples (varispeed.getLatencySamples());
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

    // ============================================================================
    // Parameter reads
    // ============================================================================
    const float speedPct = apvts.getRawParameterValue ("speed")  ->load();
    const float natural  = apvts.getRawParameterValue ("natural")->load() * 0.01f;
    const float drive    = apvts.getRawParameterValue ("drive")  ->load();
    const float warm     = apvts.getRawParameterValue ("warm")   ->load() * 0.01f;
    const float mixPct   = apvts.getRawParameterValue ("mix")    ->load();
    const float outDb    = apvts.getRawParameterValue ("output") ->load();

    // ============================================================================
    // Warm knob → head bump + wow/flutter + hiss, with designed curves
    // ============================================================================
    const float warmthDb   = warm * 3.5f;          // head bump 0–3.5 dB
    const float wowAmt     = warm * 0.7f;          // wow 0–70%
    const float flutterAmt = warm * 0.6f;
    const float hissAmt    = warm * warm * 0.35f;  // quadratic — clean at low Warm

    // ============================================================================
    // Natural knob → varispeed smoothness + Sparkle (quadratic, blooms upper-half)
    // ============================================================================
    const float sparkleAmt = natural * natural * 0.9f;

    // ---- Derived ----
    const float mix        = mixPct * 0.01f;
    const float driveGain  = juce::Decibels::decibelsToGain (drive);
    const float outGain    = juce::Decibels::decibelsToGain (outDb);
    const float makeup     = juce::Decibels::decibelsToGain (-drive * 0.5f);
    const float speedRatio = 1.0f + speedPct * 0.01f;

    // Speed-coupled head bump (replaces low shelf with a broad peak)
    *headBump.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        currentSampleRate,
        juce::jlimit (40.0f, 200.0f, 80.0f * speedRatio),
        0.7f,
        juce::Decibels::decibelsToGain (warmthDb));

    // ============================================================================
    // Detect transients in the dry input — used to align varispeed wraps
    // ============================================================================
    if (transientBuffer.getNumSamples() < numSamples)
        transientBuffer.setSize (1, numSamples, false, false, true);
    auto* transientFlags = transientBuffer.getWritePointer (0);
    for (int s = 0; s < numSamples; ++s)
    {
        float peak = 0.0f;
        for (int c = 0; c < numCh; ++c)
            peak = juce::jmax (peak, std::abs (buffer.getSample (c, s)));
        transientFlags[s] = transientDetector.detect (0, peak) ? 1.0f : 0.0f;
    }

    // ============================================================================
    // Capture dry copy for Mix
    // ============================================================================
    juce::AudioBuffer<float> dry;
    dry.makeCopyOf (buffer);

    // ============================================================================
    // WET PATH: NAB pre-emph → drive → tape sat (oversampled) → NAB de-emph
    //           → speed-coupled head bump → fixed air rolloff
    // ============================================================================
    preEmph.process (buffer);

    {
        juce::dsp::AudioBlock<float> block (buffer);
        block.multiplyBy (driveGain);

        auto upBlock = oversampler.processSamplesUp (block);
        for (size_t ch = 0; ch < upBlock.getNumChannels(); ++ch)
        {
            auto* data = upBlock.getChannelPointer (ch);
            const size_t n = upBlock.getNumSamples();
            for (size_t i = 0; i < n; ++i)
                data[i] = saturator.processSample ((int) ch, data[i]);
        }
        oversampler.processSamplesDown (block);

        block.multiplyBy (makeup);
    }

    deEmph.process (buffer);

    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        headBump.process (ctx);
        fixedAir.process (ctx);
    }

    // ============================================================================
    // Dry/Wet blend BEFORE varispeed (both share the same pitch shift)
    // ============================================================================
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

    // ============================================================================
    // Varispeed with sample-accurate wow/flutter modulation + transient awareness
    // ============================================================================
    wowFlutter.setAmounts (wowAmt, flutterAmt);

    if (modBuffer.getNumSamples() < numSamples)
        modBuffer.setSize (1, numSamples, false, false, true);

    auto* modData = modBuffer.getWritePointer (0);
    for (int s = 0; s < numSamples; ++s)
        modData[s] = wowFlutter.tick();

    varispeed.setPitchRatio (speedRatio);
    varispeed.setNatural    (natural);
    varispeed.process       (buffer, modData, transientFlags);

    // ============================================================================
    // Post-varispeed colour: Sparkle (multi-band exciter) → Hiss → HPF → trim
    // ============================================================================
    sparkle.setAmount  (sparkleAmt);
    sparkle.process    (buffer);

    tapeHiss.setAmount (hissAmt);
    tapeHiss.process   (buffer);

    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        hpf30.process (ctx);
        block.multiplyBy (outGain);
    }

    // ============================================================================
    // Safety scrub
    // ============================================================================
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            if (! std::isfinite (d[i])) d[i] = 0.0f;
            else d[i] = juce::jlimit (-4.0f, 4.0f, d[i]);
        }
    }
}

juce::AudioProcessorEditor* TapeSweetProcessor::createEditor()
{
    return new TapeSweetEditor (*this);
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
