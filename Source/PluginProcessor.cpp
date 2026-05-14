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
    saturator.prepare (sampleRate * 4.0, 2);

    varispeed.prepare         (sampleRate, 2);
    wowFlutter.prepare        (sampleRate);
    sparkle.prepare           (sampleRate, 2);
    tapeHiss.prepare          (sampleRate);
    transientDetector.prepare (sampleRate, 2);

    // ---- Latency wiring ----
    const int osLatency       = (int) std::ceil (oversampler.getLatencyInSamples());
    const int varispeedLat    = varispeed.getLatencySamples();
    totalLatencySamples       = osLatency + varispeedLat;

    dryDelay.prepare    (spec);
    dryDelay.setMaximumDelayInSamples (totalLatencySamples + 8);
    dryDelay.setDelay   ((float) totalLatencySamples);

    bypassDelay.prepare (spec);
    bypassDelay.setMaximumDelayInSamples (varispeedLat + 8);
    bypassDelay.setDelay ((float) varispeedLat);

    setLatencySamples (totalLatencySamples);

    // ---- Pre-allocated scratch buffers ----
    modBuffer.setSize        (1, samplesPerBlock, false, false, true);
    transientBuffer.setSize  (1, samplesPerBlock, false, false, true);
    delayedDryBuffer.setSize (2, samplesPerBlock, false, false, true);

    // ---- Fixed filters ----
    *hpf30.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate, 30.0f, 0.707f);
    *fixedAir.state = *juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (
        sampleRate, juce::jmin (18000.0f, (float) sampleRate * 0.45f));

    // Force first-block coefficient rebuild
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

    // ---- Parameters ----
    const float speedPct = apvts.getRawParameterValue ("speed")  ->load();
    const float natural  = apvts.getRawParameterValue ("natural")->load() * 0.01f;
    const float drive    = apvts.getRawParameterValue ("drive")  ->load();
    const float warm     = apvts.getRawParameterValue ("warm")   ->load() * 0.01f;
    const float mixPct   = apvts.getRawParameterValue ("mix")    ->load();
    const float outDb    = apvts.getRawParameterValue ("output") ->load();

    // ---- Warm-driven amounts (designed curves) ----
    const float warmthDb   = warm * 3.5f;
    const float wowAmt     = warm * 0.7f;
    const float flutterAmt = warm * 0.6f;
    const float hissAmt    = warm * warm * 0.35f;

    // ---- Natural-driven sparkle (quadratic) ----
    const float sparkleAmt = natural * natural * 0.9f;

    // ---- Derived gains ----
    const float mix        = mixPct * 0.01f;
    const float driveGain  = juce::Decibels::decibelsToGain (drive);
    const float outGain    = juce::Decibels::decibelsToGain (outDb);
    const float makeup     = juce::Decibels::decibelsToGain (-drive * 0.5f);
    const float speedRatio = 1.0f + speedPct * 0.01f;

    // ============================================================================
    // Cache head-bump coefficients - only rebuild if input parameters changed.
    // Avoids per-block allocation in the makePeakFilter() path.
    // ============================================================================
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
    // Push input into dry-delay so we have a latency-matched dry signal to mix
    // against the wet output at the end of the chain. This is what makes Mix
    // behave like a real wet/dry control instead of just a saturation blend.
    // ============================================================================
    for (int s = 0; s < numSamples; ++s)
    {
        for (int c = 0; c < numCh; ++c)
            dryDelay.pushSample (c, buffer.getSample (c, s));

        for (int c = 0; c < numCh; ++c)
            delayedDryBuffer.setSample (c, s, dryDelay.popSample (c));
    }

    // ============================================================================
    // Transient flags for varispeed alignment
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
    // WET PATH: NAB pre-emph → drive → tape sat (oversampled) → NAB de-emph
    //           → speed-coupled head bump → fixed 18 kHz air rolloff
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
    // Varispeed - bypassed via constant-latency delay when no pitch shift
    // and no wow/flutter is active. Keeps total plugin latency constant so
    // PDC stays correct, while making Speed=0 + Warm=0 truly transparent
    // (no two-tap comb filtering coloration).
    // ============================================================================
    const bool varispeedActive = (std::abs (speedPct) > 0.005f) || (wowAmt > 0.001f);

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
    // Post-varispeed colour
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
    // TRUE Mix: blend latency-matched dry input against fully processed wet.
    // Mix = 0 → output is the clean delayed dry (effective bypass).
    // Mix = 100 → output is fully processed wet.
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

    // ---- Safety scrub ----
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
