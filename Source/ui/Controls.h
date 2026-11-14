// What lives under the field: the name, the itch and its heads.
#pragma once

#include "../Processor.h"
#include "Tape.h"
#include "Theme.h"

namespace dendrite::ui
{
// How long dendrite can stand the same thing (twelve points to drag across), and the five
// heads, which light up when it can't.
class ItchStrip : public juce::Component, public juce::TooltipClient
{
    // A rendered light (baked in Blender, Resources/lamps), kept rescaled to the screen's
    // own pixels so each frame draws it as a plain copy.
    struct Sprite
    {
        juce::Image source, scaled;
        void draw (juce::Graphics&, juce::Point<float> centre, float size, float opacity, float grow = 1.0f);
    };

public:
    explicit ItchStrip (DendriteProcessor&);
    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    void setHeadsArea (juce::Rectangle<float> r) { headsArea = r; }
    // the fly is the itch: its brain takes the heads' place, so their lamps are not drawn
    void setFly (bool on) { fly = on; repaint(); }
    // 240 at 880 wide; a wider window keeps the itch group centred in the room it gains
    float dotsLeft() const { return 240.0f + 0.5f * std::max (0.0f, (float) getWidth() - 880.0f); }

    // how much the strip is showing itself (the mouse is in it): the tape above follows it
    float getReveal() const noexcept { return reveal; }
    // keep: the lamps of the last itch stay as embers; pulling one takes that itch. The tape
    // above shows the ruler while a lamp is held.
    void setTape (KeepTape* t) { tape = t; }
    // the wordmark: click it to see first light again
    std::function<void()> onWordmark;
    juce::Rectangle<float> wordmarkArea() const;

    // first light: a puppet of the strip that shows the itch without touching anything
    struct Ghost
    {
        bool on = false;
        float repetition = 0.0f, progress = 0.0f, reveal = 0.0f, wordmark = 0.0f;
        float lamps[dendrite::Heads::count] {};
        float embers[dendrite::Heads::count] {};
    };
    void setGhost (const Ghost& g) { ghost = g; }
    juce::Point<float> lampCentre (int head) const;
    juce::Point<float> chosenDot() const { return dot (choice); }

    // For the snapshot tool: the mouse resting in the strip, and an ember lamp held.
    void simulateHover (bool on) { hovering = on; }
    void simulateLampPress (bool down);

private:
    juce::Rectangle<float> dotsArea() const;
    juce::Point<float> dot (int k) const;
    int dotAt (juce::Point<float>) const;

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::ParameterAttachment link, timeLink, dryLink;
    int timeChoice = 0;
    bool dryOn = true;
    juce::Point<float> mouseAt;
    juce::Rectangle<float> timeArea() const;
    juce::Rectangle<float> dryArea() const;
    Sprite beadOff, beadOn, lampOff, lampOn;
    juce::Rectangle<float> headsArea;
    int choice = DendriteProcessor::defaultItch;
    float repetition = 0.0f, progress = 0.0f, reveal = 0.0f;
    float lamps[dendrite::Heads::count] {};
    float embers[dendrite::Heads::count] {};
    uint32_t emberMark = 0;
    int lampAt (juce::Point<float>) const;
    bool lampPressed = false, lampDragged = false;
    KeepTape* tape = nullptr;
    Ghost ghost;
    float wordGlow = 0.0f;
    bool dragging = false, hovering = false, fly = false;
};
} // namespace dendrite::ui
