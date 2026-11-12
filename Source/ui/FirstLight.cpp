#include "FirstLight.h"

namespace dendrite::ui
{
namespace
{
    // one deadpan line per step (the manual's narrator)
    struct Caption { double from, to; const char* text; };
    constexpr Caption captions[] = {
        { 0.4, 5.8, "this is the light. drag it." },
        { 6.2, 10.4, "touch the dark. it throws." },
        { 10.8, 15.6, "when the music repeats itself, it itches." },
        { 16.0, 20.0, "it keeps the last minute. pull an itch into your song." },
        { 20.4, 23.4, "click the name to see this again." },
    };

    // the three heads the demonstration fires, and when
    constexpr int shownHeads[3] = { 0, 1, 4 }; // scream, laugh, ghost
    constexpr double headAt[3] = { 12.6, 13.3, 14.0 };
    constexpr double headLength = 1.4;

    float smooth (double x) { const float u = (float) std::clamp (x, 0.0, 1.0); return u * u * (3.0f - 2.0f * u); }
    float between (double t, double a, double b) { return smooth ((t - a) / (b - a)); }
    float window (double t, double a, double b, double fadeIn = 0.4, double fadeOut = 0.4)
    {
        return std::min (between (t, a, a + fadeIn), 1.0f - between (t, b - fadeOut, b));
    }
    juce::Point<float> mix (juce::Point<float> a, juce::Point<float> b, float u) { return a + (b - a) * u; }

    juce::PropertiesFile::Options settings()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "dendrite";
        o.filenameSuffix = ".settings";
        o.folderName = "dendrite";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }
} // namespace

FirstLight::FirstLight (Field& f, ItchStrip& s, KeepTape& t) : field (f), strip (s), tape (t)
{
    glow = makeGlow (colour::ink, 48);
    setInterceptsMouseClicks (true, false);
}

FirstLight::~FirstLight()
{
    if (running)
        stop();
}

void FirstLight::startIfNew (double t)
{
    const auto policy = juce::SystemStats::getEnvironmentVariable ("DENDRITE_FIRST_LIGHT", {});
    if (policy == "0")
        return;
    if (policy == "1")
    {
        start (t);
        return;
    }
    juce::PropertiesFile props (settings());
    if (props.getIntValue ("firstLight", 0) >= lesson)
        return;
    // remembered before it plays: a window closed halfway doesn't start it over every time
    props.setValue ("firstLight", lesson);
    props.saveIfNeeded();
    start (t);
}

void FirstLight::start (double t)
{
    running = true;
    ending = false;
    started = t;
    now = t;
    nextHit = 1.0;
    fade = 0.0f;
    lightHome = field.lightUnit();
    hand = field.lightOnScreen() + juce::Point<float> (70.0f, 46.0f);
    field.setGhost (true);
    toFront (false);
}

void FirstLight::stop()
{
    running = ending = pressing = carrying = false;
    field.setGhost (false);
    strip.setGhost ({});
    tape.setGhost (false);
    repaint();
}

void FirstLight::mouseDown (const juce::MouseEvent&)
{
    // any click ends it, and is only that: it doesn't throw or move anything
    if (running && ! ending)
    {
        ending = true;
        endedAt = now;
    }
}

juce::Point<float> FirstLight::handAt (double t) const
{
    const auto stripAt = strip.getPosition().toFloat();
    const auto light = field.lightOnScreen();
    const auto home = field.screenOf (lightHome);
    const juce::Point<float> darkUnit = lightHome.x > 0.5f ? juce::Point<float> { 0.17f, 0.3f } : juce::Point<float> { 0.8f, 0.28f };
    const auto dark = field.screenOf (darkUnit);
    const auto dot = stripAt + strip.chosenDot();
    const auto lamp = stripAt + strip.lampCentre (shownHeads[1]);
    const auto away = juce::Point<float> ((float) getWidth() + 80.0f, stripAt.y - 150.0f);
    const auto word = stripAt + strip.wordmarkArea().getCentre();

    if (t < 0.8) return home + juce::Point<float> (70.0f, 46.0f);
    if (t < 1.8) return mix (home + juce::Point<float> (70.0f, 46.0f), home, between (t, 0.8, 1.8));
    if (t < 5.6) return light;
    if (t < 6.8) return mix (light, dark, between (t, 5.6, 6.8));
    if (t < 10.4) return dark + juce::Point<float> (0.0f, t > 7.0 && t < 8.8 ? 1.0f : 0.0f);
    if (t < 11.8) return mix (dark, dot + juce::Point<float> (0.0f, -4.0f), between (t, 10.4, 11.8));
    if (t < 15.4) return dot + juce::Point<float> (0.0f, -4.0f);
    if (t < 16.6) return mix (dot + juce::Point<float> (0.0f, -4.0f), lamp, between (t, 15.4, 16.6));
    if (t < 17.3) return lamp;
    if (t < 19.6)
    {
        // pulled up and out of the window, the way you'd drag it to a track
        const float u = between (t, 17.3, 19.6);
        const auto bend = juce::Point<float> (0.5f * (lamp.x + away.x), lamp.y - 120.0f);
        return mix (mix (lamp, bend, u), mix (bend, away, u), u);
    }
    if (t < 20.6) return word + juce::Point<float> (40.0f, -40.0f);
    return mix (word + juce::Point<float> (40.0f, -40.0f), word + juce::Point<float> (10.0f, -2.0f), between (t, 20.6, 21.6));
}

void FirstLight::tick (double t)
{
    now = t;
    if (! running)
        return;
    const double s = t - started;
    if ((ending && t - endedAt > 0.6) || s > length)
    {
        stop();
        return;
    }
    const float out = ending ? 1.0f - between (t - endedAt, 0.0, 0.6) : 1.0f - between (s, length - 1.0, length);
    fade = std::min (between (s, 0.0, 0.6), out);

    // the light: grabbed and dragged up and right, then left where it is, and home at the end
    {
        const juce::Point<float> a = lightHome;
        const juce::Point<float> b { std::clamp (a.x + 0.2f, 0.1f, 0.9f), std::clamp (a.y - 0.24f, 0.1f, 0.9f) };
        const juce::Point<float> c { std::clamp (a.x + 0.3f, 0.1f, 0.9f), std::clamp (a.y - 0.04f, 0.1f, 0.9f) };
        juce::Point<float> at = a;
        if (s >= 1.8 && s < 3.7)
            at = mix (a, b, between (s, 1.8, 3.7));
        else if (s >= 3.7)
            at = mix (b, c, between (s, 3.7, 5.6));
        if (s >= length - 2.0 || ending)
            at = mix (at, a, ending ? between (t - endedAt, 0.0, 0.5) : between (s, length - 2.0, length - 0.6));
        field.ghostLight (at, s >= 1.8 && s < 5.6 && ! ending);
    }

    // a heartbeat, so trees grow while it shows itself
    if (s >= nextHit && s < 21.0 && ! ending)
    {
        field.ghostHit (0.45f + 0.3f * (float) std::abs (std::sin (s * 3.1)), false);
        nextHit = s + 0.5;
    }

    // the throw
    hand = handAt (s);
    const bool pressNow = s >= 7.0 && s < 8.8 && ! ending;
    field.ghostPress (field.unitOf (hand), pressNow);

    // the itch: repetition builds, three heads fire in turn, then their embers stay
    ItchStrip::Ghost g;
    g.on = true;
    g.repetition = s < 10.8 ? 0.1f : s < 15.4 ? 0.1f + 0.85f * between (s, 10.8, 12.6) : 0.95f - 0.6f * between (s, 15.4, 17.0);
    g.progress = s >= 10.8 && s < 12.6 ? (float) ((s - 10.8) / 1.8) : 0.0f;
    float lamps[dendrite::Heads::count] {};
    for (int k = 0; k < 3; ++k)
    {
        const int h = shownHeads[k];
        lamps[h] = window (s, headAt[k], headAt[k] + headLength, 0.08, 0.6);
        const bool held = s >= 16.8 && s < 19.6;
        g.embers[h] = s >= headAt[k] + headLength - 0.3 ? (held ? 0.65f : 0.3f) : 0.0f;
    }
    std::copy (std::begin (lamps), std::end (lamps), std::begin (g.lamps));
    g.wordmark = window (s, 20.4, 23.4, 0.6, 0.8);
    if (ending)
        g = {};
    strip.setGhost (g);
    field.ghostLamps (ending ? ItchStrip::Ghost {}.lamps : lamps);

    // keep: the tape and the ruler while the ember is held and pulled out
    tape.setGhost (s >= 16.8 && s < 19.6 && ! ending, 1.0f);

    pressing = (s >= 1.8 && s < 5.6) || pressNow || (s >= 16.8 && s < 19.6);
    carrying = s >= 17.3 && s < 19.6;
    pressGlow += ((pressing ? 1.0f : 0.0f) - pressGlow) * 0.3f;
    repaint();
}

void FirstLight::paint (juce::Graphics& g)
{
    if (! running || fade < 0.005f)
        return;
    const double s = now - started;

    // the line for this step
    for (const auto& c : captions)
    {
        const float a = window (s, c.from, c.to, 0.5, 0.5) * fade;
        if (a < 0.01f)
            continue;
        g.setColour (colour::ink.withAlpha (0.85f * a));
        g.setFont (fonts->make (fonts->light, 19.0f, 0.04f));
        g.drawText (c.text, juce::Rectangle<float> (0.0f, 392.0f, (float) getWidth(), 28.0f), juce::Justification::centred);
    }

    // the ghost hand: a soft ring that tightens when it presses
    const float showHand = fade * std::min (between (s, 0.2, 0.9), 1.0f - between (s, 21.8, 22.8)) * (s > 19.4 && s < 20.4 ? 0.0f : 1.0f);
    if (showHand > 0.01f)
    {
        const float r = 9.0f - 2.5f * pressGlow;
        g.setOpacity (0.35f * showHand * (0.6f + 0.4f * pressGlow));
        g.drawImage (glow, juce::Rectangle<float> (44.0f, 44.0f).withCentre (hand));
        g.setOpacity (1.0f);
        g.setColour (colour::ink.withAlpha (0.9f * showHand));
        g.drawEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (hand), 1.4f);
        g.setColour (colour::ink.withAlpha (0.25f * pressGlow * showHand));
        g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (hand));
    }

    // what it carries out of the window: a little take of the itch
    if (carrying)
    {
        const auto box = juce::Rectangle<float> (74.0f, 20.0f).withPosition (hand + juce::Point<float> (10.0f, 6.0f));
        g.setColour (colour::black.withAlpha (0.9f * fade));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (colour::red.withAlpha (0.8f * fade));
        g.drawRoundedRectangle (box, 3.0f, 0.8f);
        for (int x = 0; x < 30; ++x)
        {
            const float u = (float) x / 30.0f;
            float v = 0.0f;
            for (float at : { 0.05f, 0.38f, 0.66f })
                if (u >= at)
                    v = std::max (v, std::exp (-(u - at) * 8.0f));
            const float h = 1.0f + 6.0f * v;
            g.fillRect (juce::Rectangle<float> (box.getX() + 5.0f + (float) x, box.getCentreY() - h, 1.0f, 2.0f * h));
        }
        g.setColour (colour::redText.withAlpha (0.9f * fade));
        g.setFont (fonts->small (*this, fonts->regular, 10.5f, 0.04f));
        g.drawText ("itch", box.withTrimmedLeft (38.0f), juce::Justification::centred);
    }
}
} // namespace dendrite::ui
