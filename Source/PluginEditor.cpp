#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
const juce::Colour background (0xff121417);
const juce::Colour text (0xffe8e6e3);
const juce::Colour dim (0xff8a8f98);
const juce::Colour accent (0xffd9a55b);
const juce::Colour track (0xff2a2e35);
const juce::Colour oxide (0xff5a3b26); // fresh tape
const juce::Colour shed (0xffcfc6b6);  // worn through
} // namespace

DendriteEditor::DendriteEditor (DendriteProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    lookAndFeel.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lookAndFeel.setColour (juce::Slider::rotarySliderOutlineColourId, track);
    lookAndFeel.setColour (juce::Slider::thumbColourId, text);
    lookAndFeel.setColour (juce::Slider::textBoxTextColourId, dim);
    lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::Label::textColourId, text);
    setLookAndFeel (&lookAndFeel);

    setUpKnob (wear, ParamIDs::wear, "wear",
               "How fast a part that keeps repeating wears the tape. Anything new heals it. At 0 it never wears");
    setUpKnob (friction, ParamIDs::friction, "friction",
               "The material: saturation, wow and flutter, and the dust left where the sound wore away");
    setUpKnob (room, ParamIDs::room, "room",
               "The space the sound, and what wears off it, falls into: wood, hall, then a long dark afterlife");

    setSize (600, 340);
    startTimerHz (30);
}

DendriteEditor::~DendriteEditor()
{
    setLookAndFeel (nullptr);
}

void DendriteEditor::setUpKnob (Knob& knob, const juce::String& paramID, const juce::String& name, const juce::String& hint)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 160, 20);
    knob.slider.setTooltip (hint);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (17.0f));
    addAndMakeVisible (knob.label);

    // The attachment also sets double-click-to-default and the value text.
    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, paramID, knob.slider);
}

void DendriteEditor::timerCallback()
{
    for (size_t c = 0; c < shownWear.size(); ++c)
        shownWear[c] += 0.3f * (processor.wearForUi[c].load (std::memory_order_relaxed) - shownWear[c]);
    head = processor.headForUi.load (std::memory_order_relaxed);
    playing = processor.playingForUi.load (std::memory_order_relaxed);
    repaint (tapeArea.expanded (4));
}

void DendriteEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto header = getLocalBounds().reduced (24, 14).removeFromTop (36);
    g.setColour (text);
    g.setFont (juce::FontOptions (26.0f));
    g.drawText ("dendrite", header, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("v" JucePlugin_VersionString, header, juce::Justification::centredRight);

    // The tape: 4 bars, 64 slivers. Bar lines are a hair darker so the loop reads.
    const auto area = tapeArea.toFloat();
    const float w = area.getWidth() / (float) shownWear.size();
    for (size_t c = 0; c < shownWear.size(); ++c)
    {
        const float worn = juce::jlimit (0.0f, 1.0f, shownWear[c] * 1.6f);
        auto sliver = juce::Rectangle<float> (area.getX() + w * (float) c, area.getY(), w, area.getHeight());
        g.setColour (oxide.interpolatedWith (shed, std::sqrt (worn)));
        g.fillRect (sliver.reduced (c % 16 == 0 ? 1.0f : 0.25f, 0.0f));
    }

    const float x = area.getX() + area.getWidth() * head / (float) shownWear.size();
    g.setColour (playing ? accent : dim);
    g.fillRect (juce::Rectangle<float> (x - 1.0f, area.getY() - 4.0f, 2.0f, area.getHeight() + 8.0f));

    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (playing ? "the tape" : "the tape  (stopped)", tapeArea.translated (0, tapeArea.getHeight() + 4).withHeight (16),
                juce::Justification::centredLeft);
}

void DendriteEditor::resized()
{
    auto area = getLocalBounds().reduced (24, 14);
    area.removeFromTop (44);
    tapeArea = area.removeFromTop (54);
    area.removeFromTop (30);

    const int column = area.getWidth() / 3;
    for (auto* knob : { &wear, &friction, &room })
    {
        auto cell = area.removeFromLeft (column).reduced (10, 0);
        knob->label.setBounds (cell.removeFromTop (24));
        knob->slider.setBounds (cell);
    }
}
