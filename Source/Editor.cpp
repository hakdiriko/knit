#include "Editor.h"

using namespace dendrite::ui;

namespace
{
constexpr int fieldHeight = 488;
}

DendriteEditor::DendriteEditor (DendriteProcessor& p)
    : AudioProcessorEditor (p), processor (p), field (p), itch (p), tape (p), flyBrain (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (canvas);
    canvas.addAndMakeVisible (field);
    canvas.addAndMakeVisible (itch);
    canvas.addAndMakeVisible (tape);
    canvas.addAndMakeVisible (flyBrain);
    itch.setFly (true);
    flyBrain.onToggle = [this] { setFlyOpen (! flyBrain.isOpen()); };
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
    const int tall = designTall();
    const float scale = std::min ((float) getWidth() / (float) designWidth, (float) getHeight() / (float) tall);
    canvasWidth = std::max (designWidth, juce::roundToInt ((float) getWidth() / scale));
    canvasHeight = std::max (tall, juce::roundToInt ((float) getHeight() / scale));
    canvas.setBounds (0, 0, canvasWidth, canvasHeight);
    canvas.setTransform (juce::AffineTransform::scale (scale));
    layout();
}

void DendriteEditor::layout()
{
    // the field runs under the strip, so the strands (and a throw's nerve) reach the bottom
    // the strip keeps its height at the bottom; anything the window adds goes to the field
    const int drawer = flyBrain.isOpen() ? drawerHeight : 0;
    const int stripTop = canvasHeight - drawer - (designHeight - fieldHeight);
    field.setBounds (0, 0, canvasWidth, canvasHeight - drawer);
    itch.setBounds (0, stripTop, canvasWidth, designHeight - fieldHeight);
    itch.setHeadsArea ({ (float) canvasWidth - 294.0f, 0.0f, 270.0f, (float) itch.getHeight() }); // 586 at 880 wide
    // the tape runs along the top of the strip; its ruler opens over the bottom of the field
    const int tapeTop = stripTop - (int) KeepTape::rulerHeight - 6;
    tape.setBounds (0, tapeTop, canvasWidth, stripTop + 16 - tapeTop);
    firstLight.setBounds (0, 0, canvasWidth, canvasHeight - drawer);
    // the fly lives where the heads' lamps were; open, it fills the drawer under the strip
    if (flyBrain.isOpen())
        flyBrain.setBounds (0, canvasHeight - drawer, canvasWidth, drawer);
    else
        flyBrain.setBounds (canvasWidth - 294, stripTop + 10, 270, designHeight - fieldHeight - 20);
}

void DendriteEditor::setFlyOpen (bool open)
{
    if (open == flyBrain.isOpen())
        return;
    // a window with room on screen grows by the drawer (and shrinks back on closing); a window
    // that is maximised, fullscreen or simply too tall to grow opens the drawer inside itself,
    // so the host's maximise/fullscreen is never undone by asking for a new size
    const float scale = std::min ((float) getWidth() / (float) designWidth, (float) getHeight() / (float) designTall());
    const int by = juce::roundToInt ((float) drawerHeight * scale);
    const int width = getWidth(), height = getHeight(); // before the new limits can stretch it
    bool resize = false;
    if (open)
        resize = grewForDrawer = ! fillsScreen (height + by);
    else
        resize = grewForDrawer && ! fillsScreen (height);
    if (! open)
        grewForDrawer = false;

    flyBrain.setOpen (open);
    if (resize)
    {
        setResizeLimits (designWidth * 3 / 4, designTall() * 3 / 4, 7680, 4320);
        setSize (width, std::max (height + (open ? by : -by), designTall() * 3 / 4));
    }
    else
    {
        // the limits only loosen here: tightening them would make the host resize the window
        setResizeLimits (designWidth * 3 / 4, std::min (designTall() * 3 / 4, height), 7680, 4320);
    }
    resized();
}

bool DendriteEditor::fillsScreen (int heightWanted) const
{
    // the display the window is on, in the same logical pixels as the window
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect (getScreenBounds());
    if (display == nullptr)
        return false;
    const auto area = display->userArea;
    return getWidth() >= area.getWidth() * 9 / 10 || heightWanted > area.getHeight() * 92 / 100;
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
    flyBrain.tick();
    tape.tick (itch.getReveal());
}

void DendriteEditor::Canvas::paint (juce::Graphics& g)
{
    juce::ignoreUnused (editor);
    g.fillAll (colour::black);
}
