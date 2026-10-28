#pragma once

#include "dsp/Tape.h"
#include <juce_audio_processors/juce_audio_processors.h>

class DendriteProcessor;

// Generic window (the real skin comes later): the tape across the top, three knobs
// under it. The tape shows its 4 bars as 64 slivers of oxide: dark where it is fresh,
// pale where it has worn, with the head moving across. Every knob shows its value and
// what that means, and double-click resets it.
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
    Knob wear, friction, room;

    juce::Rectangle<int> tapeArea;
    std::array<float, dendrite::dsp::Tape::cellsPerTape> shownWear {};
    float head = 0.0f;
    bool playing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteEditor)
};
