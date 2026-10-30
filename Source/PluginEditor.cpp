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

void DendriteEditor::ThrowPad::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (2.0f);
    const float lit = juce::jlimit (0.0f, 1.0f, held ? 1.0f : glow);

    g.setColour (track.interpolatedWith (accent, 0.15f + 0.6f * lit));
    g.fillRoundedRectangle (area, 14.0f);

    // the repeats: rings at the dotted rhythm, fading out
    g.setColour (accent.withAlpha (0.25f + 0.6f * lit));
    const auto centre = area.getCentre();
    for (int k = 1; k <= 5; ++k)
    {
        const float radius = (float) k * 3.0f / 16.0f * area.getHeight() * 0.9f;
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 1.5f / (float) k + 0.5f);
    }

    g.setColour (text);
    g.setFont (juce::FontOptions (18.0f));
    g.drawText ("hold to throw", area, juce::Justification::centred);
}

void DendriteEditor::ThrowPad::mouseDown (const juce::MouseEvent&)
{
    held = true;
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (1.0f);
    repaint();
}

void DendriteEditor::ThrowPad::mouseUp (const juce::MouseEvent&)
{
    held = false;
    parameter.setValueNotifyingHost (0.0f);
    parameter.endChangeGesture();
    repaint();
}

DendriteEditor::DendriteEditor (DendriteProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      pad (*p.parameters.getParameter (ParamIDs::throwIt))
{
    lookAndFeel.setColour (juce::Slider::trackColourId, accent);
    lookAndFeel.setColour (juce::Slider::backgroundColourId, track);
    lookAndFeel.setColour (juce::Slider::thumbColourId, text);
    lookAndFeel.setColour (juce::Slider::textBoxTextColourId, dim);
    lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::Label::textColourId, text);
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (pad);

    amount.setSliderStyle (juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 140, 20);
    addAndMakeVisible (amount);
    amountLabel.setText ("amount", juce::dontSendNotification);
    amountLabel.setFont (juce::FontOptions (15.0f));
    addAndMakeVisible (amountLabel);
    // The attachment also sets double-click-to-default and the value text.
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, ParamIDs::amount, amount);

    setSize (600, 340);
    startTimerHz (30);
}

DendriteEditor::~DendriteEditor()
{
    setLookAndFeel (nullptr);
}

void DendriteEditor::timerCallback()
{
    const float glow = processor.throwGlowForUi.load (std::memory_order_relaxed);
    if (std::abs (glow - pad.glow) > 0.01f)
    {
        pad.glow = glow;
        pad.repaint();
    }
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
    g.drawText ("v" JucePlugin_VersionString " prototype", header, juce::Justification::centredRight);
}

void DendriteEditor::resized()
{
    auto area = getLocalBounds().reduced (24, 14);
    area.removeFromTop (48);
    auto strip = area.removeFromBottom (32);
    area.removeFromBottom (14);
    pad.setBounds (area);
    amountLabel.setBounds (strip.removeFromLeft (70));
    amount.setBounds (strip);
}
