#include "Specimen.h"
#include "rig/Recording.h"
#include "rig/Identities.h"

namespace dendrite::ui
{
namespace
{
    constexpr int neurons = flydata::neurons;

    // a 1-2-5 length in um near a wanted length
    float niceMicrons (float wanted)
    {
        const float decade = std::pow (10.0f, std::floor (std::log10 (std::max (wanted, 1.0e-3f))));
        float best = decade;
        for (float m : { 1.0f, 2.0f, 5.0f, 10.0f })
            if (std::abs (std::log (m * decade / wanted)) < std::abs (std::log (best / wanted)))
                best = m * decade;
        return best;
    }

    juce::String microns (float um) { return juce::String (juce::roundToInt (um)) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb5m")); }

    // where FlyData's anchors are, and what the rig calls them
    struct Region
    {
        const char* anchor;
        const char* name;
        bool above;
    };
    const Region regions[] = {
        { "eyeLeft", "LPLC2 left, looming", true },
        { "mushroomBody", "KC, mushroom body left", true },
        { "eyeRight", "LPLC2 right, looming", true },
        { "ears", "JO, johnston's organ", false },
        { "giantFiber", "DNp01, giant fiber", false },
        { "grooming", "DNg11, grooming", false },
    };

} // namespace

Specimen::Specimen (DendriteProcessor& p)
    : processor (p),
      // while the hand holds the electrode, the hand is the truth: a late echo from the host is ignored
      feedLink (*p.params.getParameter ("feed"), [this] (float v) { if (! dragging) feed = v; }),
      spaceLink (*p.params.getParameter ("space"), [this] (float v) { if (! dragging) space = v; }),
      throwLink (*p.params.getParameter ("throw"), [this] (float v) { throwOn = v > 0.5f; })
{
    setOpaque (true);
    feedLink.sendInitialUpdate();
    spaceLink.sendInitialUpdate();
    throwLink.sendInitialUpdate();
}

Specimen::~Specimen()
{
    if (touching)
        setThrow (false); // never leave a throw held when the window closes mid-press
}

void Specimen::resized()
{
    // room for the region names above and below, the ticks and scale on the left and bottom
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft (52.0f);
    r.removeFromRight (24.0f);
    r.removeFromTop (72.0f);
    r.removeFromBottom (84.0f);
    const float aspect = widthMicrons / heightMicrons;
    const float w = std::min (r.getWidth(), r.getHeight() * aspect);
    brain = juce::Rectangle<float> (w, w / aspect).withCentre (r.getCentre());
}

void Specimen::renderPoints (float px)
{
    const int w = std::max (1, juce::roundToInt (brain.getWidth() * px)), h = std::max (1, juce::roundToInt (brain.getHeight() * px));
    if (points.getWidth() != w || points.getHeight() != h)
        points = juce::Image (juce::Image::RGB, w, h, true);
    const auto& rec = processor.recording;
    const float most = (float) std::log1p ((double) std::max<uint32_t> (1u, rec.mostExposed()));
    // one logical pixel at the default window (816 um across ~790 px: about 1 um a pixel), growing
    // with the window so a large picture keeps its density; always whole physical pixels
    const int dot = std::max (1, juce::roundToInt (px * std::max (1.0f, brain.getWidth() / 790.0f)));

    juce::Image::BitmapData bits (points, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < h; ++y)
        std::memset (bits.getLinePointer (y), 0, (size_t) (bits.pixelStride * w));

    for (int n = 0; n < neurons; ++n)
    {
        // the floor, raised by this session's exposure (to the dim step), lit by firing now
        const auto e = rec.exposure (n);
        float level = tone::floor;
        if (e > 0)
            level += (tone::dim - tone::floor) * (float) std::log1p ((double) e) / most;
        const float hz = rec.trace (n) / (float) rig::Recording::traceSeconds;
        if (hz >= 1.0f)
            level = std::max (level, tone::rate (hz));
        const auto v = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (255.0f * level));

        const int x0 = juce::jlimit (0, w - dot, (int) ((float) flydata::neuronX[n] / 65535.0f * (float) (w - dot)));
        const int y0 = juce::jlimit (0, h - dot, (int) ((float) flydata::neuronY[n] / 65535.0f * (float) (h - dot)));
        for (int y = y0; y < y0 + dot; ++y)
            for (int x = x0; x < x0 + dot; ++x)
            {
                auto* p = bits.getPixelPointer (x, y);
                if (p[0] < v) // where two neurons share a pixel, the brighter one shows
                    p[0] = p[1] = p[2] = v;
            }
    }

    // scars, burned in over the neurons: a cross at each, never removed
    const int arm = 2 * dot;
    for (int n = 0; n < neurons; ++n)
    {
        const int count = rec.scars (n);
        if (count == 0)
            continue;
        const auto v = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (255.0f * tone::scar (rec.scarAge (n), count)));
        const int cx = juce::jlimit (0, w - dot, (int) ((float) flydata::neuronX[n] / 65535.0f * (float) (w - dot)));
        const int cy = juce::jlimit (0, h - dot, (int) ((float) flydata::neuronY[n] / 65535.0f * (float) (h - dot)));
        auto put = [&] (int x, int y) {
            if (x < 0 || y < 0 || x >= w || y >= h)
                return;
            auto* p = bits.getPixelPointer (x, y);
            if (p[0] < v)
                p[0] = p[1] = p[2] = v;
        };
        for (int t = 0; t < dot; ++t)
            for (int k = -arm; k < dot + arm; ++k)
            {
                put (cx + k, cy + t); // across
                put (cx + t, cy + k); // down
            }
    }
}

void Specimen::paint (juce::Graphics& g)
{
    g.fillAll (tone::black);
    const Hairline line (g);
    renderPoints (line.px);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImageTransformed (points, juce::AffineTransform::scale (1.0f / line.px)
                                        .translated (line.snap (brain.getX()) - 0.5f / line.px, line.snap (brain.getY()) - 0.5f / line.px));
    hovered = findHover();
    paintScale (g, line);
    paintRegions (g, line);
    measureTip();
    paintElectrode (g, line);
    paintHover (g, line);
}

juce::Point<float> Specimen::neuronPoint (int n) const noexcept
{
    return { brain.getX() + brain.getWidth() * (float) flydata::neuronX[n] / 65535.0f,
             brain.getY() + brain.getHeight() * (float) flydata::neuronY[n] / 65535.0f };
}

int Specimen::findHover() const
{
    if (dragging || touching || hoverTip || ! brain.expanded (6.0f).contains (hoverAt))
        return -1;
    int nearest = -1;
    float best = 8.0f * 8.0f;
    for (int n = 0; n < neurons; ++n)
    {
        const float d = neuronPoint (n).getDistanceSquaredFrom (hoverAt);
        if (d < best)
        {
            best = d;
            nearest = n;
        }
    }
    return nearest;
}

void Specimen::paintHover (juce::Graphics& g, const Hairline& line)
{
    // the neuron under the hand: who it is in its atlas, and its rate now
    const int nearest = hovered;
    if (nearest < 0)
        return;
    // how crowded it is there: the neurons within 5 um of it
    const auto at = neuronPoint (nearest);
    const float r5 = 5.0f * brain.getWidth() / widthMicrons;
    int near = 0;
    for (int n = 0; n < neurons; ++n)
        if (neuronPoint (n).getDistanceSquaredFrom (at) <= r5 * r5)
            ++near;

    namespace id = rig::identities;
    g.setColour (tone::grey (tone::live));
    g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ line.snap (at.x), line.snap (at.y) }), line.width());
    const auto dot = utf8 ("  \xc2\xb7  ");
    const juce::String type (id::typeName[id::type[nearest]]);
    juce::String text = juce::String ((juce::int64) id::rootId[nearest]) + dot + (type.isEmpty() ? juce::String ("untyped") : type);
    if (const juce::String nt (id::transmitterName[id::transmitter[nearest]]); nt.isNotEmpty())
        text << dot << nt;
    if (const juce::String c (id::className[id::superClass[nearest]]); c.isNotEmpty())
        text << dot << c.replaceCharacter ('_', ' ');
    if (const juce::String s (id::sideName[id::side[nearest]]); s.isNotEmpty())
        text << dot << s;
    text << dot << juce::String (processor.recording.trace (nearest) / (float) rig::Recording::traceSeconds, 1) << " Hz";
    if (near > 1)
        text << dot << near << " within 5" << utf8 (" \xc2\xb5m");
    const int scarred = processor.recording.scars (nearest);
    if (scarred > 0)
        text << dot << scarred << (scarred == 1 ? " scar" : " scars");
    // written in the caption's second line, in place of what the picture is
    g.setColour (tone::grey (tone::bright));
    g.setFont (fonts->at (type::small));
    g.drawText (text, juce::Rectangle<float> (14.0f, 24.0f, (float) getWidth() - 28.0f, 16.0f), juce::Justification::centredLeft, true);
}

void Specimen::paintScale (juce::Graphics& g, const Hairline& line)
{
    const float umPerPx = widthMicrons / brain.getWidth();
    const float left = brain.getX() - 10.0f, bottom = brain.getBottom() + 10.0f;

    // calibration: a tick every 50 um, longer every 250, on the left and bottom edges
    g.setColour (tone::grey (tone::dim));
    line.v (left, brain.getY(), bottom);
    line.h (left, brain.getRight(), bottom);
    for (int um = 0; (float) um <= widthMicrons; um += 50)
    {
        const float x = left + (float) um / umPerPx, len = um % 250 == 0 ? 6.0f : 3.0f;
        if (x > brain.getRight())
            break;
        line.v (x, bottom, bottom + len);
    }
    for (int um = 0; (float) um <= heightMicrons; um += 50)
    {
        const float y = bottom - (float) um / umPerPx, len = um % 250 == 0 ? 6.0f : 3.0f;
        if (y < brain.getY())
            break;
        line.h (left - len, left, y);
    }
    g.setFont (fonts->at (type::small));
    g.setColour (tone::grey (tone::mid));
    g.drawText ("0", juce::Rectangle<float> (left - 22.0f, bottom + 2.0f, 16.0f, 12.0f), juce::Justification::centredRight);

    // the scale bar: a 1-2-5 length near 80 px, picked again on every resize
    const float um = niceMicrons (80.0f * umPerPx), len = um / umPerPx;
    const float y = bottom + 58.0f;
    const juce::String levels[] = { "silent", "fired this session", "10 Hz", utf8 ("\xe2\x89\xa5 100 Hz") };
    const auto scars = juce::String (processor.recording.scarTotal()) + (processor.recording.scarTotal() == 1 ? " scar" : " scars");
    // the bar and the legend start under the map's left edge; a narrower map (a taller atlas, the male's)
    // moves them left so the row still fits
    float x0 = left;
    {
        const auto& small = fonts->at (type::small);
        float width = len + 90.0f + 11.0f + juce::GlyphArrangement::getStringWidth (small, scars);
        for (const auto& label : levels)
            width += 10.0f + juce::GlyphArrangement::getStringWidth (small, label) + 14.0f;
        x0 = std::max (14.0f, std::min (left, (float) getWidth() - 14.0f - width));
    }
    g.setColour (tone::grey (tone::bright));
    line.h (x0, x0 + len, y);
    line.v (x0, y - 3.0f, y + 3.0f);
    line.v (x0 + len, y - 3.0f, y + 3.0f);
    g.setFont (fonts->at (type::data));
    g.drawText (microns (um), juce::Rectangle<float> (x0 + len + 8.0f, y - 8.0f, 80.0f, 16.0f), juce::Justification::centredLeft);

    // the light's calibration: what each step of grey means
    {
        g.setFont (fonts->at (type::small));
        float x = x0 + len + 90.0f;
        auto swatch = [&] (float level, const juce::String& label) {
            g.setColour (tone::grey (level));
            g.fillRect (juce::Rectangle<float> (x, y - 3.0f, 6.0f, 6.0f));
            g.setColour (tone::grey (tone::mid));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label);
            g.drawText (label, juce::Rectangle<float> (x + 10.0f, y - 8.0f, w + 2.0f, 16.0f), juce::Justification::centredLeft);
            x += 10.0f + w + 14.0f;
        };
        swatch (tone::floor, levels[0]);
        swatch (tone::dim, levels[1]);
        swatch (tone::rate (10.0f), levels[2]);
        swatch (tone::live, levels[3]);
        // a scar's mark, as the map draws it, settled
        g.setColour (tone::grey (tone::mid));
        g.fillRect (juce::Rectangle<float> (x, y - 0.5f, 7.0f, 1.0f));
        g.fillRect (juce::Rectangle<float> (x + 3.0f, y - 3.5f, 1.0f, 7.0f));
        g.setColour (tone::grey (tone::mid));
        g.drawText (scars, juce::Rectangle<float> (x + 11.0f, y - 8.0f, 90.0f, 16.0f), juce::Justification::centredLeft);
    }

    // what the picture is, and what it holds
    g.setColour (tone::grey (tone::dim));
    g.setFont (fonts->at (type::small));
    const auto dot = utf8 ("  \xc2\xb7  ");
    const auto counts = grouped (flydata::neurons) + " neurons" + dot + grouped (flydata::synapses) + " connections" + dot + grouped (synapseCount()) + " synapses";
    const auto what = "frontal projection" + dot + "anchor points" + dot + microns (widthMicrons) + utf8 (" \xc3\x97 ") + microns (heightMicrons);
    // top left, as on a slide's label
    g.drawText (counts, juce::Rectangle<float> (14.0f, 8.0f, 600.0f, 16.0f), juce::Justification::centredLeft);
    if (hovered < 0)
        g.drawText (what, juce::Rectangle<float> (14.0f, 24.0f, 600.0f, 16.0f), juce::Justification::centredLeft);
}

void Specimen::paintRegions (juce::Graphics& g, const Hairline& line)
{
    const auto font = fonts->at (type::small);
    g.setFont (font);
    struct Placed
    {
        juce::Point<float> at;
        float x, w;
        juce::String text;
    };
    for (const bool above : { true, false })
    {
        std::vector<Placed> row;
        for (const auto& r : regions)
        {
            if (r.above != above)
                continue;
            for (int k = 0; k < flydata::anchorCount; ++k)
                if (juce::String (flydata::anchors[k].name) == r.anchor)
                {
                    const juce::Point<float> at { brain.getX() + brain.getWidth() * flydata::anchors[k].x,
                                                  brain.getY() + brain.getHeight() * flydata::anchors[k].y };
                    const juce::String text (r.name);
                    const float w = juce::GlyphArrangement::getStringWidth (font, text);
                    row.push_back ({ at, at.x - 0.5f * w, w, text });
                }
        }
        std::sort (row.begin(), row.end(), [] (const Placed& a, const Placed& b) { return a.at.x < b.at.x; });

        // side by side in one row, never touching; below, where the regions crowd the midline,
        // the row is centred on them again so the leaders fan out both ways
        for (size_t k = 1; k < row.size(); ++k)
            row[k].x = std::max (row[k].x, row[k - 1].x + row[k - 1].w + 32.0f);
        if (! above && ! row.empty())
        {
            float anchorsMid = 0.0f;
            for (const auto& p : row)
                anchorsMid += p.at.x / (float) row.size();
            const float rowLeft = row.front().x, rowRight = row.back().x + row.back().w;
            float shift = anchorsMid - 0.5f * (rowLeft + rowRight);
            shift = juce::jlimit (brain.getX() - rowLeft, brain.getRight() - rowRight, shift);
            for (auto& p : row)
                p.x += shift;
        }

        const float textY = above ? brain.getY() - 26.0f : brain.getBottom() + 34.0f;
        const float bus = above ? textY + 12.0f : textY - 12.0f, stub = above ? textY + 7.0f : textY - 7.0f;
        for (size_t k = 0; k < row.size(); ++k)
        {
            const auto& p = row[k];
            g.setColour (tone::grey (tone::dim));
            g.drawRect (juce::Rectangle<float> (6.0f, 6.0f).withCentre (p.at).expanded (0.5f * line.width()), line.width());
            // from the region to the bus, along it to the name, and a short stub up or down to it;
            // the middle names keep their stub between their neighbours' leaders so none cross
            float tx = juce::jlimit (p.x, p.x + p.w, p.at.x);
            if (k > 0 && k + 1 < row.size())
                tx = juce::jlimit (row[k - 1].at.x + 4.0f, row[k + 1].at.x - 4.0f, tx);
            if (above)
                line.v (p.at.x, bus, p.at.y - 3.0f);
            else
                line.v (p.at.x, p.at.y + 3.0f, bus);
            line.h (std::min (p.at.x, tx), std::max (p.at.x, tx) + line.width(), bus);
            line.v (tx, std::min (bus, stub), std::max (bus, stub));
            g.setColour (tone::grey (tone::mid));
            g.drawText (p.text, juce::Rectangle<float> (p.x, textY - 7.0f, p.w + 2.0f, 14.0f), juce::Justification::centredLeft);
        }
    }
}

//==============================================================================
// the electrode

juce::Point<float> Specimen::tipPoint() const noexcept
{
    return { brain.getX() + space * brain.getWidth(), brain.getY() + (1.0f - feed) * brain.getHeight() };
}

void Specimen::measureTip()
{
    const auto& rec = processor.recording;
    const float tx = space * widthMicrons, ty = (1.0f - feed) * heightMicrons, r2 = tipMicrons * tipMicrons;
    Recorded out;
    out.xMicrons = tx;
    out.yMicrons = feed * heightMicrons;
    float sum = 0.0f;
    for (int n = 0; n < neurons; ++n)
    {
        const float dx = (float) flydata::neuronX[n] / 65535.0f * widthMicrons - tx;
        const float dy = (float) flydata::neuronY[n] / 65535.0f * heightMicrons - ty;
        if (dx * dx + dy * dy > r2)
            continue;
        const float hz = rec.trace (n) / (float) rig::Recording::traceSeconds;
        ++out.units;
        sum += hz;
        if (out.busiest < 0 || hz > out.busiestHz)
        {
            out.busiest = n;
            out.busiestHz = hz;
        }
    }
    out.meanHz = out.units > 0 ? sum / (float) out.units : 0.0f;
    tip = out;
}

void Specimen::paintElectrode (juce::Graphics& g, const Hairline& line)
{
    const auto at = tipPoint();
    const float r = tipRadius();
    const bool live = throwOn || touching;

    // the stimulus: a hairline from where the hand presses to the tip, and the press site
    if (touching)
    {
        g.setColour (tone::grey (tone::bright));
        const auto toward = at + (pressAt - at) * (r / std::max (r, pressAt.getDistanceFrom (at)));
        g.drawLine (juce::Line<float> (pressAt, toward), line.width());
        g.drawRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ line.snap (pressAt.x), line.snap (pressAt.y) }), line.width());
    }

    // the tip: its recording radius to scale, four ticks outside it, the point itself
    g.setColour (tone::grey (live ? tone::live : dragging || hoverTip ? tone::live : tone::bright));
    const auto centre = juce::Point<float> (line.snap (at.x), line.snap (at.y));
    g.drawEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (centre), line.width());
    line.h (centre.x - r - 7.0f, centre.x - r - 2.0f, centre.y);
    line.h (centre.x + r + 2.0f, centre.x + r + 7.0f, centre.y);
    line.v (centre.x, centre.y - r - 7.0f, centre.y - r - 2.0f);
    line.v (centre.x, centre.y + r + 2.0f, centre.y + r + 7.0f);
    g.fillRect (juce::Rectangle<float> (2.0f, 2.0f).withCentre (centre));

    // where it sits, marked on the calibration axes
    const float left = brain.getX() - 10.0f, bottom = brain.getBottom() + 10.0f;
    g.setColour (tone::grey (tone::bright));
    line.h (left - 9.0f, left, at.y);
    line.v (at.x, bottom, bottom + 9.0f);
}

void Specimen::tick (double nowSeconds)
{
    clockNow = nowSeconds;
    // a press becomes a throw once it has lasted a moment, so a click that only focuses the window is not one
    if (pending && nowSeconds - pressStarted >= 0.08)
    {
        pending = false;
        touching = true;
        touchStarted = nowSeconds;
        setThrow (true);
    }
}

void Specimen::setThrow (bool on)
{
    throwOn = on;
    throwLink.setValueAsCompleteGesture (on ? 1.0f : 0.0f);
}

juce::String Specimen::getTooltip()
{
    if (hoverTip)
        return "electrode: drag up for feed (" + DendriteProcessor::feedName (feed) + " " + juce::String (juce::roundToInt (feed * 100.0f))
               + "%), right for space (" + DendriteProcessor::spaceName (space) + " " + juce::String (juce::roundToInt (space * 100.0f))
               + utf8 ("%) \xc2\xb7 shift: fine \xc2\xb7 wheel: nudge \xc2\xb7 double-click: home");
    if (hoverPlate)
        return "press and hold to stimulate (the throw)";
    return {};
}

void Specimen::mouseMove (const juce::MouseEvent& e)
{
    hoverAt = e.position;
    hoverTip = nearTip (e.position);
    hoverPlate = ! hoverTip;
    setMouseCursor (hoverTip ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
}

void Specimen::mouseExit (const juce::MouseEvent&)
{
    hoverAt = { -1.0f, -1.0f };
    hoverTip = hoverPlate = false;
}

void Specimen::mouseDown (const juce::MouseEvent& e)
{
    if (! nearTip (e.position))
    {
        pending = true;
        pressStarted = clockNow;
        pressAt = e.position;
        return;
    }
    dragging = true;
    fine = e.mods.isShiftDown();
    grabMouse = e.position;
    grabUnit = { space, 1.0f - feed };
    feedLink.beginGesture();
    spaceLink.beginGesture();
}

void Specimen::mouseDrag (const juce::MouseEvent& e)
{
    if (touching || pending)
    {
        pressAt = e.position;
        return;
    }
    if (! dragging)
        return;
    // measured from where the drag began, not added up frame by frame (the host echoes each value late)
    if (e.mods.isShiftDown() != fine)
    {
        fine = e.mods.isShiftDown();
        grabMouse = e.position;
        grabUnit = { space, 1.0f - feed };
    }
    const float scale = fine ? 0.2f : 1.0f;
    const auto moved = e.position - grabMouse;
    space = juce::jlimit (0.0f, 1.0f, grabUnit.x + moved.x / brain.getWidth() * scale);
    feed = juce::jlimit (0.0f, 1.0f, 1.0f - (grabUnit.y + moved.y / brain.getHeight() * scale));
    spaceLink.setValueAsPartOfGesture (space);
    feedLink.setValueAsPartOfGesture (feed);
}

void Specimen::mouseUp (const juce::MouseEvent&)
{
    if (touching)
        setThrow (false);
    touching = pending = false;
    if (dragging)
    {
        feedLink.endGesture();
        spaceLink.endGesture();
    }
    dragging = false;
}

void Specimen::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! nearTip (e.position))
        return; // a double press on the plate is two stimuli, not a reset
    feedLink.setValueAsCompleteGesture (DendriteProcessor::defaultFeed);
    spaceLink.setValueAsCompleteGesture (DendriteProcessor::defaultSpace);
}

void Specimen::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float step = (e.mods.isShiftDown() ? 0.01f : 0.04f) * (w.isReversed ? -1.0f : 1.0f);
    if (std::abs (w.deltaX) > std::abs (w.deltaY))
        spaceLink.setValueAsCompleteGesture (juce::jlimit (0.0f, 1.0f, space + step * (w.deltaX > 0.0f ? 1.0f : -1.0f)));
    else if (std::abs (w.deltaY) > 0.0f)
        feedLink.setValueAsCompleteGesture (juce::jlimit (0.0f, 1.0f, feed + step * (w.deltaY > 0.0f ? 1.0f : -1.0f)));
}
} // namespace dendrite::ui
