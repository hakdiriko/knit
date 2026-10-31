#include "Controls.h"

namespace dendrite::ui
{
namespace
{
    const char* cellLabels[] = { "off", "1", "1\xc2\xbc", "1\xc2\xbd", "1\xc2\xbe", "2", "3", "4", "8" };
    const char* headNames[] = { "scream", "laugh", "manipulate" };

    void panel (juce::Graphics& g, juce::Rectangle<float> b)
    {
        g.setGradientFill (juce::ColourGradient (colour::stone2, b.getX(), b.getY(), colour::stone1, b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b.reduced (0.5f), 14.0f);
        g.setColour (colour::line);
        g.drawRoundedRectangle (b.reduced (0.5f), 14.0f, 1.0f);
    }
} // namespace

//==============================================================================
ThrowPad::ThrowPad (DendriteProcessor& p)
    : processor (p),
      link (*p.params.getParameter ("throw"), [this] (float v) { held = v > 0.5f; repaint(); })
{
    link.sendInitialUpdate();
    halo = makeGlow (colour::ochreHot, 128);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ThrowPad::tick()
{
    const float target = std::max (held ? 1.0f : 0.0f, processor.display.throwing.load (std::memory_order_relaxed));
    glow += (target - glow) * (target > glow ? 0.5f : 0.08f);
    repaint();
}

void ThrowPad::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    panel (g, b);

    g.setColour (colour::inkDim);
    g.setFont (fonts->make (fonts->medium, 13.0f, 0.05f));
    g.drawText ("throw", b.reduced (18.0f, 14.0f).removeFromTop (18.0f), juce::Justification::centredLeft);
    g.setColour (colour::inkFaint);
    g.setFont (fonts->make (fonts->regular, 11.5f, 0.02f));
    g.drawText ("into the echo", b.reduced (18.0f, 14.0f).removeFromTop (18.0f), juce::Justification::centredRight);

    const float d = std::min (b.getWidth(), b.getHeight() - 40.0f) - 44.0f;
    const auto c = b.getCentre().translated (0.0f, 10.0f);
    const auto disc = juce::Rectangle<float> (d, d).withCentre (c);

    g.setOpacity (0.15f + 0.85f * glow);
    g.drawImage (halo, disc.expanded (d * 0.35f));

    // a well in the stone
    g.setGradientFill (juce::ColourGradient (colour::stone0, c.x, c.y - d * 0.5f, colour::stone2, c.x, c.y + d * 0.5f, false));
    g.fillEllipse (disc);
    if (glow > 0.01f)
    {
        juce::ColourGradient lit (colour::ink.withAlpha (0.9f * glow), c.x, c.y, colour::ochre.withAlpha (0.25f * glow), c.x + d * 0.5f, c.y, true);
        g.setGradientFill (lit);
        g.fillEllipse (disc.reduced (3.0f));
    }
    g.setColour ((pressed ? colour::ochreHot : colour::line2).interpolatedWith (colour::ochreHot, glow));
    g.drawEllipse (disc, 1.2f + 1.2f * glow);
    g.setColour (colour::line.withAlpha (0.8f));
    g.drawEllipse (disc.reduced (10.0f), 1.0f);

    g.setColour (colour::ink.interpolatedWith (colour::stone0, 0.85f * glow));
    g.setFont (fonts->make (fonts->light, 24.0f, 0.06f));
    g.drawText ("hold", disc, juce::Justification::centred);
}

void ThrowPad::mouseDown (const juce::MouseEvent&)
{
    pressed = true;
    link.setValueAsCompleteGesture (1.0f);
}

void ThrowPad::mouseUp (const juce::MouseEvent&)
{
    pressed = false;
    link.setValueAsCompleteGesture (0.0f);
}

void ThrowPad::mouseEnter (const juce::MouseEvent&)
{
    if (onHint)
        onHint ("hold to throw what's playing into the echo. map \"throw\" to a pad or key in Live");
}

void ThrowPad::mouseExit (const juce::MouseEvent&)
{
    if (onHint)
        onHint ({});
}

//==============================================================================
ItchPanel::ItchPanel (DendriteProcessor& p)
    : processor (p),
      link (*p.params.getParameter ("frame"), [this] (float v) { choice = juce::roundToInt (v); repaint(); })
{
    link.sendInitialUpdate();
    lampGlow = makeGlow (colour::coral, 64);
}

void ItchPanel::tick()
{
    auto& d = processor.display;
    repetition += (d.repetition.load (std::memory_order_relaxed) - repetition) * 0.2f;
    progress = d.frameProgress.load (std::memory_order_relaxed);
    boredom = d.boredom.load (std::memory_order_relaxed);
    itching = d.itching.load (std::memory_order_relaxed);
    for (size_t k = 0; k < 3; ++k)
    {
        const float target = d.lamps[k].load (std::memory_order_relaxed);
        lamps[k] += (target - lamps[k]) * (target > lamps[k] ? 0.6f : 0.12f);
    }
    repaint();
}

juce::Rectangle<float> ItchPanel::cellArea() const
{
    return getLocalBounds().toFloat().reduced (16.0f, 0.0f).withTrimmedTop (42.0f).withHeight (30.0f);
}

int ItchPanel::cellAt (juce::Point<float> p) const
{
    const auto a = cellArea();
    return std::clamp ((int) std::floor ((p.x - a.getX()) / a.getWidth() * (float) dendrite::Engine::frameChoices), 0,
                       dendrite::Engine::frameChoices - 1);
}

void ItchPanel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    panel (g, b);

    auto top = b.reduced (18.0f, 14.0f).removeFromTop (18.0f);
    g.setColour (colour::inkDim);
    g.setFont (fonts->make (fonts->medium, 13.0f, 0.05f));
    g.drawText ("itch", top, juce::Justification::centredLeft);
    g.setColour (colour::inkFaint);
    g.setFont (fonts->make (fonts->regular, 11.5f, 0.02f));
    const auto names = DendriteProcessor::itchNames;
    g.drawText (choice == 0 ? juce::String ("never itches") : "after " + names[choice] + " of the same", top,
                juce::Justification::centredRight);

    // the frame: drag across
    const auto a = cellArea();
    const float w = a.getWidth() / (float) dendrite::Engine::frameChoices;
    juce::Path track;
    track.addRoundedRectangle (a, 8.0f);
    g.setColour (colour::stone0.withAlpha (0.7f));
    g.fillPath (track);
    g.setColour (colour::line);
    g.strokePath (track, juce::PathStrokeType (1.0f));
    for (int k = 0; k < dendrite::Engine::frameChoices; ++k)
    {
        const auto cell = juce::Rectangle<float> (a.getX() + (float) k * w, a.getY(), w, a.getHeight());
        if (k == choice)
        {
            g.setColour ((k == 0 ? colour::inkFaint : colour::coral).withAlpha (0.9f));
            g.fillRoundedRectangle (cell.reduced (2.0f), 6.0f);
        }
        g.setColour (k == choice ? colour::stone0 : colour::inkDim);
        g.setFont (fonts->make (k == choice ? fonts->medium : fonts->regular, 12.5f));
        g.drawText (juce::String (juce::CharPointer_UTF8 (cellLabels[k])), cell, juce::Justification::centred);
    }

    // listening: the bar fills through the frame, brighter the more it repeats
    const auto meter = b.reduced (18.0f, 0.0f).withTrimmedTop (88.0f).withHeight (16.0f);
    g.setColour (colour::inkFaint);
    g.setFont (fonts->make (fonts->regular, 11.0f, 0.02f));
    g.drawText (choice == 0 ? "off" : (itching ? "itching" : (repetition > 0.72f ? "restless" : "listening")),
                meter.withWidth (64.0f), juce::Justification::centredLeft);
    const auto bar = meter.withTrimmedLeft (68.0f).withSizeKeepingCentre (meter.getWidth() - 68.0f, 4.0f);
    g.setColour (colour::stone0);
    g.fillRoundedRectangle (bar, 2.0f);
    if (choice > 0)
    {
        g.setColour (colour::coral.withAlpha (0.18f + 0.82f * juce::jlimit (0.0f, 1.0f, (repetition - 0.3f) / 0.6f)));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progress), 2.0f);
        for (int k = 0; k < 3; ++k) // how many frames in a row it has been the same
        {
            g.setColour (boredom * 3.0f > (float) k + 0.5f ? colour::coral : colour::line2);
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ bar.getRight() - 4.0f - 8.0f * (float) k, bar.getY() - 8.0f }));
        }
    }

    // the three heads
    const auto row = b.reduced (12.0f, 0.0f).withTrimmedTop (114.0f).withHeight (56.0f);
    const float cw = row.getWidth() / 3.0f;
    for (int k = 0; k < 3; ++k)
    {
        const auto col = juce::Rectangle<float> (row.getX() + (float) k * cw, row.getY(), cw, row.getHeight());
        const auto c = juce::Point<float> (col.getCentreX(), col.getY() + 14.0f);
        const float lit = lamps[k];
        g.setOpacity (lit);
        g.drawImage (lampGlow, juce::Rectangle<float> (40.0f + 20.0f * lit, 40.0f + 20.0f * lit).withCentre (c));
        g.setColour (colour::stone0);
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (c));
        g.setColour (colour::line2.interpolatedWith (colour::coral, lit));
        g.drawEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (c), 1.2f);
        g.setColour (colour::coral.withAlpha (lit));
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c));
        g.setColour (colour::inkFaint.interpolatedWith (colour::ink, lit));
        g.setFont (fonts->make (fonts->regular, 11.5f, 0.02f));
        g.drawText (headNames[k], col.withTrimmedTop (28.0f).withHeight (16.0f), juce::Justification::centred);
    }
}

void ItchPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! cellArea().expanded (0.0f, 8.0f).contains (e.position))
        return;
    dragging = true;
    link.beginGesture();
    link.setValueAsPartOfGesture ((float) cellAt (e.position));
}

void ItchPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        link.setValueAsPartOfGesture ((float) cellAt (e.position));
}

void ItchPanel::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        link.endGesture();
    dragging = false;
}

void ItchPanel::mouseMove (const juce::MouseEvent& e)
{
    const bool overCells = cellArea().expanded (0.0f, 8.0f).contains (e.position);
    setMouseCursor (overCells ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (onHint)
        onHint ("drag to choose how long dendrite can stand the same thing. when it can't, its three heads play");
}

void ItchPanel::mouseExit (const juce::MouseEvent&)
{
    if (onHint)
        onHint ({});
}
} // namespace dendrite::ui
