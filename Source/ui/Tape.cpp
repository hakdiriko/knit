#include "Tape.h"

namespace dendrite::ui
{
void focusHostWindow (void* nativeHandle); // HostFocus.cpp

namespace
{
    const juce::String dot { juce::CharPointer_UTF8 (" \xc2\xb7 ") };

    juce::String barsText (int bars) { return juce::String (bars) + (bars == 1 ? " bar" : " bars"); }
} // namespace

bool dragOut (dendrite::keep::Keep& keep, const dendrite::keep::Take& take, juce::Component& source, std::function<void()> done)
{
    const auto file = keep.write (take);
    if (! file.existsAsFile())
        return false;
    // The file stays where it is after the drop: Live reads it after the drag has ended.
    if (juce::PluginHostType().isAbletonLive())
        focusHostWindow (source.getPeer() != nullptr ? source.getPeer()->getNativeHandle() : nullptr);
    return juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, &source, std::move (done));
}

KeepTape::KeepTape (DendriteProcessor& p) : processor (p)
{
    setInterceptsMouseClicks (true, false);
}

float KeepTape::xOf (int64_t frame) const
{
    const auto oldest = written - capacity;
    return left + (float) ((double) (frame - oldest) / (double) capacity) * (right() - left);
}

int64_t KeepTape::frameAt (float x) const
{
    const auto oldest = written - capacity;
    return oldest + (int64_t) ((double) (x - left) / (double) (right() - left) * (double) capacity);
}

bool KeepTape::hitTest (int, int y)
{
    // only the band of the tape takes the mouse; the ruler above it is a picture
    return (float) y >= rulerHeight - 2.0f && processor.keep.capacity() > 0;
}

void KeepTape::setGhost (bool on, float alpha)
{
    ghost = on;
    ghostAlpha = on ? alpha : 0.0f;
    if (on && alpha > 0.5f && ! take)
    {
        // the ruler over the newest itch, as if pressed
        const auto& d = processor.keep.diary();
        for (auto it = d.rbegin(); it != d.rend(); ++it)
            if (it->kind == dendrite::keep::Kind::itch && it->endSample >= 0)
            {
                if (auto t = processor.keep.takeMark (*it, false))
                    openRuler (*t, &*it);
                break;
            }
        if (! take)
        {
            // nothing has itched yet: a made-up itch of two bars, three heads in turn
            dendrite::keep::Take t;
            const double sr = processor.keep.sampleRate();
            t.bars = 2;
            t.bpm = 120.0;
            t.length = (int64_t) (4.0 * sr);
            t.from = std::max<int64_t> (processor.keep.written(), (int64_t) (8.0 * sr)) - (int64_t) (6.0 * sr);
            t.itchOnly = true;
            take = t;
            takeIsItch = true;
            caption = juce::StringArray { "the itch alone", "scream laugh ghost", barsText (2), "120 bpm" }.joinIntoString (dot);
            peaks.assign (192, 0.0f);
            juce::Random r (7);
            for (size_t x = 0; x < peaks.size(); ++x)
            {
                const float u = (float) x / (float) peaks.size();
                float v = 0.0f;
                for (float at : { 0.08f, 0.36f, 0.62f })
                    if (u >= at)
                        v = std::max (v, std::exp (-(u - at) * 9.0f));
                peaks[x] = v * (0.75f + 0.25f * r.nextFloat());
            }
        }
    }
    if (! on && ! pressed)
        take.reset();
}

void KeepTape::tick (float stripReveal)
{
    auto& keep = processor.keep;
    written = keep.written();
    capacity = std::max<int64_t> (1, keep.capacity());
    reveal = stripReveal;
    const float target = std::max ({ reveal, hovering || pressed || held ? 1.0f : 0.0f, ghostAlpha });
    show += (target - show) * (target > show ? 0.2f : 0.06f);
    rulerAlpha += (((pressed && ! dragged) || held || (ghost && take) ? 1.0f : 0.0f) - rulerAlpha) * 0.25f;
    flash *= 0.94f;
    if (rulerAlpha < 0.01f && ! pressed && ! ghost && ! held)
        take.reset();
    if (show > 0.005f || flash > 0.005f)
        repaint();
}

const dendrite::keep::Mark* KeepTape::markAt (juce::Point<float> p) const
{
    if (std::abs (p.y - lineY()) > bandHeight)
        return nullptr;
    const dendrite::keep::Mark* best = nullptr;
    for (const auto& m : processor.keep.diary())
    {
        const float x0 = xOf (m.sample) - 4.0f, x1 = xOf (m.endSample >= 0 ? m.endSample : written) + 4.0f;
        if (p.x >= x0 && p.x <= x1)
            if (best == nullptr || (m.kind == dendrite::keep::Kind::itch && best->kind != dendrite::keep::Kind::itch))
                best = &m;
    }
    return best;
}

void KeepTape::openRuler (const dendrite::keep::Take& t, const dendrite::keep::Mark* m)
{
    take = t;
    takeIsItch = t.itchOnly;
    juce::StringArray parts;
    if (m != nullptr && m->kind == dendrite::keep::Kind::itch)
    {
        parts.add (t.itchOnly ? "the itch alone" : "the itch, with everything");
        if (m->headsMask != 0)
            parts.add (dendrite::keep::headNames (m->headsMask));
    }
    else if (m != nullptr)
        parts.add (m->kind == dendrite::keep::Kind::throwing ? "the throw and its echoes" : "the runaway");
    else
        parts.add ("everything");
    parts.add (barsText (t.bars));
    parts.add (juce::String (juce::roundToInt (t.bpm)) + " bpm");
    caption = parts.joinIntoString (dot);

    // its waveform, for the ruler: the loudest sample under each of its pixels
    const int width = (int) std::clamp ((float) t.bars * 96.0f, 160.0f, 560.0f);
    peaks.assign ((size_t) width, 0.0f);
    juce::AudioBuffer<float> audio (2, (int) std::max<int64_t> (1, t.length));
    processor.keep.read (t.from, (int) t.length, t.itchOnly, audio, 0);
    float loudest = 1.0e-6f;
    for (int x = 0; x < width; ++x)
    {
        const int a = (int) ((int64_t) x * t.length / width), b = (int) ((int64_t) (x + 1) * t.length / width);
        if (b > a)
            peaks[(size_t) x] = std::max (audio.getMagnitude (0, a, b - a), audio.getMagnitude (1, a, b - a));
        loudest = std::max (loudest, peaks[(size_t) x]);
    }
    for (auto& p : peaks)
        p = std::sqrt (p / loudest); // a gentle curve, so quiet tails still show
}

void KeepTape::paint (juce::Graphics& g)
{
    const float alpha = std::max (show, flash);
    if (alpha < 0.005f)
        return;
    const float y = lineY();
    const auto oldest = written - capacity;
    const float x0 = oldest < 0 ? xOf (0) : left;

    // the tape itself, and the bar lines on it
    g.setColour (colour::line2.withAlpha (0.9f * alpha));
    g.drawLine (x0, y, right(), y, 0.7f);
    const auto c = processor.keep.clock();
    if (c.frame > 0)
    {
        const double framesPerBar = processor.keep.sampleRate() * 60.0 / c.bpm * c.barBeats;
        const double lastBar = std::floor ((c.beat + 1.0e-3) / c.barBeats) * c.barBeats;
        double f = (double) c.frame - (c.beat - lastBar) / c.barBeats * framesPerBar;
        const float spacing = (float) (framesPerBar / (double) capacity) * (right() - left);
        if (spacing > 4.0f)
            for (int k = 0; f >= (double) std::max<int64_t> (0, oldest) && k < 2000; ++k, f -= framesPerBar)
            {
                const float x = xOf ((int64_t) f);
                const auto barIndex = std::llround (lastBar / c.barBeats) - k; // every fourth bar a little brighter
                g.setColour (colour::inkFaint.withAlpha ((barIndex % 4 == 0 ? 0.7f : 0.35f) * alpha));
                g.drawLine (x, y - 2.0f, x, y + 2.0f, 0.6f);
            }
    }

    // what happened: red where it itched, grey where you threw, white where it ran away
    for (const auto& m : processor.keep.diary())
    {
        const float a = xOf (m.sample), b = std::max (a + 2.0f, xOf (m.endSample >= 0 ? m.endSample : written));
        if (b < x0)
            continue;
        const bool over = m.id == hoverId;
        juce::Colour col = m.kind == dendrite::keep::Kind::itch ? colour::redText
                         : m.kind == dendrite::keep::Kind::throwing ? colour::inkDim : colour::ink;
        const float thick = m.kind == dendrite::keep::Kind::itch ? 2.6f : 1.6f;
        const float yy = m.kind == dendrite::keep::Kind::itch ? y : y + 4.0f;
        g.setColour (col.withAlpha ((over ? 1.0f : 0.7f) * alpha));
        g.fillRoundedRectangle (juce::Rectangle<float> (std::max (a, x0), yy - 0.5f * thick, b - std::max (a, x0), thick), 0.5f * thick);
    }

    // now
    g.setColour (colour::ink.withAlpha (0.8f * alpha));
    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ right(), y }));

    if (rulerAlpha > 0.01f && take)
        paintRuler (g);
}

void KeepTape::paintRuler (juce::Graphics& g)
{
    const auto& t = *take;
    const float alpha = rulerAlpha * std::max (show, 0.3f);
    const float width = (float) peaks.size();
    const float a = xOf (t.from), b = xOf (t.from + t.length);
    const float rx = std::clamp (0.5f * (a + b) - 0.5f * width, 8.0f, (float) getWidth() - 8.0f - width);
    const juce::Rectangle<float> box { rx, 27.0f, width, rulerHeight - 29.0f };
    const juce::Colour wave = takeIsItch ? colour::red : colour::inkDim;

    // a lens from the stretch on the tape up to the ruler
    g.setColour (colour::black.withAlpha (0.82f * alpha));
    g.fillRect (box.expanded (6.0f, 27.0f).withTrimmedBottom (25.0f));
    g.setColour (colour::line2.withAlpha (0.8f * alpha));
    g.drawLine (a, lineY() - 3.0f, box.getX(), box.getBottom() + 1.0f, 0.5f);
    g.drawLine (b, lineY() - 3.0f, box.getRight(), box.getBottom() + 1.0f, 0.5f);
    g.setColour ((takeIsItch ? colour::red : colour::ink).withAlpha (0.25f * alpha));
    g.fillRect (juce::Rectangle<float> (a, lineY() - 1.5f, std::max (1.0f, b - a), 3.0f));

    // the waveform
    const float mid = box.getCentreY(), half = 0.5f * box.getHeight() - 1.0f;
    g.setColour (wave.withAlpha (0.85f * alpha));
    for (size_t x = 0; x < peaks.size(); ++x)
    {
        const float h = std::max (0.5f, peaks[x] * half);
        g.fillRect (juce::Rectangle<float> (box.getX() + (float) x, mid - h, 1.0f, 2.0f * h));
    }

    // bars and beats: 1 2 3 4
    const int beatsPerBar = std::max (1, juce::roundToInt (t.barBeats));
    const float barW = width / (float) t.bars, beatW = barW / (float) beatsPerBar;
    g.setFont (fonts->small (*this, fonts->mono, 9.5f));
    for (int bar = 0; bar <= t.bars; ++bar)
    {
        const float x = box.getX() + barW * (float) bar;
        g.setColour (colour::ink.withAlpha (0.55f * alpha));
        g.drawLine (x, box.getY(), x, box.getBottom(), 0.7f);
        if (bar == t.bars)
            break;
        for (int beat = 0; beat < beatsPerBar; ++beat)
        {
            const float bx = x + beatW * (float) beat;
            if (beat > 0)
            {
                g.setColour (colour::inkFaint.withAlpha (0.7f * alpha));
                g.drawLine (bx, box.getBottom() - 3.0f, bx, box.getBottom(), 0.6f);
            }
            if (beatW >= 13.0f)
            {
                g.setColour ((beat == 0 ? colour::ink : colour::inkDim).withAlpha ((beat == 0 ? 0.8f : 0.5f) * alpha));
                g.drawText (juce::String (beat + 1), juce::Rectangle<float> (bx - 6.0f, box.getY() - 12.0f, 12.0f, 10.0f), juce::Justification::centred);
            }
        }
    }

    // what it is
    const auto font = fonts->small (*this, fonts->regular, 11.0f, 0.04f);
    g.setFont (font);
    g.setColour ((takeIsItch ? colour::redText : colour::ink).withAlpha (0.85f * alpha));
    const float textWidth = juce::GlyphArrangement::getStringWidth (font, caption) + 4.0f;
    const float tx = std::clamp (box.getX(), 8.0f, (float) getWidth() - 8.0f - textWidth);
    g.drawText (caption, juce::Rectangle<float> (tx, 0.0f, textWidth, 12.0f), juce::Justification::centredLeft);
}

void KeepTape::mouseMove (const juce::MouseEvent& e)
{
    hovering = true;
    mouseAt = e.position;
    const auto* m = markAt (e.position);
    hoverId = m != nullptr ? m->id : 0;
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void KeepTape::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    hoverId = 0;
}

juce::String KeepTape::getTooltip()
{
    if (const auto* m = markAt (mouseAt))
    {
        if (m->kind == dendrite::keep::Kind::itch)
            return "this itch, alone: pull it into Live (shift: with everything)";
        return m->kind == dendrite::keep::Kind::throwing ? "this throw and its echoes: pull it into Live"
                                                         : "this runaway: pull it into Live";
    }
    return "the last minute. press where it should begin, pull it into Live";
}

void KeepTape::mouseDown (const juce::MouseEvent& e)
{
    pressed = true;
    dragged = false;
    auto& keep = processor.keep;
    std::optional<dendrite::keep::Take> t;
    const auto* m = markAt (e.position);
    if (m != nullptr)
        t = keep.takeMark (*m, e.mods.isShiftDown());
    else
        t = keep.takeFrom (frameAt (e.position.x));
    if (t)
        openRuler (*t, m);
    else
        take.reset();
}

void KeepTape::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressed || dragged || ! take || e.getDistanceFromDragStart() < 6)
        return;
    dragged = true;
    juce::Component::SafePointer<KeepTape> self (this);
    if (! dragOut (processor.keep, *take, *this, [self] {
            if (self != nullptr)
            {
                self->pressed = false;
                self->flash = 1.0f;
            }
        }))
        pressed = false;
}

void KeepTape::mouseUp (const juce::MouseEvent&)
{
    pressed = false;
}
} // namespace dendrite::ui
