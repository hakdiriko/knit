#include "PluginEditor.h"
#include "PluginProcessor.h"

ViberEditor::ViberEditor (ViberProcessor& p)
    : AudioProcessorEditor (p)
{
    setSize (420, 240);
}

void ViberEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff121417));

    auto area = getLocalBounds().reduced (24);

    g.setColour (juce::Colour (0xffe8e6e3));
    g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
    g.drawText ("viber", area.removeFromTop (60), juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff8a8f98));
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (juce::String::fromUTF8 ("v" JucePlugin_VersionString "  \xc2\xb7  passthrough"),
                area.removeFromTop (24),
                juce::Justification::centredLeft);
}
