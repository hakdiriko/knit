// The field: dendrite's one surface. A slab of dark stone with faint mineral dendrites in
// it, and a point of light you drag: up for more echo (feed), right for more space.
//
// Every time the music hits, a dendrite grows from the point. It is drawn from the echo
// itself: a branch appears exactly when a repeat plays (a dotted branch that keeps its
// side, a quarter branch that crosses), as bright as that repeat, and spreads as far as
// the space takes it. Throws grow white; the itch's heads grow coral.
#pragma once

#include "../Processor.h"
#include "Theme.h"

namespace dendrite::ui
{
class Field : public juce::Component
{
public:
    explicit Field (DendriteProcessor&);

    void tick (double nowSeconds);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    float feedValue() const noexcept { return feed; }
    float spaceValue() const noexcept { return space; }
    bool isEngaged() const noexcept { return dragging || hovering; }

    std::function<void (const juce::String&)> onHint;

private:
    enum Kind : uint8_t { echoKind, throwKind, screamKind, laughKind, manipulateKind };

    struct Node
    {
        juce::Point<float> from, to, dir; // field units: x and y in 0..1, y down
        double born = 0.0;
        float level = 0.0f, life = 2.0f;
        uint8_t gen = 0;
        Kind kind = echoKind;
        bool alive = false;
    };
    struct Due
    {
        double at = 0.0;
        int parent = -1;
        bool quarter = false;
    };

    juce::Rectangle<float> inner() const;
    juce::Point<float> toScreen (juce::Point<float> unit) const;
    juce::Point<float> pointUnit() const { return { space, 1.0f - feed }; }
    int addNode (const Node&);
    void spawnRoot (float level, Kind kind, double now);
    void spawnChild (int parent, bool quarter, double now);
    void growHeads (double now);
    void setFromUnit (juce::Point<float> unit);
    void renderStone();

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::ParameterAttachment feedLink, spaceLink;
    float feed = DendriteProcessor::defaultFeed, space = DendriteProcessor::defaultSpace;

    std::vector<Node> nodes;
    std::vector<Due> due;
    juce::Random random { 0x51ed };
    double now = 0.0;

    // the itch heads' own growth
    float lastLamp[3] {};
    juce::Point<float> screamTip, manipulateCentre;
    float manipulateAngle = 0.0f, manipulateRadius = 0.0f, laughSide = 1.0f;
    double lastScream = 0.0, lastManipulate = 0.0;

    juce::Image stone, glowOchre, glowIvory, glowCoral;
    juce::Point<float> grabUnit;
    juce::Point<float> lastMouse;
    bool dragging = false, hovering = false;
    float labelAlpha = 0.0f, pointGlow = 0.0f;
};
} // namespace dendrite::ui
