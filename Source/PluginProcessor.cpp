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
    params.push_back (std::make_unique<P>("speed",   "Speed",   R(-10.0f, 10.0f,   0.01f),   0.0f,    "%"));
    params.push_back (std::make_unique<P>("natural", "Natural", R(0.0f,   100.0f,  0.1f),   60.0f,    "%"));
    params.push_back (std::make_unique<P>("drive",   "Drive",   R(0.0f,   10.0f,   0.01f),   3.0f,   "dB"));
    params.push_back (std::make_unique<P>("warm",    "Warm",    R(0.0f,   100.0f,  0.1f),   20.0f,    "%"));
    params.push_back (std::make_unique<P>("tone",    "Tone",    R(8000.0f, 22000.0f, 1.0f), 16000.0f, "Hz"));
    params.push_back (std::make_unique<P>("mix",     "Mix",     R(0.0f,   100.0f,  0.1f),  100.0f,    "%"));
    params.push_back (std::make_unique<P>("output",  "Output",  R(-12.0f, 12.0f,   0.01f),   0.0f,   "dB"));
    return { params.begin(), params.end() };
}

void TapeSweetProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  2 };

    preEmph.prepare (sampleRate, 2, NABEmphasis::Pre);
    deEmph.prepare  (sampleRate, 2, NABEmphasis::De);
    headBump.prepare (spec);
    gapLoss.prepare  (spec);
    hpf30.prepare    (spec);

    oversampler.initProcessing ((size_t) samplesPerBlock);
    oversampler.reset();
    saturator.prepare (2);

    varispeed.prepare     (sampleRate, 2);
    wowFlutter.prepare    (sampleRate);
    sparkle.prepare       (sampleRate, 2);
    scrapeFlutter.prepare (sampleRate, 2);
    tapeHiss.prepare      (sampleRate);

    modBuffer.setSize (1, samplesPerBlock, false, false, true);

    *hpf30.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate, 30.0f, 0.707f);

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

    // ---- Parameter reads ----
    const float speedPct = apvts.getRawParameterValue ("speed")  ->load();
    const float natural  = apvts.getRawParameterValue ("natural")->load() * 0.01f;
    const float drive    = apvts.getRawParameterValue ("drive")  ->load();
    const float warm     = apvts.getRawParameterValue ("warm")   ->load() * 0.01f;
    const float toneHz   = apvts.getRawParameterValue ("tone")   ->load();
    const float mixPct   = apvts.getRawParameterValue ("mix")    ->load();
    const float outDb    = apvts.getRawParameterValue ("output") ->load();

    // ---- Warm knob breakdown — coherent curves so one knob feels musical ----
    // Subtle to extreme tape character without exposing four separate sliders.
    const float warmthDb   = warm * 3.0f;         // head bump 0–3 dB
    const float wowAmt     = warm * 0.6f;         // 0–60% of max wow depth
    const float flutterAmt = warm * 0.6f;
    const float scrapeAmt  = warm * 0.5f;
    const float hissAmt    = warm * warm * 0.4f;  // quadratic — hiss only at high settings

    // ---- Natural knob breakdown — pitch smoothness AND sparkle scale together ----
    const float sparkleAmt = natural * natural * 0.7f; // quadratic so sparkle blooms late

    // ---- Derived ----
    const float mix        = mixPct * 0.01f;
    const float driveGain  = juce::Decibels::decibelsToGain (drive);
    const float outGain    = juce::Decibels::decibelsToGain (outDb);
    const float makeup     = juce::Decibels::decibelsToGain (-drive * 0.5f);
    const float speedRatio = 1.0f + speedPct * 0.01f;

    // ---- Speed-coupled filter coefficients (clamped to stay below Nyquist) ----
    *headBump.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        currentSampleRate,
        juce::jlimit (40.0f, 200.0f, 80.0f * speedRatio),
        0.7f,
        juce::Decibels::decibelsToGain (warmthDb));

    const float maxCorner = (float) currentSampleRate * 0.45f;
    const float gapCorner = juce::jlimit (2000.0f, maxCorner, toneHz * speedRatio);
    *gapLoss.state = *juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (
        currentSampleRate, gapCorner);

    // ---- Capture dry copy for Mix ----
    juce::AudioBuffer<float> dry;
    dry.makeCopyOf (buffer);

    // ============================================================================
    // WET PATH:  pre-emph → drive → tape-sat (oversampled, with memory) →
    //            de-emph → speed-coupled head bump → speed-coupled gap-loss
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
        gapLoss.process  (ctx);
    }

    // ============================================================================
    // Mix dry/wet BEFORE varispeed (so both share the same pitch shift)
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
    // Varispeed with sample-accurate wow/flutter
    // ============================================================================
    wowFlutter.setAmounts (wowAmt, flutterAmt);

    if (modBuffer.getNumSamples() < numSamples)
        modBuffer.setSize (1, numSamples, false, false, true);

    auto* modData = modBuffer.getWritePointer (0);
    for (int s = 0; s < numSamples; ++s)
        modData[s] = wowFlutter.tick();

    varispeed.setPitchRatio (speedRatio);
    varispeed.setNatural    (natural);
    varispeed.process       (buffer, modData);

    // ============================================================================
    // Post-varispeed colour: HF Sparkle exciter → scrape flutter → hiss → HPF
    // ============================================================================
    sparkle.setAmount (sparkleAmt);
    sparkle.process   (buffer);

    scrapeFlutter.setAmount (scrapeAmt);
    scrapeFlutter.process   (buffer);

    tapeHiss.setAmount (hissAmt);
    tapeHiss.process   (buffer);

    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        hpf30.process (ctx);
        block.multiplyBy (outGain);
    }

    // ============================================================================
    // Safety: scrub NaN/Inf and clamp catastrophic peaks.
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
