#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class DendriteProcessor;

// Generic window (the real skin comes in Phase 3): five macro knobs plus an Output
// trim, Sync and Steady under Size, the itch's switches under Itch (Auto, After N
// bars, Build-up, Scratch), and an itch meter in the header that counts bars. Every knob shows its value (Ecology and Itch also name
// their regime), and double-click resets it to default.
class DendriteEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit DendriteEditor (DendriteProcessor&);
    ~DendriteEditor() override;

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
    void timerCallback() override;

    DendriteProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };
    Knob ecology, size, tone, itch, mix, output;
    juce::ToggleButton syncButton, steadyButton, autoButton, buildButton;
    juce::ComboBox barsBox;
    juce::TextButton scratchButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment, steadyAttachment, autoAttachment, buildAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> barsAttachment;

    // Itch meter state, refreshed by the timer.
    juce::Rectangle<int> meterArea;
    float meterLevel = 0.0f, barsHeard = 0.0f;
    int patience = 16, shownMove = -1;
    bool autoOn = false;
    unsigned lastScratchCount = 0;
    double showUntil = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteEditor)
};
