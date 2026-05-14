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
    params.push_back (std::make_unique<P>("speed",   "Speed",   R(-10.0f, 10.0f,   0.01f),   0.0f,  "%"));
    params.push_back (std::make_unique<P>("natural", "Natural", R(0.0f,   100.0f,  0.1f),   60.0f,  "%"));
    params.push_back (std::make_unique<P>("warm",    "Warm",    R(0.0f,   100.0f,  0.1f),   25.0f,  "%"));
    params.push_back (std::make_unique<P>("wear",    "Wear",    R(0.0f,   100.0f,  0.1f),    0.0f,  "%"));
    params.push_back (std::make_unique<P>("mix",     "Mix",     R(0.0f,   100.0f,  0.1f),  100.0f,  "%"));
    params.push_back (std::make_unique<P>("output",  "Output",  R(-12.0f, 12.0f,   0.01f),   0.0f, "dB"));
    return { params.begin(), params.end() };
}

void TapeSweetProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };

    preEmph.prepare  (sampleRate, 2);
    deEmph.prepare   (sampleRate, 2);
    headBump.prepare (spec);
    hpf30.prepare    (spec);

    oversampler.initProcessing ((size_t) samplesPerBlock);
    oversampler.reset();
    saturator.prepare (sampleRate * 4.0, 2);
    glue.prepare      (sampleRate, 2);

    varispeed.prepare         (sampleRate, 2);
    wowFlutter.prepare        (sampleRate);
    tapeHiss.prepare          (sampleRate);
    transientDetector.prepare (sampleRate, 2);

    const int osLatency    = (int) std::ceil (oversampler.getLatencyInSamples());
    const int varispeedLat = varispeed.getLatencySamples();
    totalLatencySamples    = osLatency + varispeedLat;

    dryDelay.prepare    (spec);
    dryDelay.setMaximumDelayInSamples (totalLatencySamples + 8);
    dryDelay.setDelay   ((float) totalLatencySamples);

    bypassDelay.prepare (spec);
    bypassDelay.setMaximumDelayInSamples (varispeedLat + 8);
    bypassDelay.setDelay ((float) varispeedLat);

    setLatencySamples (totalLatencySamples);

    modBuffer.setSize        (1, samplesPerBlock, false, false, true);
    transientBuffer.setSize  (1, samplesPerBlock, false, false, true);
    delayedDryBuffer.setSize (2, samplesPerBlock, false, false, true);
    satBlendBuffer.setSize   (2, samplesPerBlock, false, false, true);

    *hpf30.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate, 30.0f, 0.707f);

    lastHeadBumpHz = lastHeadBumpDb = -1.0f;
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
    // Parameter reads (6 user-facing knobs)
    // ============================================================================
    const float speedPct = apvts.getRawParameterValue ("speed")  ->load();
    const float natural  = apvts.getRawParameterValue ("natural")->load() * 0.01f;
    const float warm     = apvts.getRawParameterValue ("warm")   ->load() * 0.01f;
    const float wear     = apvts.getRawParameterValue ("wear")   ->load() * 0.01f;
    const float mixPct   = apvts.getRawParameterValue ("mix")    ->load();
    const float outDb    = apvts.getRawParameterValue ("output") ->load();

    // ============================================================================
    // Macro mappings - each knob drives multiple internal stages in a coherent way
    // ============================================================================

    // Warm: saturation density macro. At higher Warm values the saturator is
    // driven harder AND fed a 3 kHz-boosted version of the signal, so it
    // generates harmonics in the upper-mid band that imprint a "tape-mid"
    // character. The heavily saturated leg is then parallel-blended with the
    // pre-saturation signal so the user gets the harmonic colour without the
    // full-band amplitude crush.
    const float warmDriveDb   = warm * 10.0f;        // up to +10 dB into the saturator
    const float bandEmphasisDb = warm * 6.0f;        // up to +6 dB mid emphasis pre-sat
    const float satMix        = warm * 0.55f;        // up to 55 % parallel blend
    const float warmthDb      = warm * 3.0f;
    const float glueBlend     = warm * 0.15f;

    // Wear: imperfection macro. wow + flutter + hiss only. Hiss is quadratic
    // so the bottom half of the knob stays clean.
    const float wowAmt      = wear * 0.7f;
    const float flutterAmt  = wear * 0.6f;
    const float hissAmt     = wear * wear * 0.5f;

    // Derived
    const float mix         = mixPct * 0.01f;
    const float driveGain   = juce::Decibels::decibelsToGain (warmDriveDb);
    const float outGain     = juce::Decibels::decibelsToGain (outDb);
    const float makeup      = juce::Decibels::decibelsToGain (-warmDriveDb * 0.5f);
    const float speedRatio  = 1.0f + speedPct * 0.01f;

    // Speed-coupled head bump (rebuild filter coefficients only when inputs move)
    const float bumpHz = juce::jlimit (40.0f, 200.0f, 80.0f * speedRatio);
    if (std::abs (bumpHz - lastHeadBumpHz) > 0.5f
     || std::abs (warmthDb - lastHeadBumpDb) > 0.05f)
    {
        *headBump.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            currentSampleRate, bumpHz, 0.7f,
            juce::Decibels::decibelsToGain (warmthDb));
        lastHeadBumpHz = bumpHz;
        lastHeadBumpDb = warmthDb;
    }

    // ============================================================================
    // Latency-matched dry path
    // ============================================================================
    for (int s = 0; s < numSamples; ++s)
    {
        for (int c = 0; c < numCh; ++c)
            dryDelay.pushSample (c, buffer.getSample (c, s));

        for (int c = 0; c < numCh; ++c)
            delayedDryBuffer.setSample (c, s, dryDelay.popSample (c));
    }

    // ============================================================================
    // Transient flags (drive varispeed wrap alignment + tap-ducking)
    // ============================================================================
    auto* transientFlags = transientBuffer.getWritePointer (0);
    for (int s = 0; s < numSamples; ++s)
    {
        float peak = 0.0f;
        for (int c = 0; c < numCh; ++c)
            peak = juce::jmax (peak, std::abs (buffer.getSample (c, s)));
        transientFlags[s] = transientDetector.detect (0, peak) ? 1.0f : 0.0f;
    }

    // ============================================================================
    // WET PATH:
    //   Snapshot pre-sat signal for parallel blending. Then run the heavy sat
    //   path (band pre-emph -> drive -> oversampled saturator -> band de-emph)
    //   and blend it back at satMix. After that, head bump + parallel glue.
    // ============================================================================

    // Snapshot for parallel sat blend
    for (int c = 0; c < numCh; ++c)
        satBlendBuffer.copyFrom (c, 0, buffer, c, 0, numSamples);

    preEmph.setGainDb (bandEmphasisDb);
    deEmph.setGainDb (-bandEmphasisDb);

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

    // Parallel sat blend: buffer = (1 - satMix) * pre-sat + satMix * post-sat
    if (satMix < 0.999f)
    {
        const float oneMinusMix = 1.0f - satMix;
        for (int c = 0; c < numCh; ++c)
        {
            auto* w = buffer.getWritePointer (c);
            auto* dry = satBlendBuffer.getReadPointer (c);
            for (int s = 0; s < numSamples; ++s)
                w[s] = w[s] * satMix + dry[s] * oneMinusMix;
        }
    }

    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        headBump.process (ctx);
    }

    glue.setBlend (glueBlend);
    glue.process  (buffer);

    // ============================================================================
    // Varispeed (constant-latency bypass when Speed AND Wear are at zero)
    // ============================================================================
    const bool varispeedActive = (std::abs (speedPct) > 0.005f) || (wear > 0.001f);

    if (varispeedActive)
    {
        wowFlutter.setAmounts (wowAmt, flutterAmt);

        auto* modData = modBuffer.getWritePointer (0);
        for (int s = 0; s < numSamples; ++s)
            modData[s] = wowFlutter.tick();

        varispeed.setPitchRatio (speedRatio);
        varispeed.setNatural    (natural);
        varispeed.process       (buffer, modData, transientFlags);
    }
    else
    {
        for (int s = 0; s < numSamples; ++s)
        {
            for (int c = 0; c < numCh; ++c)
            {
                const float in = buffer.getSample (c, s);
                bypassDelay.pushSample (c, in);
                buffer.setSample (c, s, bypassDelay.popSample (c));
            }
        }
    }

    // ============================================================================
    // Hiss (Wear) -> HPF -> output trim
    // ============================================================================
    tapeHiss.setAmount (hissAmt);
    tapeHiss.process   (buffer);

    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        hpf30.process (ctx);
        block.multiplyBy (outGain);
    }

    // ============================================================================
    // TRUE Mix: blend latency-matched dry against fully processed wet.
    //   Mix = 0   -> output is the clean delayed dry (full bypass).
    //   Mix = 100 -> output is fully processed wet.
    // ============================================================================
    if (mix < 0.999f)
    {
        const float oneMinusMix = 1.0f - mix;
        for (int c = 0; c < numCh; ++c)
        {
            auto* w = buffer.getWritePointer (c);
            auto* d = delayedDryBuffer.getReadPointer (c);
            for (int s = 0; s < numSamples; ++s)
                w[s] = w[s] * mix + d[s] * oneMinusMix;
        }
    }

    // Safety scrub
    for (int c = 0; c < numCh; ++c)
    {
        auto* d = buffer.getWritePointer (c);
        for (int s = 0; s < numSamples; ++s)
        {
            if (! std::isfinite (d[s])) d[s] = 0.0f;
            else d[s] = juce::jlimit (-4.0f, 4.0f, d[s]);
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
