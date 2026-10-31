// The two things beside the field: the throw pad and the itch.
#pragma once

#include "../Processor.h"
#include "Theme.h"

namespace dendrite::ui
{
// Hold to throw: the send opens fully, the feedback rides up, the echo flies off somewhere
// around your head, and it all falls back over a couple of bars after you let go.
class ThrowPad : public juce::Component
{
public:
    explicit ThrowPad (DendriteProcessor&);
    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    std::function<void (const juce::String&)> onHint;

private:
    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::ParameterAttachment link;
    juce::Image halo;
    bool held = false, pressed = false;
    float glow = 0.0f;
};

// How long dendrite can stand the same thing, and the three heads that play when it can't.
class ItchPanel : public juce::Component
{
public:
    explicit ItchPanel (DendriteProcessor&);
    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    std::function<void (const juce::String&)> onHint;

private:
    juce::Rectangle<float> cellArea() const;
    int cellAt (juce::Point<float>) const;

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::ParameterAttachment link;
    juce::Image lampGlow;
    int choice = DendriteProcessor::defaultItch;
    float repetition = 0.0f, progress = 0.0f, boredom = 0.0f;
    float lamps[3] {};
    bool itching = false, dragging = false;
};
} // namespace dendrite::ui
