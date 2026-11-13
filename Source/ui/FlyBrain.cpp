#include "FlyBrain.h"

namespace dendrite::ui
{
FlyBrain::FlyBrain (DendriteProcessor& p) : processor (p)
{
    // the whole brain's density, drawn once as a soft grey shape
    backdrop = juce::Image (juce::Image::ARGB, flydata::backdropWidth, flydata::backdropHeight, true);
    for (int y = 0; y < flydata::backdropHeight; ++y)
        for (int x = 0; x < flydata::backdropWidth; ++x)
        {
            const float d = (float) flydata::backdrop[y * flydata::backdropWidth + x] / 255.0f;
            backdrop.setPixelAt (x, y, colour::tissue.withAlpha (0.55f * d));
        }
    setRepaintsOnMouseActivity (false);
}

void FlyBrain::tick()
{
    // take every neuron's spikes since the last frame: a fresh spike is red, then it dries to grey
    float active = 0.0f;
    for (int n = 0; n < flydata::neurons; ++n)
    {
        const float s = (float) processor.takeFlySpikes (n);
        fresh[(size_t) n] = 0.78f * fresh[(size_t) n] + s;
        dry[(size_t) n] = 0.965f * dry[(size_t) n] + 0.3f * s;
        active += s;
    }
    awake = processor.display.flyAwake.load (std::memory_order_relaxed);
    quiet += ((active > 0.0f ? 0.0f : 1.0f) - quiet) * 0.05f;
    repaint();
}

juce::Rectangle<float> FlyBrain::brainArea() const
{
    auto r = getLocalBounds().toFloat();
    if (open)
        r = r.reduced (40.0f, 26.0f).withTrimmedBottom (26.0f);
    else
        r = r.reduced (2.0f, 4.0f);
    // keep the brain's own shape
    const float w = std::min (r.getWidth(), r.getHeight() * flydata::aspect);
    return juce::Rectangle<float> (w, w / flydata::aspect).withCentre (r.getCentre());
}

juce::String FlyBrain::doingLine() const
{
    const auto& d = processor.display;
    constexpr auto relaxed = std::memory_order_relaxed;
    if (! awake)
        return "asleep";
    juce::StringArray words;
    if (d.flyJump.load (relaxed) > 0.0f)
    {
        juce::String w = "jumps  tape " + juce::String (d.flySemitones.load (relaxed), 1) + " st";
        if (d.flyThrowing.load (relaxed))
            w << "  throw";
        words.add (w);
    }
    if (d.flyFrozen.load (relaxed))
        words.add ("freezes");
    if (d.flyScratch.load (relaxed) > 0.2f)
        words.add ("scratches  " + juce::String (d.flyScratchHz.load (relaxed) / 1000.0f, 1) + " kHz");
    const float turn = d.flyTurn.load (relaxed);
    if (std::abs (turn) > 0.25f)
        words.add (turn > 0.0f ? "turns left" : "turns right");
    return words.isEmpty() ? "listens" : words.joinIntoString ("   \xc2\xb7   ");
}

void FlyBrain::paint (juce::Graphics& g)
{
    const auto area = brainArea();
    const float glow = ghostGlow;
    if (open)
    {
        g.setColour (colour::line);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    }

    // the whole brain, faint; a little brighter while the ghost's itch plays
    const float scale = juce::Component::getApproximateScaleFactorForComponent (this);
    const int pw = std::max (1, juce::roundToInt (area.getWidth() * scale)), ph = std::max (1, juce::roundToInt (area.getHeight() * scale));
    if (backdropScaled.getWidth() != pw || backdropScaled.getHeight() != ph)
        backdropScaled = backdrop.rescaled (pw, ph, juce::Graphics::highResamplingQuality);
    g.setOpacity ((open ? 1.0f : 0.8f) * (awake ? 1.0f : 0.5f) + 0.3f * glow);
    g.drawImage (backdropScaled, area);
    g.setOpacity (1.0f);

    // every neuron that has fired lately, at its place: red while fresh, drying to grey
    const float dot = open ? 1.7f : 1.0f;
    const auto red = colour::redText;
    for (int n = 0; n < flydata::neurons; ++n)
    {
        const float dr = dry[(size_t) n], fr = fresh[(size_t) n];
        if (dr < 0.02f && fr < 0.05f)
            continue;
        const float x = area.getX() + area.getWidth() * (float) flydata::neuronX[n] / 65535.0f;
        const float y = area.getY() + area.getHeight() * (float) flydata::neuronY[n] / 65535.0f;
        // a neuron firing at 50 Hz keeps about four spikes in hand: soft, so a busy part does not burn
        const float f = 1.0f - std::exp (-0.3f * fr);
        const float grey = std::min (1.0f, 0.5f * dr) * 0.5f;
        g.setColour (colour::inkDim.withAlpha (grey).interpolatedWith (red.withAlpha (open ? 0.95f : 0.8f), f));
        const float s = dot * (1.0f + 0.5f * f);
        g.fillRect (x - 0.5f * s, y - 0.5f * s, s, s);
    }

    if (! open)
        return;

    // where its senses and reflexes sit
    g.setFont (fonts->small (*this, fonts->light, 14.0f, 0.02f));
    auto label = [&] (int k, const juce::String& text, float dx, float dy)
    {
        const auto& a = flydata::anchors[k];
        const juce::Point<float> at { area.getX() + area.getWidth() * a.x, area.getY() + area.getHeight() * a.y };
        const juce::Point<float> to = at.translated (dx, dy);
        g.setColour (colour::line2);
        g.drawLine ({ at, to }, 0.7f);
        g.setColour (colour::inkDim);
        const float w = 160.0f;
        const auto box = dx >= 0.0f ? juce::Rectangle<float> (to.x + 4.0f, to.y - 8.0f, w, 16.0f)
                                    : juce::Rectangle<float> (to.x - 4.0f - w, to.y - 8.0f, w, 16.0f);
        g.drawText (text, box, dx >= 0.0f ? juce::Justification::centredLeft : juce::Justification::centredRight);
    };
    const float lift = area.getHeight() * 0.42f;
    label (0, "ears", -area.getWidth() * 0.14f, area.getHeight() * 0.32f);
    label (1, "eye", -area.getWidth() * 0.12f, -lift);
    label (2, "eye", area.getWidth() * 0.10f, -lift);
    label (3, "giant fiber: the jump", area.getWidth() * 0.30f, -area.getHeight() * 0.14f);
    label (4, "grooming: the scratch", area.getWidth() * 0.16f, area.getHeight() * 0.22f);

    // what it is doing, and what this is
    const auto bottom = getLocalBounds().toFloat().reduced (40.0f, 0.0f).removeFromBottom (34.0f);
    g.setFont (fonts->small (*this, fonts->mono, 13.0f));
    g.setColour (awake ? colour::redText : colour::inkFaint);
    g.drawText (doingLine(), bottom, juce::Justification::centredLeft);
    g.setFont (fonts->small (*this, fonts->light, 13.0f, 0.02f));
    g.setColour (colour::inkFaint);
    const auto count = juce::String (flydata::neurons / 1000) + "," + juce::String (flydata::neurons % 1000).paddedLeft ('0', 3);
    g.drawText (count + " neurons of a fly's brain, wired as FlyWire mapped them", bottom,
                juce::Justification::centredRight);
}

void FlyBrain::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && onToggle)
        onToggle();
}

juce::String FlyBrain::getTooltip()
{
    return open ? "close the fly" : "the fly: click to watch it think";
}
} // namespace dendrite::ui
