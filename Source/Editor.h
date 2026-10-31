#pragma once

#include "Processor.h"
#include "ui/Controls.h"
#include "ui/Field.h"

// dendrite's window: the field (drag the light), the throw pad and the itch. It scales
// from 75% to 150% and keeps its shape.
class DendriteEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DendriteEditor (DendriteProcessor&);
    ~DendriteEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 880, designHeight = 560;

    // For the snapshot tool: advance the animation to a given time without a timer.
    void animateTo (double seconds);

private:
    void timerCallback() override;

    class Canvas : public juce::Component
    {
    public:
        explicit Canvas (DendriteEditor& e) : editor (e) {}
        void paint (juce::Graphics&) override;

    private:
        DendriteEditor& editor;
    };

    DendriteProcessor& processor;
    dendrite::ui::LookAndFeel lookAndFeel;
    juce::SharedResourcePointer<dendrite::ui::Fonts> fonts;
    Canvas canvas { *this };
    dendrite::ui::Field field;
    dendrite::ui::ThrowPad throwPad;
    dendrite::ui::ItchPanel itch;
    juce::String hint;
    double started = 0.0;
    int frame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteEditor)
};
