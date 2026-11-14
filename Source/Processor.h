#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"
#include "keep/Keep.h"

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
        // the fly: what it is doing now
        std::atomic<float> flyJump { 0.0f }, flySemitones { 0.0f }, flyScratch { 0.0f }, flyScratchHz { 1000.0f }, flyTurn { 0.0f };
        std::atomic<float> flyArousal { 0.0f }, flyFatigue { 0.0f }, flyFear { 0.0f }, flyMemory { 1.0f };
        std::atomic<bool> flyThrowing { false }, flyFrozen { false }, flyAwake { false };

        // onsets: each one starts a new tree in the window
        struct Onset { float level = 0.0f; bool thrown = false; };
        juce::AbstractFifo fifo { 64 };
        std::array<Onset, 64> onsets {};
    } display;

    // The fly's neurons, for the window: how often each fired since the window last asked.
    uint16_t takeFlySpikes (int neuron) noexcept { return engine.itchFly().takeSpikes (neuron); }
    // A new fly: it forgets what it learned and its moods (from the window; done at the next block).
    void forgetFly() noexcept { flyForget.store (true, std::memory_order_release); }

    // keep: the last minute, on tape, and a diary of what happened (read by the window)
    dendrite::keep::Keep keep;

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

    // The creature's life crosses threads here. The audio thread seals a copy into the vault
    // once per bar and after every itch (a seqlock: a reader that raced a write tries again);
    // a life loaded from a set waits in `arriving` until the next block adopts it.
    void keepLife() noexcept;                 // audio thread
    dendrite::Creature::Life readLife() const; // message thread
    struct Vault
    {
        std::atomic<uint32_t> seq { 0 };
        dendrite::Creature::Life life;
    };
    mutable Vault vault;
    dendrite::Creature::Life arriving;
    std::atomic<int> arrivingState { 0 }; // 0 nothing, 1 being written, 2 ready, 3 being adopted
    uint64_t keptAge = 0;
    uint32_t keptItches = 0;

    // The fly's memory crosses threads the same way: sealed by the audio thread about every two
    // seconds (and at once after it adopts or forgets), read by the message thread to save it.
    using FlyMemory = dendrite::Fly::Memory;
    void keepFly (int samples) noexcept;  // audio thread
    void readFly (FlyMemory&) const;      // message thread
    struct FlyVault
    {
        std::atomic<uint32_t> seq { 0 };
        FlyMemory memory;
    };
    std::unique_ptr<FlyVault> flyVault = std::make_unique<FlyVault>();
    std::unique_ptr<FlyMemory> flyArriving = std::make_unique<FlyMemory>();
    std::atomic<int> flyArrivingState { 0 }; // 0 nothing, 1 being written, 2 ready, 3 being adopted
    std::atomic<bool> flyForget { false };
    int flyUntilSeal = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteProcessor)
};
