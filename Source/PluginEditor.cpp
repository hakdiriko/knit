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
    setUpKnob (itch, ParamIDs::itch, "Itch", "Still, breathing, restless; past 60% it gets bored of repetition and scratches");
    setUpKnob (mix, ParamIDs::mix, "Mix", "Dry to wet, equal power");
    setUpKnob (output, ParamIDs::output, "Output", "Trim after auto-gain");

    syncButton.setButtonText ("Sync");
    syncButton.setTooltip ("Locks Echo, Shift and Memory to the host tempo; Size picks the note value");
    addAndMakeVisible (syncButton);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters, ParamIDs::sync, syncButton);

    jumpButton.setButtonText ("jump");
    jumpButton.setTooltip ("A/B for Sync steps: off bends the pitch like tape, on jumps cleanly");
    addAndMakeVisible (jumpButton);
    jumpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters, ParamIDs::jump, jumpButton);

    scratchButton.setButtonText ("scratch");
    scratchButton.setTooltip ("Scratch now (MIDI-map it in Live to play it)");
    scratchButton.setColour (juce::TextButton::buttonColourId, track);
    scratchButton.setColour (juce::TextButton::textColourOffId, text);
    scratchButton.onClick = [this] { processor.pressScratch(); };
    addAndMakeVisible (scratchButton);

    setSize (820, 340);
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

    // Itch meter: the level eases toward the engine's, and a scratch's name stays up
    // for at least 1.5 s so a short gesture can still be read.
    const float level = processor.itchLevelForUi.load (std::memory_order_relaxed);
    const int gesture = processor.gestureForUi.load (std::memory_order_relaxed);
    const unsigned count = processor.scratchCountForUi.load (std::memory_order_relaxed);
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (count != lastScratchCount && gesture >= 0)
    {
        lastScratchCount = count;
        shownGesture = gesture;
        showUntil = now + 1500.0;
    }
    if (gesture < 0 && now > showUntil)
        shownGesture = -1;
    else if (gesture >= 0)
        shownGesture = gesture;

    meterLevel += 0.25f * (level - meterLevel);
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

    // Itch meter: a thin line that fills as boredom builds, red while a scratch plays.
    auto meter = meterArea.toFloat();
    const auto line = meter.removeFromTop (meter.getHeight() * 0.5f).withSizeKeepingCentre (meter.getWidth(), 3.0f);
    const bool scratching = shownGesture >= 0;

    g.setColour (track);
    g.fillRoundedRectangle (line, 1.5f);
    g.setColour (scratching ? blood : accent);
    g.fillRoundedRectangle (line.withWidth (line.getWidth() * (scratching ? 1.0f : juce::jlimit (0.0f, 1.0f, meterLevel))), 1.5f);

    g.setColour (scratching ? blood : dim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (scratching ? juce::String::fromUTF8 ("scratch \xc2\xb7 ") + dendrite::dsp::Itch::gestureName (shownGesture)
                           : juce::String ("itch"),
                meter, juce::Justification::centred);
}

void DendriteEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (52);

    meterArea = { getWidth() / 2 - 130, 22, 260, 34 };

    auto outputArea = area.removeFromRight (100);
    auto syncRow = area.removeFromBottom (30);
    const int column = area.getWidth() / 5;

    // Sync sits under Size (it is Size's time base) with room for the note name, then
    // its jump switch; Scratch sits under Itch.
    syncButton.setBounds (syncRow.withX (area.getX() + column).withWidth (column * 5 / 4).reduced (6, 3));
    jumpButton.setBounds (syncRow.withX (area.getX() + column * 9 / 4).withWidth (column * 3 / 4).reduced (6, 3));
    scratchButton.setBounds (syncRow.withX (area.getX() + 3 * column).withWidth (column).reduced (14, 2));

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
