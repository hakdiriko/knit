#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class DendriteProcessor;

// Prototype window (the real skin comes later, without knobs): a pad you hold to
// throw what's playing into the echo, which also glows when dendrite throws on its
// own, and one strip for the amount. Both are plain parameters, so Live can map
// them to Push, a MIDI controller or an LFO.
class DendriteEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit DendriteEditor (DendriteProcessor&);
    ~DendriteEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class ThrowPad final : public juce::Component
    {
    public:
        explicit ThrowPad (juce::RangedAudioParameter& throwParameter) : parameter (throwParameter) {}

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        float glow = 0.0f;
        bool held = false;

    private:
        juce::RangedAudioParameter& parameter;
    };

    void timerCallback() override;

    DendriteProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;
    ThrowPad pad;
    juce::Slider amount;
    juce::Label amountLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteEditor)
};
