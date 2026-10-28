#pragma once

#include "dsp/OutputChain.h"
#include "dsp/Room.h"
#include "dsp/Tape.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace ParamIDs
{
// Never rename these: hosts store automation and saved state by ID.
inline constexpr auto wear     = "wear";
inline constexpr auto friction = "friction";
inline constexpr auto room     = "room";
inline constexpr auto bypass   = "bypass";
} // namespace ParamIDs

// Signal flow:
//   input -> tape (wear, friction) -> + room (fed by the tape and by what wore away)
//         -> safety limiter -> NaN guard -> bypass crossfade -> output
class DendriteProcessor final : public juce::AudioProcessor
{
public:
    DendriteProcessor();
    ~DendriteProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioParameterBool* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState parameters;

    // For the window: how worn each 16th of the 4-bar tape is, where the head is, and
    // whether the tape is moving.
    std::array<std::atomic<float>, dendrite::dsp::Tape::cellsPerTape> wearForUi {};
    std::atomic<float> headForUi { 0.0f };
    std::atomic<bool> playingForUi { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void resetDsp();
    void setRoom (float amount);

    static constexpr int subBlock = 64;

    std::atomic<float>* wearParam = nullptr;
    std::atomic<float>* frictionParam = nullptr;
    std::atomic<float>* roomParam = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    dendrite::dsp::Tape tape;
    dendrite::dsp::Room room;
    dendrite::dsp::SafetyLimiter limiter;
    std::array<dendrite::dsp::DelayLine, 2> dryDelay; // keeps bypass in time with the tape's latency

    juce::SmoothedValue<float> roomReturnSm, directSm, bypassSm;
    float roomAmount = 0.0f, frictionAmount = 0.0f, setRoomAmount = -1.0f;
    float knobCoef = 0.1f;
    double internalPpq = 0.0;
    float sampleRate = 48000.0f;

    std::array<float, subBlock> dryL {}, dryR {}, lostL {}, lostR {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteProcessor)
};
