#pragma once

#include "Processor.h"
#include "ui/Controls.h"
#include "ui/Field.h"
#include "ui/FirstLight.h"
#include "ui/Tape.h"

// dendrite's window: true black, the field (drag the light) and a quiet strip under it
// with the itch and its heads. It scales from 75% to 150% and keeps its shape.
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
    dendrite::ui::Field& getField() { return field; }
    dendrite::ui::FirstLight& getFirstLight() { return firstLight; }
    dendrite::ui::ItchStrip& getItchStrip() { return itch; }

private:
    void timerCallback() override;
    void layout();

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
    dendrite::ui::ItchStrip itch;
    dendrite::ui::KeepTape tape;
    dendrite::ui::FirstLight firstLight { field, itch, tape };
    bool firstLightChecked = false;
    juce::TooltipWindow tooltips { this, 1800 }; // the hidden moves, told after a short rest
    double started = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteEditor)
};
