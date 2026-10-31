#include "Editor.h"

using namespace dendrite::ui;

DendriteEditor::DendriteEditor (DendriteProcessor& p)
    : AudioProcessorEditor (p), processor (p), field (p), throwPad (p), itch (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (canvas);
    canvas.addAndMakeVisible (field);
    canvas.addAndMakeVisible (throwPad);
    canvas.addAndMakeVisible (itch);

    auto showHint = [this] (const juce::String& h) {
        hint = h;
        canvas.repaint (0, designHeight - 44, designWidth, 44);
    };
    field.onHint = showHint;
    throwPad.onHint = showHint;
    itch.onHint = showHint;

    setResizable (true, true);
    setResizeLimits (designWidth * 3 / 4, designHeight * 3 / 4, designWidth * 3 / 2, designHeight * 3 / 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    setSize (designWidth, designHeight);

    started = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);
}

DendriteEditor::~DendriteEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DendriteEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour::stone0);
}

void DendriteEditor::resized()
{
    const float scale = (float) getWidth() / (float) designWidth;
    canvas.setBounds (0, 0, designWidth, designHeight);
    canvas.setTransform (juce::AffineTransform::scale (scale));

    field.setBounds (24, 68, 572, 436);
    throwPad.setBounds (612, 68, 244, 230);
    itch.setBounds (612, 310, 244, 194);
}

void DendriteEditor::timerCallback()
{
    animateTo (juce::Time::getMillisecondCounterHiRes() * 0.001 - started);
}

void DendriteEditor::animateTo (double now)
{
    field.tick (now);
    throwPad.tick();
    itch.tick();
    if (++frame % 15 == 0)
        canvas.repaint (600, 16, designWidth - 600, 40); // the tempo chip
    if (hint.isEmpty())
        canvas.repaint (0, designHeight - 48, designWidth, 48);
}

void DendriteEditor::Canvas::paint (juce::Graphics& g)
{
    const auto& f = *editor.fonts;
    auto& d = editor.processor.display;
    const auto b = getLocalBounds().toFloat();

    juce::ColourGradient ground (juce::Colour (0xff12100e), 0.0f, 0.0f, colour::stone0, 0.0f, b.getBottom(), false);
    g.setGradientFill (ground);
    g.fillRect (b);

    // header: the name, what it is, and the tempo it lives at
    g.setColour (colour::ink);
    g.setFont (f.make (f.light, 31.0f, 0.04f));
    g.drawText ("dendrite", juce::Rectangle<float> (26.0f, 14.0f, 160.0f, 40.0f), juce::Justification::centredLeft);
    g.setColour (colour::inkFaint);
    g.setFont (f.make (f.regular, 12.5f, 0.03f));
    g.drawText ("a dub echo that grows around your head", juce::Rectangle<float> (170.0f, 24.0f, 320.0f, 24.0f),
                juce::Justification::centredLeft);

    auto chip = [&] (juce::Rectangle<float> r, const juce::String& text, juce::Colour dot, float dotAlpha) {
        g.setColour (colour::stone1);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (colour::line);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        g.setColour (dot.withAlpha (dotAlpha));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ r.getX() + 14.0f, r.getCentreY() }));
        g.setColour (colour::inkDim);
        g.setFont (f.make (f.regular, 12.0f, 0.03f));
        g.drawText (text, r.withTrimmedLeft (24.0f).withTrimmedRight (10.0f), juce::Justification::centred);
    };
    const float bpm = d.bpm.load (std::memory_order_relaxed);
    const bool playing = d.hostPlaying.load (std::memory_order_relaxed);
    chip ({ 600.0f, 22.0f, 148.0f, 26.0f }, juce::String (juce::CharPointer_UTF8 ("dotted \xe2\x85\x9b  \xc2\xb7  ")) + juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.05f ? 0 : 1) + " bpm",
          colour::ochre, playing ? 0.95f : 0.3f);
    chip ({ 756.0f, 22.0f, 100.0f, 26.0f }, "mono-safe", colour::cool, 0.8f);

    // footer: what you're touching, or where you are
    const auto foot = juce::Rectangle<float> (26.0f, b.getBottom() - 46.0f, b.getWidth() - 52.0f, 36.0f);
    g.setFont (f.make (f.regular, 12.0f, 0.02f));
    if (editor.hint.isNotEmpty())
    {
        g.setColour (colour::inkDim);
        g.drawText (editor.hint, foot, juce::Justification::centredLeft);
    }
    else
    {
        const float feed = editor.field.feedValue(), space = editor.field.spaceValue();
        const juce::String text = "feed  " + DendriteProcessor::feedName (feed) + " " + juce::String (juce::roundToInt (feed * 100.0f))
                                  + "      space  " + DendriteProcessor::spaceName (space) + " " + juce::String (juce::roundToInt (space * 100.0f));
        g.setColour (colour::inkFaint);
        g.drawText (text, foot, juce::Justification::centredLeft);
    }
    g.setColour (colour::inkFaint.withAlpha (0.6f));
    g.drawText ("v" JucePlugin_VersionString, foot, juce::Justification::centredRight);
}
