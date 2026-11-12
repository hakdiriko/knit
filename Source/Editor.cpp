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

    // any shape, up to a whole 8K screen: a fixed aspect and a 150% cap stopped the host's
    // maximise/fullscreen from doing anything. The design scales to fit and the field and the
    // strip stretch to fill the rest, so there are no bars
    setResizable (true, true);
    setResizeLimits (designWidth * 3 / 4, designHeight * 3 / 4, 7680, 4320);
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
    // scale so the 880 x 560 design fits, then give the canvas the window's own shape in design units
    const float scale = std::min ((float) getWidth() / (float) designWidth, (float) getHeight() / (float) designHeight);
    canvasWidth = std::max (designWidth, juce::roundToInt ((float) getWidth() / scale));
    canvasHeight = std::max (designHeight, juce::roundToInt ((float) getHeight() / scale));
    canvas.setBounds (0, 0, canvasWidth, canvasHeight);
    canvas.setTransform (juce::AffineTransform::scale (scale));
    layout();
}

void DendriteEditor::layout()
{
    // the field runs under the strip, so the strands (and a throw's nerve) reach the bottom
    // the strip keeps its height at the bottom; anything the window adds goes to the field
    const int stripTop = canvasHeight - (designHeight - fieldHeight);
    field.setBounds (0, 0, canvasWidth, canvasHeight);
    itch.setBounds (0, stripTop, canvasWidth, designHeight - fieldHeight);
    itch.setHeadsArea ({ (float) canvasWidth - 294.0f, 0.0f, 270.0f, (float) itch.getHeight() }); // 586 at 880 wide
    // the tape runs along the top of the strip; its ruler opens over the bottom of the field
    const int tapeTop = stripTop - (int) KeepTape::rulerHeight - 6;
    tape.setBounds (0, tapeTop, canvasWidth, stripTop + 16 - tapeTop);
    firstLight.setBounds (0, 0, canvasWidth, canvasHeight);
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
