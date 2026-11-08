#include "Controls.h"
#include "BinaryData.h"

namespace dendrite::ui
{
namespace
{
    const auto& cellLabels = dendrite::Engine::frameLabels;
    const auto& headNames = dendrite::Heads::names;
    constexpr int heads = dendrite::Heads::count;
    constexpr int cells = dendrite::Engine::frameChoices;
    constexpr float beadSize = 17.0f, lampSize = 24.0f; // sprite sizes: the object fills ~78% of each

    juce::Image load (const void* data, int size) { return juce::ImageCache::getFromMemory (data, size); }

} // namespace

//==============================================================================
ItchStrip::ItchStrip (DendriteProcessor& p)
    : processor (p),
      link (*p.params.getParameter ("frames"), [this] (float v) { choice = juce::roundToInt (v); })
{
    link.sendInitialUpdate();
    lampGlow = makeGlow (colour::red, 64);
    beadOff.source = load (BinaryData::beadoff_png, BinaryData::beadoff_pngSize);
    beadOn.source = load (BinaryData::beadon_png, BinaryData::beadon_pngSize);
    lampOff.source = load (BinaryData::lampoff_png, BinaryData::lampoff_pngSize);
    lampOn.source = load (BinaryData::lampon_png, BinaryData::lampon_pngSize);
}

void ItchStrip::Sprite::draw (juce::Graphics& g, juce::Point<float> centre, float size, float opacity)
{
    if (opacity <= 0.003f || ! source.isValid())
        return;
    const float px = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int side = std::max (1, juce::roundToInt (size * px));
    if (scaled.getWidth() != side)
        scaled = source.rescaled (side, side, juce::Graphics::highResamplingQuality);
    // snap to whole screen pixels so the copy stays sharp
    const float x = std::round ((centre.x - 0.5f * size) * px) / px, y = std::round ((centre.y - 0.5f * size) * px) / px;
    g.setOpacity (std::min (1.0f, opacity));
    g.drawImageTransformed (scaled, juce::AffineTransform::scale (1.0f / px).translated (x, y));
    g.setOpacity (1.0f);
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

    g.setColour (colour::redText.withAlpha (0.85f)); // the itch's things are the red ones
    g.setFont (fonts->make (fonts->regular, 12.5f, 0.08f));
    g.drawText ("itch", juce::Rectangle<float> (dotsLeft - 44.0f, y - 9.0f, 40.0f, 18.0f), juce::Justification::centredLeft);

    // the frame it listens through fills along the line, warmer as the music repeats
    const float x0 = dot (0).x, x1 = dot (cells - 1).x;
    g.setColour (colour::line2.withAlpha (0.6f));
    g.drawLine (x0, y, x1, y, 0.6f);
    if (choice > 0)
    {
        const float heat = juce::jlimit (0.0f, 1.0f, (repetition - 0.3f) / 0.6f);
        g.setColour (colour::red.withAlpha (0.15f + 0.6f * heat));
        g.drawLine (x0, y, x0 + (x1 - x0) * progress, y, 1.0f);
    }

    for (int k = 0; k < cells; ++k)
    {
        const auto p = dot (k);
        const bool chosen = k == choice;
        if (chosen)
        {
            // a ruby bead, glowing while the itch listens, dark glass when it is off
            if (k > 0)
            {
                g.setOpacity (0.35f);
                g.drawImage (lampGlow, juce::Rectangle<float> (30.0f, 30.0f).withCentre (p));
                g.setOpacity (1.0f);
            }
            (k == 0 ? beadOff : beadOn).draw (g, p, beadSize, 1.0f);
        }
        else
        {
            g.setColour (colour::red.withAlpha (0.55f));
            g.fillEllipse (juce::Rectangle<float> (2.4f, 2.4f).withCentre (p));
        }

        // the lengths show while you reach for them; otherwise only the chosen one
        const float alpha = chosen ? 0.9f : 0.55f * reveal;
        if (alpha > 0.02f)
        {
            g.setColour ((chosen && k > 0 ? colour::redText : colour::red).withAlpha (alpha));
            g.setFont (fonts->make (fonts->mono, 11.5f)); // numbers are mono
            g.drawText (juce::String (juce::CharPointer_UTF8 (cellLabels[k])), juce::Rectangle<float> (22.0f, 14.0f).withCentre (p.translated (0.0f, -17.0f)),
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
            // a red jewel lamp in a black bezel: dark glass at rest, lit from inside as its head plays
            g.setOpacity (0.7f * lit);
            g.drawImage (lampGlow, juce::Rectangle<float> (34.0f + 24.0f * lit, 34.0f + 24.0f * lit).withCentre (c));
            g.setOpacity (1.0f);
            lampOff.draw (g, c, lampSize, 1.0f);
            lampOn.draw (g, c, lampSize, lit);
            const float nameAlpha = std::max (lit, 0.5f * reveal);
            if (nameAlpha > 0.02f)
            {
                g.setColour (colour::red.interpolatedWith (colour::redText, lit).withAlpha (nameAlpha));
                g.setFont (fonts->make (fonts->regular, 12.0f, 0.04f));
                g.drawText (headNames[k], juce::Rectangle<float> (cw + 20.0f, 14.0f).withCentre (c.translated (0.0f, 20.0f)),
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
