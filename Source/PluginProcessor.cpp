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
    params.push_back (std::make_unique<P>("speed",   "Speed",   R(-10.0f, 10.0f,  0.01f),   0.0f,  "%"));
    params.push_back (std::make_unique<P>("natural", "Natural", R(0.0f,  100.0f,  0.1f),   60.0f,  "%"));
    params.push_back (std::make_unique<P>("drive",   "Drive",   R(0.0f,   10.0f,  0.01f),   3.0f, "dB"));
    params.push_back (std::make_unique<P>("warmth",  "Warmth",  R(0.0f,    3.0f,  0.01f),   1.5f, "dB"));
    params.push_back (std::make_unique<P>("tone",    "Tone",    R(8000.0f, 22000.0f, 1.0f), 16000.0f, "Hz"));
    params.push_back (std::make_unique<P>("wear",    "Wear",    R(0.0f,  100.0f,  0.1f),   25.0f,  "%"));
    params.push_back (std::make_unique<P>("mix",     "Mix",     R(0.0f,  100.0f,  0.1f),  100.0f,  "%"));
    params.push_back (std::make_unique<P>("hiss",    "Hiss",    R(0.0f,  100.0f,  0.1f),    0.0f,  "%"));
    params.push_back (std::make_unique<P>("output",  "Output",  R(-12.0f, 12.0f,  0.01f),   0.0f, "dB"));
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

    varispeed.prepare     (sampleRate, 2);
    wowFlutter.prepare    (sampleRate);
    scrapeFlutter.prepare (sampleRate, 2);
    tapeHiss.prepare      (sampleRate);

    // 30 Hz subsonic HPF — fixed corner
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

    const float speedPct  = apvts.getRawParameterValue ("speed")  ->load();
    const float natural   = apvts.getRawParameterValue ("natural")->load() * 0.01f;
    const float drive     = apvts.getRawParameterValue ("drive")  ->load();
    const float warmth    = apvts.getRawParameterValue ("warmth") ->load();
    const float toneHz    = apvts.getRawParameterValue ("tone")   ->load();
    const float wear      = apvts.getRawParameterValue ("wear")   ->load() * 0.01f;
    const float mixPct    = apvts.getRawParameterValue ("mix")    ->load();
    const float hissAmt   = apvts.getRawParameterValue ("hiss")   ->load() * 0.01f;
    const float outDb     = apvts.getRawParameterValue ("output") ->load();

    const float mix       = mixPct * 0.01f;
    const float driveGain = juce::Decibels::decibelsToGain (drive);
    const float outGain   = juce::Decibels::decibelsToGain (outDb);
    const float makeup    = juce::Decibels::decibelsToGain (-drive * 0.5f);
    const float speedRatio = 1.0f + speedPct * 0.01f;

    // Speed-coupled head bump: centre frequency shifts with tape speed.
    // Real machines have the head-bump peak track playback speed linearly.
    *headBump.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        currentSampleRate,
        juce::jlimit (40.0f, 200.0f, 80.0f * speedRatio),
        1.5f,
        juce::Decibels::decibelsToGain (warmth));

    // Speed-coupled gap-loss: HF rolloff corner also tracks speed (faster
    // tape = brighter, the "shine" of varispeed)
    *gapLoss.state = *juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (
        currentSampleRate, juce::jlimit (4000.0f, 30000.0f, toneHz * speedRatio));

    // Stash dry copy for Mix (pre-saturation, post-input)
    juce::AudioBuffer<float> dry;
    dry.makeCopyOf (buffer);

    // ===== Wet path: pre-emph -> drive -> sat -> de-emph -> head bump -> gap-loss =====
    preEmph.process (buffer);

    {
        juce::dsp::AudioBlock<float> block (buffer);
        block.multiplyBy (driveGain);

        auto upBlock = oversampler.processSamplesUp (block);
        for (size_t ch = 0; ch < upBlock.getNumChannels(); ++ch)
        {
            auto* data = upBlock.getChannelPointer (ch);
            const size_t n = upBlock.getNumSamples();
            const float bias = 0.1f;
            const float biasOut = std::tanh (bias);
            for (size_t i = 0; i < n; ++i)
                data[i] = std::tanh (data[i] + bias) - biasOut;
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

    // ===== Mix dry/wet BEFORE varispeed so the speed effect applies to both
    //       (and we avoid phasing between shifted-wet vs unshifted-dry) =====
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

    // ===== Varispeed with sample-rate wow/flutter modulation =====
    // Wear governs both the wow/flutter depth and the scrape/hiss-mod intensity.
    // Natural adds a small baseline wow even when Wear is zero, since the
    // research found that a tiny pitch wobble is what makes the ear stop
    // hearing grain-rate modulation as "digital".
    const float wowAmt     = juce::jmin (1.0f, wear + natural * 0.25f);
    const float flutterAmt = wear;
    wowFlutter.setAmounts (wowAmt, flutterAmt);

    varispeed.setPitchRatio (speedRatio);
    varispeed.setNatural    (natural);

    // Drive the varispeed with per-sample wow/flutter modulation. We feed a
    // block-rate average since varispeed.process is block-based — the
    // pitch modulation moves slowly enough (≤12 Hz) that sample-accuracy
    // isn't needed for the wow/flutter character.
    float modSum = 0.0f;
    for (int s = 0; s < numSamples; ++s)
        modSum += wowFlutter.tick();
    varispeed.setPitchModulationCents (modSum / (float) numSamples);

    varispeed.process (buffer);

    // ===== Scrape flutter — wear-controlled high-frequency haze =====
    scrapeFlutter.setAmount (wear);
    scrapeFlutter.process (buffer);

    // ===== Tape hiss — pink, signal-modulated, auto-muted on silence =====
    tapeHiss.setAmount (hissAmt);
    tapeHiss.process (buffer);

    // ===== 30 Hz HPF and output trim =====
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        hpf30.process (ctx);
        block.multiplyBy (outGain);
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
