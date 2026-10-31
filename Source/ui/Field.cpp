#include "Field.h"

namespace dendrite::ui
{
namespace
{
    constexpr float leftMargin = 34.0f, bottomMargin = 30.0f, topMargin = 18.0f, rightMargin = 18.0f;
    constexpr size_t maxNodes = 700, maxDue = 1400;

    const float spaceEdges[] = { 0.0f, 0.2f, 0.5f, 0.78f, 1.0f };
    const char* spaceNames[] = { "inside", "around", "envelop", "beyond" };
    const float feedEdges[] = { 0.0f, 0.3f, 0.62f, 0.87f, 1.0f };
    const char* feedNames[] = { "whisper", "dub", "drown", "endless" };

    juce::Point<float> rotated (juce::Point<float> v, float a)
    {
        const float c = std::cos (a), s = std::sin (a);
        return { v.x * c - v.y * s, v.x * s + v.y * c };
    }

    // A mineral dendrite: a wandering stroke that tapers and forks, with small twigs.
    void mineral (juce::Graphics& g, juce::Random& r, juce::Point<float> p, float angle, float length, float width, int depth)
    {
        const int steps = 7;
        for (int i = 0; i < steps; ++i)
        {
            angle += (r.nextFloat() - 0.5f) * 0.55f;
            const auto q = p + juce::Point<float> (std::cos (angle), std::sin (angle)) * (length / (float) steps);
            g.drawLine ({ p, q }, width);
            p = q;
            width *= 0.92f;
            if (depth > 1 && r.nextFloat() < 0.3f)
                mineral (g, r, p, angle + (r.nextBool() ? 1.0f : -1.0f) * (0.7f + 0.6f * r.nextFloat()), length * 0.3f,
                         width * 0.55f, depth - 3);
        }
        if (depth <= 0 || width < 0.35f)
            return;
        const int kids = r.nextFloat() < 0.3f ? 3 : 2;
        for (int k = 0; k < kids; ++k)
        {
            const float spread = (0.35f + 0.45f * r.nextFloat()) * (k == 0 ? -1.0f : (k == 1 ? 1.0f : 0.2f));
            mineral (g, r, p, angle + spread, length * (0.62f + 0.16f * r.nextFloat()), width * 0.7f, depth - 1);
        }
    }
} // namespace

Field::Field (DendriteProcessor& p)
    : processor (p),
      feedLink (*p.params.getParameter ("feed"), [this] (float v) { feed = v; repaint(); }),
      spaceLink (*p.params.getParameter ("space"), [this] (float v) { space = v; repaint(); })
{
    feedLink.sendInitialUpdate();
    spaceLink.sendInitialUpdate();
    nodes.reserve (maxNodes);
    due.reserve (maxDue);
    glowOchre = makeGlow (colour::ochreHot, 64);
    glowIvory = makeGlow (colour::ink, 64);
    glowCoral = makeGlow (colour::coral, 64);
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

void Field::resized()
{
    renderStone();
}

void Field::renderStone()
{
    const int scale = 2;
    const auto w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0)
        return;
    stone = juce::Image (juce::Image::ARGB, w * scale, h * scale, true);
    juce::Graphics g (stone);
    g.addTransform (juce::AffineTransform::scale ((float) scale));
    const auto b = getLocalBounds().toFloat();

    juce::Path slab;
    slab.addRoundedRectangle (b.reduced (0.5f), 14.0f);
    g.reduceClipRegion (slab);
    g.setGradientFill (juce::ColourGradient (colour::stone2, b.getX(), b.getY(), colour::stone0, b.getRight(), b.getBottom(), false));
    g.fillPath (slab);

    // grain
    juce::Random r (0xd3d2);
    for (int i = 0; i < w * h / 7; ++i)
    {
        g.setColour (juce::Colour::greyLevel (r.nextFloat()).withAlpha (0.018f + 0.02f * r.nextFloat()));
        g.fillRect (r.nextFloat() * b.getWidth(), r.nextFloat() * b.getHeight(), 0.6f, 0.6f);
    }

    // manganese dendrites grown from the cracks at the edges
    struct Seed { float x, y, angle, length; };
    const Seed seeds[] = { { 0.0f, 0.92f, -0.55f, 0.20f }, { 0.12f, 1.0f, -1.35f, 0.17f }, { 0.55f, 1.0f, -1.75f, 0.13f },
                           { 1.0f, 0.08f, 2.6f, 0.19f },   { 0.86f, 0.0f, 1.9f, 0.14f },   { 1.0f, 0.62f, 3.35f, 0.11f },
                           { 0.0f, 0.25f, 0.35f, 0.10f } };
    for (const auto& s : seeds)
    {
        const juce::Point<float> p { b.getX() + s.x * b.getWidth(), b.getY() + s.y * b.getHeight() };
        const float len = s.length * b.getWidth();
        juce::Random rr ((juce::int64) (s.x * 1000 + s.y * 77 + 13));
        g.setColour (colour::ochre.withAlpha (0.03f));
        mineral (g, rr, p, s.angle, len, 6.0f, 6);
        juce::Random rr2 ((juce::int64) (s.x * 1000 + s.y * 77 + 13));
        g.setColour (juce::Colour (0xff7a5a33).withAlpha (0.2f));
        mineral (g, rr2, p, s.angle, len, 1.6f, 6);
    }

    // vignette
    juce::ColourGradient v (juce::Colours::transparentBlack, b.getCentreX(), b.getCentreY(),
                            juce::Colours::black.withAlpha (0.5f), b.getX(), b.getY(), true);
    g.setGradientFill (v);
    g.fillPath (slab);
}

int Field::addNode (const Node& n)
{
    for (size_t i = 0; i < nodes.size(); ++i)
        if (! nodes[i].alive)
        {
            nodes[i] = n;
            return (int) i;
        }
    if (nodes.size() < maxNodes)
    {
        nodes.push_back (n);
        return (int) nodes.size() - 1;
    }
    return -1;
}

void Field::spawnRoot (float level, Kind kind, double t)
{
    auto& d = processor.display;
    const float theta = d.theta.load (std::memory_order_relaxed);
    const float a = -0.5f * pi + 0.55f * theta + (random.nextFloat() - 0.5f) * 1.1f;
    Node n;
    n.from = n.to = pointUnit();
    n.dir = { std::cos (a), std::sin (a) };
    n.level = std::clamp (level, 0.25f, 1.0f) * (kind == throwKind ? 1.3f : 1.0f);
    n.life = (0.9f + 2.8f * feed) * (kind == throwKind ? 1.6f : 1.0f);
    n.kind = kind;
    n.born = t;
    n.alive = true;
    const int index = addNode (n);
    if (index < 0 || due.size() + 2 > maxDue)
        return;
    due.push_back ({ t + d.dotted.load (std::memory_order_relaxed), index, false });
    due.push_back ({ t + d.quarter.load (std::memory_order_relaxed), index, true });
}

void Field::spawnChild (int parentIndex, bool quarter, double t)
{
    if (parentIndex < 0 || parentIndex >= (int) nodes.size())
        return;
    const Node parent = nodes[(size_t) parentIndex];
    if (! parent.alive)
        return;
    auto& d = processor.display;
    const float g = quarter ? d.gainQuarter.load (std::memory_order_relaxed) : d.gainDotted.load (std::memory_order_relaxed);

    // A crowded field prunes harder, so a busy drum loop stays legible.
    int alive = 0;
    for (const auto& n : nodes)
        alive += n.alive ? 1 : 0;
    const float threshold = 0.06f + 0.16f * (float) alive / (float) maxNodes;
    const float level = parent.level * g * 1.12f;
    if (level < threshold || parent.gen >= 12)
        return;

    // dotted keeps its side, quarter crosses; space opens the angles, diffuse scatters them,
    // beyond curls everything the same way round
    auto dir = parent.dir;
    if (quarter)
        dir.x = -dir.x;
    const float spreadAmount = 0.2f + 0.9f * space + 1.1f * d.diffuse.load (std::memory_order_relaxed);
    const float curl = 3.5f * d.rotate.load (std::memory_order_relaxed);
    dir = rotated (dir, (random.nextFloat() - 0.5f) * spreadAmount * 0.7f + curl);

    const auto r = inner();
    const float aspect = r.getHeight() / std::max (1.0f, r.getWidth());
    const float length = (0.075f + 0.12f * space) * (quarter ? 1.0f : 0.8f) * std::pow (0.93f, (float) parent.gen);

    Node child;
    child.from = parent.to;
    child.to = { parent.to.x + dir.x * length * aspect, parent.to.y + dir.y * length };
    if (child.from.x < -0.05f || child.from.x > 1.05f || child.from.y < -0.05f || child.from.y > 1.05f)
        return; // it has grown out of the stone
    child.dir = dir;
    child.level = level;
    child.life = parent.life;
    child.gen = (uint8_t) (parent.gen + 1);
    child.kind = parent.kind;
    child.born = t;
    child.alive = true;
    const int index = addNode (child);
    if (index < 0 || due.size() + 2 > maxDue)
        return;
    due.push_back ({ t + d.dotted.load (std::memory_order_relaxed), index, false });
    due.push_back ({ t + d.quarter.load (std::memory_order_relaxed), index, true });
}

void Field::growHeads (double t)
{
    auto& d = processor.display;
    float lamp[3];
    for (int k = 0; k < 3; ++k)
        lamp[k] = d.lamps[(size_t) k].load (std::memory_order_relaxed);
    const auto r = inner();
    const float aspect = r.getHeight() / std::max (1.0f, r.getWidth());

    // scream: a coral tendril climbs from the point
    if (lamp[0] > 0.04f)
    {
        if (lastLamp[0] <= 0.04f)
            screamTip = pointUnit();
        if (t - lastScream > 0.035)
        {
            lastScream = t;
            const float a = -0.5f * pi + 0.5f * std::sin ((float) t * 9.0f) + (random.nextFloat() - 0.5f) * 0.4f;
            Node n;
            n.from = screamTip;
            n.to = { std::clamp (screamTip.x + std::cos (a) * 0.022f * aspect, 0.0f, 1.0f),
                     std::clamp (screamTip.y + std::sin (a) * 0.022f * (0.6f + lamp[0]), 0.0f, 1.0f) };
            n.dir = { std::cos (a), std::sin (a) };
            n.level = 0.35f + 0.65f * lamp[0];
            n.life = 1.8f;
            n.kind = screamKind;
            n.born = t;
            n.alive = true;
            addNode (n);
            screamTip = n.to;
        }
    }

    // laugh: a twig for every "ha", bouncing left and right
    if (lamp[1] > 0.3f && lastLamp[1] <= 0.3f)
    {
        const float a = laughSide > 0.0f ? -0.35f - 0.3f * random.nextFloat() : pi + 0.35f + 0.3f * random.nextFloat();
        laughSide = -laughSide;
        const auto p = pointUnit();
        Node n;
        n.from = p;
        n.to = { std::clamp (p.x + std::cos (a) * 0.07f * aspect, 0.0f, 1.0f), std::clamp (p.y + std::sin (a) * 0.07f, 0.0f, 1.0f) };
        n.dir = { std::cos (a), std::sin (a) };
        n.level = 0.4f + 0.6f * lamp[1];
        n.life = 0.9f;
        n.kind = laughKind;
        n.born = t;
        n.alive = true;
        addNode (n);
    }

    // manipulate: a spiral curls around where the point was
    if (lamp[2] > 0.04f)
    {
        if (lastLamp[2] <= 0.04f)
        {
            manipulateCentre = pointUnit();
            manipulateAngle = random.nextFloat() * tau;
            manipulateRadius = 0.02f;
        }
        if (t - lastManipulate > 0.03)
        {
            lastManipulate = t;
            const float next = manipulateAngle + 0.24f;
            manipulateRadius += 0.0035f;
            auto polar = [&] (float a) {
                return juce::Point<float> { std::clamp (manipulateCentre.x + std::cos (a) * manipulateRadius * aspect, 0.0f, 1.0f),
                                            std::clamp (manipulateCentre.y + std::sin (a) * manipulateRadius, 0.0f, 1.0f) };
            };
            Node n;
            n.from = polar (manipulateAngle);
            n.to = polar (next);
            n.level = 0.35f + 0.65f * lamp[2];
            n.life = 2.2f;
            n.kind = manipulateKind;
            n.born = t;
            n.alive = true;
            addNode (n);
            manipulateAngle = next;
        }
    }

    for (int k = 0; k < 3; ++k)
        lastLamp[k] = lamp[k];
}

void Field::tick (double t)
{
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
            spawnChild (item.parent, item.quarter, item.at);
        }
        else
        {
            ++i;
        }
    }

    for (auto& n : nodes)
        if (n.alive && t - n.born > (double) n.life)
            n.alive = false;

    growHeads (t);

    const float target = isEngaged() ? 1.0f : 0.0f;
    labelAlpha += (target - labelAlpha) * 0.12f;
    const float out = d.outLevel.load (std::memory_order_relaxed);
    pointGlow += (std::min (1.0f, 4.0f * out) - pointGlow) * 0.15f;
    repaint();
}

void Field::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto r = inner();
    auto& d = processor.display;

    if (stone.isValid())
        g.drawImage (stone, b, juce::RectanglePlacement::stretchToFit);
    g.setColour (colour::line);
    g.drawRoundedRectangle (b.reduced (0.5f), 14.0f, 1.0f);

    // regime guides
    const float dash[] = { 2.0f, 5.0f };
    g.setColour (colour::line2.withAlpha (0.55f));
    for (int k = 1; k < 4; ++k)
    {
        const float x = r.getX() + spaceEdges[k] * r.getWidth();
        g.drawDashedLine ({ x, r.getY(), x, r.getBottom() }, dash, 2, 1.0f);
        const float y = r.getBottom() - feedEdges[k] * r.getHeight();
        g.drawDashedLine ({ r.getX(), y, r.getRight(), y }, dash, 2, 1.0f);
    }

    // regime names along the edges; the current ones light up
    g.setFont (fonts->make (fonts->regular, 12.0f, 0.04f));
    for (int k = 0; k < 4; ++k)
    {
        const bool here = space >= spaceEdges[k] && (space < spaceEdges[k + 1] || k == 3);
        g.setColour (here ? colour::ink.withAlpha (0.85f) : colour::inkFaint);
        const float x0 = r.getX() + spaceEdges[k] * r.getWidth(), x1 = r.getX() + spaceEdges[k + 1] * r.getWidth();
        g.drawText (spaceNames[k], juce::Rectangle<float> (x0, r.getBottom() + 7.0f, x1 - x0, 16.0f), juce::Justification::centred);
    }
    for (int k = 0; k < 4; ++k)
    {
        const bool here = feed >= feedEdges[k] && (feed < feedEdges[k + 1] || k == 3);
        g.setColour (here ? colour::ink.withAlpha (0.85f) : colour::inkFaint);
        const float y0 = r.getBottom() - feedEdges[k + 1] * r.getHeight(), y1 = r.getBottom() - feedEdges[k] * r.getHeight();
        juce::Graphics::ScopedSaveState save (g);
        const float cx = r.getX() - 17.0f, cy = 0.5f * (y0 + y1);
        g.addTransform (juce::AffineTransform::rotation (-0.5f * pi, cx, cy));
        g.drawText (feedNames[k], juce::Rectangle<float> (y1 - y0, 16.0f).withCentre ({ cx, cy }), juce::Justification::centred);
    }

    // hairlines through the point
    const auto p = toScreen (pointUnit());
    g.setColour (colour::ink.withAlpha (0.06f + 0.06f * labelAlpha));
    g.drawLine (r.getX(), p.y, r.getRight(), p.y, 1.0f);
    g.drawLine (p.x, r.getY(), p.x, r.getBottom(), 1.0f);

    // the dendrites
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (b.reduced (2.0f).toNearestInt());
        for (const auto& n : nodes)
        {
            if (! n.alive)
                continue;
            const float age = (float) (now - n.born);
            if (age < 0.0f)
                continue;
            const float fade = std::pow (std::max (0.0f, 1.0f - age / n.life), 1.5f);
            const float alpha = std::min (1.0f, n.level) * fade;
            if (alpha < 0.01f)
                continue;
            const float grow = std::min (1.0f, age / 0.09f);
            const auto a = toScreen (n.from);
            const auto e = a + (toScreen (n.to) - a) * grow;

            juce::Colour c = colour::ochre;
            const juce::Image* glow = &glowOchre;
            if (n.kind == throwKind) { c = colour::ink; glow = &glowIvory; }
            else if (n.kind == screamKind || n.kind == laughKind) { c = colour::coral; glow = &glowCoral; }
            else if (n.kind == manipulateKind) { c = colour::coral.interpolatedWith (colour::cool, 0.45f); glow = &glowCoral; }

            if (n.from != n.to)
            {
                g.setColour (c.withAlpha (std::min (1.0f, alpha * 1.1f)));
                g.drawLine ({ a, e }, 0.8f + 2.4f * std::min (1.0f, n.level));
            }
            const float size = (n.gen == 0 ? 34.0f : 9.0f) + 22.0f * std::min (1.0f, n.level);
            g.setOpacity (alpha * (n.gen == 0 ? 0.9f : 0.7f));
            g.drawImage (*glow, juce::Rectangle<float> (size, size).withCentre (e));
        }
    }

    // the point: glows with what comes out, breathes with the echo
    const float breath = d.breath.load (std::memory_order_relaxed);
    const float throwing = d.throwing.load (std::memory_order_relaxed);
    const float halo = 70.0f + 40.0f * pointGlow + 8.0f * breath + 30.0f * throwing;
    g.setOpacity (0.25f + 0.45f * pointGlow + 0.3f * throwing);
    g.drawImage (throwing > 0.3f ? glowIvory : glowOchre, juce::Rectangle<float> (halo, halo).withCentre (p));
    g.setColour (colour::ink.withAlpha (0.55f + 0.3f * labelAlpha));
    g.drawEllipse (juce::Rectangle<float> (26.0f, 26.0f).withCentre (p), 1.2f);
    g.setColour (colour::ink);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p));

    // what the point means, while you touch it
    if (labelAlpha > 0.02f)
    {
        const auto text = DendriteProcessor::feedName (feed) + "  " + juce::String (juce::roundToInt (feed * 100.0f)) + "   ·   "
                          + DendriteProcessor::spaceName (space) + "  " + juce::String (juce::roundToInt (space * 100.0f));
        const auto font = fonts->make (fonts->medium, 12.5f, 0.03f);
        juce::GlyphArrangement ga;
        ga.addLineOfText (font, text, 0.0f, 0.0f);
        const float w = ga.getBoundingBox (0, -1, true).getWidth() + 22.0f;
        auto box = juce::Rectangle<float> (w, 24.0f).withCentre (p.translated (0.0f, p.y > r.getY() + 50.0f ? -34.0f : 34.0f));
        box = box.constrainedWithin (r);
        g.setColour (colour::stone0.withAlpha (0.78f * labelAlpha));
        g.fillRoundedRectangle (box, 12.0f);
        g.setColour (colour::line2.withAlpha (labelAlpha));
        g.drawRoundedRectangle (box, 12.0f, 1.0f);
        g.setColour (colour::ink.withAlpha (labelAlpha));
        g.setFont (font);
        g.drawText (text, box, juce::Justification::centred);
    }
}

void Field::setFromUnit (juce::Point<float> unit)
{
    space = std::clamp (unit.x, 0.0f, 1.0f);
    feed = std::clamp (1.0f - unit.y, 0.0f, 1.0f);
    spaceLink.setValueAsPartOfGesture (space);
    feedLink.setValueAsPartOfGesture (feed);
}

void Field::mouseMove (const juce::MouseEvent&)
{
    if (! hovering && onHint)
        onHint ("drag the light: up for more echo, right for more space. shift = fine, double-click = home");
    hovering = true;
}

void Field::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    if (onHint)
        onHint ({});
}

void Field::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    feedLink.beginGesture();
    spaceLink.beginGesture();
    const auto r = inner();
    // Grab the point where it is; a click elsewhere moves it there (the sound glides).
    if (e.position.getDistanceFrom (toScreen (pointUnit())) > 22.0f)
        setFromUnit ({ (e.position.x - r.getX()) / r.getWidth(), (e.position.y - r.getY()) / r.getHeight() });
    grabUnit = pointUnit();
    lastMouse = e.position;
}

void Field::mouseDrag (const juce::MouseEvent& e)
{
    const auto r = inner();
    const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
    const auto delta = e.position - lastMouse;
    lastMouse = e.position;
    grabUnit += { delta.x / r.getWidth() * fine, delta.y / r.getHeight() * fine };
    grabUnit = { std::clamp (grabUnit.x, 0.0f, 1.0f), std::clamp (grabUnit.y, 0.0f, 1.0f) };
    setFromUnit (grabUnit);
}

void Field::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    feedLink.endGesture();
    spaceLink.endGesture();
}

void Field::mouseDoubleClick (const juce::MouseEvent&)
{
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
