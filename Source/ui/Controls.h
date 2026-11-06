// What lives under the field: the itch, its heads, and (in one of the throw options) the soma.
#pragma once

#include "../Processor.h"
#include "Theme.h"

namespace dendrite::ui
{
// Hold to throw: a cell body that lights up while you hold it. The send opens fully, the
// feedback rides up, the echo flies off around your head, and it falls back over a couple
// of bars after you let go.
class Soma : public juce::Component
{
public:
    explicit Soma (DendriteProcessor&);
    void tick (double now);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;

private:
    DendriteProcessor& processor;
    juce::ParameterAttachment link;
    juce::Image halo;
    bool held = false;
    float glow = 0.0f;
    double now = 0.0;
};

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
