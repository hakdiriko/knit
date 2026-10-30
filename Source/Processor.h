#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "space/Itch.h"

class HypnagogicProcessor : public juce::AudioProcessor
{
public:
    HypnagogicProcessor();

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
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState params;

    // The itch frame choices, in bars. Index 0 is off.
    static constexpr float frameBars[] = { 0.0f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 3.0f, 4.0f, 8.0f };
    static const juce::StringArray frameNames;
    static constexpr float defaultOrbit = 0.5f, defaultDepth = 0.45f;

    // What the window shows. Written by the audio thread, read by the UI timer.
    struct Display
    {
        static constexpr int points = 512;
        std::array<std::atomic<float>, points * 2> scope {};
        std::atomic<int> scopeHead { 0 };
        std::atomic<float> correlation { 1.0f }, repetition { 0.0f }, frameProgress { 0.0f };
        std::array<std::atomic<float>, 3> lamps {};
        std::atomic<bool> hostTempo { false };
    } display;

private:
    void run (juce::AudioBuffer<float>&, bool forceBypass);
    void resetEngine();
    void tickClock (double beatsPerSample, double frameBeats, int stepsPerBar);

    std::atomic<float>* orbitParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* frameParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    double sr = 48000.0;
    hyp::Analytic analytic;
    hyp::Line dryLeft, dryRight;
    hyp::Field field;
    hyp::Listener listener;
    hyp::Heads heads;

    // attack detector: runs on the undelayed input, so it sees onsets a few ms early
    hyp::Biquad attackFilter;
    float attackFast = 0.0f, attackSlow = 0.0f, attack = 0.0f;
    float fastUp = 0, fastDown = 0, slowUp = 0, slowDown = 0, attackUp = 0, attackDown = 0;

    // musical clock (from the host when it plays, otherwise free at 120 BPM)
    double beat = 0.0;
    int64_t lastStep = -1, lastFrame = std::numeric_limits<int64_t>::min();
    float frameSum = 0.0f;
    int frameSteps = 0, boredFrames = 0;
    bool frameStartedIdle = true;

    // output chain
    hyp::Glide bandPower, sidePower, balance, wet;
    float limiterEnv = 0.0f, limiterRelease = 0.0f;
    float corrLR = 0.0f, corrLL = 0.0f, corrRR = 0.0f;
    int scopeDecimate = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HypnagogicProcessor)
};
