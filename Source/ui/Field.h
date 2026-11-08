// The field: dendrite's one surface. True black, with living strands drifting in it, and
// a point of light you drag: up for more echo (feed), right for more space.
//
// Every time the music hits, a dendrite grows from the light. Its timing comes from the
// echo itself (a branch appears when a repeat plays: a dotted branch keeps its side, a
// quarter branch crosses, as bright as that repeat), but its shape is chance: every tree
// gets its own temper (how it curls, forks, wanders, how long its limbs are).
//
// The growth is a soft body. Young branches hang on the light and trail behind it when
// you drag; older ones root into the tissue, so dragging stretches them thin until they
// peel off and stay where they are. Throws grow white; the itch's heads grow dark red.
//
// Throw has no button: grab the light to move it, touch anywhere else in the dark to throw
// (a nerve runs from your touch to the light). The throw parameter stays mappable in Live.
#pragma once

#include "../Processor.h"
#include "Theme.h"

namespace dendrite::ui
{
class Field : public juce::Component
{
public:
    explicit Field (DendriteProcessor&);
    ~Field() override;

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

    // For the snapshot tool: move the light or touch the field without a mouse.
    void simulateTouch (juce::Point<float> unit, bool down);
    void simulateMove (juce::Point<float> unit);

private:
    enum Kind : uint8_t { echoKind, throwKind, screamKind, laughKind, manipulateKind, swarmKind, ghostKind };

    // One tree's character, drawn at its birth and inherited by every branch.
    struct Temper
    {
        float curl = 0.0f, fork = 0.3f, reach = 1.0f, bend = 0.3f, jolt = 0.1f, peel = 120.0f, stretch = 2.6f;
    };

    // A joint of the growth: a point mass hanging on its parent (or on the light).
    struct Joint
    {
        juce::Point<float> p, prev, home, dir;
        int parent = -1;          // >= 0: a joint; tethered: on the light; rooted: on its home
        uint32_t parentUid = 0, uid = 0;
        float rest = 0.0f, bend = 0.0f, level = 0.0f, life = 2.0f, grow = 0.1f;
        double born = 0.0;
        Temper temper;
        uint8_t gen = 0;
        Kind kind = echoKind;
        bool alive = false, twig = false;
    };
    static constexpr int tethered = -1, rooted = -2;

    struct Due
    {
        double at = 0.0;
        int joint = -1;
        uint32_t uid = 0;
        bool quarter = false;
    };

    // The tissue: slow strands that grow in, sway, and retract, forever replaced.
    struct Strand
    {
        int first = 0, count = 0, maxOrder = 1;
        double born = 0.0;
        float life = 40.0f, growIn = 8.0f, brightness = 0.2f;
        juce::Colour colour;
    };
    struct Fibre
    {
        juce::Point<float> rest;
        int parent = -1; // index within the strand, -1 for its root
        uint8_t depth = 0;
        uint16_t order = 0;
    };
    struct LitFibre
    {
        juce::Line<float> line;
        juce::Colour colour;
        float width = 1.0f;
    };
    struct Spark
    {
        int strand = -1, fibre = -1;
        float along = 0.0f, speed = 100.0f, level = 0.5f;
        bool itch = false; // impulses run dark red while the itch plays
    };

    juce::Rectangle<float> inner() const;
    juce::Point<float> toScreen (juce::Point<float> unit) const;
    juce::Point<float> toUnit (juce::Point<float> screen) const;
    juce::Point<float> pointUnit() const { return { space, 1.0f - feed }; }
    juce::Point<float> light() const { return toScreen (pointUnit()); }

    int addJoint (Joint&);
    bool parentOf (const Joint&, Joint*& out);
    void spawnRoot (float level, Kind kind, double now);
    void spawnFrom (int parent, bool quarter, double now);
    void growHeads (double now);
    void simulate (float frames);
    void seedStrand (Strand&, double now, bool midLife);
    void tickTissue (double now, float dt);
    juce::Point<float> flow (juce::Point<float> p, float t) const;
    juce::Point<float> fibreAt (const Strand&, int fibre) const;
    void paintTissue (juce::Graphics&);
    void paintGrowth (juce::Graphics&);
    void setFromUnit (juce::Point<float> unit);
    void setThrow (bool on);
    float gauss();

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::ParameterAttachment feedLink, spaceLink, throwLink;
    float feed = DendriteProcessor::defaultFeed, space = DendriteProcessor::defaultSpace;

    std::vector<Joint> joints;
    std::vector<Due> due;
    uint32_t nextUid = 1;
    juce::Random random;
    double now = 0.0, lastTick = -1.0;

    std::vector<Strand> strands;
    std::vector<Fibre> fibres;
    std::vector<Spark> sparks;
    juce::Array<LitFibre> lit;
    double nextSpark = 0.0;

    // the itch heads' own growth
    float lastLamp[dendrite::Heads::count] {};
    int screamTip = -1, laughTip = -1, manipulateLast = -1;
    juce::Point<float> manipulateCentre;
    float screamAngle = 0.0f, manipulateAngle = 0.0f, manipulateRadius = 0.0f, manipulateTurn = 1.0f, laughSide = 1.0f;
    double lastScream = 0.0, lastManipulate = 0.0, lastSwarm = 0.0, lastGhost = 0.0, ghostStart = 0.0;
    float screamTurn = 1.0f, ghostSide = 1.0f;

    juce::Image glowOchre, glowIvory, glowRed, glowCool;
    juce::Point<float> lastLight, lightStep, lastMouse, touch;
    float lightSpeed = 0.0f;
    bool dragging = false, hovering = false, touching = false, throwHeld = false, grabbedLight = false;
    float labelAlpha = 0.0f, pointGlow = 0.0f, touchGlow = 0.0f, throwGlow = 0.0f;
};
} // namespace dendrite::ui
