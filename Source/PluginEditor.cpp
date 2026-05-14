#include "PluginEditor.h"

namespace Col
{
    const juce::Colour bg         { 0xFF0A0A0A };
    const juce::Colour ink        { 0xFFE8E8E8 };
    const juce::Colour subtle     { 0xFF6E6E73 };
    const juce::Colour knobTrack  { 0xFF2A2A2C };
    const juce::Colour knobActive { 0xFFFFFFFF };
}

//==============================================================================
struct TapeSweetEditor::KnobControl : public juce::Component
{
    KnobControl (juce::AudioProcessorValueTreeState& s,
                 const juce::String& id,
                 const juce::String& display,
                 int decimals,
                 const juce::String& suffix,
                 double textScale = 1.0)
        : nameText (display)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
        slider.setColour (juce::Slider::textBoxTextColourId,       Col::ink);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        slider.setNumDecimalPlacesToDisplay (decimals);

        if (std::abs (textScale - 1.0) > 1e-6)
        {
            slider.textFromValueFunction = [decimals, suffix, textScale] (double v)
            {
                return juce::String (v * textScale, decimals) + suffix;
            };
            slider.valueFromTextFunction = [textScale] (const juce::String& t)
            {
                return t.retainCharacters ("0123456789.-").getDoubleValue() / textScale;
            };
            slider.updateText();
        }
        else
        {
            slider.setTextValueSuffix (suffix);
        }

        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Col::subtle);
        auto font = juce::Font (juce::FontOptions (10.0f)).withExtraKerningFactor (0.22f);
        g.setFont (font);
        g.drawText (nameText.toUpperCase(),
                    getLocalBounds().removeFromTop (18),
                    juce::Justification::centred);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        b.removeFromTop (22);
        slider.setBounds (b);
    }

    juce::String nameText;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
TapeSweetLookAndFeel::TapeSweetLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId,    Col::knobActive);
    setColour (juce::Slider::rotarySliderOutlineColourId, Col::knobTrack);
    setColour (juce::Label::textColourId,                 Col::ink);
    setColour (juce::Label::backgroundColourId,           juce::Colours::transparentBlack);
}

void TapeSweetLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                             float pos, float startAng, float endAng,
                                             juce::Slider&)
{
    const float diameter = (float) juce::jmin (w, h) - 10.0f;
    const float radius   = diameter * 0.5f;
    const float cx = (float) x + (float) w * 0.5f;
    const float cy = (float) y + (float) h * 0.5f;
    const float strokeW = 1.6f;

    juce::Path track;
    track.addCentredArc (cx, cy, radius, radius, 0.0f, startAng, endAng, true);
    g.setColour (Col::knobTrack);
    g.strokePath (track, juce::PathStrokeType (strokeW,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    const float currentAng = startAng + pos * (endAng - startAng);
    juce::Path active;
    active.addCentredArc (cx, cy, radius, radius, 0.0f, startAng, currentAng, true);
    g.setColour (Col::knobActive);
    g.strokePath (active, juce::PathStrokeType (strokeW,
                                                juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    const float innerR = radius - 9.0f;
    const float outerR = radius - 2.0f;
    const float sinA = std::sin (currentAng);
    const float cosA = std::cos (currentAng);
    juce::Path indicator;
    indicator.startNewSubPath (cx + innerR * sinA, cy - innerR * cosA);
    indicator.lineTo         (cx + outerR * sinA, cy - outerR * cosA);
    g.setColour (Col::knobActive);
    g.strokePath (indicator, juce::PathStrokeType (strokeW,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
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

    struct Spec
    {
        const char* id;
        const char* name;
        int decimals;
        const char* suffix;
        double scale;
    };

    static const Spec specs[] =
    {
        { "speed",   "Speed",   1, " %",  1.0 },
        { "natural", "Natural", 0, " %",  1.0 },
        { "warm",    "Warm",    0, " %",  1.0 },
        { "wear",    "Wear",    0, " %",  1.0 },
        { "mix",     "Mix",     0, " %",  1.0 },
        { "output",  "Output",  1, " dB", 1.0 },
    };

    for (auto& spec : specs)
    {
        auto knob = std::make_unique<KnobControl> (proc.apvts,
                                                   spec.id, spec.name,
                                                   spec.decimals, spec.suffix,
                                                   spec.scale);
        addAndMakeVisible (knob.get());
        knobs.push_back (std::move (knob));
    }

    setSize (680, 230);
}

TapeSweetEditor::~TapeSweetEditor()
{
    setLookAndFeel (nullptr);
}

void TapeSweetEditor::paint (juce::Graphics& g)
{
    g.fillAll (Col::bg);

    auto titleBounds = getLocalBounds().removeFromTop (56).toFloat();
    auto titleFont = juce::Font (juce::FontOptions (13.0f)).withExtraKerningFactor (0.46f);
    g.setFont (titleFont);
    g.setColour (Col::ink);
    g.drawText ("TAPESWEET", titleBounds, juce::Justification::centred);

    g.setColour (Col::knobTrack);
    auto line = getLocalBounds().removeFromTop (57).removeFromBottom (1).reduced (40, 0);
    g.fillRect (line);
}

void TapeSweetEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (64);
    bounds.removeFromBottom (14);
    bounds.reduce (18, 0);

    const int n = (int) knobs.size();
    const int slot = bounds.getWidth() / n;
    for (int i = 0; i < n; ++i)
        knobs[(size_t) i]->setBounds (bounds.removeFromLeft (slot).reduced (4, 0));
}
