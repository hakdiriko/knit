#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/Itch.h"

namespace
{
const juce::Colour background (0xff121417);
const juce::Colour text (0xffe8e6e3);
const juce::Colour dim (0xff8a8f98);
const juce::Colour accent (0xffd9a55b);
const juce::Colour track (0xff2a2e35);
const juce::Colour blood (0xffc8553d); // a scratch in progress
} // namespace

DendriteEditor::DendriteEditor (DendriteProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    lookAndFeel.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lookAndFeel.setColour (juce::Slider::rotarySliderOutlineColourId, track);
    lookAndFeel.setColour (juce::Slider::thumbColourId, text);
    lookAndFeel.setColour (juce::Slider::textBoxTextColourId, text);
    lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::Label::textColourId, text);
    lookAndFeel.setColour (juce::ToggleButton::textColourId, text);
    lookAndFeel.setColour (juce::ToggleButton::tickColourId, accent);
    lookAndFeel.setColour (juce::ToggleButton::tickDisabledColourId, dim);
    setLookAndFeel (&lookAndFeel);

    setUpKnob (ecology, ParamIDs::ecology, "Ecology", "How much the parts talk: colour, space, ringing, life");
    setUpKnob (size, ParamIDs::size, "Size", "Small and close to huge and far");
    setUpKnob (tone, ParamIDs::tone, "Tone", "Dark (downward spirals) to bright (shimmer)");
    setUpKnob (itch, ParamIDs::itch, "Itch", "How alive: still, breathing, restless, feverish. Also how far its moves go");
    setUpKnob (mix, ParamIDs::mix, "Mix", "Dry to wet, equal power");
    setUpKnob (output, ParamIDs::output, "Output", "Trim after auto-gain");

    syncButton.setButtonText ("Sync");
    syncButton.setTooltip ("Locks Echo, Shift and Memory to the host tempo; Size picks the note value");
    addAndMakeVisible (syncButton);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters, ParamIDs::sync, syncButton);

    steadyButton.setButtonText ("steady");
    steadyButton.setTooltip ("Delay times hold still (no wobble, no flanging on drums), and time changes crossfade like Live's Delay in Fade mode");
    addAndMakeVisible (steadyButton);
    steadyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters, ParamIDs::jump, steadyButton);

    autoButton.setButtonText ("auto");
    autoButton.setTooltip ("Off: Dendrite never breaks your loop by itself. On: after the set number of bars of repetition, it makes a move");
    addAndMakeVisible (autoButton);
    autoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters, ParamIDs::autoMoves, autoButton);

    barsBox.addItemList ({ "4 bars", "8 bars", "16 bars", "32 bars", "64 bars" }, 1);
    barsBox.setTooltip ("How many bars of repetition before an auto move");
    barsBox.setColour (juce::ComboBox::backgroundColourId, track);
    barsBox.setColour (juce::ComboBox::textColourId, text);
    barsBox.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    barsBox.setColour (juce::ComboBox::arrowColourId, dim);
    addAndMakeVisible (barsBox);
    barsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.parameters, ParamIDs::bars, barsBox);

    buildButton.setButtonText ("build-up");
    buildButton.setTooltip ("While the bars fill, the network already leans toward its next move, so you feel it coming");
    addAndMakeVisible (buildButton);
    buildAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters, ParamIDs::build, buildButton);

    scratchButton.setButtonText ("scratch");
    scratchButton.setTooltip ("Make a move now, auto or not (MIDI-map it in Live to play it)");
    scratchButton.setColour (juce::TextButton::buttonColourId, track);
    scratchButton.setColour (juce::TextButton::textColourOffId, text);
    scratchButton.onClick = [this] { processor.pressScratch(); };
    addAndMakeVisible (scratchButton);

    setSize (820, 380);
    startTimerHz (30);
}

DendriteEditor::~DendriteEditor()
{
    setLookAndFeel (nullptr);
}

void DendriteEditor::setUpKnob (Knob& knob, const juce::String& paramID, const juce::String& name, const juce::String& hint)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 120, 20);
    knob.slider.setTooltip (hint);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    addAndMakeVisible (knob.label);

    // The attachment also sets double-click-to-default and the value text.
    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, paramID, knob.slider);
}

void DendriteEditor::timerCallback()
{
    // While Sync is on, the switch also names the Echo node's note value.
    juce::String label ("Sync");

    if (syncButton.getToggleState())
    {
        const int note = processor.echoNoteForUi.load (std::memory_order_relaxed);

        if (! processor.hostHasTempo.load (std::memory_order_relaxed))
            label << ": no tempo";
        else if (note >= 0)
            label << ": " << dendrite::dsp::Network::noteName (note);
    }

    if (syncButton.getButtonText() != label)
        syncButton.setButtonText (label);

    // Itch meter: counts the bars of repetition heard, and names a move while it
    // plays (for at least 1.5 s, so a short one can still be read).
    static constexpr int patienceChoices[] { 4, 8, 16, 32, 64 };
    patience = patienceChoices[juce::jlimit (0, 4, barsBox.getSelectedItemIndex())];
    autoOn = autoButton.getToggleState();
    barsHeard = processor.barsHeardForUi.load (std::memory_order_relaxed);

    const int move = processor.moveForUi.load (std::memory_order_relaxed);
    const unsigned count = processor.scratchCountForUi.load (std::memory_order_relaxed);
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (count != lastScratchCount && move >= 0)
    {
        lastScratchCount = count;
        shownMove = move;
        showUntil = now + 1500.0;
    }
    if (move < 0 && now > showUntil)
        shownMove = -1;
    else if (move >= 0)
        shownMove = move;

    meterLevel += 0.25f * (barsHeard / (float) patience - meterLevel);
    repaint (meterArea.expanded (2));
}

void DendriteEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto header = getLocalBounds().reduced (24, 16).removeFromTop (40);

    g.setColour (text);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText ("dendrite", header, juce::Justification::centredLeft);

    g.setColour (dim);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (juce::String::fromUTF8 ("v" JucePlugin_VersionString "  \xc2\xb7  the itch"),
                header, juce::Justification::centredRight);

    // Itch meter: a thin line that fills bar by bar as the same material repeats,
    // red while a move plays.
    auto meter = meterArea.toFloat();
    const auto line = meter.removeFromTop (meter.getHeight() * 0.5f).withSizeKeepingCentre (meter.getWidth(), 3.0f);
    const bool moving = shownMove >= 0;

    g.setColour (track);
    g.fillRoundedRectangle (line, 1.5f);
    g.setColour (moving ? blood : accent);
    g.fillRoundedRectangle (line.withWidth (line.getWidth() * (moving ? 1.0f : juce::jlimit (0.0f, 1.0f, meterLevel))), 1.5f);

    juce::String caption;
    if (moving)
        caption = juce::String::fromUTF8 ("move \xc2\xb7 ") + dendrite::dsp::Itch::moveName (shownMove);
    else if (! autoOn)
        caption = juce::String::fromUTF8 ("auto off \xc2\xb7 moves only by hand");
    else
        caption = "same for " + juce::String (juce::jmin (patience, (int) barsHeard)) + " of " + juce::String (patience) + " bars";

    g.setColour (moving ? blood : dim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (caption, meter, juce::Justification::centred);
}

void DendriteEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (52);

    meterArea = { getWidth() / 2 - 130, 22, 260, 34 };

    auto outputArea = area.removeFromRight (100);
    auto lowerRow = area.removeFromBottom (30);
    auto upperRow = area.removeFromBottom (30);
    const int column = area.getWidth() / 5;

    // Under Size: Sync (its time base, with room for the note name) over Steady.
    // Under Itch: Auto and its bar count over Build-up and Scratch.
    syncButton.setBounds (upperRow.withX (area.getX() + column).withWidth (column * 5 / 4).reduced (6, 3));
    steadyButton.setBounds (lowerRow.withX (area.getX() + column).withWidth (column).reduced (6, 3));

    const int itchX = area.getX() + 3 * column, half = column / 2;
    autoButton.setBounds (upperRow.withX (itchX).withWidth (half).reduced (4, 3));
    barsBox.setBounds (upperRow.withX (itchX + half).withWidth (column - half).reduced (4, 4));
    buildButton.setBounds (lowerRow.withX (itchX - 12).withWidth (half + 12).reduced (4, 3));
    scratchButton.setBounds (lowerRow.withX (itchX + half).withWidth (column - half).reduced (4, 3));

    for (auto* knob : { &ecology, &size, &tone, &itch, &mix })
    {
        auto cell = area.removeFromLeft (column).reduced (6, 0);
        knob->label.setBounds (cell.removeFromTop (24));
        knob->slider.setBounds (cell);
    }

    auto cell = outputArea.reduced (6, 0);
    output.label.setBounds (cell.removeFromTop (24));
    output.slider.setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 130));
}
