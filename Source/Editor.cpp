#include "Editor.h"

using namespace dendrite::ui;

namespace
{
constexpr int fieldHeight = 488;
}

DendriteEditor::DendriteEditor (DendriteProcessor& p)
    : AudioProcessorEditor (p), processor (p), field (p), itch (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (canvas);
    canvas.addAndMakeVisible (field);
    canvas.addAndMakeVisible (itch);

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
}

void DendriteEditor::timerCallback()
{
    animateTo (juce::Time::getMillisecondCounterHiRes() * 0.001 - started);
}

void DendriteEditor::animateTo (double now)
{
    field.tick (now);
    itch.tick();
}

void DendriteEditor::Canvas::paint (juce::Graphics& g)
{
    juce::ignoreUnused (editor);
    g.fillAll (colour::black);
}
