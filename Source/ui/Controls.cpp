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
    // sprite sizes: the LED's lens is ~18% of its sprite, the rest is its light in the dark
    constexpr float beadSize = 50.0f, lampSize = 62.0f;

    juce::Image load (const void* data, int size) { return juce::ImageCache::getFromMemory (data, size); }

} // namespace

//==============================================================================
ItchStrip::ItchStrip (DendriteProcessor& p)
    : processor (p),
      link (*p.params.getParameter ("frames"), [this] (float v) { choice = juce::roundToInt (v); }),
      timeLink (*p.params.getParameter ("time"), [this] (float v) { timeChoice = juce::roundToInt (v); }),
      dryLink (*p.params.getParameter ("dry"), [this] (float v) { dryOn = v > 0.5f; })
{
    link.sendInitialUpdate();
    timeLink.sendInitialUpdate();
    dryLink.sendInitialUpdate();
    // one image each, kept rescaled separately for the bead's size and the lamps' size
    beadOff.source = lampOff.source = load (BinaryData::ledoff_png, BinaryData::ledoff_pngSize);
    beadOn.source = lampOn.source = load (BinaryData::ledon_png, BinaryData::ledon_pngSize);
}

void ItchStrip::Sprite::draw (juce::Graphics& g, juce::Point<float> centre, float size, float opacity, float grow)
{
    if (opacity <= 0.003f || ! source.isValid())
        return;
    const float px = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int side = std::max (1, juce::roundToInt (size * px));
    if (scaled.getWidth() != side)
        scaled = source.rescaled (side, side, juce::Graphics::highResamplingQuality);
    // snap to whole screen pixels so the copy stays sharp
    g.setOpacity (std::min (1.0f, opacity));
    if (std::abs (grow - 1.0f) < 1.0e-3f)
    {
        const float x = std::round ((centre.x - 0.5f * size) * px) / px, y = std::round ((centre.y - 0.5f * size) * px) / px;
        g.drawImageTransformed (scaled, juce::AffineTransform::scale (1.0f / px).translated (x, y));
    }
    else
    {
        const float s = grow / px, half = 0.5f * size * grow;
        g.drawImageTransformed (scaled, juce::AffineTransform::scale (s).translated (centre.x - half, centre.y - half));
    }
    g.setOpacity (1.0f);
}

void ItchStrip::tick()
{
    auto& d = processor.display;
    if (ghost.on)
    {
        repetition += (ghost.repetition - repetition) * 0.2f;
        progress = ghost.progress;
        for (size_t k = 0; k < (size_t) heads; ++k)
        {
            lamps[k] += (ghost.lamps[k] - lamps[k]) * (ghost.lamps[k] > lamps[k] ? 0.6f : 0.12f);
            embers[k] = ghost.embers[k];
        }
        reveal += (std::max (ghost.reveal, hovering || dragging ? 1.0f : 0.0f) - reveal) * 0.15f;
        wordGlow += (ghost.wordmark - wordGlow) * 0.1f;
        repaint();
        return;
    }
    repetition += (d.repetition.load (std::memory_order_relaxed) - repetition) * 0.2f;
    progress = d.frameProgress.load (std::memory_order_relaxed);
    for (size_t k = 0; k < (size_t) heads; ++k)
    {
        const float target = d.lamps[k].load (std::memory_order_relaxed);
        lamps[k] += (target - lamps[k]) * (target > lamps[k] ? 0.6f : 0.12f);
    }

    // keep: the last itch's lamps stay as embers, dimming as the minute forgets it
    const auto& keep = processor.keep;
    const dendrite::keep::Mark* last = nullptr;
    for (auto it = keep.diary().rbegin(); it != keep.diary().rend(); ++it)
        if (it->kind == dendrite::keep::Kind::itch && it->endSample >= 0)
        {
            last = &*it;
            break;
        }
    emberMark = last != nullptr ? last->id : 0;
    const float age = last != nullptr ? (float) ((double) (keep.written() - last->endSample) / (double) std::max<int64_t> (1, keep.capacity())) : 1.0f;
    const float glow = 0.3f * std::pow (std::clamp (1.0f - age, 0.0f, 1.0f), 1.5f);
    const float breathe = 0.85f + 0.15f * (float) std::sin (juce::Time::getMillisecondCounterHiRes() * 0.0011);
    for (int k = 0; k < heads; ++k)
    {
        const bool in = last != nullptr && ((last->headsMask >> k) & 1u);
        const float target = in ? glow * breathe * (lampPressed ? 2.2f : 1.0f) : 0.0f;
        embers[(size_t) k] += (target - embers[(size_t) k]) * 0.1f;
    }
    reveal += ((hovering || dragging ? 1.0f : 0.0f) - reveal) * 0.15f;
    const bool overWord = hovering && wordmarkArea().contains (mouseAt);
    wordGlow += ((overWord ? 0.6f : 0.0f) - wordGlow) * 0.15f;
    repaint();
}

void ItchStrip::simulateLampPress (bool down)
{
    if (down && ! lampPressed)
    {
        if (const auto* m = processor.keep.find (emberMark))
            if (auto t = processor.keep.takeMark (*m, false))
            {
                lampPressed = true;
                lampDragged = false;
                if (tape != nullptr)
                    tape->hold (*t, m);
            }
    }
    else if (! down && lampPressed)
    {
        lampPressed = false;
        if (tape != nullptr)
            tape->release();
    }
}

juce::Rectangle<float> ItchStrip::wordmarkArea() const
{
    const float y = dot (0).y;
    return { 26.0f, y - 14.0f, 92.0f, 26.0f };
}

juce::Point<float> ItchStrip::lampCentre (int head) const
{
    const float cw = headsArea.getWidth() / (float) heads;
    return { headsArea.getX() + cw * ((float) head + 0.5f), dot (0).y };
}

int ItchStrip::lampAt (juce::Point<float> p) const
{
    if (fly || headsArea.isEmpty() || ! headsArea.contains (p))
        return -1;
    for (int k = 0; k < heads; ++k)
        if (p.getDistanceFrom (lampCentre (k)) < 18.0f)
            return k;
    return -1;
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
    g.setColour (colour::ink.withAlpha (0.55f + 0.45f * wordGlow));
    g.setFont (fonts->make (fonts->light, 19.0f, 0.06f));
    g.drawText ("dendrite", juce::Rectangle<float> (30.0f, y - 14.0f, 150.0f, 26.0f), juce::Justification::centredLeft);
    if (wordGlow > 0.01f)
    {
        // a thin line of light under the name: it can be pressed (first light plays again)
        g.setColour (colour::ink.withAlpha (0.5f * wordGlow));
        g.drawLine (31.0f, y + 12.0f, 31.0f + 74.0f * std::min (1.0f, 0.4f + wordGlow), y + 12.0f, 0.7f);
    }

    g.setColour (colour::redText.withAlpha (0.85f)); // the itch's things are the red ones
    g.setFont (fonts->small (*this, fonts->regular, 12.5f, 0.08f));
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
            // a red LED on the line: lit while the itch listens, dark when it is off
            // ... and warms as the music repeats and the itch gets closer to firing
            const float warm = juce::jlimit (0.0f, 1.0f, (repetition - 0.2f) / 0.7f);
            (k == 0 ? beadOff : beadOn).draw (g, p, beadSize, k == 0 ? 1.0f : 0.6f + 0.4f * warm, 1.0f + 0.25f * warm);
        }
        else
        {
            g.setColour (colour::red.withAlpha (0.2f + 0.4f * reveal)); // faint ticks at rest, dots as you reach
            g.fillEllipse (juce::Rectangle<float> (1.6f + 0.8f * reveal, 1.6f + 0.8f * reveal).withCentre (p));
        }

        // the lengths show while you reach for them; otherwise only the chosen one
        const float alpha = chosen ? 0.9f : 0.55f * reveal;
        if (alpha > 0.02f)
        {
            g.setColour ((chosen && k > 0 ? colour::redText : colour::red).withAlpha (alpha));
            g.setFont (fonts->small (*this, fonts->mono, 11.5f)); // numbers are mono
            g.drawText (juce::String (juce::CharPointer_UTF8 (cellLabels[k])), juce::Rectangle<float> (22.0f, 14.0f).withCentre (p.translated (0.0f, -17.0f)),
                        juce::Justification::centred);
        }
    }

    // how the repeats fall, and the dry: grey, since red is the itch's alone
    {
        static const char* times[] = { "dotted", "triplet", "half" };
        const bool overTime = timeArea().contains (mouseAt) && hovering, overDry = dryArea().contains (mouseAt) && hovering;
        g.setFont (fonts->small (*this, fonts->light, 12.0f, 0.06f));
        g.setColour (colour::inkDim.withAlpha (overTime ? 0.95f : 0.6f));
        g.drawText (times[std::clamp (timeChoice, 0, 2)], timeArea(), juce::Justification::centredLeft);
        g.setColour (colour::inkDim.withAlpha (dryOn ? (overDry ? 0.95f : 0.6f) : (overDry ? 0.45f : 0.22f)));
        g.drawText ("dry", dryArea(), juce::Justification::centredLeft);
        if (! dryOn)
        {
            const auto r = dryArea();
            g.drawLine (r.getX(), r.getCentreY() + 0.5f, r.getX() + 19.0f, r.getCentreY() + 0.5f, 0.8f);
        }
    }

    // the heads: five lamps; a name appears only while its head plays
    if (! headsArea.isEmpty() && ! fly)
    {
        const float cw = headsArea.getWidth() / (float) heads;
        for (int k = 0; k < heads; ++k)
        {
            const juce::Point<float> c { headsArea.getX() + cw * ((float) k + 0.5f), y };
            // a red LED in a dark panel: nearly invisible at rest, lit as its head plays; after an
            // itch its lamps keep a faint ember while keep still holds it
            const float lit = std::max (lamps[k], embers[k]);
            lampOff.draw (g, c, lampSize, 1.0f - lit);
            lampOn.draw (g, c, lampSize, lit);
            const float nameAlpha = std::max ({ lit, 0.5f * reveal, 0.25f, std::min (1.0f, 2.0f * embers[k]) }); // faint at rest, so you can learn them
            if (nameAlpha > 0.02f)
            {
                g.setColour (colour::red.interpolatedWith (colour::redText, lit).withAlpha (std::min (1.0f, nameAlpha)));
                g.setFont (fonts->small (*this, fonts->regular, 12.0f, 0.04f));
                g.drawText (headNames[k], juce::Rectangle<float> (cw + 20.0f, 14.0f).withCentre (c.translated (0.0f, 20.0f)),
                            juce::Justification::centred);
            }
        }
    }
}

juce::Rectangle<float> ItchStrip::timeArea() const
{
    // two quiet words between the itch and the heads: how the repeats fall, and the dry
    return { dotsLeft + 12.0f * 22.0f + 10.0f, dot (0).y - 17.0f, 64.0f, 15.0f };
}

juce::Rectangle<float> ItchStrip::dryArea() const
{
    return timeArea().translated (0.0f, 18.0f);
}

juce::String ItchStrip::getTooltip()
{
    if (timeArea().contains (mouseAt))
        return juce::String (juce::CharPointer_UTF8 ("how the repeats fall: dotted, triplet or half time \xc2\xb7 click to change"));
    if (dryArea().contains (mouseAt))
        return "dry: off for a return track (echo only)";
    if (wordmarkArea().contains (mouseAt))
        return "dendrite: click to see how it works";
    if (const int k = lampAt (mouseAt); k >= 0 && embers[(size_t) k] > 0.01f)
        return "the last itch: pull it into Live (shift: with everything)";
    return {};
}

void ItchStrip::mouseDown (const juce::MouseEvent& e)
{
    if (wordmarkArea().contains (e.position))
    {
        if (onWordmark)
            onWordmark();
        return;
    }
    if (const int k = lampAt (e.position); k >= 0 && embers[(size_t) k] > 0.01f)
    {
        if (const auto* m = processor.keep.find (emberMark))
            if (auto t = processor.keep.takeMark (*m, e.mods.isShiftDown()))
            {
                lampPressed = true;
                lampDragged = false;
                if (tape != nullptr)
                    tape->hold (*t, m);
            }
        return;
    }
    if (timeArea().contains (e.position))
    {
        timeLink.setValueAsCompleteGesture ((float) ((timeChoice + 1) % 3));
        return;
    }
    if (dryArea().contains (e.position))
    {
        dryLink.setValueAsCompleteGesture (dryOn ? 0.0f : 1.0f);
        return;
    }
    if (! dotsArea().expanded (4.0f, 0.0f).contains (e.position))
        return;
    dragging = true;
    link.beginGesture();
    link.setValueAsPartOfGesture ((float) dotAt (e.position));
}

void ItchStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (lampPressed)
    {
        if (! lampDragged && e.getDistanceFromDragStart() >= 6)
        {
            lampDragged = true;
            const auto* m = processor.keep.find (emberMark);
            auto t = m != nullptr ? processor.keep.takeMark (*m, e.mods.isShiftDown()) : std::nullopt;
            juce::Component::SafePointer<ItchStrip> self (this);
            auto done = [self] {
                if (self == nullptr)
                    return;
                self->lampPressed = false;
                if (self->tape != nullptr)
                {
                    self->tape->release();
                    self->tape->flashKept();
                }
            };
            if (! t || ! dragOut (processor.keep, *t, *this, done))
                done();
        }
        return;
    }
    if (dragging)
        link.setValueAsPartOfGesture ((float) dotAt (e.position));
}

void ItchStrip::mouseUp (const juce::MouseEvent&)
{
    if (lampPressed && ! lampDragged)
    {
        lampPressed = false;
        if (tape != nullptr)
            tape->release();
    }
    if (dragging)
        link.endGesture();
    dragging = false;
}

void ItchStrip::mouseMove (const juce::MouseEvent& e)
{
    hovering = true;
    mouseAt = e.position;
    const bool over = dotsArea().expanded (4.0f, 0.0f).contains (e.position) || timeArea().contains (e.position)
                      || dryArea().contains (e.position) || wordmarkArea().contains (e.position);
    const int lamp = lampAt (e.position);
    setMouseCursor (lamp >= 0 && embers[(size_t) lamp] > 0.01f ? juce::MouseCursor::DraggingHandCursor
                    : over                                     ? juce::MouseCursor::PointingHandCursor
                                                               : juce::MouseCursor::NormalCursor);
}

void ItchStrip::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
}
} // namespace dendrite::ui
