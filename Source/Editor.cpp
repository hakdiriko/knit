#include "Editor.h"

using namespace dendrite::ui;

namespace
{
constexpr int fieldHeight = 488;
}

DendriteEditor::DendriteEditor (DendriteProcessor& p)
    : AudioProcessorEditor (p), processor (p), field (p), itch (p), tape (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (canvas);
    canvas.addAndMakeVisible (field);
    canvas.addAndMakeVisible (itch);
    canvas.addAndMakeVisible (tape);
    canvas.addAndMakeVisible (firstLight);
    itch.setTape (&tape);
    itch.onWordmark = [this] { firstLight.start (juce::Time::getMillisecondCounterHiRes() * 0.001 - started); };

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
    g.fillAll (colour::black);
}

void DendriteEditor::resized()
{
    const float scale = (float) getWidth() / (float) designWidth;
    canvas.setBounds (0, 0, designWidth, designHeight);
    canvas.setTransform (juce::AffineTransform::scale (scale));
    layout();
}

void DendriteEditor::layout()
{
    // the field runs under the strip, so the strands (and a throw's nerve) reach the bottom
    field.setBounds (0, 0, designWidth, designHeight);
    itch.setBounds (0, fieldHeight, designWidth, designHeight - fieldHeight);
    itch.setHeadsArea ({ 586.0f, 0.0f, 270.0f, (float) itch.getHeight() });
    // the tape runs along the top of the strip; its ruler opens over the bottom of the field
    const int tapeTop = fieldHeight - (int) KeepTape::rulerHeight - 6;
    tape.setBounds (0, tapeTop, designWidth, fieldHeight + 16 - tapeTop);
    firstLight.setBounds (0, 0, designWidth, designHeight);
}

void DendriteEditor::timerCallback()
{
    animateTo (juce::Time::getMillisecondCounterHiRes() * 0.001 - started);
}

void DendriteEditor::animateTo (double now)
{
    if (! firstLightChecked)
    {
        firstLightChecked = true;
        firstLight.startIfNew (now);
    }
    processor.keep.collect();
    firstLight.tick (now);
    field.tick (now);
    itch.tick();
    tape.tick (itch.getReveal());
}

void DendriteEditor::Canvas::paint (juce::Graphics& g)
{
    juce::ignoreUnused (editor);
    g.fillAll (colour::black);
}
