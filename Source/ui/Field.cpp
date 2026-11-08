#include "Field.h"

namespace dendrite::ui
{
namespace
{
    constexpr float stripHeight = 72.0f; // the itch strip lies over the bottom of the field
    constexpr float leftMargin = 46.0f, rightMargin = 46.0f, topMargin = 40.0f, bottomMargin = 40.0f + stripHeight;
    constexpr int jointBudget = 1100; // where pruning gets strict; the pool has room above it
    constexpr size_t maxJoints = 2400, maxDue = 1600, maxSparks = 48, maxTrees = 96;
    constexpr int strandCount = 28, fibresPerStrand = 60;
    constexpr float grabRadius = 30.0f;
    constexpr int substeps = 6;
    constexpr float maxSpeed = 2500.0f; // px/s: a guard, never reached in normal use

    juce::Point<float> rotated (juce::Point<float> v, float a)
    {
        const float c = std::cos (a), s = std::sin (a);
        return { v.x * c - v.y * s, v.x * s + v.y * c };
    }

    juce::Point<float> normalised (juce::Point<float> v)
    {
        const float l = v.getDistanceFromOrigin();
        return l > 1.0e-6f ? v / l : juce::Point<float> { 0.0f, -1.0f };
    }

    juce::Point<float> quad (juce::Point<float> a, juce::Point<float> c, juce::Point<float> e, float t)
    {
        const float u = 1.0f - t;
        return a * (u * u) + c * (2.0f * u * t) + e * (t * t);
    }

    // XPBD with one projection per substep ("small steps"): a constraint of stiffness k
    // (1/s², per unit mass) has compliance alpha = 1/k, scaled by 1/h².
    float alphaTilde (float stiffness, float h) { return stiffness > 0.0f ? 1.0f / (stiffness * h * h) : 1.0e9f; }

    // keep two points `rest` apart; returns how stretched the pair is (length / rest)
    float solveDistance (juce::Point<float>& a, float wa, juce::Point<float>& b, float wb, float rest, float at)
    {
        const auto d = b - a;
        const float len = d.getDistanceFromOrigin();
        if (len < 1.0e-5f || wa + wb <= 0.0f)
            return 1.0f;
        const float lambda = -(len - rest) / (wa + wb + at);
        const auto n = d / len;
        a -= n * (wa * lambda);
        b += n * (wb * lambda);
        return len / std::max (1.0e-3f, rest);
    }

    // pull a point towards a target
    void solveAnchor (juce::Point<float>& p, juce::Point<float> target, float at)
    {
        p += (target - p) * (1.0f / (1.0f + at));
    }

    juce::Point<float> clampSpeed (juce::Point<float> v)
    {
        const float s = v.getDistanceFromOrigin();
        return s > maxSpeed ? v * (maxSpeed / s) : v;
    }

    // how far a strand has grown in (or pulled back), in fibre orders
    float grownOrder (double age, float life, float growIn, int maxOrder)
    {
        const float a = (float) age;
        float grown = std::min (1.0f, a / growIn) * (float) maxOrder;
        const float left = life - a;
        if (left < 7.0f)
            grown = std::min (grown, (float) maxOrder * std::max (0.0f, left / 7.0f));
        return grown;
    }
} // namespace

Field::Field (DendriteProcessor& p)
    : processor (p),
      feedLink (*p.params.getParameter ("feed"), [this] (float v) { feed = v; }),
      spaceLink (*p.params.getParameter ("space"), [this] (float v) { space = v; }),
      throwLink (*p.params.getParameter ("throw"), [this] (float v) { throwHeld = v > 0.5f; }),
      random (juce::Random::getSystemRandom().nextInt64())
{
    feedLink.sendInitialUpdate();
    spaceLink.sendInitialUpdate();
    throwLink.sendInitialUpdate();
    joints.reserve (maxJoints);
    freeJoints.reserve (maxJoints);
    jointFlows.reserve (maxJoints);
    trees.resize (maxTrees);
    due.reserve (maxDue);
    sparks.reserve (maxSparks);
    strands.resize (strandCount);
    fibres.resize ((size_t) (strandCount * fibresPerStrand));
    glowWhite = makeGlow (colour::ink, 64);
    glowRed = makeGlow (colour::red, 64);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setRepaintsOnMouseActivity (false);
}

Field::~Field()
{
    if (touching)
        setThrow (false); // never leave a throw held when the window closes mid-touch
}

juce::Rectangle<float> Field::inner() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (leftMargin).withTrimmedBottom (bottomMargin)
        .withTrimmedTop (topMargin).withTrimmedRight (rightMargin);
}

juce::Point<float> Field::toScreen (juce::Point<float> unit) const
{
    const auto r = inner();
    return { r.getX() + unit.x * r.getWidth(), r.getY() + unit.y * r.getHeight() };
}

juce::Point<float> Field::toUnit (juce::Point<float> s) const
{
    const auto r = inner();
    return { (s.x - r.getX()) / r.getWidth(), (s.y - r.getY()) / r.getHeight() };
}

float Field::gauss()
{
    // cheap and good enough: the sum of three uniforms
    return (random.nextFloat() + random.nextFloat() + random.nextFloat() - 1.5f) * 2.0f;
}

void Field::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    medium.setSize ((float) getWidth(), (float) getHeight());
    lastLight = light();
    for (auto& s : strands)
        seedStrand (s, now, true);
}

//==============================================================================
// The tissue

void Field::seedStrand (Strand& s, double t, bool midLife)
{
    const auto b = getLocalBounds().toFloat();
    if (b.isEmpty())
        return;
    const int index = (int) (&s - strands.data());
    s.first = index * fibresPerStrand;
    s.count = 0;
    s.maxOrder = 1;

    juce::Point<float> start;
    float angle = 0.0f;
    if (random.nextFloat() < 0.55f)
    {
        // from beyond an edge, reaching in
        const int side = random.nextInt (4);
        const float u = random.nextFloat();
        start = side == 0 ? juce::Point<float> { b.getX() + u * b.getWidth(), b.getY() - 8.0f }
              : side == 1 ? juce::Point<float> { b.getRight() + 8.0f, b.getY() + u * b.getHeight() }
              : side == 2 ? juce::Point<float> { b.getX() + u * b.getWidth(), b.getBottom() + 8.0f }
                          : juce::Point<float> { b.getX() - 8.0f, b.getY() + u * b.getHeight() };
        angle = (float) side * 0.5f * juce::MathConstants<float>::pi + 0.5f * juce::MathConstants<float>::pi + gauss() * 0.5f;
    }
    else
    {
        start = { b.getX() + random.nextFloat() * b.getWidth(), b.getY() + random.nextFloat() * b.getHeight() };
        angle = random.nextFloat() * juce::MathConstants<float>::twoPi;
    }

    auto place = [&] (int me, juce::Point<float> q, int parent, uint8_t depth, uint16_t order) {
        auto& f = fibres[(size_t) (s.first + me)];
        f.rest = f.p = f.prev = q;
        f.v = f.flowVel = {};
        f.parent = parent;
        f.depth = depth;
        f.order = order;
        f.length = parent >= 0 ? q.getDistanceFrom (fibres[(size_t) (s.first + parent)].rest) : 0.0f;
        const int grand = parent >= 0 ? fibres[(size_t) (s.first + parent)].parent : -1;
        f.bendRest = grand >= 0 ? q.getDistanceFrom (fibres[(size_t) (s.first + grand)].rest) : 0.0f;
    };

    struct Arm { juce::Point<float> p; float angle; int parent; uint8_t depth; int steps; uint16_t order; };
    Arm stack[24];
    int top = 0;
    place (0, start, -1, 0, 0);
    s.count = 1;
    stack[top++] = { start, angle, 0, 0, 10 + random.nextInt (22), 0 };
    const float wander = 0.18f + 0.3f * random.nextFloat();
    while (top > 0 && s.count < fibresPerStrand)
    {
        auto arm = stack[--top];
        for (int k = 0; k < arm.steps && s.count < fibresPerStrand; ++k)
        {
            arm.angle += gauss() * wander;
            const float len = (6.0f + 9.0f * random.nextFloat()) * std::pow (0.86f, (float) arm.depth);
            const auto q = arm.p + juce::Point<float> (std::cos (arm.angle), std::sin (arm.angle)) * len;
            const int me = s.count++;
            place (me, q, arm.parent, arm.depth, ++arm.order);
            s.maxOrder = std::max (s.maxOrder, (int) arm.order);
            arm.parent = me;
            arm.p = q;
            if (! b.expanded (40.0f).contains (q))
                break;
            if (arm.depth < 4 && top < 24 && random.nextFloat() < 0.14f)
                stack[top++] = { q, arm.angle + (random.nextBool() ? 1.0f : -1.0f) * (0.45f + 0.75f * random.nextFloat()), me,
                                 (uint8_t) (arm.depth + 1), (int) ((float) (arm.steps - k) * (0.4f + 0.4f * random.nextFloat())), arm.order };
        }
    }

    s.life = 30.0f + 50.0f * random.nextFloat();
    s.growIn = 5.0f + 9.0f * random.nextFloat();
    s.brightness = random.nextFloat() < 0.1f ? 0.34f : 0.08f + 0.17f * random.nextFloat();
    s.born = midLife ? t - (double) (random.nextFloat() * s.life * 0.85f) : t;
}

void Field::tickSparks (double t, float dt)
{
    for (auto& s : strands)
        if (t - s.born > (double) s.life)
            seedStrand (s, t, false);

    // impulses run along the strands toward their roots: more when the echo speaks, while
    // throwing, and in red while the itch plays
    auto& d = processor.display;
    const float out = d.outLevel.load (std::memory_order_relaxed);
    if (t >= nextSpark && sparks.size() < maxSparks)
    {
        const float rate = 0.35f + 7.0f * std::min (1.0f, 3.0f * out) + 10.0f * throwGlow + 8.0f * itchLit;
        nextSpark = t - std::log (std::max (1.0e-4f, random.nextFloat())) / rate;
        const int si = random.nextInt (strandCount);
        const auto& s = strands[(size_t) si];
        const float grown = grownOrder (t - s.born, s.life, s.growIn, s.maxOrder);
        const int fi = s.count > 1 ? 1 + random.nextInt (s.count - 1) : 0;
        if (fi > 0 && (float) fibres[(size_t) (s.first + fi)].order <= grown)
            sparks.push_back ({ si, fi, 0.0f, 40.0f + 110.0f * random.nextFloat(), 0.4f + 0.6f * random.nextFloat(), itchLit > 0.1f });
    }

    for (size_t i = 0; i < sparks.size();)
    {
        auto& sp = sparks[i];
        const auto& s = strands[(size_t) sp.strand];
        const auto& f = fibres[(size_t) (s.first + sp.fibre)];
        bool dead = f.parent < 0;
        if (! dead)
        {
            sp.along += sp.speed * dt / std::max (1.0f, f.length);
            while (sp.along >= 1.0f && ! dead)
            {
                sp.along -= 1.0f;
                sp.fibre = fibres[(size_t) (s.first + sp.fibre)].parent;
                dead = sp.fibre <= 0;
            }
        }
        if (dead)
        {
            sparks[i] = sparks.back();
            sparks.pop_back();
        }
        else
        {
            ++i;
        }
    }
}

void Field::paintTissue (juce::Graphics& g)
{
    const auto lp = light();
    const float reveal = 0.3f + 0.7f * pointGlow;
    const float breathe = 0.85f + 0.3f * processor.display.breath.load (std::memory_order_relaxed);
    const float sigma2 = 2.0f * 95.0f * 95.0f;
    juce::Path paths[3];
    lit.clearQuick();
    for (const auto& s : strands)
    {
        const double age = now - s.born;
        if (age < 0.0 || s.count < 2)
            continue;
        const float grown = grownOrder (age, s.life, s.growIn, s.maxOrder);
        const float alpha = s.brightness * std::min (1.0f, (float) age / 3.0f) * breathe;
        if (alpha < 0.005f)
            continue;
        for (auto& p : paths)
            p.clear();

        for (int i = 1; i < s.count; ++i)
        {
            const auto& f = fibres[(size_t) (s.first + i)];
            const float o = (float) f.order;
            if (o - 1.0f > grown)
                continue;
            const auto a = fibres[(size_t) (s.first + f.parent)].p;
            auto e = f.p;
            if (o > grown)
                e = a + (e - a) * (grown - (o - 1.0f));
            auto& path = paths[std::min<int> (2, f.depth)];
            path.startNewSubPath (a);
            path.lineTo (e);

            // the light reveals the tissue around it
            const float d2 = e.getDistanceSquaredFrom (lp);
            if (d2 < 4.0f * sigma2)
            {
                const float boost = std::exp (-d2 / sigma2) * reveal;
                if (boost > 0.03f)
                    lit.add ({ { a, e }, colour::ink.withAlpha (std::min (0.7f, boost * (0.2f + 1.4f * s.brightness))),
                               f.depth == 0 ? 1.3f : (f.depth == 1 ? 0.95f : 0.7f) });
            }
        }
        const float widths[] = { 1.3f, 0.9f, 0.6f };
        for (int k = 0; k < 3; ++k)
        {
            if (paths[k].isEmpty())
                continue;
            g.setColour (colour::tissue.withAlpha (alpha * (k == 0 ? 1.0f : (k == 1 ? 0.8f : 0.6f))));
            g.strokePath (paths[k], juce::PathStrokeType (widths[k], juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    for (const auto& l : lit)
    {
        g.setColour (l.colour);
        g.drawLine (l.line, l.width);
    }

    for (const auto& sp : sparks)
    {
        const auto& s = strands[(size_t) sp.strand];
        const auto& f = fibres[(size_t) (s.first + sp.fibre)];
        if (f.parent < 0)
            continue;
        const auto a = f.p, e = fibres[(size_t) (s.first + f.parent)].p;
        const auto p = a + (e - a) * sp.along;
        const float fade = std::min (1.0f, (float) (now - s.born) / 3.0f) * sp.level;
        g.setOpacity ((sp.itch ? 0.8f : 0.45f) * fade);
        g.drawImage (sp.itch ? glowRed : glowWhite, juce::Rectangle<float> (12.0f, 12.0f).withCentre (p));
        g.setColour ((sp.itch ? colour::redText : colour::ink).withAlpha (0.7f * fade));
        g.fillEllipse (juce::Rectangle<float> (1.6f, 1.6f).withCentre (p));
    }
}

//==============================================================================
// The growth

int Field::addJoint (Joint& j)
{
    j.uid = nextUid++;
    j.alive = true;
    j.prev = j.p;
    ++aliveJoints;
    if (! freeJoints.empty())
    {
        const int i = freeJoints.back();
        freeJoints.pop_back();
        joints[(size_t) i] = j;
        return i;
    }
    if (joints.size() < maxJoints)
    {
        joints.push_back (j);
        return (int) joints.size() - 1;
    }
    --aliveJoints;
    return -1;
}

Field::Joint* Field::parentOf (const Joint& j)
{
    if (j.parent < 0 || j.parent >= (int) joints.size())
        return nullptr;
    auto& p = joints[(size_t) j.parent];
    return p.alive && p.uid == j.parentUid ? &p : nullptr;
}

int Field::newTree (juce::Point<float> centre, float reach, double t)
{
    // reuse the oldest tree's slot
    int slot = 0;
    for (int i = 1; i < (int) trees.size(); ++i)
        if (trees[(size_t) i].born < trees[(size_t) slot].born)
            slot = i;
    auto& tree = trees[(size_t) slot];
    tree.uid = nextUid++;
    tree.born = t;
    tree.reach = reach;

    // a lopsided cloud: an ellipse at a random angle, thinner near the light, sometimes
    // pushed off to one side, so the tree has somewhere it wants to go
    const float angle = random.nextFloat() * juce::MathConstants<float>::twoPi;
    const float squash = 0.35f + 0.65f * random.nextFloat();
    const auto lean = juce::Point<float> (std::cos (angle), std::sin (angle)) * (reach * 0.45f * random.nextFloat());
    const auto bounds = getLocalBounds().toFloat().reduced (10.0f).withTrimmedBottom (stripHeight);
    for (int k = 0; k < Tree::points; ++k)
    {
        const float a = random.nextFloat() * juce::MathConstants<float>::twoPi;
        const float r = reach * (0.2f + 0.8f * std::sqrt (random.nextFloat()));
        const auto local = rotated ({ std::cos (a) * r, std::sin (a) * r * squash }, angle);
        tree.attractors[(size_t) k] = bounds.getConstrainedPoint (centre + lean + local);
        tree.open[(size_t) k] = true;
    }
    return slot;
}

juce::Point<float> Field::colonise (const Joint& parent, juce::Point<float> fallback, float length)
{
    // space colonisation (Runions et al.): grow towards the open points this tip is the
    // nearest to sense; points the branch reaches are used up
    if (parent.tree < 0 || trees[(size_t) parent.tree].uid != parent.treeUid)
        return fallback;
    auto& tree = trees[(size_t) parent.tree];
    const float influence = tree.reach * 0.9f;
    juce::Point<float> pull;
    int seen = 0;
    for (int k = 0; k < Tree::points; ++k)
    {
        if (! tree.open[(size_t) k])
            continue;
        const auto d = tree.attractors[(size_t) k] - parent.p;
        const float dist = d.getDistanceFromOrigin();
        if (dist > influence || dist < 1.0e-3f)
            continue;
        pull += d / dist;
        ++seen;
    }
    if (seen == 0)
        return fallback;
    const auto dir = normalised (normalised (pull) + fallback * 0.55f);
    const auto tip = parent.p + dir * length;
    const float kill = std::max (8.0f, length * 0.7f);
    for (int k = 0; k < Tree::points; ++k)
        if (tree.open[(size_t) k] && tree.attractors[(size_t) k].getDistanceFrom (tip) < kill)
            tree.open[(size_t) k] = false;
    return dir;
}

void Field::spawnRoot (float level, Kind kind, double t)
{
    auto& d = processor.display;
    const float theta = d.theta.load (std::memory_order_relaxed);
    const float pi = juce::MathConstants<float>::pi;
    const auto lp = light();

    Joint root;
    auto& tp = root.temper;
    tp.fork = 0.1f + 0.55f * random.nextFloat();
    tp.reach = random.nextFloat() < 0.12f ? 1.6f + 0.5f * random.nextFloat() : 0.55f + 0.85f * random.nextFloat();
    tp.bend = 0.05f + 0.4f * random.nextFloat();
    tp.wander = 0.15f + 0.5f * random.nextFloat();
    tp.peel = 55.0f + 150.0f * random.nextFloat();
    tp.stretch = 1.6f + 1.4f * random.nextFloat();
    tp.stiff = 0.4f + 1.2f * random.nextFloat(); // some trees are wiry, some limp

    const float a = -0.5f * pi + 0.55f * theta + (random.nextFloat() - 0.5f) * 1.6f * pi;
    root.p = root.home = lp;
    root.dir = { std::cos (a), std::sin (a) };
    root.parent = tethered;
    root.level = std::clamp (level, 0.25f, 1.0f) * (kind == throwKind ? 1.3f : 1.0f);
    root.life = (1.1f + 3.2f * feed) * (kind == throwKind ? 1.6f : 1.0f) * (0.8f + 0.45f * random.nextFloat());
    root.kind = kind;
    root.born = t;
    root.tree = newTree (lp, (45.0f + 150.0f * space) * tp.reach * (kind == throwKind ? 1.4f : 1.0f), t);
    root.treeUid = trees[(size_t) root.tree].uid;
    const int index = addJoint (root);
    if (index < 0)
        return;
    const uint32_t uid = joints[(size_t) index].uid;

    // the hit pushes the medium outwards: everything near the light flinches
    medium.pulse (lp, (110.0f + 160.0f * root.level) * (kind == throwKind ? 1.6f : 1.0f), 60.0f + 70.0f * space);

    // and bursts: a few short shoots at once, before the echo answers
    const int burst = random.nextInt (4);
    for (int k = 0; k < burst; ++k)
    {
        const float b = random.nextFloat() * juce::MathConstants<float>::twoPi;
        Joint twig;
        twig.dir = { std::cos (b), std::sin (b) };
        twig.rest = 3.0f + 9.0f * random.nextFloat() * root.level;
        twig.p = twig.home = lp + twig.dir * twig.rest;
        twig.v = twig.dir * (40.0f + 80.0f * random.nextFloat());
        twig.parent = index;
        twig.parentUid = uid;
        twig.bend = gauss() * 0.4f;
        twig.level = root.level * 0.6f;
        twig.life = root.life * 0.35f;
        twig.grow = 0.04f + 0.08f * random.nextFloat();
        twig.gen = 1;
        twig.kind = kind;
        twig.twig = true;
        twig.born = t;
        twig.temper = tp;
        addJoint (twig);
    }

    if (due.size() + 2 > maxDue)
        return;
    due.push_back ({ t + d.dotted.load (std::memory_order_relaxed), index, uid, false });
    due.push_back ({ t + d.quarter.load (std::memory_order_relaxed), index, uid, true });
}

void Field::spawnFrom (int parentIndex, bool quarter, double t)
{
    const Joint parent = joints[(size_t) parentIndex];
    auto& d = processor.display;
    const float g = quarter ? d.gainQuarter.load (std::memory_order_relaxed) : d.gainDotted.load (std::memory_order_relaxed);

    // a crowded field prunes harder, so a busy drum loop stays legible
    const float threshold = 0.06f + 0.22f * (float) aliveJoints / (float) jointBudget;
    const float level = parent.level * g * 1.12f * (0.7f + 0.6f * random.nextFloat());
    if (level < threshold || parent.gen >= 14 || random.nextFloat() < 0.07f)
        return;

    const auto& tp = parent.temper;
    const float spread = 0.2f + 0.9f * space + 1.1f * d.diffuse.load (std::memory_order_relaxed);
    const float curl = 3.5f * d.rotate.load (std::memory_order_relaxed);
    const Joint* grand = parentOf (parent);

    auto grow = [&] (float extraTurn, float lengthScale, bool twig) {
        // dotted keeps its side, quarter crosses; the tree's cloud and chance do the rest
        auto dir = parent.dir;
        if (quarter)
            dir.x = -dir.x;
        dir = rotated (dir, gauss() * spread * tp.wander + curl * (0.5f + random.nextFloat()) + extraTurn);

        float length = (16.0f + 34.0f * space) * (quarter ? 1.0f : 0.8f) * tp.reach * (0.45f + 1.1f * random.nextFloat())
                       * std::pow (0.93f, (float) parent.gen) * lengthScale;
        if (twig)
            length = 3.0f + 7.0f * random.nextFloat();
        else
            dir = colonise (parent, dir, length);

        if (! getLocalBounds().toFloat().expanded (20.0f).contains (parent.p + dir * length))
            return -1; // it would grow out of the field

        // a limb is a chain of short joints, so it can bend, drape and whip as a fibre
        const int pieces = twig ? 1 : std::clamp ((int) std::ceil (length / 11.0f), 1, 8);
        const float piece = length / (float) pieces;
        const float growTime = 0.05f + 0.2f * random.nextFloat();
        int previous = parentIndex;
        uint32_t previousUid = parent.uid;
        auto from = parent.p;
        auto heading = dir;
        const Joint* before = grand;
        for (int k = 0; k < pieces; ++k)
        {
            if (k > 0)
                heading = rotated (heading, gauss() * 0.12f * (0.5f + tp.wander));
            Joint child;
            child.p = child.home = from + heading * piece;
            child.v = parent.v + heading * (20.0f + 40.0f * random.nextFloat()); // it shoots out a little
            child.dir = heading;
            child.rest = piece;
            child.bendRest = before != nullptr ? child.p.getDistanceFrom (before->p) : 0.0f;
            child.parent = previous;
            child.parentUid = previousUid;
            child.tree = parent.tree;
            child.treeUid = parent.treeUid;
            child.bend = gauss() * tp.bend;
            child.level = twig ? level * 0.6f : level;
            child.life = parent.life * (twig ? 0.5f : 0.85f + 0.3f * random.nextFloat());
            child.grow = growTime / (float) pieces;
            child.gen = (uint8_t) (parent.gen + 1);
            child.kind = parent.kind;
            child.twig = twig;
            child.temper = tp;
            child.born = t + (double) (growTime * (float) k / (float) pieces); // it extends piece by piece
            child.end = k == pieces - 1;
            const int index = addJoint (child);
            if (index < 0)
                return previous == parentIndex ? -1 : previous;
            before = previous == parentIndex ? &joints[(size_t) parentIndex] : &joints[(size_t) previous];
            previous = index;
            previousUid = joints[(size_t) index].uid;
            from = child.p;
        }
        return previous;
    };
    auto schedule = [&] (int index) {
        if (index < 0 || due.size() + 2 > maxDue)
            return;
        const uint32_t uid = joints[(size_t) index].uid;
        due.push_back ({ t + d.dotted.load (std::memory_order_relaxed), index, uid, false });
        due.push_back ({ t + d.quarter.load (std::memory_order_relaxed), index, uid, true });
    };

    schedule (grow (0.0f, 1.0f, false));
    if (random.nextFloat() < tp.fork * 0.6f)
    {
        const int sibling = grow ((random.nextBool() ? 1.0f : -1.0f) * (0.4f + 0.8f * random.nextFloat()), 0.75f, false);
        if (random.nextFloat() < 0.6f)
            schedule (sibling);
    }
    if (random.nextFloat() < 0.35f)
        grow ((random.nextBool() ? 1.0f : -1.0f) * (0.6f + random.nextFloat()), 1.0f, true);
}

void Field::growHeads (double t)
{
    auto& d = processor.display;
    float lamp[dendrite::Heads::count];
    for (int k = 0; k < dendrite::Heads::count; ++k)
        lamp[k] = d.lamps[(size_t) k].load (std::memory_order_relaxed);
    const float pi = juce::MathConstants<float>::pi;
    const auto lp = light();

    auto chain = [&] (int from, juce::Point<float> start, float angle, float length, float level, float life, Kind kind) {
        Joint j;
        Joint* p = nullptr;
        if (from >= 0 && from < (int) joints.size() && joints[(size_t) from].alive)
            p = &joints[(size_t) from];
        j.dir = { std::cos (angle), std::sin (angle) };
        j.rest = length;
        j.p = j.home = (p != nullptr ? p->p : start) + j.dir * length;
        j.parent = p != nullptr ? from : tethered;
        j.parentUid = p != nullptr ? p->uid : 0;
        if (p != nullptr)
            if (const auto* grand = parentOf (*p))
                j.bendRest = j.p.getDistanceFrom (grand->p);
        j.bend = gauss() * 0.3f;
        j.level = level;
        j.life = life;
        j.grow = 0.04f;
        j.kind = kind;
        j.gen = p != nullptr ? (uint8_t) std::min (200, p->gen + 1) : 0;
        j.temper.peel = 90.0f + 80.0f * random.nextFloat();
        j.temper.stretch = 2.2f + 1.5f * random.nextFloat();
        j.born = t;
        return addJoint (j);
    };

    // scream: a tendril that circles as it climbs from the light
    if (lamp[0] > 0.04f)
    {
        if (lastLamp[0] <= 0.04f)
        {
            screamTip = -1;
            screamAngle = -0.5f * pi + gauss() * 0.6f;
            screamTurn = random.nextBool() ? 1.0f : -1.0f;
        }
        if (t - lastScream > 0.03 + 0.03 * random.nextFloat())
        {
            lastScream = t;
            screamAngle += screamTurn * (0.22f + 0.2f * lamp[0]) + gauss() * 0.35f;
            screamTip = chain (screamTip, lp, screamAngle, (5.0f + 8.0f * random.nextFloat()) * (0.6f + lamp[0]),
                               0.35f + 0.65f * lamp[0], 1.6f + 0.8f * random.nextFloat(), screamKind);
        }
    }

    // laugh: a short twig for every "ha", bouncing left and right
    if (lamp[1] > 0.3f && lastLamp[1] <= 0.3f)
    {
        const float a = laughSide > 0.0f ? -0.2f - 0.6f * random.nextFloat() : pi + 0.2f + 0.6f * random.nextFloat();
        laughSide = -laughSide;
        int tip = -1;
        float angle = a;
        for (int k = 0, n = 1 + random.nextInt (3); k < n; ++k)
        {
            tip = chain (tip, lp, angle, 10.0f + 22.0f * random.nextFloat(), 0.4f + 0.6f * lamp[1], 0.9f, laughKind);
            if (tip >= 0)
                joints[(size_t) tip].v = juce::Point<float> (std::cos (angle), std::sin (angle)) * 160.0f; // a hop
            angle += gauss() * 0.5f;
        }
    }

    // manipulate: a spiral curls around where the light was
    if (lamp[2] > 0.04f)
    {
        if (lastLamp[2] <= 0.04f)
        {
            manipulateCentre = lp;
            manipulateAngle = random.nextFloat() * juce::MathConstants<float>::twoPi;
            manipulateRadius = 8.0f + 10.0f * random.nextFloat();
            manipulateTurn = random.nextBool() ? 1.0f : -1.0f;
            manipulateLast = -1;
        }
        if (t - lastManipulate > 0.03)
        {
            lastManipulate = t;
            const float next = manipulateAngle + manipulateTurn * (0.18f + 0.12f * random.nextFloat());
            manipulateRadius += 1.2f + 1.6f * random.nextFloat();
            const auto from = manipulateLast >= 0 && joints[(size_t) manipulateLast].alive
                                  ? joints[(size_t) manipulateLast].p
                                  : manipulateCentre + juce::Point<float> (std::cos (manipulateAngle), std::sin (manipulateAngle)) * manipulateRadius;
            const auto to = manipulateCentre + juce::Point<float> (std::cos (next), std::sin (next)) * manipulateRadius;
            const auto step = to - from;
            manipulateLast = chain (manipulateLast, from, std::atan2 (step.y, step.x), step.getDistanceFromOrigin(),
                                    0.35f + 0.65f * lamp[2], 2.2f, manipulateKind);
            if (manipulateLast >= 0 && joints[(size_t) manipulateLast].parent == tethered)
                joints[(size_t) manipulateLast].parent = rooted;
            manipulateAngle = next;
            // the hand on the reel turns the medium with it
            medium.stir (to, juce::Point<float> (-std::sin (next), std::cos (next)) * (manipulateTurn * 90.0f * lamp[2]), 50.0f, 0.3f);
        }
    }

    // swarm: scattered grains that close in on the light as they lock into step
    if (lamp[3] > 0.04f && t - lastSwarm > 0.016)
    {
        lastSwarm = t;
        const float lock = std::clamp ((lamp[3] - 0.25f) / 0.75f, 0.0f, 1.0f);
        for (int k = 0, n = 1 + random.nextInt (3 + (int) (4.0f * lamp[3])); k < n; ++k)
        {
            const float a = random.nextFloat() * juce::MathConstants<float>::twoPi;
            const float r = (10.0f + 110.0f * (1.0f - lock)) * (0.4f + 0.6f * std::sqrt (random.nextFloat()));
            const juce::Point<float> offset { std::cos (a) * r, std::sin (a) * r };
            Joint j;
            j.p = j.home = lp + offset;
            j.v = -offset * (1.0f + 3.0f * lock) + juce::Point<float> (gauss(), gauss()) * (90.0f * (1.0f - lock)); // a buzz, drawn inward as they lock
            j.parent = rooted;
            j.level = (0.3f + 0.7f * lamp[3]) * (0.4f + 0.6f * random.nextFloat());
            j.life = 0.25f + 0.5f * random.nextFloat();
            j.kind = swarmKind;
            j.twig = true;
            j.born = t;
            addJoint (j);
        }
    }

    // ghost: pale strands rise from one side of the light, then above it, then the other
    // (a whisper at one ear passing behind your head to the other)
    if (lamp[4] > 0.04f && lastLamp[4] <= 0.04f)
    {
        ghostStart = t;
        ghostSide = random.nextBool() ? 1.0f : -1.0f;
    }
    if (lamp[4] > 0.04f && t - lastGhost > 0.07 + 0.06 * random.nextFloat())
    {
        lastGhost = t;
        int tip = -1;
        float angle = -0.5f * pi + gauss() * 0.7f;
        const float around = pi * std::clamp ((float) (t - ghostStart) / 2.0f, 0.0f, 1.0f);
        const auto start = lp + juce::Point<float> (ghostSide * 70.0f * std::cos (around), -30.0f * std::sin (around))
                           + juce::Point<float> (gauss(), gauss()) * 10.0f;
        for (int k = 0, n = 2 + random.nextInt (4); k < n; ++k)
        {
            tip = chain (tip, start, angle, 9.0f + 14.0f * random.nextFloat(), 0.2f + 0.4f * lamp[4], 3.0f + 2.0f * random.nextFloat(), ghostKind);
            if (k == 0 && tip >= 0)
                joints[(size_t) tip].parent = rooted;
            angle += gauss() * 0.35f;
        }
    }

    for (int k = 0; k < dendrite::Heads::count; ++k)
        lastLamp[k] = lamp[k];
}

void Field::simulate (float dt)
{
    if (dt <= 0.0f)
        return;
    const auto lp = light();
    const auto step = lp - lastLight;
    lastLight = lp;
    lightVelocity += (step / dt - lightVelocity) * 0.5f;

    // the medium: the light stirs it as it moves, a throw keeps breathing out of the light
    const float speed = lightVelocity.getDistanceFromOrigin();
    if (speed > 5.0f)
        medium.stir (lp, lightVelocity, 55.0f, 0.55f);
    if (throwGlow > 0.05f)
        medium.pulse (lp, 30.0f * throwGlow * dt * 60.0f, 90.0f);
    const float breath = processor.display.breath.load (std::memory_order_relaxed);
    medium.step (dt, (float) now, 9.0f + 6.0f * breath);

    // what the medium wants each particle to do (sampled once per frame)
    for (auto& f : fibres)
        f.flowVel = medium.velocity (f.p);
    std::vector<juce::Point<float>>& flows = jointFlows;
    flows.resize (joints.size());
    for (size_t i = 0; i < joints.size(); ++i)
        if (joints[i].alive)
            flows[i] = medium.velocity (joints[i].p) + (joints[i].kind == ghostKind ? juce::Point<float> (0.0f, -14.0f) : juce::Point<float>());

    const float h = dt / (float) substeps;
    const float carryTissue = 1.0f - std::exp (-3.0f * h), carryGrowth = 1.0f - std::exp (-5.0f * h);
    const float rodTissue = alphaTilde (9000.0f, h), bendTissue = alphaTilde (700.0f, h), shapeTissue = alphaTilde (5.0f, h);
    const float rootAnchor = alphaTilde (60.0f, h);

    for (int sub = 0; sub < substeps; ++sub)
    {
        // predict
        for (auto& f : fibres)
        {
            f.v += (f.flowVel - f.v) * carryTissue;
            f.prev = f.p;
            if (f.parent >= 0)
                f.p += f.v * h;
        }
        for (size_t i = 0; i < joints.size(); ++i)
        {
            auto& j = joints[i];
            if (! j.alive)
                continue;
            j.v += (flows[i] - j.v) * carryGrowth;
            j.prev = j.p;
            j.p += j.v * h;
        }

        // the tissue: rods that bend, and a memory of their shape
        for (const auto& s : strands)
            for (int i = 1; i < s.count; ++i)
            {
                auto& f = fibres[(size_t) (s.first + i)];
                auto& parent = fibres[(size_t) (s.first + f.parent)];
                solveDistance (parent.p, parent.parent >= 0 ? 1.0f : 0.0f, f.p, 1.0f, f.length, rodTissue);
                if (parent.parent >= 0)
                {
                    auto& grand = fibres[(size_t) (s.first + parent.parent)];
                    solveDistance (grand.p, grand.parent >= 0 ? 1.0f : 0.0f, f.p, 1.0f, f.bendRest, bendTissue);
                }
                solveAnchor (f.p, f.rest, shapeTissue);
            }

        // the growth
        for (auto& j : joints)
        {
            if (! j.alive)
                continue;
            const auto& tp = j.temper;
            const float age = (float) (now - j.born);
            if (j.parent == tethered)
            {
                j.p = lp; // held by the light
                if (lp.getDistanceFrom (j.home) > tp.peel)
                {
                    j.parent = rooted; // carried far enough: it lets go and roots where it is
                    j.home = j.p;
                }
                continue;
            }
            if (j.parent == rooted)
            {
                if (j.kind != swarmKind)
                    solveAnchor (j.p, j.home, rootAnchor);
                continue;
            }
            auto* p = parentOf (j);
            if (p == nullptr)
            {
                j.parent = rooted;
                j.home = j.p;
                continue;
            }
            // young growth hangs free on the light; older growth roots into the tissue
            if (age > 0.6f)
                solveAnchor (j.p, j.home, alphaTilde (std::min (25.0f, (age - 0.6f) * 20.0f) * tp.stiff, h));
            const float wp = p->parent == tethered ? 0.0f : 0.6f; // a parent is a little heavier than its child
            const float strain = solveDistance (p->p, wp, j.p, 1.0f, j.rest, alphaTilde (5000.0f * tp.stiff, h));
            if (j.bendRest > 0.0f)
                if (auto* grand = parentOf (*p))
                    solveDistance (grand->p, grand->parent == tethered ? 0.0f : 0.4f, j.p, 1.0f, j.bendRest, alphaTilde (500.0f * tp.stiff, h));
            if (strain > tp.stretch)
            {
                // pulled past its limit: it tears off and stays where it is
                j.parent = rooted;
                j.home = j.p;
            }
        }

        // velocities from what the constraints did
        const float inv = 1.0f / h;
        for (auto& f : fibres)
            f.v = f.parent >= 0 ? clampSpeed ((f.p - f.prev) * inv) : juce::Point<float>();
        for (auto& j : joints)
            if (j.alive)
                j.v = clampSpeed ((j.p - j.prev) * inv);
    }
}

juce::Colour Field::growthColour (Kind kind, float vis) const
{
    // monochrome, with one colour: the echo grows grey to white, and only the itch's
    // heads grow red
    vis = std::min (1.0f, vis);
    switch (kind)
    {
        case throwKind: return colour::ink;
        case ghostKind: return colour::red.withMultipliedBrightness (0.8f);
        case swarmKind:
        case screamKind:
        case laughKind:
        case manipulateKind: return colour::red.interpolatedWith (colour::redText, vis);
        case echoKind: break;
    }
    return colour::inkDim.interpolatedWith (colour::ink, vis);
}

void Field::paintGrowth (juce::Graphics& g)
{
    for (const auto& j : joints)
    {
        if (! j.alive)
            continue;
        const float age = (float) (now - j.born);
        if (age < 0.0f)
            continue;
        const float fade = std::pow (std::max (0.0f, 1.0f - age / j.life), 1.4f);
        const float vis = std::min (1.0f, 1.15f * std::sqrt (j.level)); // quiet repeats still read
        const float alpha = vis * fade;
        if (alpha < 0.01f)
            continue;

        const auto c = growthColour (j.kind, vis);
        const bool warm = c.getRed() > c.getGreen() + 30;
        const juce::Image* glow = warm ? &glowRed : &glowWhite;

        auto tip = j.p;
        if (const auto* p = parentOf (j))
        {
            // a smooth limb: a Catmull-Rom piece through grandparent, parent and joint, so
            // a chain of joints reads as one bending fibre rather than a zig-zag
            const auto a = p->p;
            const auto d = j.p - a;
            const float len = d.getDistanceFromOrigin();
            const auto* gp = parentOf (*p);
            const auto before = gp != nullptr ? gp->p : a - d;
            const auto after = j.p + d + juce::Point<float> (-d.y, d.x) * j.bend * 0.3f;
            const auto c1 = a + (j.p - before) / 6.0f, c2 = j.p - (after - a) / 6.0f;
            auto bez = [&] (float t) {
                const float u = 1.0f - t;
                return a * (u * u * u) + c1 * (3.0f * u * u * t) + c2 * (3.0f * u * t * t) + j.p * (t * t * t);
            };
            const float grow = std::min (1.0f, age / j.grow);
            tip = bez (grow);
            // thinner as it stretches, like a pulled thread
            const float thin = std::clamp (std::sqrt (j.rest / std::max (1.0f, len)), 0.3f, 1.15f);
            float width = (0.4f + 1.9f * vis) * std::pow (0.94f, (float) j.gen) * thin;
            if (j.twig || j.kind == ghostKind)
                width *= 0.6f;
            g.setColour (c.withAlpha (std::min (1.0f, alpha * 0.95f)));
            // a few straight pieces along the curve: native lines are cheap on every renderer
            const int pieces = len < 12.0f ? 1 : (len < 30.0f ? 2 : 4);
            auto from = a;
            for (int k = 1; k <= pieces; ++k)
            {
                const auto to = bez (grow * (float) k / (float) pieces);
                g.drawLine ({ from, to }, width);
                from = to;
            }
            if (j.end && ! j.twig && grow >= 1.0f) // a small bead where it forks on
            {
                g.setColour (c.withAlpha (alpha));
                g.fillEllipse (juce::Rectangle<float> (1.2f + width, 1.2f + width).withCentre (j.p));
            }
        }
        else if (j.kind == swarmKind)
        {
            g.setColour (c.withAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (1.8f, 1.8f).withCentre (j.p));
        }

        if (j.gen == 0 && j.parent != rooted && j.kind != swarmKind && j.kind != ghostKind)
        {
            const float size = 26.0f + 22.0f * vis;
            g.setOpacity (alpha * 0.8f);
            g.drawImage (*glow, juce::Rectangle<float> (size, size).withCentre (j.p));
        }
        else if (age < 0.7f && j.end && ! j.twig)
        {
            const float size = 8.0f + 18.0f * vis;
            g.setOpacity (alpha * 0.8f * (1.0f - age / 0.7f));
            g.drawImage (*glow, juce::Rectangle<float> (size, size).withCentre (tip));
        }
        else if (j.kind == swarmKind)
        {
            g.setOpacity (alpha * 0.5f);
            g.drawImage (*glow, juce::Rectangle<float> (9.0f, 9.0f).withCentre (j.p));
        }
    }
}

//==============================================================================

void Field::tick (double t)
{
    const float dt = lastTick < 0.0 ? 1.0f / 60.0f : (float) std::clamp (t - lastTick, 0.0, 0.05);
    lastTick = t;
    now = t;
    auto& d = processor.display;

    float brightest = 0.0f;
    for (const auto& lamp : d.lamps)
        brightest = std::max (brightest, lamp.load (std::memory_order_relaxed));
    itchLit = brightest;

    // new trees: one per onset the audio thread heard
    const int ready = d.fifo.getNumReady();
    if (ready > 0)
    {
        const auto scope = d.fifo.read (ready);
        auto take = [&] (int start, int size) {
            for (int i = 0; i < size; ++i)
            {
                const auto& o = d.onsets[(size_t) (start + i)];
                spawnRoot (o.level, o.thrown ? throwKind : echoKind, t);
                nextSpark = std::min (nextSpark, t + 0.02); // the tissue answers the hit
            }
        };
        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);
    }

    // repeats that are due grow their branches
    for (size_t i = 0; i < due.size();)
    {
        if (due[i].at <= t)
        {
            const auto item = due[i];
            due[i] = due.back();
            due.pop_back();
            if (item.joint >= 0 && item.joint < (int) joints.size() && joints[(size_t) item.joint].alive
                && joints[(size_t) item.joint].uid == item.uid)
                spawnFrom (item.joint, item.quarter, item.at);
        }
        else
        {
            ++i;
        }
    }

    for (size_t i = 0; i < joints.size(); ++i)
        if (joints[i].alive && t - joints[i].born > (double) joints[i].life)
        {
            joints[i].alive = false;
            freeJoints.push_back ((int) i);
            --aliveJoints;
        }

    growHeads (t);
    simulate (dt);
    tickSparks (t, dt);

    const float target = dragging || hovering ? 1.0f : 0.0f;
    labelAlpha += (target - labelAlpha) * 0.12f;
    const float out = d.outLevel.load (std::memory_order_relaxed);
    pointGlow += (std::min (1.0f, 4.0f * out) - pointGlow) * 0.15f;
    const float throwing = std::max (throwHeld ? 1.0f : 0.0f, d.throwing.load (std::memory_order_relaxed));
    throwGlow += (throwing - throwGlow) * (throwing > throwGlow ? 0.5f : 0.08f);
    touchGlow += ((touching ? 1.0f : 0.0f) - touchGlow) * (touching ? 0.5f : 0.1f);
    repaint();
}

void Field::paint (juce::Graphics& g)
{
    auto& d = processor.display;
    const auto lp = light();
    const float pi = juce::MathConstants<float>::pi;

    g.fillAll (colour::black);
    paintTissue (g);
    paintGrowth (g);
    g.setOpacity (1.0f);

    // the edges sink into the black, so the field has no frame (drawn as gradients: cheaper
    // than a full-window image when the window is scaled)
    {
        const auto b = getLocalBounds().toFloat();
        const float edge = 70.0f;
        auto band = [&] (juce::Rectangle<float> r, juce::Point<float> from, juce::Point<float> to) {
            g.setGradientFill (juce::ColourGradient (colour::black, from, colour::black.withAlpha (0.0f), to, false));
            g.fillRect (r);
        };
        const float floor = b.getBottom() - stripHeight + 6.0f;
        band (b.withHeight (edge), { 0.0f, 0.0f }, { 0.0f, edge });
        band (b.withTop (floor - edge).withHeight (edge), { 0.0f, floor }, { 0.0f, floor - edge });
        band (b.withWidth (edge).withBottom (floor), { 0.0f, 0.0f }, { edge, 0.0f });
        band (b.withTrimmedLeft (b.getWidth() - edge).withBottom (floor), { b.getRight(), 0.0f }, { b.getRight() - edge, 0.0f });
        g.setColour (colour::black);
        g.fillRect (b.withTop (floor));
    }

    // a nerve carries the throw from your touch to the light
    if (touchGlow > 0.01f)
    {
        const auto from = touch;
        const float amount = touchGlow;
        const auto dv = lp - from;
        const auto bow = medium.velocity ((from + lp) * 0.5f) * 0.25f;
        const auto mid = (from + lp) * 0.5f + juce::Point<float> (-dv.y, dv.x) * 0.12f + bow;
        juce::Path nerve;
        nerve.startNewSubPath (from);
        nerve.quadraticTo (mid, lp);
        g.setColour (colour::ink.withAlpha (0.12f * amount));
        g.strokePath (nerve, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour::ink.withAlpha (0.7f * amount));
        g.strokePath (nerve, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (int k = 0; k < 3; ++k) // impulses running to the light
        {
            const float ph = std::fmod ((float) now * 2.2f + (float) k / 3.0f, 1.0f);
            g.setOpacity (amount * (1.0f - ph * 0.5f));
            g.drawImage (glowWhite, juce::Rectangle<float> (14.0f, 14.0f).withCentre (quad (from, mid, lp, ph)));
        }
        g.setOpacity (0.6f * amount);
        g.drawImage (glowWhite, juce::Rectangle<float> (40.0f, 40.0f).withCentre (from));
        g.setColour (colour::ink.withAlpha (0.8f * amount));
        g.drawEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (from), 1.0f);
    }

    // the light: glows with what comes out, breathes with the echo, flares on a throw
    const float breath = d.breath.load (std::memory_order_relaxed);
    const float halo = 64.0f + 44.0f * pointGlow + 10.0f * breath + 70.0f * throwGlow;
    g.setOpacity (0.2f + 0.4f * pointGlow + 0.35f * throwGlow);
    g.drawImage (glowWhite, juce::Rectangle<float> (halo, halo).withCentre (lp));

    // a membrane, not a circle: it never holds the same shape
    juce::Path membrane;
    const float t = (float) now;
    const float radius = 11.0f + 2.5f * labelAlpha + 6.0f * throwGlow;
    for (int k = 0; k <= 48; ++k)
    {
        const float a = (float) k / 48.0f * 2.0f * pi;
        const float r = radius + 0.9f * std::sin (3.0f * a + 1.3f * t) + 0.6f * std::sin (5.0f * a - 0.9f * t + 2.0f)
                        + 0.4f * std::sin (7.0f * a + 2.1f * t);
        const juce::Point<float> q { lp.x + r * std::cos (a), lp.y + r * std::sin (a) };
        if (k == 0)
            membrane.startNewSubPath (q);
        else
            membrane.lineTo (q);
    }
    membrane.closeSubPath();
    g.setColour (colour::ink.withAlpha (0.3f + 0.35f * labelAlpha + 0.3f * throwGlow));
    g.strokePath (membrane, juce::PathStrokeType (1.0f));
    g.setColour (colour::ink);
    g.fillEllipse (juce::Rectangle<float> (7.0f + 3.0f * throwGlow, 7.0f + 3.0f * throwGlow).withCentre (lp));

    // where you are, in two words, only while you touch it
    if (labelAlpha > 0.02f)
    {
        const auto text = DendriteProcessor::feedName (feed) + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "))
                          + DendriteProcessor::spaceName (space);
        const auto r = inner();
        auto box = juce::Rectangle<float> (180.0f, 18.0f).withCentre (lp.translated (0.0f, lp.y < r.getBottom() - 40.0f ? 30.0f : -30.0f));
        g.setColour (colour::inkDim.withAlpha (0.85f * labelAlpha));
        g.setFont (fonts->make (fonts->regular, 11.5f, 0.06f));
        g.drawText (text, box, juce::Justification::centred);
    }
}

//==============================================================================

void Field::setFromUnit (juce::Point<float> unit)
{
    space = std::clamp (unit.x, 0.0f, 1.0f);
    feed = std::clamp (1.0f - unit.y, 0.0f, 1.0f);
    spaceLink.setValueAsPartOfGesture (space);
    feedLink.setValueAsPartOfGesture (feed);
}

void Field::setThrow (bool on)
{
    throwHeld = on;
    throwLink.setValueAsCompleteGesture (on ? 1.0f : 0.0f);
}

void Field::simulateTouch (juce::Point<float> unit, bool down)
{
    touching = down;
    touch = toScreen (unit);
}

void Field::simulateMove (juce::Point<float> unit)
{
    setFromUnit (unit);
}

void Field::mouseMove (const juce::MouseEvent& e)
{
    const bool near = e.position.getDistanceFrom (light()) < grabRadius;
    hovering = near;
    setMouseCursor (near ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::PointingHandCursor);
}

void Field::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
}

void Field::mouseDown (const juce::MouseEvent& e)
{
    lastMouse = e.position;
    const bool near = e.position.getDistanceFrom (light()) < grabRadius;

    if (e.position.y > (float) getHeight() - stripHeight)
        return;
    if (! near)
    {
        // touch the tissue anywhere: the nerve fires and the light throws
        touching = true;
        touch = e.position;
        setThrow (true);
        return;
    }

    dragging = true;
    grabbedLight = true;
    feedLink.beginGesture();
    spaceLink.beginGesture();
}

void Field::mouseDrag (const juce::MouseEvent& e)
{
    if (touching)
    {
        touch = e.position;
        return;
    }
    if (! grabbedLight)
        return;
    const auto r = inner();
    const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
    const auto delta = e.position - lastMouse;
    lastMouse = e.position;
    auto unit = pointUnit() + juce::Point<float> (delta.x / r.getWidth() * fine, delta.y / r.getHeight() * fine);
    setFromUnit ({ std::clamp (unit.x, 0.0f, 1.0f), std::clamp (unit.y, 0.0f, 1.0f) });
}

void Field::mouseUp (const juce::MouseEvent&)
{
    if (touching)
        setThrow (false);
    touching = false;
    if (grabbedLight)
    {
        feedLink.endGesture();
        spaceLink.endGesture();
    }
    grabbedLight = dragging = false;
}

void Field::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.getDistanceFrom (light()) > grabRadius * 1.5f)
        return; // a double tap in the dark is two throws, not a reset
    feedLink.setValueAsCompleteGesture (DendriteProcessor::defaultFeed);
    spaceLink.setValueAsCompleteGesture (DendriteProcessor::defaultSpace);
}

void Field::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float step = (e.mods.isShiftDown() ? 0.01f : 0.04f) * (w.isReversed ? -1.0f : 1.0f);
    if (std::abs (w.deltaX) > std::abs (w.deltaY))
        spaceLink.setValueAsCompleteGesture (std::clamp (space + step * w.deltaX / std::abs (w.deltaX), 0.0f, 1.0f));
    else if (std::abs (w.deltaY) > 0.0f)
        feedLink.setValueAsCompleteGesture (std::clamp (feed + step * w.deltaY / std::abs (w.deltaY), 0.0f, 1.0f));
}
} // namespace dendrite::ui
