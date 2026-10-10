#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class ViberProcessor;

// Generic Phase 1 window: four macro knobs plus an Output trim. Every knob shows its
// value (Ecology also names its regime), and double-click resets it to default.
class ViberEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ViberEditor (ViberProcessor&);
    ~ViberEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setUpKnob (Knob&, const juce::String& paramID, const juce::String& name, const juce::String& hint);

    ViberProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };
    Knob ecology, size, tone, mix, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViberEditor)
};
