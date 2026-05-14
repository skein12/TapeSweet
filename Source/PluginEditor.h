#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class TapeSweetLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TapeSweetLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;
};

class TapeSweetEditor : public juce::AudioProcessorEditor
{
public:
    explicit TapeSweetEditor (TapeSweetProcessor&);
    ~TapeSweetEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct KnobControl;

    TapeSweetProcessor& proc;
    TapeSweetLookAndFeel laf;
    std::vector<std::unique_ptr<KnobControl>> knobs;
    std::vector<int> rowOf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeSweetEditor)
};
