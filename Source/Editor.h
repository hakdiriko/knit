#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Processor.h"

// No knobs. A disc you drag a point around (the space around your head) and a strip
// you drag to set the itch. Both are plain parameters, so Push, MIDI and automation
// reach them too.
class HypnagogicEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HypnagogicEditor (HypnagogicProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // The space, seen from above, you in the middle facing up. The angle is orbit.
    // The distance from the centre is depth, measured like a Poincare disc: equal
    // steps of depth bunch up toward the rim, so the outer ring is where the smallest
    // moves make the biggest shifts.
    struct Disc : juce::Component
    {
        Disc (juce::RangedAudioParameter& orbit, juce::RangedAudioParameter& depth);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        juce::Point<float> pointFor (float orbit, float depth) const;    // unit disc, y down
        float radius() const { return (float) juce::jmin (getWidth(), getHeight()) * 0.5f - 6.0f; }

        juce::ParameterAttachment orbitLink, depthLink;
        float orbit = 0.5f, depth = 0.45f;
        juce::Point<float> grabPoint, grabMouse;
        std::array<juce::Point<float>, HypnagogicProcessor::Display::points> trace {};
    };

    // The itch frame: drag left and right through off, 1, 1.25 ... 8 bars.
    struct FrameStrip : juce::Component
    {
        explicit FrameStrip (juce::RangedAudioParameter&);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        int indexAt (float x) const;

        juce::ParameterAttachment link;
        int index = 5;
        float repetition = 0.0f, progress = 0.0f;
        std::array<float, 3> lamps {};
    };

    HypnagogicProcessor& proc;
    Disc disc;
    FrameStrip strip;
    float correlation = 1.0f;
    bool hostTempo = false;
};
