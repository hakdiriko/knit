#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
const juce::Colour background (0xff121417);
const juce::Colour text (0xffe8e6e3);
const juce::Colour dim (0xff8a8f98);
const juce::Colour accent (0xffd9a55b);
const juce::Colour track (0xff2a2e35);
} // namespace

ViberEditor::ViberEditor (ViberProcessor& p)
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
    setUpKnob (mix, ParamIDs::mix, "Mix", "Dry to wet, equal power");
    setUpKnob (output, ParamIDs::output, "Output", "Trim after auto-gain");

    syncButton.setButtonText ("Sync");
    syncButton.setTooltip ("Locks Echo, Shift and Memory to the host tempo; Size picks the note value");
    addAndMakeVisible (syncButton);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters, ParamIDs::sync, syncButton);

    setSize (640, 330);
    startTimerHz (10);
}

ViberEditor::~ViberEditor()
{
    setLookAndFeel (nullptr);
}

void ViberEditor::setUpKnob (Knob& knob, const juce::String& paramID, const juce::String& name, const juce::String& hint)
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

void ViberEditor::timerCallback()
{
    // While Sync is on, the switch also names the Echo node's note value.
    juce::String label ("Sync");

    if (syncButton.getToggleState())
    {
        const int note = processor.echoNoteForUi.load (std::memory_order_relaxed);

        if (! processor.hostHasTempo.load (std::memory_order_relaxed))
            label << ": no tempo";
        else if (note >= 0)
            label << ": " << viber::dsp::Network::noteName (note);
    }

    if (syncButton.getButtonText() != label)
        syncButton.setButtonText (label);
}

void ViberEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto header = getLocalBounds().reduced (24, 16).removeFromTop (40);

    g.setColour (text);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText ("viber", header, juce::Justification::centredLeft);

    g.setColour (dim);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (juce::String::fromUTF8 ("v" JucePlugin_VersionString "  \xc2\xb7  the network"),
                header, juce::Justification::centredRight);
}

void ViberEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (52);

    auto outputArea = area.removeFromRight (100);
    auto syncRow = area.removeFromBottom (30);
    const int column = area.getWidth() / 4;

    // Sync sits under Size (it is Size's time base); the extra width fits the note name.
    syncButton.setBounds (syncRow.withX (area.getX() + column).withWidth (2 * column).reduced (6, 3));

    for (auto* knob : { &ecology, &size, &tone, &mix })
    {
        auto cell = area.removeFromLeft (column).reduced (6, 0);
        knob->label.setBounds (cell.removeFromTop (24));
        knob->slider.setBounds (cell);
    }

    auto cell = outputArea.reduced (6, 0);
    output.label.setBounds (cell.removeFromTop (24));
    output.slider.setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 130));
}
