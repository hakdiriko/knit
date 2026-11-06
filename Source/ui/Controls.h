// What lives under the field: the name, the itch and its heads.
#pragma once

#include "../Processor.h"
#include "Theme.h"

namespace dendrite::ui
{
// How long dendrite can stand the same thing (twelve points to drag across), and the five
// heads, which light up when it can't.
class ItchStrip : public juce::Component
{
public:
    explicit ItchStrip (DendriteProcessor&);
    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void setHeadsArea (juce::Rectangle<float> r) { headsArea = r; }
    static constexpr float dotsLeft = 240.0f;

private:
    juce::Rectangle<float> dotsArea() const;
    juce::Point<float> dot (int k) const;
    int dotAt (juce::Point<float>) const;

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::ParameterAttachment link;
    juce::Image lampGlow;
    juce::Rectangle<float> headsArea;
    int choice = DendriteProcessor::defaultItch;
    float repetition = 0.0f, progress = 0.0f, reveal = 0.0f;
    float lamps[dendrite::Heads::count] {};
    bool dragging = false, hovering = false;
};
} // namespace dendrite::ui
