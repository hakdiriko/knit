#include "Controls.h"

namespace dendrite::ui
{
namespace
{
    const auto& cellLabels = dendrite::Engine::frameLabels;
    const auto& headNames = dendrite::Heads::names;
    constexpr int heads = dendrite::Heads::count;
    constexpr int cells = dendrite::Engine::frameChoices;

    // a closed outline that wobbles a little, so nothing is a perfect circle
    juce::Path membrane (juce::Point<float> c, float radius, float t, float wobble)
    {
        juce::Path p;
        for (int k = 0; k <= 40; ++k)
        {
            const float a = (float) k / 40.0f * juce::MathConstants<float>::twoPi;
            const float r = radius + wobble * (std::sin (3.0f * a + 1.1f * t) + 0.7f * std::sin (5.0f * a - 0.8f * t + 1.0f)
                                               + 0.5f * std::sin (2.0f * a + 0.5f * t + 4.0f));
            const juce::Point<float> q { c.x + r * std::cos (a), c.y + r * std::sin (a) };
            if (k == 0)
                p.startNewSubPath (q);
            else
                p.lineTo (q);
        }
        p.closeSubPath();
        return p;
    }
} // namespace

//==============================================================================
Soma::Soma (DendriteProcessor& p)
    : processor (p),
      link (*p.params.getParameter ("throw"), [this] (float v) { held = v > 0.5f; })
{
    link.sendInitialUpdate();
    halo = makeGlow (colour::ink, 128);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool Soma::hitTest (int x, int y)
{
    return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) < 28.0f;
}

void Soma::tick (double t)
{
    now = t;
    const float target = std::max (held ? 1.0f : 0.0f, processor.display.throwing.load (std::memory_order_relaxed));
    glow += (target - glow) * (target > glow ? 0.5f : 0.08f);
    repaint();
}

void Soma::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float t = (float) now;
    const float r = 15.0f + 4.0f * glow;

    g.setOpacity (0.08f + 0.9f * glow);
    g.drawImage (halo, juce::Rectangle<float> (r * 5.0f, r * 5.0f).withCentre (c));

    // the cell: a dim body with a nucleus that lights when you hold it
    const auto body = membrane (c, r, t, 0.9f + 0.6f * glow);
    g.setGradientFill (juce::ColourGradient (colour::ink.withAlpha (0.06f + 0.75f * glow), c.x, c.y,
                                             colour::ink.withAlpha (0.0f), c.x + r, c.y, true));
    g.fillPath (body);
    g.setColour (colour::ink.withAlpha (0.35f + 0.6f * glow));
    g.strokePath (body, juce::PathStrokeType (1.0f));
    g.setColour (colour::ink.withAlpha (0.5f + 0.5f * glow));
    g.fillPath (membrane (c.translated (1.5f * std::sin (0.4f * t), 1.0f * std::cos (0.3f * t)), 3.5f + 2.0f * glow, t * 1.7f, 0.4f));
}

void Soma::mouseDown (const juce::MouseEvent&)
{
    link.setValueAsCompleteGesture (1.0f);
}

void Soma::mouseUp (const juce::MouseEvent&)
{
    link.setValueAsCompleteGesture (0.0f);
}

//==============================================================================
ItchStrip::ItchStrip (DendriteProcessor& p)
    : processor (p),
      link (*p.params.getParameter ("frames"), [this] (float v) { choice = juce::roundToInt (v); })
{
    link.sendInitialUpdate();
    lampGlow = makeGlow (colour::coral, 64);
}

void ItchStrip::tick()
{
    auto& d = processor.display;
    repetition += (d.repetition.load (std::memory_order_relaxed) - repetition) * 0.2f;
    progress = d.frameProgress.load (std::memory_order_relaxed);
    for (size_t k = 0; k < (size_t) heads; ++k)
    {
        const float target = d.lamps[k].load (std::memory_order_relaxed);
        lamps[k] += (target - lamps[k]) * (target > lamps[k] ? 0.6f : 0.12f);
    }
    reveal += ((hovering || dragging ? 1.0f : 0.0f) - reveal) * 0.15f;
    repaint();
}

juce::Rectangle<float> ItchStrip::dotsArea() const
{
    return { dotsLeft, 0.0f, 12.0f * 22.0f, (float) getHeight() };
}

juce::Point<float> ItchStrip::dot (int k) const
{
    const auto a = dotsArea();
    return { a.getX() + 11.0f + 22.0f * (float) k, a.getCentreY() + 2.0f };
}

int ItchStrip::dotAt (juce::Point<float> p) const
{
    const auto a = dotsArea();
    return std::clamp ((int) std::floor ((p.x - a.getX()) / 22.0f), 0, cells - 1);
}

void ItchStrip::paint (juce::Graphics& g)
{
    const float y = dot (0).y;

    // the name, and nothing else
    g.setColour (colour::ink.withAlpha (0.55f));
    g.setFont (fonts->make (fonts->light, 19.0f, 0.06f));
    g.drawText ("dendrite", juce::Rectangle<float> (30.0f, y - 14.0f, 150.0f, 26.0f), juce::Justification::centredLeft);

    g.setColour (colour::inkFaint);
    g.setFont (fonts->make (fonts->regular, 11.5f, 0.08f));
    g.drawText ("itch", juce::Rectangle<float> (dotsLeft - 44.0f, y - 9.0f, 40.0f, 18.0f), juce::Justification::centredLeft);

    // the frame it listens through fills along the line, warmer as the music repeats
    const float x0 = dot (0).x, x1 = dot (cells - 1).x;
    g.setColour (colour::line2.withAlpha (0.6f));
    g.drawLine (x0, y, x1, y, 0.6f);
    if (choice > 0)
    {
        const float heat = juce::jlimit (0.0f, 1.0f, (repetition - 0.3f) / 0.6f);
        g.setColour (colour::coral.withAlpha (0.15f + 0.6f * heat));
        g.drawLine (x0, y, x0 + (x1 - x0) * progress, y, 1.0f);
    }

    for (int k = 0; k < cells; ++k)
    {
        const auto p = dot (k);
        const bool chosen = k == choice;
        if (chosen)
        {
            g.setOpacity (k == 0 ? 0.15f : 0.5f);
            g.drawImage (lampGlow, juce::Rectangle<float> (26.0f, 26.0f).withCentre (p));
            g.setColour (k == 0 ? colour::inkDim : colour::coral);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (p));
        }
        else
        {
            g.setColour (colour::inkFaint.withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (2.4f, 2.4f).withCentre (p));
        }

        // the lengths show while you reach for them; otherwise only the chosen one
        const float alpha = chosen ? 0.9f : 0.55f * reveal;
        if (alpha > 0.02f)
        {
            g.setColour ((chosen && k > 0 ? colour::coral : colour::inkDim).withAlpha (alpha));
            g.setFont (fonts->make (chosen ? fonts->medium : fonts->regular, 11.5f));
            g.drawText (juce::String (juce::CharPointer_UTF8 (cellLabels[k])), juce::Rectangle<float> (22.0f, 14.0f).withCentre (p.translated (0.0f, -15.0f)),
                        juce::Justification::centred);
        }
    }

    // the heads: five lamps; a name appears only while its head plays
    if (! headsArea.isEmpty())
    {
        const float cw = headsArea.getWidth() / (float) heads;
        for (int k = 0; k < heads; ++k)
        {
            const juce::Point<float> c { headsArea.getX() + cw * ((float) k + 0.5f), y };
            const float lit = lamps[k];
            g.setOpacity (lit);
            g.drawImage (lampGlow, juce::Rectangle<float> (30.0f + 24.0f * lit, 30.0f + 24.0f * lit).withCentre (c));
            g.setColour (colour::line2.interpolatedWith (colour::coral, lit));
            g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c), 0.9f);
            g.setColour (colour::coral.withAlpha (lit));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c));
            const float nameAlpha = std::max (lit, 0.5f * reveal);
            if (nameAlpha > 0.02f)
            {
                g.setColour (colour::inkDim.interpolatedWith (colour::coral, lit).withAlpha (nameAlpha));
                g.setFont (fonts->make (fonts->regular, 11.0f, 0.04f));
                g.drawText (headNames[k], juce::Rectangle<float> (cw + 20.0f, 14.0f).withCentre (c.translated (0.0f, 16.0f)),
                            juce::Justification::centred);
            }
        }
    }
}

void ItchStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! dotsArea().expanded (4.0f, 0.0f).contains (e.position))
        return;
    dragging = true;
    link.beginGesture();
    link.setValueAsPartOfGesture ((float) dotAt (e.position));
}

void ItchStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        link.setValueAsPartOfGesture ((float) dotAt (e.position));
}

void ItchStrip::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        link.endGesture();
    dragging = false;
}

void ItchStrip::mouseMove (const juce::MouseEvent& e)
{
    hovering = true;
    const bool over = dotsArea().expanded (4.0f, 0.0f).contains (e.position);
    setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void ItchStrip::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
}
} // namespace dendrite::ui
