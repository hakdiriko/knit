// The field: dendrite's one surface. True black, a living tissue drifting in it, and a
// point of light you drag: up for more echo (feed), right for more space.
//
// Three layers, one physics:
//   the medium   a coarse fluid (Medium.h). Dragging the light stirs it, every hit pushes
//                a ring of it outwards, and a slow curl noise keeps it moving.
//   the tissue   strands that grow in, live for a while and pull back, forever replaced.
//                They are rods carried by the medium, so they sway in the light's wake
//                and spring back to their shape.
//   the growth   every hit grows a dendrite from the light. Its timing comes from the echo
//                (a branch appears when a repeat plays: a dotted branch keeps its side, a
//                quarter branch crosses, as bright as that repeat). Its shape comes from
//                space colonisation: each tree scatters its own cloud of points around the
//                light and its branches grow towards the ones nearest them, so trees fill
//                space the way real dendrites and leaf veins do, and no two are alike.
//
// Tissue and growth are simulated with XPBD (extended position-based dynamics, Macklin and
// Müller) in small substeps: stretch and bend are springs with real stiffnesses, so a
// branch drapes, whips and recoils; young growth hangs on the light, older growth roots,
// and a branch pulled past its limit tears off and stays.
//
// Throw has no button: grab the light to move it, touch anywhere else in the dark to throw
// (a nerve runs from your touch to the light). The throw parameter stays mappable in Live.
#pragma once

#include "../Processor.h"
#include "Medium.h"
#include "Theme.h"

namespace dendrite::ui
{
class Field : public juce::Component, public juce::TooltipClient
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
    juce::String getTooltip() override;

    // For the snapshot tool: move the light or touch the field without a mouse.
    void simulateTouch (juce::Point<float> unit, bool down);
    void simulateMove (juce::Point<float> unit);
    void simulateHover (juce::Point<float> unit);
    // For the snapshot tool's with/without comparison: let strands join (synapses) or not.
    void setSynapses (bool on) { synapsesOn = on; }

private:
    enum Kind : uint8_t { echoKind, throwKind, screamKind, laughKind, manipulateKind, swarmKind, ghostKind };

    // One tree's character, drawn at its birth and inherited by every branch.
    struct Temper
    {
        float fork = 0.3f, reach = 1.0f, bend = 0.3f, wander = 0.3f, peel = 120.0f, stretch = 2.6f, stiff = 1.0f;
    };

    // The cloud of points a tree grows towards (space colonisation).
    struct Tree
    {
        static constexpr int points = 32;
        std::array<juce::Point<float>, points> attractors {};
        std::array<bool, points> open {};
        float reach = 100.0f;
        uint32_t uid = 0;
        double born = 0.0;
    };

    // A joint of the growth: a point mass on a rod to its parent (or on the light).
    struct Joint
    {
        juce::Point<float> p, v, prev, home, dir;
        int parent = -1; // >= 0: a joint; tethered: on the light; rooted: on its home
        uint32_t parentUid = 0, uid = 0, treeUid = 0;
        int tree = -1;
        float rest = 0.0f, bendRest = 0.0f, bend = 0.0f, level = 0.0f, life = 2.0f, grow = 0.1f;
        double born = 0.0;
        Temper temper;
        uint8_t gen = 0;
        Kind kind = echoKind;
        bool alive = false, twig = false, end = true; // end: the last piece of a limb, where it forks on
    };
    static constexpr int tethered = -1, rooted = -2;

    struct Due
    {
        double at = 0.0;
        int joint = -1;
        uint32_t uid = 0;
        bool quarter = false;
    };

    struct Strand
    {
        int first = 0, count = 0, maxOrder = 1;
        double born = 0.0;
        float life = 40.0f, growRate = 3.0f, brightness = 0.2f; // growRate: how fast its tips creep out (px/s)
        float shiver = 0.0f; // a flash of light as a hit's wave passes through it
        // impulses: a hit's wave enters the strand at its root and runs out to every tip
        struct Pulse { double t0 = -1.0; float strength = 0.0f; bool red = false; };
        std::array<Pulse, 3> pulses {};
        uint32_t lastWave = 0;
        float reach = 0.0f; // the longest path from root to tip (px)
        float witherNow = 0.0f; // 0..1 as it withers at the end of its life
        float swayAmp = 0.15f, swayRate = 0.2f, swayPhase = 0.0f; // its own slow sway (rad, rad/s)
    };
    struct Fibre
    {
        juce::Point<float> rest, restBase, p, v, prev, flowVel; // restBase: the shape it grew in; rest: that shape, swaying
        int parent = -1; // index within the strand, -1 for its root
        float length = 0.0f, bendRest = 0.0f, path = 0.0f; // path: distance from the root along the strand
        float shake = 0.0f, shakeLast = 0.0f, shakeHz = 11.0f; // the tremor a passing wave leaves (px)
        double shakeAt = -1.0;
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
        bool itch = false; // impulses run red while the itch plays
    };

    juce::Rectangle<float> inner() const;
    juce::Point<float> toScreen (juce::Point<float> unit) const;
    juce::Point<float> toUnit (juce::Point<float> screen) const;
    juce::Point<float> pointUnit() const { return { space, 1.0f - feed }; }
    juce::Point<float> light() const { return toScreen (pointUnit()); }

    int addJoint (Joint&);
    Joint* parentOf (const Joint&);
    int newTree (juce::Point<float> centre, float reach, double now);
    juce::Point<float> colonise (const Joint& parent, juce::Point<float> fallback, float length);
    void spawnRoot (float level, Kind kind, double now);
    void spawnFrom (int parent, bool quarter, double now);
    void growHeads (double now);
    void simulate (float dt);
    void seedStrand (Strand&, double now, bool midLife);
    void tickSparks (double now, float dt);
    void tremor (juce::Point<float> origin, float strength, double at);
    void tickTremors();
    void tickSynapses (float dt);
    void solveSynapses (float h);
    void paintSynapses (juce::Graphics&);
    juce::Colour growthColour (Kind, float vis) const;
    void paintTissue (juce::Graphics&);
    void paintGrowth (juce::Graphics&);
    void setFromUnit (juce::Point<float> unit);
    void setThrow (bool on);
    float gauss();

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::ParameterAttachment feedLink, spaceLink, throwLink;
    float feed = DendriteProcessor::defaultFeed, space = DendriteProcessor::defaultSpace;

    Medium medium;
    std::vector<Joint> joints;
    std::vector<juce::Point<float>> jointFlows;
    std::vector<int> freeJoints;
    int aliveJoints = 0;
    std::vector<Tree> trees;
    std::vector<Due> due;
    uint32_t nextUid = 1;
    juce::Random random;
    double now = 0.0, lastTick = -1.0;

    std::vector<Strand> strands;
    std::vector<Fibre> fibres;
    std::vector<Spark> sparks;

    // every hit (and, softer, every repeat) sends a wave through the tissue from where it grew
    struct Wave
    {
        juce::Point<float> origin;
        double born = 0.0;
        float strength = 0.0f, reached = 0.0f;
        uint32_t id = 0;
        bool red = false;
    };
    uint32_t nextWave = 1;
    float hitGlow = 0.0f, bodyRel = 0.0f; // hits and the held level, against the track's own recent loudness
    std::vector<Wave> waves;
    juce::Array<LitFibre> lit;
    struct PulseHead { juce::Point<float> at; float level = 0.0f; bool red = false; };
    std::vector<PulseHead> pulseHeads;
    std::vector<juce::Point<float>> growthTips;

    // synapses: where a strand's tip touches another strand they join with a soft spring,
    // pull each other taut, pass impulses across, and tear apart when stretched too far
    struct Synapse
    {
        int sa = -1, fa = -1, sb = -1, fb = -1; // strand and fibre at each end
        double bornA = 0.0, bornB = 0.0;        // the strands' birth times, to notice a reseed
        double born = 0.0;
        float rest = 4.0f, life = 30.0f, stretch = 0.0f, flash = 0.0f;
        double crossedA = -1.0, crossedB = -1.0;  // the last impulse each way, so one crosses once
    };
    std::vector<Synapse> synapses;
    struct Snap { juce::Point<float> at; double when = 0.0; };
    std::vector<Snap> snaps;
    double nextSynapseCheck = 0.0;
    bool synapsesOn = true;

    // hints: the two words at the edges while the mouse is over the field, a faint nerve from
    // the pointer while it rests in the dark, and a short wait before a press throws (a click
    // to focus the window is not a throw)
    bool hoverDark = false, pendingThrow = false;
    juce::Point<float> hoverAt;
    double pressAt = 0.0;
    float hoverDarkGlow = 0.0f, hintAlpha = 0.0f;
    static inline int dragsThisSession = 0;
    double nextSpark = 0.0;

    // the itch heads' own growth
    float lastLamp[dendrite::Heads::count] {};
    int screamTip = -1, manipulateLast = -1;
    juce::Point<float> manipulateCentre;
    float screamAngle = 0.0f, manipulateAngle = 0.0f, manipulateRadius = 0.0f, manipulateTurn = 1.0f, laughSide = 1.0f;
    double lastScream = 0.0, lastManipulate = 0.0, lastSwarm = 0.0, lastGhost = 0.0, ghostStart = 0.0;
    float screamTurn = 1.0f, ghostSide = 1.0f;

    juce::Image glowWhite, glowRed;
    juce::Point<float> lastLight, lightVelocity, lastMouse, touch;
    bool dragging = false, hovering = false, touching = false, throwHeld = false, grabbedLight = false;
    float labelAlpha = 0.0f, pointGlow = 0.0f, touchGlow = 0.0f, throwGlow = 0.0f, itchLit = 0.0f;
};
} // namespace dendrite::ui
