#include "Editor.h"

namespace
{
    const juce::Colour ground  { 0xff0e0c14 };
    const juce::Colour ink     { 0xffe8e3f0 };
    const juce::Colour dim     { 0xff6d6680 };
    const juce::Colour faint   { 0xff2a2535 };
    const juce::Colour space   { 0xff8fb7ff };
    const juce::Colour itch    { 0xffe7a35a };
    const char* headNames[] = { "scream", "laugh", "manipulate" };

    constexpr float rimRadius = 0.95f;                       // the disc's edge, in unit-disc terms
    const float rimDistance = std::atanh (rimRadius);        // its hyperbolic distance from the centre

    juce::Font font (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    float depthToRadius (float depth) { return std::tanh (juce::jlimit (0.0f, 1.0f, depth) * rimDistance); }
    float radiusToDepth (float r) { return std::atanh (juce::jlimit (0.0f, rimRadius, r)) / rimDistance; }
}

// ------------------------------------------------------------------ disc

HypnagogicEditor::Disc::Disc (juce::RangedAudioParameter& o, juce::RangedAudioParameter& d)
    : orbitLink (o, [this] (float v) { orbit = v; repaint(); }),
      depthLink (d, [this] (float v) { depth = v; repaint(); })
{
    orbitLink.sendInitialUpdate();
    depthLink.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

juce::Point<float> HypnagogicEditor::Disc::pointFor (float o, float d) const
{
    const float angle = hyp::tau * (o - 0.5f), r = depthToRadius (d);
    return { r * std::sin (angle), -r * std::cos (angle) };
}

void HypnagogicEditor::Disc::mouseDown (const juce::MouseEvent& e)
{
    // The point never jumps to the mouse: it moves by how far you drag.
    grabPoint = pointFor (orbit, depth);
    grabMouse = e.position;
    orbitLink.beginGesture();
    depthLink.beginGesture();
}

void HypnagogicEditor::Disc::mouseDrag (const juce::MouseEvent& e)
{
    const float scale = (e.mods.isShiftDown() ? 0.2f : 1.0f) / radius();     // shift: five times finer
    grabPoint += (e.position - grabMouse) * scale;
    grabMouse = e.position;

    const float r = grabPoint.getDistanceFromOrigin();
    if (r > rimRadius) grabPoint *= rimRadius / r;

    depthLink.setValueAsPartOfGesture (radiusToDepth (grabPoint.getDistanceFromOrigin()));
    if (r > 0.04f)          // at the very centre the direction means nothing, so keep it
        orbitLink.setValueAsPartOfGesture (hyp::wrapUnit (std::atan2 (grabPoint.x, -grabPoint.y) / hyp::tau + 0.5f));
}

void HypnagogicEditor::Disc::mouseUp (const juce::MouseEvent&)
{
    orbitLink.endGesture();
    depthLink.endGesture();
}

void HypnagogicEditor::Disc::mouseDoubleClick (const juce::MouseEvent&)
{
    orbitLink.setValueAsCompleteGesture (HypnagogicProcessor::defaultOrbit);
    depthLink.setValueAsCompleteGesture (HypnagogicProcessor::defaultDepth);
}

void HypnagogicEditor::Disc::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float R = radius();
    auto at = [&] (juce::Point<float> unit) { return c + unit * R; };

    g.setGradientFill (juce::ColourGradient (faint.withAlpha (0.55f), c, ground, c.translated (R, 0.0f), true));
    g.fillEllipse (juce::Rectangle<float> (2.0f * R, 2.0f * R).withCentre (c));

    // Rings at equal steps of depth: they crowd toward the rim, like Escher's discs.
    for (int k = 1; k <= 20; ++k)
    {
        const float d = (float) k / 20.0f, r = depthToRadius (d) * R;
        const bool regime = k == 5 || k == 11 || k == 16 || k == 20;
        g.setColour ((regime ? dim : faint).withAlpha (regime ? 0.55f : 0.6f));
        g.drawEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c), regime ? 1.0f : 0.6f);
    }
    for (int k = 0; k < 8; ++k)
    {
        const float a = hyp::tau * (float) k / 8.0f;
        g.setColour (faint);
        g.drawLine ({ c, at ({ rimRadius * std::sin (a), -rimRadius * std::cos (a) }) }, 0.6f);
    }

    g.setColour (dim);
    g.setFont (font (11.0f));
    const float labelDepth[] = { 0.12f, 0.40f, 0.68f, 0.90f };
    const char* labels[] = { "inside", "around", "envelop", "dissolve" };
    for (int k = 0; k < 4; ++k)
    {
        const auto p = at ({ 0.0f, depthToRadius (labelDepth[k]) });
        g.drawText (labels[k], juce::Rectangle<float> (70.0f, 14.0f).withCentre (p), juce::Justification::centred);
    }
    g.drawText ("front", juce::Rectangle<float> (60.0f, 14.0f).withCentre (c.translated (0.0f, -R - 0.0f + 8.0f)), juce::Justification::centred);
    g.drawText ("left", juce::Rectangle<float> (40.0f, 14.0f).withCentre (c.translated (-R + 18.0f, 0.0f)), juce::Justification::centred);
    g.drawText ("right", juce::Rectangle<float> (40.0f, 14.0f).withCentre (c.translated (R - 18.0f, 0.0f)), juce::Justification::centred);

    // What comes out, drawn as mid (up) and side (across): mono is a vertical line.
    float peak = 0.05f;
    for (auto& p : trace) peak = juce::jmax (peak, std::abs (p.x), std::abs (p.y));
    const float scale = 0.8f * R / peak;
    for (size_t i = 0; i < trace.size(); ++i)
    {
        const float m = (trace[i].x + trace[i].y) * 0.7071f, s = (trace[i].x - trace[i].y) * 0.7071f;
        g.setColour (space.withAlpha (0.05f + 0.4f * (float) i / (float) trace.size()));
        g.fillRect (c.x + s * scale, c.y - m * scale, 1.5f, 1.5f);
    }

    // You.
    g.setColour (dim);
    g.drawEllipse (juce::Rectangle<float> (22.0f, 26.0f).withCentre (c), 1.2f);
    g.drawLine (c.x, c.y - 13.0f, c.x, c.y - 19.0f, 1.2f);
    g.fillEllipse (juce::Rectangle<float> (4.0f, 8.0f).withCentre (c.translated (-12.5f, 0.0f)));
    g.fillEllipse (juce::Rectangle<float> (4.0f, 8.0f).withCentre (c.translated (12.5f, 0.0f)));

    // Where the space is.
    const auto p = at (pointFor (orbit, depth));
    g.setColour (space.withAlpha (0.35f));
    g.drawLine ({ c, p }, 1.0f);
    g.setColour (space.withAlpha (0.18f));
    g.fillEllipse (juce::Rectangle<float> (26.0f, 26.0f).withCentre (p));
    g.setColour (ink);
    g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (p));
}

// ------------------------------------------------------------------ itch strip

HypnagogicEditor::FrameStrip::FrameStrip (juce::RangedAudioParameter& p)
    : link (p, [this] (float v) { index = juce::roundToInt (v); repaint(); })
{
    link.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

int HypnagogicEditor::FrameStrip::indexAt (float x) const
{
    const int n = HypnagogicProcessor::frameNames.size();
    return juce::jlimit (0, n - 1, (int) (x / (float) getWidth() * (float) n));
}

void HypnagogicEditor::FrameStrip::mouseDown (const juce::MouseEvent& e)
{
    link.beginGesture();
    link.setValueAsPartOfGesture ((float) indexAt (e.position.x));
}

void HypnagogicEditor::FrameStrip::mouseDrag (const juce::MouseEvent& e)
{
    link.setValueAsPartOfGesture ((float) indexAt (e.position.x));
}

void HypnagogicEditor::FrameStrip::mouseUp (const juce::MouseEvent&) { link.endGesture(); }

void HypnagogicEditor::FrameStrip::paint (juce::Graphics& g)
{
    const auto& names = HypnagogicProcessor::frameNames;
    const int n = names.size();
    const float cell = (float) getWidth() / (float) n;
    auto area = getLocalBounds().toFloat();
    auto stops = area.removeFromTop (32.0f);

    g.setColour (faint);
    g.fillRoundedRectangle (stops, 6.0f);

    // How repetitive it sounds right now, filling under the stops.
    if (index > 0)
    {
        g.setColour (itch.withAlpha (0.16f));
        g.fillRoundedRectangle (stops.withWidth (stops.getWidth() * juce::jlimit (0.0f, 1.0f, repetition)), 6.0f);
    }

    for (int i = 0; i < n; ++i)
    {
        auto r = juce::Rectangle<float> ((float) i * cell, stops.getY(), cell, stops.getHeight());
        if (i == index)
        {
            g.setColour (i == 0 ? dim : itch);
            g.fillRoundedRectangle (r.reduced (3.0f), 5.0f);
            if (i > 0)       // where we are inside the frame
            {
                const auto inner = r.reduced (3.0f);
                g.setColour (ground.withAlpha (0.45f));
                g.fillRect (inner.getX() + 4.0f, inner.getBottom() - 5.0f, (inner.getWidth() - 8.0f) * progress, 2.0f);
            }
        }
        g.setColour (i == index ? ground : dim);
        g.setFont (font (12.5f, i == index));
        g.drawText (names[i].upToFirstOccurrenceOf (" ", false, false), r, juce::Justification::centred);
    }

    // The three heads, lit while they play.
    area.removeFromTop (10.0f);
    const float w = area.getWidth() / 3.0f;
    for (int h = 0; h < 3; ++h)
    {
        auto r = juce::Rectangle<float> (area.getX() + (float) h * w, area.getY(), w, area.getHeight());
        const float lit = juce::jlimit (0.0f, 1.0f, lamps[(size_t) h] * 1.6f);
        const auto dot = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ r.getX() + 12.0f, r.getCentreY() });
        g.setColour (faint);
        g.fillEllipse (dot);
        g.setColour (itch.withAlpha (lit));
        g.fillEllipse (dot);
        g.setColour (dim.interpolatedWith (ink, lit));
        g.setFont (font (13.0f));
        g.drawText (juce::String (h + 1) + "  " + headNames[h], r.withTrimmedLeft (24.0f), juce::Justification::centredLeft);
    }
}

// ------------------------------------------------------------------ editor

HypnagogicEditor::HypnagogicEditor (HypnagogicProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      disc (*p.params.getParameter ("orbit"), *p.params.getParameter ("depth")),
      strip (*p.params.getParameter ("frame"))
{
    addAndMakeVisible (disc);
    addAndMakeVisible (strip);
    setSize (640, 450);
    startTimerHz (30);
}

void HypnagogicEditor::resized()
{
    disc.setBounds (20, 46, 310, 310);
    strip.setBounds (20, 376, 600, 60);
}

void HypnagogicEditor::timerCallback()
{
    auto& d = proc.display;
    const int head = d.scopeHead.load (std::memory_order_relaxed);
    for (int i = 0; i < HypnagogicProcessor::Display::points; ++i)
    {
        const int k = (head + i) % HypnagogicProcessor::Display::points;
        disc.trace[(size_t) i] = { d.scope[(size_t) (2 * k)].load (std::memory_order_relaxed),
                                   d.scope[(size_t) (2 * k + 1)].load (std::memory_order_relaxed) };
    }
    correlation = d.correlation.load (std::memory_order_relaxed);
    hostTempo = d.hostTempo.load (std::memory_order_relaxed);
    strip.repetition = d.repetition.load (std::memory_order_relaxed);
    strip.progress = d.frameProgress.load (std::memory_order_relaxed);
    for (int h = 0; h < 3; ++h) strip.lamps[(size_t) h] = d.lamps[(size_t) h].load (std::memory_order_relaxed);
    disc.repaint();
    strip.repaint();
    repaint (340, 40, 300, 320);
}

void HypnagogicEditor::paint (juce::Graphics& g)
{
    g.fillAll (ground);

    g.setColour (ink);
    g.setFont (font (22.0f, true));
    g.drawText ("dendrite", 20, 10, 200, 28, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (font (13.0f));
    g.drawText ("hypnagogic", 124, 16, 200, 20, juce::Justification::centredLeft);

    auto* orbit = proc.params.getParameter ("orbit");
    auto* depth = proc.params.getParameter ("depth");
    const int x = 350, w = 270;

    g.setColour (ink);
    g.setFont (font (20.0f, true));
    g.drawText (orbit->getCurrentValueAsText(), x, 70, w, 26, juce::Justification::centredLeft);
    g.drawText (depth->getCurrentValueAsText(), x, 98, w, 26, juce::Justification::centredLeft);

    g.setColour (dim);
    g.setFont (font (12.5f));
    g.drawFittedText ("drag the point around your head to turn the space, outward to go deeper. "
                      "near the rim the smallest moves change the most. "
                      "shift for five times finer, double-click to come home.",
                      juce::Rectangle<int> (x, 140, w, 70), juce::Justification::topLeft, 5);
    g.drawFittedText ("the space lives only between your ears: fold to mono and you get your dry signal back.",
                      juce::Rectangle<int> (x, 220, w, 40), juce::Justification::topLeft, 3);

    g.setColour (ink.withAlpha (0.8f));
    g.setFont (font (12.5f));
    g.drawText ("correlation " + juce::String (correlation, 2), x, 270, w, 18, juce::Justification::centredLeft);

    g.setColour (ink);
    g.setFont (font (13.0f, true));
    g.drawText ("itch", 20, 352, 60, 20, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (font (12.0f));
    g.drawText (hostTempo ? "drag: how long it can stand the same thing before it itches"
                          : "drag: how long it can stand the same thing (no tempo: 120 BPM)",
                60, 352, 560, 20, juce::Justification::centredRight);
}
