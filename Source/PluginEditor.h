#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class ViberProcessor;

// Placeholder window: name and build version, so a loaded build is easy to identify.
class ViberEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ViberEditor (ViberProcessor&);

    void paint (juce::Graphics&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViberEditor)
};
