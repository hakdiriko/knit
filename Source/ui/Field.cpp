#include "Field.h"

namespace dendrite::ui
{
namespace
{
    constexpr float stripHeight = 72.0f; // the itch strip lies over the bottom of the field
    constexpr float leftMargin = 46.0f, rightMargin = 46.0f, topMargin = 40.0f, bottomMargin = 40.0f + stripHeight;
    constexpr size_t maxJoints = 900, maxDue = 1600, maxSparks = 48;
    constexpr int strandCount = 28, fibresPerStrand = 60;
    constexpr float grabRadius = 30.0f;

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

    // a point along a quadratic curve, and the control point of its first part
    juce::Point<float> quad (juce::Point<float> a, juce::Point<float> c, juce::Point<float> e, float t)
    {
        const float u = 1.0f - t;
        return a * (u * u) + c * (2.0f * u * t) + e * (t * t);
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
    due.reserve (maxDue);
    sparks.reserve (maxSparks);
    strands.resize (strandCount);
    fibres.resize ((size_t) (strandCount * fibresPerStrand));
    glowOchre = makeGlow (colour::ochreHot, 64);
    glowIvory = makeGlow (colour::ink, 64);
    glowCoral = makeGlow (colour::coral, 64);
    glowCool = makeGlow (colour::cool, 64);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setRepaintsOnMouseActivity (false);
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
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0)
        return;

    // the edges sink into the black, so the field has no frame
    vignette = juce::Image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (vignette);
        const auto b = getLocalBounds().toFloat();
        const float edge = 70.0f;
        auto band = [&] (juce::Rectangle<float> r, juce::Point<float> from, juce::Point<float> to) {
            g.setGradientFill (juce::ColourGradient (colour::black, from, colour::black.withAlpha (0.0f), to, false));
            g.fillRect (r);
        };
        band (b.withHeight (edge), { 0.0f, 0.0f }, { 0.0f, edge });
        const float floor = b.getBottom() - stripHeight + 6.0f;
        g.setColour (colour::black);
        g.fillRect (b.withTop (floor));
        band (b.withTop (floor - edge).withHeight (edge), { 0.0f, floor }, { 0.0f, floor - edge });
        band (b.withWidth (edge), { 0.0f, 0.0f }, { edge, 0.0f });
        band (b.withTrimmedLeft (b.getWidth() - edge), { b.getRight(), 0.0f }, { b.getRight() - edge, 0.0f });
        g.setGradientFill (juce::ColourGradient (colour::black.withAlpha (0.0f), b.getCentreX(), b.getCentreY(),
                                                 colour::black.withAlpha (0.3f), b.getX(), b.getY(), true));
        g.fillRect (b);
    }

    lastLight = light();
    for (auto& s : strands)
        seedStrand (s, now, true);
}

//==============================================================================
// The tissue

juce::Point<float> Field::flow (juce::Point<float> p, float t) const
{
    // a few slow travelling waves: coherent, so neighbouring strands sway together
    return { 3.4f * std::sin (0.011f * p.y + 0.29f * t + 1.3f) + 2.2f * std::sin (0.023f * p.x - 0.17f * t + 0.4f)
                 + 1.1f * std::sin (0.043f * (p.x + p.y) + 0.53f * t),
             3.1f * std::sin (0.013f * p.x - 0.23f * t + 2.1f) + 2.0f * std::sin (0.021f * p.y + 0.19f * t + 5.0f)
                 + 1.0f * std::sin (0.037f * (p.x - p.y) - 0.47f * t) };
}

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

    struct Arm { juce::Point<float> p; float angle; int parent; uint8_t depth; int steps; uint16_t order; };
    Arm stack[24];
    int top = 0;
    fibres[(size_t) s.first] = { start, -1, 0, 0 };
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
            fibres[(size_t) (s.first + me)] = { q, arm.parent, arm.depth, ++arm.order };
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
    const float hue = random.nextFloat();
    s.colour = hue < 0.12f ? colour::cool.interpolatedWith (colour::tissue, 0.5f)
             : hue < 0.3f ? colour::inkDim
                          : colour::tissue;
    s.born = midLife ? t - (double) (random.nextFloat() * s.life * 0.85f) : t;
}

juce::Point<float> Field::fibreAt (const Strand& s, int i) const
{
    const auto& f = fibres[(size_t) (s.first + i)];
    return f.rest + flow (f.rest, (float) now) * (0.35f + 0.3f * (float) f.depth);
}

namespace
{
    // how far a strand has grown in (or retracted), in fibre orders
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

void Field::tickTissue (double t, float dt)
{
    for (auto& s : strands)
        if (t - s.born > (double) s.life)
            seedStrand (s, t, false);

    // impulses run along the strands toward their roots, more of them when the echo speaks
    auto& d = processor.display;
    const float out = d.outLevel.load (std::memory_order_relaxed);
    if (t >= nextSpark && sparks.size() < maxSparks)
    {
        const float rate = 0.35f + 7.0f * std::min (1.0f, 3.0f * out);
        nextSpark = t - std::log (std::max (1.0e-4f, random.nextFloat())) / rate;
        const int si = random.nextInt (strandCount);
        const auto& s = strands[(size_t) si];
        const float grown = grownOrder (t - s.born, s.life, s.growIn, s.maxOrder);
        const int fi = s.count > 1 ? 1 + random.nextInt (s.count - 1) : 0;
        if (fi > 0 && (float) fibres[(size_t) (s.first + fi)].order <= grown)
            sparks.push_back ({ si, fi, 0.0f, 40.0f + 110.0f * random.nextFloat(), 0.4f + 0.6f * random.nextFloat() });
    }

    for (size_t i = 0; i < sparks.size();)
    {
        auto& sp = sparks[i];
        const auto& s = strands[(size_t) sp.strand];
        const auto& f = fibres[(size_t) (s.first + sp.fibre)];
        bool dead = f.parent < 0;
        if (! dead)
        {
            const float len = std::max (1.0f, f.rest.getDistanceFrom (fibres[(size_t) (s.first + f.parent)].rest));
            sp.along += sp.speed * dt / len;
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
    const float sigma2 = 2.0f * 95.0f * 95.0f;
    juce::Path paths[3];
    for (const auto& s : strands)
    {
        const double age = now - s.born;
        if (age < 0.0 || s.count < 2)
            continue;
        const float grown = grownOrder (age, s.life, s.growIn, s.maxOrder);
        const float alpha = s.brightness * std::min (1.0f, (float) age / 3.0f);
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
            const auto a = fibreAt (s, f.parent);
            auto e = fibreAt (s, i);
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
                {
                    g.setColour (s.colour.interpolatedWith (colour::ochreHot, 0.5f).withAlpha (std::min (0.8f, boost * (0.25f + 1.6f * s.brightness))));
                    g.drawLine ({ a, e }, f.depth == 0 ? 1.3f : (f.depth == 1 ? 0.95f : 0.7f));
                }
            }
        }
        const float widths[] = { 1.3f, 0.9f, 0.6f };
        for (int k = 0; k < 3; ++k)
        {
            if (paths[k].isEmpty())
                continue;
            g.setColour (s.colour.withAlpha (alpha * (k == 0 ? 1.0f : (k == 1 ? 0.8f : 0.6f))));
            g.strokePath (paths[k], juce::PathStrokeType (widths[k], juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    for (const auto& sp : sparks)
    {
        const auto& s = strands[(size_t) sp.strand];
        const auto& f = fibres[(size_t) (s.first + sp.fibre)];
        if (f.parent < 0)
            continue;
        const auto a = fibreAt (s, sp.fibre), e = fibreAt (s, f.parent);
        const auto p = a + (e - a) * sp.along;
        const float fade = std::min (1.0f, (float) (now - s.born) / 3.0f) * sp.level;
        g.setOpacity (0.55f * fade);
        g.drawImage (glowOchre, juce::Rectangle<float> (12.0f, 12.0f).withCentre (p));
        g.setColour (colour::ink.withAlpha (0.7f * fade));
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
    for (size_t i = 0; i < joints.size(); ++i)
        if (! joints[i].alive)
        {
            joints[i] = j;
            return (int) i;
        }
    if (joints.size() < maxJoints)
    {
        joints.push_back (j);
        return (int) joints.size() - 1;
    }
    return -1;
}

bool Field::parentOf (const Joint& j, Joint*& out)
{
    if (j.parent < 0 || j.parent >= (int) joints.size())
        return false;
    auto& p = joints[(size_t) j.parent];
    if (! p.alive || p.uid != j.parentUid)
        return false;
    out = &p;
    return true;
}

void Field::spawnRoot (float level, Kind kind, double t)
{
    auto& d = processor.display;
    const float theta = d.theta.load (std::memory_order_relaxed);
    const float pi = juce::MathConstants<float>::pi;

    Joint root;
    auto& tp = root.temper;
    tp.curl = gauss() * 0.3f;
    tp.fork = 0.08f + 0.55f * random.nextFloat();
    tp.reach = random.nextFloat() < 0.12f ? 1.7f + 0.6f * random.nextFloat() : 0.5f + 0.9f * random.nextFloat();
    tp.bend = 0.05f + 0.55f * random.nextFloat();
    tp.jolt = 0.04f + 0.22f * random.nextFloat();
    tp.peel = 55.0f + 150.0f * random.nextFloat();
    tp.stretch = 1.7f + 2.3f * random.nextFloat();

    const float a = -0.5f * pi + 0.55f * theta + (random.nextFloat() - 0.5f) * 1.6f * pi;
    root.p = root.home = light() + juce::Point<float> (gauss(), gauss()) * 2.0f;
    root.dir = { std::cos (a), std::sin (a) };
    root.parent = tethered;
    root.level = std::clamp (level, 0.25f, 1.0f) * (kind == throwKind ? 1.3f : 1.0f);
    root.life = (1.1f + 3.2f * feed) * (kind == throwKind ? 1.6f : 1.0f) * (0.8f + 0.45f * random.nextFloat());
    root.kind = kind;
    root.born = t;
    const int index = addJoint (root);
    if (index < 0)
        return;
    const uint32_t uid = joints[(size_t) index].uid;

    // the hit itself bursts: a few short shoots at once, before the echo answers
    const int burst = random.nextInt (4);
    for (int k = 0; k < burst; ++k)
    {
        const float b = random.nextFloat() * juce::MathConstants<float>::twoPi;
        Joint twig;
        twig.dir = { std::cos (b), std::sin (b) };
        twig.rest = 3.0f + 9.0f * random.nextFloat() * root.level;
        twig.p = twig.home = root.p + twig.dir * twig.rest;
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
    int alive = 0;
    for (const auto& j : joints)
        alive += j.alive ? 1 : 0;
    const float threshold = 0.06f + 0.18f * (float) alive / (float) maxJoints;
    const float level = parent.level * g * 1.12f * (0.7f + 0.6f * random.nextFloat());
    if (level < threshold || parent.gen >= 14 || random.nextFloat() < 0.07f)
        return;

    const auto& tp = parent.temper;
    const float spread = 0.2f + 0.9f * space + 1.1f * d.diffuse.load (std::memory_order_relaxed);
    const float curl = 3.5f * d.rotate.load (std::memory_order_relaxed);
    const auto lp = light();
    const float near = std::exp (-parent.p.getDistanceSquaredFrom (lp) / (2.0f * 80.0f * 80.0f));
    const auto trail = lightSpeed > 0.5f ? normalised (lightStep) * -std::min (1.0f, lightSpeed / 6.0f) * near : juce::Point<float>();

    auto grow = [&] (float extraTurn, float lengthScale, bool twig) {
        // dotted keeps its side, quarter crosses; the tree's temper and chance do the rest
        auto dir = parent.dir;
        if (quarter)
            dir.x = -dir.x;
        float turn = tp.curl + gauss() * spread * 0.45f + curl * (0.5f + random.nextFloat()) + extraTurn;
        if (random.nextFloat() < tp.jolt)
            turn += (random.nextBool() ? 1.0f : -1.0f) * (0.7f + 0.9f * random.nextFloat());
        dir = normalised (rotated (dir, turn) + trail * 0.9f);

        float length = (16.0f + 34.0f * space) * (quarter ? 1.0f : 0.8f) * tp.reach * (0.45f + 1.1f * random.nextFloat())
                       * std::pow (0.93f, (float) parent.gen) * lengthScale;
        if (twig)
            length = 3.0f + 7.0f * random.nextFloat();

        Joint child;
        child.p = child.home = parent.p + dir * length;
        if (! getLocalBounds().toFloat().expanded (20.0f).contains (child.p))
            return -1; // it has grown out of the field
        child.dir = dir;
        child.rest = length;
        child.parent = parentIndex;
        child.parentUid = parent.uid;
        child.bend = gauss() * tp.bend;
        child.level = twig ? level * 0.6f : level;
        child.life = parent.life * (twig ? 0.5f : 0.85f + 0.3f * random.nextFloat());
        child.grow = 0.05f + 0.2f * random.nextFloat();
        child.gen = (uint8_t) (parent.gen + 1);
        child.kind = parent.kind;
        child.twig = twig;
        child.temper = tp;
        child.born = t;
        return addJoint (child);
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
        const bool linked = from >= 0 && from < (int) joints.size() && joints[(size_t) from].alive;
        if (linked)
            p = &joints[(size_t) from];
        j.dir = { std::cos (angle), std::sin (angle) };
        j.rest = length;
        j.p = j.home = (linked ? p->p : start) + j.dir * length;
        j.parent = linked ? from : tethered;
        j.parentUid = linked ? p->uid : 0;
        j.bend = gauss() * 0.3f;
        j.level = level;
        j.life = life;
        j.grow = 0.04f;
        j.kind = kind;
        j.gen = linked ? (uint8_t) std::min (200, p->gen + 1) : 0;
        j.temper.peel = 90.0f + 80.0f * random.nextFloat();
        j.temper.stretch = 2.5f + 2.0f * random.nextFloat();
        j.born = t;
        return addJoint (j);
    };

    // scream: a coral tendril climbs from the light, wandering
    if (lamp[0] > 0.04f)
    {
        if (lastLamp[0] <= 0.04f)
        {
            screamTip = -1;
            screamAngle = -0.5f * pi + gauss() * 0.6f;
        }
        if (t - lastScream > 0.03 + 0.03 * random.nextFloat())
        {
            lastScream = t;
            screamAngle += gauss() * 0.5f + (-0.5f * pi - screamAngle) * 0.12f;
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
        const int segments = 1 + random.nextInt (3);
        float angle = a;
        for (int k = 0; k < segments; ++k)
        {
            tip = chain (tip, lp, angle, 10.0f + 22.0f * random.nextFloat(), 0.4f + 0.6f * lamp[1], 0.9f, laughKind);
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
        }
    }

    // swarm: a cloud of tiny sparks buzzing around the light
    if (lamp[3] > 0.04f && t - lastSwarm > 0.016)
    {
        lastSwarm = t;
        const int n = 1 + random.nextInt (3 + (int) (4.0f * lamp[3]));
        for (int k = 0; k < n; ++k)
        {
            const float a = random.nextFloat() * juce::MathConstants<float>::twoPi;
            const float r = 14.0f + 80.0f * std::pow (random.nextFloat(), 0.7f);
            Joint j;
            j.p = j.home = lp + juce::Point<float> (std::cos (a), std::sin (a)) * r;
            j.parent = rooted;
            j.level = (0.3f + 0.7f * lamp[3]) * (0.4f + 0.6f * random.nextFloat());
            j.life = 0.25f + 0.5f * random.nextFloat();
            j.kind = swarmKind;
            j.twig = true;
            j.born = t;
            const int i = addJoint (j);
            if (i >= 0)
                joints[(size_t) i].prev = j.p - juce::Point<float> (gauss(), gauss()) * 1.6f; // a buzz
        }
    }

    // ghost: pale strands float up from the light and hang there
    if (lamp[4] > 0.04f && t - lastGhost > 0.07 + 0.06 * random.nextFloat())
    {
        lastGhost = t;
        int tip = -1;
        float angle = -0.5f * pi + gauss() * 0.7f;
        const auto start = lp + juce::Point<float> (gauss(), gauss()) * 18.0f;
        for (int k = 0, n = 2 + random.nextInt (4); k < n; ++k)
        {
            tip = chain (tip, start, angle, 9.0f + 14.0f * random.nextFloat(), (0.2f + 0.4f * lamp[4]), 3.0f + 2.0f * random.nextFloat(), ghostKind);
            if (k == 0 && tip >= 0)
                joints[(size_t) tip].parent = rooted;
            angle += gauss() * 0.35f;
        }
    }

    for (int k = 0; k < dendrite::Heads::count; ++k)
        lastLamp[k] = lamp[k];
}

void Field::simulate (float frames)
{
    const auto lp = light();
    lightStep = lp - lastLight;
    lastLight = lp;
    lightSpeed += (lightStep.getDistanceFromOrigin() - lightSpeed) * 0.3f;
    const float sigma2 = 2.0f * 60.0f * 60.0f;
    const float t = (float) now, dtSway = frames / 60.0f;

    for (auto& j : joints)
    {
        if (! j.alive)
            continue;
        const float age = (float) (now - j.born);
        const auto velocity = (j.p - j.prev) * 0.86f;
        j.prev = j.p;
        j.p += velocity;

        // the light drags the medium around it
        const float w = std::exp (-j.p.getDistanceSquaredFrom (lp) / sigma2);
        j.p += lightStep * (w * 0.55f);

        // everything sways with the tissue
        const auto sway = (flow (j.home, t) - flow (j.home, t - dtSway)) * 0.6f;
        j.home += sway;
        j.p += sway;
        if (j.kind == ghostKind)
        {
            j.home.y -= 0.22f * frames;
            j.p.y -= 0.22f * frames;
        }

        // young growth hangs free on the light; older growth roots into the tissue
        float anchor = 0.0f;
        if (j.parent == rooted)
            anchor = j.kind == swarmKind ? 0.0f : 0.15f;
        else if (j.parent >= 0 && age > 0.6f)
            anchor = std::min (0.05f, (age - 0.6f) * 0.03f);
        j.p += (j.home - j.p) * std::min (1.0f, anchor * frames);
    }

    for (int iteration = 0; iteration < 2; ++iteration)
        for (auto& j : joints)
        {
            if (! j.alive)
                continue;
            if (j.parent == tethered)
            {
                j.p += (lp - j.p) * 0.55f;
                if (lp.getDistanceFrom (j.home) > j.temper.peel)
                {
                    j.parent = rooted; // carried far enough: it lets go and roots here
                    j.home = j.p;
                }
                continue;
            }
            if (j.parent == rooted)
                continue;
            Joint* p = nullptr;
            if (! parentOf (j, p))
            {
                j.parent = rooted;
                j.home = j.p;
                continue;
            }
            const auto d = j.p - p->p;
            const float len = d.getDistanceFromOrigin();
            if (len > j.rest * j.temper.stretch + 14.0f)
            {
                // stretched too far: it peels off and stays where it is
                j.parent = rooted;
                j.home = j.p;
                j.prev = j.p + d / len * 2.5f; // a little recoil
                continue;
            }
            if (len > 1.0e-3f)
            {
                const auto corr = d * ((len - j.rest) / len);
                j.p -= corr * 0.5f;
                if (p->parent != tethered)
                    p->p += corr * 0.15f;
            }
        }
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

        juce::Colour c = colour::ochre;
        const juce::Image* glow = &glowOchre;
        switch (j.kind)
        {
            case throwKind: c = colour::ink; glow = &glowIvory; break;
            case screamKind:
            case laughKind: c = colour::coral; glow = &glowCoral; break;
            case manipulateKind: c = colour::coral.interpolatedWith (colour::cool, 0.45f); glow = &glowCoral; break;
            case swarmKind: c = colour::coral.interpolatedWith (colour::ink, 0.4f); glow = &glowCoral; break;
            case ghostKind: c = colour::cool.interpolatedWith (colour::ink, 0.3f); glow = &glowCool; break;
            case echoKind: break;
        }
        if (j.kind == echoKind)
            c = colour::ochre.interpolatedWith (colour::ochreHot, std::min (1.0f, j.level));

        Joint* p = nullptr;
        auto tip = j.p;
        if (j.parent >= 0 && parentOf (j, p))
        {
            const auto a = p->p;
            const auto d = j.p - a;
            const float len = d.getDistanceFromOrigin();
            const float grow = std::min (1.0f, age / j.grow);
            const auto ctrl = (a + j.p) * 0.5f + juce::Point<float> (-d.y, d.x) * j.bend * 0.5f;
            tip = quad (a, ctrl, j.p, grow);
            // thinner as it stretches, like a pulled thread
            const float thin = std::clamp (std::sqrt (j.rest / std::max (1.0f, len)), 0.3f, 1.15f);
            float width = (0.4f + 1.9f * vis) * std::pow (0.94f, (float) j.gen) * thin;
            if (j.twig)
                width *= 0.6f;
            if (j.kind == ghostKind)
                width *= 0.6f;
            g.setColour (c.withAlpha (std::min (1.0f, alpha * 0.95f)));
            if (len < 6.0f)
            {
                g.drawLine ({ a, tip }, width);
            }
            else
            {
                juce::Path path;
                path.startNewSubPath (a);
                path.quadraticTo (a + (ctrl - a) * grow, tip);
                g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            if (! j.twig && grow >= 1.0f) // a small bead where it forks on
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
        else if (age < 0.7f && ! j.twig)
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
    const float dt = lastTick < 0.0 ? 1.0f / 60.0f : (float) std::clamp (t - lastTick, 0.0, 0.1);
    lastTick = t;
    now = t;
    auto& d = processor.display;

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

    for (auto& j : joints)
        if (j.alive && t - j.born > (double) j.life)
            j.alive = false;

    growHeads (t);
    simulate (dt * 60.0f);
    if (std::getenv ("DENDRITE_DEBUG") != nullptr)
    {
        int counts[8] {}, links = 0;
        for (auto& j : joints)
            if (j.alive) { counts[j.kind]++; links += j.parent >= 0 ? 1 : 0; }
        float ratio = 0.0f, lev = 0.0f; int n = 0;
        for (auto& j : joints)
            if (j.alive && j.parent >= 0) { Joint* q = nullptr; if (parentOf (j, q)) { ratio += j.p.getDistanceFrom (q->p) / std::max (0.1f, j.rest); lev += j.level; ++n; } }
        std::printf ("ratio %.2f level %.2f ", n ? ratio / n : 0.0f, n ? lev / n : 0.0f);
        std::printf ("%.2f echo %d throw %d heads %d %d %d %d %d links %d due %d\n", t, counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], links, (int) due.size());
    }
    tickTissue (t, dt);

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
    g.drawImageAt (vignette, 0, 0);

    // a nerve carries the throw to the light: from your touch, or from the soma
    const bool fromTouch = throwStyle == ThrowStyle::tissue && touchGlow > 0.01f;
    const bool fromSoma = throwStyle == ThrowStyle::soma && throwGlow > 0.01f;
    if (fromTouch || fromSoma)
    {
        const auto from = fromTouch ? touch : somaCentre;
        const float amount = fromTouch ? touchGlow : throwGlow;
        const auto dv = lp - from;
        const auto mid = (from + lp) * 0.5f + juce::Point<float> (-dv.y, dv.x) * 0.12f + flow (from, (float) now * 3.0f) * 3.0f;
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
            g.drawImage (glowIvory, juce::Rectangle<float> (14.0f, 14.0f).withCentre (quad (from, mid, lp, ph)));
        }
        if (fromTouch)
        {
            g.setOpacity (0.6f * touchGlow);
            g.drawImage (glowIvory, juce::Rectangle<float> (40.0f, 40.0f).withCentre (touch));
            g.setColour (colour::ink.withAlpha (0.8f * touchGlow));
            g.drawEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (touch), 1.0f);
        }
    }

    // the light: glows with what comes out, breathes with the echo, flares on a throw
    const float breath = d.breath.load (std::memory_order_relaxed);
    const float halo = 64.0f + 44.0f * pointGlow + 10.0f * breath + 70.0f * throwGlow;
    g.setOpacity (0.22f + 0.45f * pointGlow + 0.33f * throwGlow);
    g.drawImage (throwGlow > 0.3f ? glowIvory : glowOchre, juce::Rectangle<float> (halo, halo).withCentre (lp));

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
    setMouseCursor (near ? juce::MouseCursor::DraggingHandCursor
                         : (throwStyle == ThrowStyle::tissue ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor));
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
    if (throwStyle == ThrowStyle::tissue && ! near)
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
    if (throwStyle == ThrowStyle::light && near)
        setThrow (true); // the light itself is the throw: hold it (and move it, if you like)
    else if (! near)
        setFromUnit (toUnit (e.position)); // a click elsewhere moves the light there (the sound glides)
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
    if (touching || (throwStyle == ThrowStyle::light && throwHeld))
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
    if (e.position.getDistanceFrom (light()) > grabRadius * 1.5f && throwStyle == ThrowStyle::tissue)
        return;
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
