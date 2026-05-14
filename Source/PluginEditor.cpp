#include "PluginEditor.h"

namespace Col
{
    const juce::Colour bg         { 0xFF0A0A0A };
    const juce::Colour ink        { 0xFFE8E8E8 };
    const juce::Colour subtle     { 0xFF6E6E73 };
    const juce::Colour track      { 0xFF2A2A2C };
    const juce::Colour active     { 0xFFFFFFFF };
}

//==============================================================================
struct TapeSweetEditor::Control : public juce::Component
{
    enum class Style { BigKnob, SmallKnob, HSlider, VFader };

    Control (juce::AudioProcessorValueTreeState& state,
             const juce::String& paramId,
             const juce::String& display,
             Style s,
             int decimals,
             const juce::String& suffix)
        : style (s),
          nameText (display)
    {
        switch (style)
        {
            case Style::BigKnob:
            case Style::SmallKnob:
                slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                            juce::MathConstants<float>::pi * 2.75f, true);
                slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 20);
                break;

            case Style::HSlider:
                slider.setSliderStyle (juce::Slider::LinearHorizontal);
                slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
                break;

            case Style::VFader:
                slider.setSliderStyle (juce::Slider::LinearVertical);
                slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
                break;
        }

        slider.setColour (juce::Slider::textBoxTextColourId,       Col::ink);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        slider.setNumDecimalPlacesToDisplay (decimals);
        slider.setTextValueSuffix (suffix);

        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
            (state, paramId, slider);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Col::subtle);
        auto font = juce::Font (juce::FontOptions (10.0f)).withExtraKerningFactor (0.22f);
        g.setFont (font);

        auto labelArea = getLocalBounds().removeFromTop (16);
        g.drawText (nameText.toUpperCase(), labelArea, juce::Justification::centred);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        b.removeFromTop (20);            // name label area

        if (style == Style::HSlider)
            b.reduce (6, 0);

        slider.setBounds (b);
    }

    Style style;
    juce::String nameText;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
TapeSweetLookAndFeel::TapeSweetLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId,    Col::active);
    setColour (juce::Slider::rotarySliderOutlineColourId, Col::track);
    setColour (juce::Slider::backgroundColourId,          Col::track);
    setColour (juce::Slider::trackColourId,               Col::active);
    setColour (juce::Slider::thumbColourId,               Col::active);
    setColour (juce::Label::textColourId,                 Col::ink);
    setColour (juce::Label::backgroundColourId,           juce::Colours::transparentBlack);
}

void TapeSweetLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                             float pos, float startAng, float endAng,
                                             juce::Slider&)
{
    const float diameter = (float) juce::jmin (w, h) - 10.0f;
    const float radius   = diameter * 0.5f;
    const float cx       = (float) x + (float) w * 0.5f;
    const float cy       = (float) y + (float) h * 0.5f;
    const float strokeW  = (radius > 40.0f) ? 2.0f : 1.5f;

    juce::Path trackPath;
    trackPath.addCentredArc (cx, cy, radius, radius, 0.0f, startAng, endAng, true);
    g.setColour (Col::track);
    g.strokePath (trackPath,
                  juce::PathStrokeType (strokeW,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));

    const float currentAng = startAng + pos * (endAng - startAng);
    juce::Path activePath;
    activePath.addCentredArc (cx, cy, radius, radius, 0.0f, startAng, currentAng, true);
    g.setColour (Col::active);
    g.strokePath (activePath,
                  juce::PathStrokeType (strokeW,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));

    const float innerR = radius - (radius > 40.0f ? 12.0f : 8.0f);
    const float outerR = radius - 2.0f;
    const float sinA = std::sin (currentAng);
    const float cosA = std::cos (currentAng);
    juce::Path indicator;
    indicator.startNewSubPath (cx + innerR * sinA, cy - innerR * cosA);
    indicator.lineTo         (cx + outerR * sinA, cy - outerR * cosA);
    g.setColour (Col::active);
    g.strokePath (indicator,
                  juce::PathStrokeType (strokeW,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));
}

void TapeSweetLookAndFeel::drawLinearSlider (juce::Graphics& g,
                                             int x, int y, int w, int h,
                                             float sliderPos,
                                             float /*minSliderPos*/,
                                             float /*maxSliderPos*/,
                                             const juce::Slider::SliderStyle style,
                                             juce::Slider& slider)
{
    const bool isVertical = (style == juce::Slider::LinearVertical);

    if (isVertical)
    {
        const float cx = (float) x + (float) w * 0.5f;
        const float topY    = (float) y + 6.0f;
        const float botY    = (float) (y + h) - 6.0f;
        const float trackW  = 2.0f;

        // Inactive track
        g.setColour (Col::track);
        g.fillRoundedRectangle (cx - trackW * 0.5f, topY,
                                trackW, botY - topY, 1.0f);

        // Active portion (below the thumb)
        g.setColour (Col::active);
        g.fillRoundedRectangle (cx - trackW * 0.5f, sliderPos,
                                trackW, botY - sliderPos, 1.0f);

        // Fader cap: wider rectangle with rounded corners
        const float capW = 28.0f;
        const float capH = 10.0f;
        g.setColour (Col::active);
        g.fillRoundedRectangle (cx - capW * 0.5f, sliderPos - capH * 0.5f,
                                capW, capH, 3.0f);

        // Subtle centre line through the cap
        g.setColour (Col::bg);
        g.fillRoundedRectangle (cx - capW * 0.4f, sliderPos - 0.6f,
                                capW * 0.8f, 1.2f, 0.5f);
    }
    else
    {
        const float cy = (float) y + (float) h * 0.5f;
        const float leftX  = (float) x + 8.0f;
        const float rightX = (float) (x + w) - 8.0f;
        const float trackH = 2.0f;

        // Inactive track
        g.setColour (Col::track);
        g.fillRoundedRectangle (leftX, cy - trackH * 0.5f,
                                rightX - leftX, trackH, 1.0f);

        // Active portion (from minimum to thumb)
        g.setColour (Col::active);
        g.fillRoundedRectangle (leftX, cy - trackH * 0.5f,
                                sliderPos - leftX, trackH, 1.0f);

        // Circle thumb
        const float r = 7.0f;
        g.setColour (Col::active);
        g.fillEllipse (sliderPos - r, cy - r, r * 2.0f, r * 2.0f);
    }

    juce::ignoreUnused (slider);
}

juce::Label* TapeSweetLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::textColourId, Col::ink);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::TextEditor::textColourId, Col::ink);
    l->setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::TextEditor::highlightedTextColourId, Col::bg);
    l->setColour (juce::TextEditor::highlightColourId, Col::ink);
    l->setFont (juce::Font (juce::FontOptions (11.5f)));
    return l;
}

juce::Font TapeSweetLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font (juce::FontOptions (11.5f));
}

//==============================================================================
TapeSweetEditor::TapeSweetEditor (TapeSweetProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    using S = Control::Style;
    speedC   = std::make_unique<Control> (proc.apvts, "speed",   "Speed",   S::BigKnob,   1, " %");
    naturalC = std::make_unique<Control> (proc.apvts, "natural", "Natural", S::BigKnob,   0, " %");
    warmC    = std::make_unique<Control> (proc.apvts, "warm",    "Warm",    S::SmallKnob, 0, " %");
    wearC    = std::make_unique<Control> (proc.apvts, "wear",    "Wear",    S::SmallKnob, 0, " %");
    mixC     = std::make_unique<Control> (proc.apvts, "mix",     "Mix",     S::HSlider,   0, " %");
    outputC  = std::make_unique<Control> (proc.apvts, "output",  "Output",  S::VFader,    1, " dB");

    for (auto* c : { speedC.get(), naturalC.get(), warmC.get(), wearC.get(), mixC.get(), outputC.get() })
        addAndMakeVisible (c);

    setSize (720, 380);
}

TapeSweetEditor::~TapeSweetEditor()
{
    setLookAndFeel (nullptr);
}

void TapeSweetEditor::paint (juce::Graphics& g)
{
    g.fillAll (Col::bg);

    // Title
    auto titleBounds = getLocalBounds().removeFromTop (50).toFloat();
    auto titleFont = juce::Font (juce::FontOptions (13.0f)).withExtraKerningFactor (0.46f);
    g.setFont (titleFont);
    g.setColour (Col::ink);
    g.drawText ("TAPESWEET", titleBounds, juce::Justification::centred);

    // Thin divider under the title
    g.setColour (Col::track);
    auto line = getLocalBounds().removeFromTop (51).removeFromBottom (1).reduced (50, 0);
    g.fillRect (line);
}

void TapeSweetEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (54);             // title

    // Bottom-of-window: Mix slider takes the full row
    auto mixRow = bounds.removeFromBottom (54);
    mixC->setBounds (mixRow.reduced (40, 6));

    // Right column: Output vertical fader
    auto outCol = bounds.removeFromRight (90);
    outputC->setBounds (outCol.reduced (10, 14));

    // Remaining area splits into two rows (big knobs / small knobs)
    bounds.reduce (20, 6);
    auto bigRow   = bounds.removeFromTop (bounds.getHeight() * 6 / 10);
    auto smallRow = bounds.removeFromTop (bounds.getHeight());

    const int bigHalf   = bigRow.getWidth() / 2;
    speedC  ->setBounds (bigRow.removeFromLeft  (bigHalf).reduced (10, 4));
    naturalC->setBounds (bigRow                          .reduced (10, 4));

    const int smallHalf = smallRow.getWidth() / 2;
    warmC->setBounds (smallRow.removeFromLeft  (smallHalf).reduced (40, 6));
    wearC->setBounds (smallRow                            .reduced (40, 6));
}
