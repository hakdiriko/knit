#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"

class DendriteProcessor : public juce::AudioProcessor
{
public:
    DendriteProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorParameter* getBypassParameter() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState params;

    static constexpr float defaultFeed = 0.5f, defaultSpace = 0.4f;
    static constexpr int defaultItch = dendrite::Engine::defaultFrame; // 4 bars
    static const juce::StringArray itchNames;

    static juce::String feedName (float feed);
    static juce::String spaceName (float space);

    // What the window shows. Written by the audio thread once per block, read by the UI.
    struct Display
    {
        std::atomic<float> dotted { 0.375f }, quarter { 0.5f }, gainDotted { 0.62f }, gainQuarter { 0.42f };
        std::atomic<float> pan { 0.0f }, spread { 0.0f }, diffuse { 0.0f }, rotate { 0.0f }, theta { 0.0f };
        std::atomic<float> returnLevel { 0.0f }, fill { 0.0f }, throwing { 0.0f }, breath { 0.0f };
        std::atomic<float> repetition { 0.0f }, frameProgress { 0.0f }, boredom { 0.0f };
        std::array<std::atomic<float>, dendrite::Heads::count> lamps {};
        std::atomic<float> correlation { 1.0f }, inLevel { 0.0f }, outLevel { 0.0f }, bpm { 120.0f };
        // 0..1 against the track's own recent peaks, so a quiet track looks as alive as a loud
        // one: the strongest hit since the window last took it (read it with exchange (0.0f)),
        // the input's held level, the echo's level
        std::atomic<float> hit { 0.0f }, body { 0.0f }, echoBody { 0.0f };
        std::atomic<float> runaway { 0.0f }; // 0..1 while a throw held at endless runs away
        std::atomic<bool> hostPlaying { false }, itching { false };

        // onsets: each one starts a new tree in the window
        struct Onset { float level = 0.0f; bool thrown = false; };
        juce::AbstractFifo fifo { 64 };
        std::array<Onset, 64> onsets {};
    } display;

private:
    void run (juce::AudioBuffer<float>&, bool forceBypass);
    void publish();

    std::atomic<float>* feedParam = nullptr;
    std::atomic<float>* spaceParam = nullptr;
    std::atomic<float>* throwParam = nullptr;
    std::atomic<float>* itchParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* dryParam = nullptr;

    dendrite::Engine engine;
    dendrite::HostTime hostTime;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteProcessor)
};
